#pragma once
#include "xenon/branding.hpp"
#include <commctrl.h>
#include <algorithm>
#include <string>

namespace xenon {
namespace cef_branding {
inline constexpr UINT_PTR subclass_id=0x58454e4f; // XENO; one per native root.

inline std::wstring window_title(std::wstring title){
  constexpr const wchar_t* chrome=L" - Chromium";
  if(title.empty()||title==L"Chromium")return L"Xenon Browser";
  const auto branded=title.rfind(L" — Xenon");
  if(title==L"Xenon Browser"||(branded!=std::wstring::npos&&
     (branded+8==title.size()||title.compare(branded+8,2,L" (")==0)))return title;
  const auto product=title.rfind(chrome);
  if(product!=std::wstring::npos&&
     (product+11==title.size()||title.compare(product+11,2,L" (")==0)){
    title.replace(product,11,L" — Xenon");return title;
  }
  title+=L" — Xenon";
  return title;
}

inline LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
  if(message==WM_SETTEXT){
    try {
      const auto source=reinterpret_cast<LPCWSTR>(lp);
      const auto branded=window_title(source?std::wstring(source,wcsnlen_s(source,32768)):std::wstring{});
      return DefSubclassProc(window,message,wp,reinterpret_cast<LPARAM>(branded.c_str()));
    } catch(...) {
      // Branding is cosmetic: retain Chromium's title if allocation fails.
      return DefSubclassProc(window,message,wp,lp);
    }
  }
  if(message==WM_SETICON&&(wp==ICON_SMALL||wp==ICON_BIG)){
    const auto icon=branding_icon(GetSystemMetrics(wp==ICON_BIG?SM_CXICON:SM_CXSMICON));
    if(icon)lp=reinterpret_cast<LPARAM>(icon);
  }
  if(message==WM_NCDESTROY)RemoveWindowSubclass(window,window_proc,id);
  // No input, focus, activation, navigation, or handoff message is intercepted.
  return DefSubclassProc(window,message,wp,lp);
}

inline void apply_native_window(HWND browser_window){
  const auto root=GetAncestor(browser_window,GA_ROOT);if(!root)return;
  DWORD process{};GetWindowThreadProcessId(root,&process);if(process!=GetCurrentProcessId())return;
  DWORD_PTR existing{};if(GetWindowSubclass(root,window_proc,subclass_id,&existing))return;
  if(!SetWindowSubclass(root,window_proc,subclass_id,0))return;
  for(const auto kind:{ICON_SMALL,ICON_BIG}){
    const auto icon=branding_icon(GetSystemMetrics(kind==ICON_BIG?SM_CXICON:SM_CXSMICON));
    if(icon)SendMessageW(root,WM_SETICON,kind,reinterpret_cast<LPARAM>(icon));
  }
  const int length=(std::min)(GetWindowTextLengthW(root),32768);
  std::wstring title(static_cast<size_t>(length)+1,L'\0');
  const int copied=GetWindowTextW(root,title.data(),length+1);title.resize(static_cast<size_t>((std::max)(copied,0)));
  SetWindowTextW(root,title.c_str());
}
}
}
