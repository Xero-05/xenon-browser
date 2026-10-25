#pragma once
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
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
  using Dismiss=std::function<void(HWND)>;
  PagePopupGuard()=default;
  PagePopupGuard(const PagePopupGuard&)=delete;
  PagePopupGuard& operator=(const PagePopupGuard&)=delete;
  void start(Allowed allowed,Dismiss dismiss={}){
    if(current_)throw std::runtime_error("Page popup guard already running");
    allowed_=std::move(allowed);dismiss_=std::move(dismiss);current_=this;
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
  HHOOK hook_{};Allowed allowed_;Dismiss dismiss_;std::map<HWND,Source> windows_;
  std::map<HWND,uint64_t> sources_;uint64_t next_token_{};
  static UINT close_message(){static const UINT message=RegisterWindowMessageW(L"Xenon.CloseBackgroundPagePopup");return message;}
  bool allowed(const Source& source) const {
    const auto found=sources_.find(source.window);
    return found!=sources_.end()&&found->second==source.token&&allowed_(source.window);
  }
  static void fit_work_area(HWND window,HWND source,WINDOWPOS& position){
    MONITORINFO monitor{sizeof(monitor)};RECT current{};
    if(!GetMonitorInfoW(MonitorFromWindow(source,MONITOR_DEFAULTTONEAREST),&monitor)||!GetWindowRect(window,&current))return;
    // Both APIs and WINDOWPOS use the native window's coordinate space. Do
    // not mix Chromium's CSS/DIP menu dimensions with these screen pixels.
    const auto work=monitor.rcWork;
    const int available_width=work.right-work.left,available_height=work.bottom-work.top;
    const int width=(position.flags&SWP_NOSIZE)?current.right-current.left:position.cx;
    const int height=(position.flags&SWP_NOSIZE)?current.bottom-current.top:position.cy;
    if(width<=0||height<=0||available_width<=0||available_height<=0)return;
    const int x=(position.flags&SWP_NOMOVE)?current.left:position.x,y=(position.flags&SWP_NOMOVE)?current.top:position.y;
    const int fitted_width=std::min(width,available_width),fitted_height=std::min(height,available_height);
    const int fitted_x=std::clamp(x,static_cast<int>(work.left),static_cast<int>(work.right)-fitted_width);
    const int fitted_y=std::clamp(y,static_cast<int>(work.top),static_cast<int>(work.bottom)-fitted_height);
    if(fitted_width!=width||fitted_height!=height){position.cx=fitted_width;position.cy=fitted_height;position.flags&=~SWP_NOSIZE;}
    if(fitted_x!=x||fitted_y!=y){position.x=fitted_x;position.y=fitted_y;position.flags&=~SWP_NOMOVE;}
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
      const bool eligible=self->allowed(entry->second);
      if((position->flags&SWP_SHOWWINDOW)&&!eligible){
        position->flags=(position->flags&~SWP_SHOWWINDOW)|SWP_HIDEWINDOW|SWP_NOACTIVATE;
        // Closing during Chromium's show callback could delete its widget
        // underneath that callback. Defer only the close; visibility is
        // suppressed synchronously, before any desktop pixels are displayed.
        PostMessageW(window,close_message(),0,0);
      }else if(eligible&&!(position->flags&SWP_HIDEWINDOW)&&((position->flags&SWP_SHOWWINDOW)||IsWindowVisible(window))){
        // Long select/datalist labels can make Chromium request a popup that
        // extends offscreen. Fit only this tracked transient, never its page,
        // and keep it on the source monitor rather than a neighboring screen.
        fit_work_area(window,entry->second.window,*position);
      }
    }
    if(entry!=self->windows_.end()&&message==close_message()){
      // A reused popup that is now legitimately visible must not be closed by
      // an earlier queued request. Notify CEF before closing the HWND so its
      // renderer popup state is retired too; WM_CLOSE alone is insufficient.
      if(!IsWindowVisible(window)||!self->allowed(entry->second)){
        const auto source=entry->second;
        if(self->dismiss_&&self->sources_.contains(source.window)&&self->sources_.at(source.window)==source.token)self->dismiss_(source.window);
        // CEF's dismissal may synchronously destroy this widget/subclass.
        if(self->windows_.contains(window))return DefSubclassProc(window,WM_CLOSE,0,0);
      }
      return 0;
    }
    if(message==WM_NCDESTROY){self->windows_.erase(window);RemoveWindowSubclass(window,proc,1);}
    return DefSubclassProc(window,message,wp,lp);
  }
};
}
