#include "xenon/ui_theme.hpp"
#include "xenon/introduction.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <set>
using namespace xenon;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
COLORREF button_pixel(HWND button){
  const auto dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=48;info.bmiHeader.biHeight=-36;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* pixels{};
  auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);require(dc&&bitmap&&pixels,"Create fixed 32-bit drawing surface");const auto old=SelectObject(dc,bitmap);
  DRAWITEMSTRUCT item{};item.CtlType=ODT_BUTTON;item.hwndItem=button;item.hDC=dc;item.rcItem={0,0,48,36};ui::button(item,reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
  GdiFlush();
  // Read the actual 32-bit DIB after flushing GDI. GetPixel can report a
  // device-converted value that differs from these offscreen bitmap bytes.
  const auto pixel=reinterpret_cast<const unsigned*>(pixels)[18*48+24];const auto color=RGB((pixel>>16)&255,(pixel>>8)&255,pixel&255);
  SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return color;
}
void reset_preferences(){ui::theme_mode=ui::ThemeMode::system;ui::sidebar_width=240;ui::language=ui::preferred_language=ui::Language::english;}
struct EditCommands {int submitted{},cancelled{};};
LRESULT CALLBACK edit_fixture(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
  auto& commands=*reinterpret_cast<EditCommands*>(data);
  if(message==WM_KEYDOWN&&wp==VK_RETURN){++commands.submitted;return 0;}
  if(message==WM_KEYDOWN&&wp==VK_ESCAPE){++commands.cancelled;return 0;}
  return DefSubclassProc(window,message,wp,lp);
}
}
int main(){try{
  // Hidden real Win32 controls exercise class-name normalization and subclass
  // attachment. These messages simulate native UI events, not physical input.
  INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);ui::VectorRenderer vectors;
  auto parent=CreateWindowExW(0,L"STATIC",L"Synthetic native UI test",WS_OVERLAPPED,0,0,200,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  require(parent!=nullptr,"Create hidden fixture parent");
  auto address=CreateWindowExW(0,L"EDIT",L"http://127.0.0.1/fixture",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,180,24,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
  require(address!=nullptr,"Create native address-edit fixture");EditCommands commands;SetWindowSubclass(address,edit_fixture,1,reinterpret_cast<DWORD_PTR>(&commands));
  for(const auto key:{VK_RETURN,VK_ESCAPE}){MSG message{};message.hwnd=address;message.message=WM_KEYDOWN;message.wParam=key;
    require(ui::edit_command(message,address),"Address command is handled before Windows dialog translation");
  }
  require(commands.submitted==1&&commands.cancelled==1,"Enter and Escape each reach the edit handler exactly once");
  require(ui::text(address)=="http://127.0.0.1/fixture","Command routing preserves the entered address");
  for(const auto key:{VK_TAB,static_cast<int>('A'),VK_LEFT}){MSG message{};message.hwnd=address;message.message=WM_KEYDOWN;message.wParam=key;
    require(!ui::edit_command(message,address),"Traversal and editing keys retain native Windows behavior");
  }
  MSG elsewhere{};elsewhere.hwnd=parent;elsewhere.message=WM_KEYDOWN;elsewhere.wParam=VK_RETURN;require(!ui::edit_command(elsewhere,address),"Page and other controls cannot trigger address submission");
  auto button=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,48,36,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
  require(button!=nullptr,"Create real built-in button");ui::control_theme(button);DWORD_PTR data{};
  require(GetWindowSubclass(button,ui::hover_proc,1,&data)!=FALSE,"Attach hover handler to the mixed-case Win32 Button class");
  SendMessageW(button,WM_UPDATEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEFOCUS),0);require(!ui::keyboard_focus(button),"Mouse focus hides the keyboard-only focus mark");
  SendMessageW(button,WM_UPDATEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);require(ui::keyboard_focus(button),"Keyboard navigation retains a visible focus mark");
  // Reduced motion paints the final hover state immediately.
  ui::motion_override=0;
  for(const auto mode:{ui::ThemeMode::light,ui::ThemeMode::dark}){
    ui::theme_mode=mode;SendMessageW(button,WM_MOUSELEAVE,0,0);const auto normal=button_pixel(button);if(normal!=ui::palette().surface)throw std::runtime_error("Normal button uses themed surface: actual="+std::to_string(normal)+" expected="+std::to_string(ui::palette().surface));
    SendMessageW(button,WM_MOUSEMOVE,0,MAKELPARAM(20,15));require(GetPropW(button,L"XenonHover")!=nullptr,"Mouse movement enters hover state");
    const auto hovered=button_pixel(button);require(hovered==ui::hover_background()&&hovered!=normal,"Hover paints a distinct background in Light and Dark");
    SendMessageW(button,WM_MOUSELEAVE,0,0);require(GetPropW(button,L"XenonHover")==nullptr&&button_pixel(button)==normal,"Mouse leave restores normal background");
  }
  require(ui::mix(RGB(0,0,0),RGB(255,255,255),0)==RGB(0,0,0)&&ui::mix(RGB(0,0,0),RGB(255,255,255),1)==RGB(255,255,255)&&ui::mix(RGB(10,200,30),RGB(30,100,30),0.5)==RGB(20,150,30),"Color mixing reaches both endpoints and the midpoint");
  for(const double t:{0.0,0.2,0.5,0.9,1.0})require(std::abs(ui::ease(1-t)-(1-ui::ease(t)))<1e-9&&ui::ease(t)>=0&&ui::ease(t)<=1,"Easing is bounded and symmetric so reversals stay continuous");
  {const auto accent=ui::shell_repaint(false,false,true,false);require(accent.tree&&accent.window,"An ownership color fade repaints the tab pill with the page frame");
    const auto focus=ui::shell_repaint(false,false,false,true);require(!focus.tree&&focus.window,"An address focus fade repaints only the window");
    const auto rows=ui::shell_repaint(true,false,false,false);require(rows.tree&&!rows.window,"A row hover fade repaints only the tab tree");
    const auto glide=ui::shell_repaint(false,true,false,false);require(glide.tree&&glide.window,"A selection glide repaints the tab tree and the window");}
  // With animation effects on, hover fades both ways and reversal never jumps.
  ui::motion_override=1;
  {ui::Fade fade;fade.set(true,1000);Sleep(300);const auto shown=fade.value(1000);fade.set(false,1000);require(std::abs(fade.value(1000)-shown)<0.05&&fade.active(1000),"Reversing a fade resumes from the displayed value");
    ui::motion_override=0;require(fade.value(1000)==0.0&&!fade.active(1000),"Reduced motion shows the target state immediately");ui::motion_override=1;}
  for(const auto mode:{ui::ThemeMode::light,ui::ThemeMode::dark}){
    ui::theme_mode=mode;const auto rest=ui::palette().surface;require(button_pixel(button)==rest,"Fade test starts at rest");
    SendMessageW(button,WM_MOUSEMOVE,0,MAKELPARAM(20,15));require(GetPropW(button,L"XenonHover")!=nullptr&&button_pixel(button)!=ui::hover_background(),"Hover starts from the resting color when motion is enabled");
    Sleep(ui::hover_duration+80);require(button_pixel(button)==ui::hover_background(),"Hover fade settles on the hover color");
    SendMessageW(button,WM_MOUSELEAVE,0,0);require(button_pixel(button)!=rest,"Mouse leave fades out instead of jumping");
    Sleep(ui::hover_duration+80);require(button_pixel(button)==rest,"Mouse leave fade settles on the resting color");
  }
  ui::motion_override=-1;
  auto list=CreateWindowExW(0,L"LISTBOX",L"",WS_CHILD|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,0,0,100,60,parent,nullptr,GetModuleHandleW(nullptr),nullptr);require(list!=nullptr,"Create real built-in listbox");ui::control_theme(list);
  require(SendMessageW(list,LB_GETITEMHEIGHT,0,0)==ui::dip(list,28),"Theme mixed-case Win32 ListBox at its physical DPI");DestroyWindow(parent);

  const auto fixture_directory=std::filesystem::absolute("ui-theme-tests"),root=fixture_directory/local_security::random_hex(8);std::filesystem::create_directories(root);
  require(ui::needs_introduction(root)&&ui::needs_introduction(root/"not-created"),"Fresh and empty profiles start with the introduction");
  {std::ofstream temporary(root/"ui-settings.json.tmp");temporary<<"partial save";}
  require(ui::needs_introduction(root),"A failed settings save does not suppress a fresh-start retry");std::filesystem::remove(root/"ui-settings.json.tmp");
  {std::ofstream output(root/"ui-settings.json");output<<Json{{"theme","dark"}};}
  reset_preferences();ui::load_theme(root);require(ui::theme_mode==ui::ThemeMode::dark&&ui::sidebar_width==240,"Load legacy theme without changing default sidebar width");
  require(!ui::needs_introduction(root)&&!ui::introduction_completed,"Existing installations skip the introduction without losing legacy preferences");
  require(ui::language==ui::Language::english,"Legacy settings keep English");
  require(ui::save_language(ui::Language::simplified_chinese),"Save Simplified Chinese");
  require(ui::language==ui::Language::english&&ui::preferred_language==ui::Language::simplified_chinese,"Language changes wait for restart");
  ui::sidebar_width=340;require(ui::save_theme(ui::ThemeMode::light),"Save theme and sidebar atomically");reset_preferences();ui::load_theme(root);
  require(ui::theme_mode==ui::ThemeMode::light&&ui::sidebar_width==340,"Restore chosen width with saved theme");
  require(ui::language==ui::Language::simplified_chinese,"Theme and sidebar saves preserve the language choice");
  require(std::string_view(ui::web_color_scheme_switch(ui::ThemeMode::light))=="force-light-mode"&&std::string_view(ui::web_color_scheme_switch(ui::ThemeMode::dark))=="force-dark-mode"
    &&ui::web_color_scheme_switch(ui::ThemeMode::system)==nullptr,"Light and Dark force web content; System follows Windows");
  require(ui::web_theme_mode==ui::ThemeMode::system,"Saving a theme leaves this run's web content theme unchanged");
  require(std::wstring(ui::tr(L"Xenon Controls"))==L"Xenon 控制中心"&&ui::tr8("New tab")=="新标签页","Translate native captions in Unicode");
  require(std::string(ui::language_tag(ui::language))=="zh-CN","Chinese uses the matching CEF locale");
  LOGFONTW chinese_font{};const auto face=ui::font(GetDesktopWindow());require(GetObjectW(face,sizeof(chinese_font),&chinese_font)!=0,"Inspect localized UI font");DeleteObject(face);
  require(std::wstring(chinese_font.lfFaceName)==L"Microsoft YaHei UI","Choose a font that supports Chinese controls");
  std::set<std::wstring_view> translated_keys;
  for(const auto& entry:ui::translations){require(translated_keys.insert(entry.english).second,"Translation keys are unique");require(std::wstring_view(entry.chinese).size()!=0,"Translations cannot be empty");}
  require(ui::workspace_label("native-default","Personal")=="个人"&&ui::workspace_label("custom","Personal")=="Personal","Translate built-in workspace labels without changing user names");
  require(ui::tab_title("New tab","about:blank")=="新标签页"&&ui::tab_title("New tab","https://example.test")=="New tab","Translate blank-tab labels without translating website titles");
  require(ui::tab_title("New tab","")=="新标签页","Translate the initial blank-tab label before its first URL callback");
  require(ui::tab_title("about:blank","about:blank")=="新标签页"&&ui::tab_title("about:blank","https://example.test")=="about:blank","Translate CEF's blank URL title without changing website titles");
  const auto* file_filter=ui::file_filter(true);const auto* pattern=file_filter+wcslen(file_filter)+1;
  require(std::wstring(pattern)==L"*.csv"&&std::wstring(pattern+wcslen(pattern)+1)==L"所有文件","Localized file filters preserve embedded NUL separators");
  {std::ofstream output(root/"ui-settings.json");output<<Json{{"theme","dark"},{"language","unsupported"}};}
  reset_preferences();ui::load_theme(root);require(ui::language==ui::Language::english&&ui::theme_mode==ui::ThemeMode::dark,"Unknown language falls back to English");
  {std::ofstream output(root/"ui-settings.json");output<<Json{{"theme","light"},{"language",42},{"sidebarWidth",310}};}
  reset_preferences();ui::load_theme(root);require(ui::language==ui::Language::english&&ui::sidebar_width==310,"Invalid language type preserves other settings");
  ui::settings_path=root/"missing"/"ui-settings.json";require(!ui::save_language(ui::Language::simplified_chinese)&&ui::preferred_language==ui::Language::english,"Failed language save preserves the previous choice");
  require(!ui::complete_introduction(ui::Language::simplified_chinese)&&!ui::introduction_completed&&ui::preferred_language==ui::Language::english,"Failed introduction save cannot mark setup complete or change language");
  ui::settings_path=root/"ui-settings.json";require(ui::save_language(ui::Language::english),"Save English selection");reset_preferences();ui::load_theme(root);require(ui::language==ui::Language::english,"Restore English after switching back");
  require(ui::complete_introduction(ui::Language::simplified_chinese)&&ui::introduction_completed&&ui::language==ui::Language::english,"Completion saves the language for startup without changing a live process");
  reset_preferences();ui::load_theme(root);require(ui::introduction_completed&&ui::language==ui::Language::simplified_chinese&&ui::sidebar_width==310&&ui::theme_mode==ui::ThemeMode::light,"Completion restores language and preserves other preferences");
  require(ui::save_language(ui::Language::english)&&ui::save_theme(ui::ThemeMode::dark),"Later settings changes succeed");
  reset_preferences();ui::load_theme(root);require(ui::introduction_completed,"Later theme and language changes preserve completed introduction");
  for(const auto width:{-1000,10000}){{std::ofstream output(root/"ui-settings.json");output<<Json{{"theme","system"},{"sidebarWidth",width}};}
    reset_preferences();ui::load_theme(root);require(ui::sidebar_width==(width<0?180:480),"Bound persisted sidebar widths");}
  {std::ofstream output(root/"ui-settings.json");output<<"{ invalid settings";}
  reset_preferences();ui::load_theme(root);require(ui::theme_mode==ui::ThemeMode::system&&ui::sidebar_width==240,"Malformed settings retain safe defaults");
  require(std::filesystem::canonical(root).parent_path()==fixture_directory,"Cleanup stays inside the generated fixture directory");std::filesystem::remove_all(root);
  std::cout<<"Native UI tests passed: address commands, focus, hover and hover fades, DPI, settings and language persistence, Unicode captions, Chinese font and unchanged user/site names\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
