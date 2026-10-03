#include "xenon/ui_theme.hpp"
#include <iostream>
#include <stdexcept>
using namespace xenon;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
COLORREF button_pixel(HWND button){
  const auto screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);auto bitmap=CreateCompatibleBitmap(screen,48,36);const auto old=SelectObject(dc,bitmap);
  DRAWITEMSTRUCT item{};item.CtlType=ODT_BUTTON;item.hwndItem=button;item.hDC=dc;item.rcItem={0,0,48,36};ui::button(item,reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
  const auto color=GetPixel(dc,24,18);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);return color;
}
void reset_preferences(){ui::theme_mode=ui::ThemeMode::system;ui::sidebar_width=240;}
}
int main(){try{
  // Hidden real Win32 controls exercise class-name normalization and subclass
  // attachment. These messages simulate native UI events, not physical input.
  INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);ui::VectorRenderer vectors;
  auto parent=CreateWindowExW(0,L"STATIC",L"Synthetic native UI test",WS_OVERLAPPED,0,0,200,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  require(parent!=nullptr,"Create hidden fixture parent");
  auto button=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,48,36,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
  require(button!=nullptr,"Create real built-in button");ui::control_theme(button);DWORD_PTR data{};
  require(GetWindowSubclass(button,ui::hover_proc,1,&data)!=FALSE,"Attach hover handler to the mixed-case Win32 Button class");
  for(const auto mode:{ui::ThemeMode::light,ui::ThemeMode::dark}){
    ui::theme_mode=mode;SendMessageW(button,WM_MOUSELEAVE,0,0);const auto normal=button_pixel(button);require(normal==ui::palette().surface,"Normal button uses themed surface");
    SendMessageW(button,WM_MOUSEMOVE,0,MAKELPARAM(20,15));require(GetPropW(button,L"XenonHover")!=nullptr,"Mouse movement enters hover state");
    const auto hovered=button_pixel(button);require(hovered==ui::hover_background()&&hovered!=normal,"Hover paints a distinct background in Light and Dark");
    SendMessageW(button,WM_MOUSELEAVE,0,0);require(GetPropW(button,L"XenonHover")==nullptr&&button_pixel(button)==normal,"Mouse leave restores normal background");
  }
  auto list=CreateWindowExW(0,L"LISTBOX",L"",WS_CHILD|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,0,0,100,60,parent,nullptr,GetModuleHandleW(nullptr),nullptr);require(list!=nullptr,"Create real built-in listbox");ui::control_theme(list);
  require(SendMessageW(list,LB_GETITEMHEIGHT,0,0)==ui::dip(list,28),"Theme mixed-case Win32 ListBox at its physical DPI");DestroyWindow(parent);

  const auto fixture_directory=std::filesystem::absolute("ui-theme-tests"),root=fixture_directory/local_security::random_hex(8);std::filesystem::create_directories(root);
  {std::ofstream output(root/"ui-settings.json");output<<Json{{"theme","dark"}};}
  reset_preferences();ui::load_theme(root);require(ui::theme_mode==ui::ThemeMode::dark&&ui::sidebar_width==240,"Load legacy theme without changing default sidebar width");
  ui::sidebar_width=340;require(ui::save_theme(ui::ThemeMode::light),"Save theme and sidebar atomically");reset_preferences();ui::load_theme(root);
  require(ui::theme_mode==ui::ThemeMode::light&&ui::sidebar_width==340,"Restore chosen width with saved theme");
  for(const auto width:{-1000,10000}){{std::ofstream output(root/"ui-settings.json");output<<Json{{"theme","system"},{"sidebarWidth",width}};}
    reset_preferences();ui::load_theme(root);require(ui::sidebar_width==(width<0?180:480),"Bound persisted sidebar widths");}
  {std::ofstream output(root/"ui-settings.json");output<<"{ invalid settings";}
  reset_preferences();ui::load_theme(root);require(ui::theme_mode==ui::ThemeMode::system&&ui::sidebar_width==240,"Malformed settings retain safe defaults");
  require(std::filesystem::canonical(root).parent_path()==fixture_directory,"Cleanup stays inside the generated fixture directory");std::filesystem::remove_all(root);
  std::cout<<"Native UI tests passed: real Win32 class matching, hover paint/leave, DPI list height, legacy settings and sidebar persistence\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
