#pragma once
#include <windows.h>
#include <commctrl.h>
#include <functional>
#include <map>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace xenon {
// Chromium's transient HWNDs are owned by the top-level shell after creation.
// Retain their original child parent while Windows is still creating them.
class PagePopupGuard {
 public:
  using Allowed=std::function<bool(HWND)>;
  PagePopupGuard()=default;
  PagePopupGuard(const PagePopupGuard&)=delete;
  PagePopupGuard& operator=(const PagePopupGuard&)=delete;
  void start(Allowed allowed){
    if(current_)throw std::runtime_error("Page popup guard already running");
    allowed_=std::move(allowed);current_=this;
    hook_=SetWindowsHookExW(WH_CBT,hook,nullptr,GetCurrentThreadId());
    if(!hook_)throw std::runtime_error("Cannot guard page popup windows");
  }
  ~PagePopupGuard(){
    if(hook_)UnhookWindowsHookEx(hook_);
    for(const auto& [window,source]:windows_)if(IsWindow(window))RemoveWindowSubclass(window,proc,1);
    for(const auto& [source,token]:sources_)RemoveWindowSubclass(source,source_proc,1);
    if(current_==this)current_=nullptr;
  }
  void refresh(){
    const auto windows=windows_;
    for(const auto& [window,source]:windows)if(windows_.contains(window)&&IsWindowVisible(window)&&!allowed(source)){
      ShowWindow(window,SW_HIDE);PostMessageW(window,close_message(),0,0);
    }
  }
 private:
  static inline thread_local PagePopupGuard* current_{};
  struct Source {HWND window;uint64_t token;};
  HHOOK hook_{};Allowed allowed_;std::map<HWND,Source> windows_;
  std::map<HWND,uint64_t> sources_;uint64_t next_token_{};
  static UINT close_message(){static const UINT message=RegisterWindowMessageW(L"Xenon.CloseBackgroundPagePopup");return message;}
  bool allowed(const Source& source) const {
    const auto found=sources_.find(source.window);
    return found!=sources_.end()&&found->second==source.token&&allowed_(source.window);
  }
  static LRESULT CALLBACK source_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
    auto self=reinterpret_cast<PagePopupGuard*>(data);
    if(message==WM_NCDESTROY){self->sources_.erase(window);RemoveWindowSubclass(window,source_proc,1);self->refresh();}
    return DefSubclassProc(window,message,wp,lp);
  }
  static LRESULT CALLBACK hook(int code,WPARAM wp,LPARAM lp){
    auto self=current_;
    if(self&&code==HCBT_CREATEWND){
      const auto create=reinterpret_cast<CBT_CREATEWNDW*>(lp)->lpcs;
      const auto window=reinterpret_cast<HWND>(wp);
      if(!(create->style&WS_CHILD)){
        auto source=create->hwndParent;
        for(int i=0;source&&i<16;++i){
          wchar_t name[128]{};GetClassNameW(source,name,128);
          if(wcscmp(name,L"XenonTabHost")==0){
            if(!self->sources_.contains(source)&&SetWindowSubclass(source,source_proc,1,reinterpret_cast<DWORD_PTR>(self)))self->sources_[source]=++self->next_token_;
            if(self->sources_.contains(source)&&SetWindowSubclass(window,proc,1,reinterpret_cast<DWORD_PTR>(self)))self->windows_[window]={source,self->sources_.at(source)};
            break;
          }
          source=GetParent(source);
        }
      }
    }
    return CallNextHookEx(self?self->hook_:nullptr,code,wp,lp);
  }
  static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
    auto self=reinterpret_cast<PagePopupGuard*>(data);
    const auto entry=self->windows_.find(window);
    if(entry!=self->windows_.end()&&message==WM_WINDOWPOSCHANGING&&lp){
      auto position=reinterpret_cast<WINDOWPOS*>(lp);
      if((position->flags&SWP_SHOWWINDOW)&&!self->allowed(entry->second)){
        position->flags=(position->flags&~SWP_SHOWWINDOW)|SWP_HIDEWINDOW|SWP_NOACTIVATE;
        // Closing during Chromium's show callback could delete its widget
        // underneath that callback. Defer only the close; visibility is
        // suppressed synchronously, before any desktop pixels are displayed.
        PostMessageW(window,close_message(),0,0);
      }
    }
    if(entry!=self->windows_.end()&&message==close_message()){
      // A reused popup that is now legitimately visible must not be closed by
      // an earlier queued request. Otherwise let Chromium retire its widget
      // and renderer popup state using the ordinary native close path.
      if(!IsWindowVisible(window)||!self->allowed(entry->second))return DefSubclassProc(window,WM_CLOSE,0,0);
      return 0;
    }
    if(message==WM_NCDESTROY){self->windows_.erase(window);RemoveWindowSubclass(window,proc,1);}
    return DefSubclassProc(window,message,wp,lp);
  }
};
}
