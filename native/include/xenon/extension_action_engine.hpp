#pragma once
#include <windows.h>
#include <functional>

namespace xenon::extension_action {
// Optional versioned export from Xenon's pinned CEF source patch. All calls
// stay on CEF's UI thread and originate in native human chrome, never MCP.
enum Command {version=0,register_tab=1,select_tab=2,unregister_tab=3,invoke=4,
  close=5,pending=6,invoke_with_reload=7,pin=8,unpin=9,block_site=10,
  allow_site=11,on_click=12,on_site=13,on_all_sites=14,remove=15,clear_selection=16,info=17,capabilities=18,
  anchor=19,side_visible=20,side_close=21,side_layout=22,close_surfaces=24,side_toggle=25,side_pending=26};
inline std::function<int(int)> surface_guard;
inline int guard_callback(int browser){return surface_guard?surface_guard(browser):0;}
inline void register_guard(){
  using Register=void(*)(int(*)(int));
  const auto api=reinterpret_cast<Register>(GetProcAddress(GetModuleHandleW(L"libcef.dll"),"cef_xenon_extension_guard_v1"));
  if(api)api(surface_guard?guard_callback:nullptr);
}
inline void set_guard(std::function<int(int)> guard){surface_guard=std::move(guard);register_guard();}
using Entry=int(*)(int,int,const char*,int,int,int,int);
inline Entry entry(){static const auto value=reinterpret_cast<Entry>(
  GetProcAddress(GetModuleHandleW(L"libcef.dll"),"cef_xenon_extension_action_v1"));return value;}
inline int call(Command command,int browser=0,const char* extension=nullptr,RECT anchor={}){
  // OnContextInitialized may precede Chromium's UI-thread registration. Bind
  // again when an ordinary tab is registered/selected on the live UI thread.
  if((command==register_tab||command==select_tab)&&surface_guard)register_guard();
  const auto api=entry();return api?api(command,browser,extension,anchor.left,anchor.top,
    anchor.right-anchor.left,anchor.bottom-anchor.top):-100;
}
}
