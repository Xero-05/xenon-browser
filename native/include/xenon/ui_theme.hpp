#pragma once
#include "xenon/branding.hpp"
#include "xenon/contracts.hpp"
#include "xenon/local_security.hpp"
#include "xenon/ui_language.hpp"
#include <dwmapi.h>
#include <commctrl.h>
#include <fstream>
#include <filesystem>
#include <string>
#include <uxtheme.h>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <objidl.h>
#include <gdiplus.h>

namespace xenon::ui {
enum class ThemeMode { system,light,dark };
struct Palette {COLORREF canvas,surface,ink,muted,border,teal,orange,gray;bool dark{},contrast{};bool operator==(const Palette&)const=default;};
inline ThemeMode theme_mode=ThemeMode::system;
// Native-hosted Alloy pages take prefers-color-scheme from Chromium's
// process-wide NativeTheme, which reads these CEF switches once at startup.
// Web content therefore keeps the theme this process started with.
inline ThemeMode web_theme_mode=ThemeMode::system;
inline const char* web_color_scheme_switch(ThemeMode mode) {return mode==ThemeMode::light?"force-light-mode":mode==ThemeMode::dark?"force-dark-mode":nullptr;}
inline int sidebar_width=240;
inline std::filesystem::path settings_path;
inline bool introduction_completed{};
// Metadata-only release checks in the background. Downloading and installing
// always remain explicit native actions.
inline bool automatic_update_checks=true;
inline bool high_contrast() {HIGHCONTRASTW value{sizeof(value)};return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(value),&value,0)&&(value.dwFlags&HCF_HIGHCONTRASTON);}
inline Palette palette() {
  if(high_contrast())return {GetSysColor(COLOR_WINDOW),GetSysColor(COLOR_WINDOW),GetSysColor(COLOR_WINDOWTEXT),GetSysColor(COLOR_WINDOWTEXT),GetSysColor(COLOR_WINDOWFRAME),GetSysColor(COLOR_HIGHLIGHT),GetSysColor(COLOR_HIGHLIGHT),GetSysColor(COLOR_WINDOWFRAME),false,true};
  DWORD light=1,size=sizeof(light);RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);
  const bool dark=theme_mode==ThemeMode::dark||(theme_mode==ThemeMode::system&&!light);
  return dark?Palette{RGB(33,33,33),RGB(48,48,48),RGB(243,243,243),RGB(185,185,185),RGB(73,73,73),RGB(20,184,166),RGB(245,158,11),RGB(139,139,139),true,false}:
    Palette{RGB(255,255,255),RGB(247,247,247),RGB(17,17,17),RGB(100,100,100),RGB(220,220,220),RGB(0,132,120),RGB(180,95,0),RGB(115,115,115),false,false};
}
inline void load_theme(const std::filesystem::path& root) {
  settings_path=root/"ui-settings.json";
  language=preferred_language=Language::english;
  introduction_completed=false;automatic_update_checks=true;
  try{if(std::filesystem::exists(settings_path)&&std::filesystem::file_size(settings_path)<4096){Json value;std::ifstream(settings_path)>>value;
    const auto mode=value.value("theme",std::string("system"));theme_mode=mode=="dark"?ThemeMode::dark:mode=="light"?ThemeMode::light:ThemeMode::system;
    if(auto locale=value.find("language");locale!=value.end()&&locale->is_string())
      language=preferred_language=*locale=="zh-CN"?Language::simplified_chinese:Language::english;
    if(auto completed=value.find("introductionCompleted");completed!=value.end()&&completed->is_boolean())introduction_completed=completed->get<bool>();
    if(auto updates=value.find("automaticUpdateChecks");updates!=value.end()&&updates->is_boolean())automatic_update_checks=updates->get<bool>();
    if(auto width=value.find("sidebarWidth");width!=value.end()&&width->is_number_integer())sidebar_width=std::clamp(width->get<int>(),180,480);}}catch(...){}
}
inline bool save_settings() {
  try{auto temporary=settings_path;temporary+=L".tmp";{std::ofstream stream(temporary);stream<<Json{{"version",1},{"theme",theme_mode==ThemeMode::dark?"dark":theme_mode==ThemeMode::light?"light":"system"},{"sidebarWidth",sidebar_width},{"language",language_tag(preferred_language)},{"introductionCompleted",introduction_completed},{"automaticUpdateChecks",automatic_update_checks}};stream.flush();if(!stream)return false;}
    local_security::restrict_path(temporary);return MoveFileExW(temporary.c_str(),settings_path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;}catch(...){return false;}
}
inline bool save_theme(ThemeMode mode) {theme_mode=mode;return save_settings();}
inline bool save_automatic_update_checks(bool enabled) {
  const auto previous=automatic_update_checks;automatic_update_checks=enabled;
  if(save_settings())return true;automatic_update_checks=previous;return false;
}
inline bool save_language(Language value) {
  const auto previous=preferred_language;preferred_language=value;
  if(save_settings())return true;preferred_language=previous;return false;
}
inline bool complete_introduction(Language value) {
  const auto previous=preferred_language;const bool completed=introduction_completed;
  preferred_language=value;introduction_completed=true;
  if(save_settings())return true;
  preferred_language=previous;introduction_completed=completed;return false;
}
inline int dip(HWND window,int value) {return MulDiv(value,GetDpiForWindow(window),96);}
inline void frame(HWND window) {BOOL dark=palette().dark;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));}
inline HFONT font(HWND window,int size=14,int weight=FW_NORMAL,Language locale=language) {return CreateFontW(-dip(window,size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,locale==Language::simplified_chinese?L"Microsoft YaHei UI":L"Segoe UI");}
inline void icons(HWND window) {SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(branding_icon(GetSystemMetrics(SM_CXSMICON))));SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(branding_icon(GetSystemMetrics(SM_CXICON))));frame(window);}
inline void fill(HDC dc,RECT rect,COLORREF color) {auto brush=CreateSolidBrush(color);FillRect(dc,&rect,brush);DeleteObject(brush);}
inline COLORREF selection() {const auto colors=palette();return colors.contrast?GetSysColor(COLOR_HIGHLIGHT):colors.dark?RGB(65,65,65):RGB(228,228,228);}
inline COLORREF hover_background() {const auto colors=palette();return colors.contrast?GetSysColor(COLOR_HIGHLIGHT):colors.dark?RGB(80,80,80):RGB(226,226,226);}
inline COLORREF selection_ink() {return palette().contrast?GetSysColor(COLOR_HIGHLIGHTTEXT):palette().ink;}
// The notification dot for an available update, distinct from the ownership
// accents (teal/orange) used for agents and pairing requests.
inline COLORREF notice_blue() {const auto colors=palette();return colors.contrast?GetSysColor(COLOR_HOTLIGHT):colors.dark?RGB(96,165,250):RGB(0,103,192);}
// Native chrome motion follows the Windows "Animation effects" setting and is
// off in high contrast. Website geometry never animates; only shell paint does.
inline int motion_override=-1;
inline bool motion() {if(motion_override>=0)return motion_override!=0;BOOL enabled=TRUE;if(!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0))enabled=TRUE;return enabled&&!high_contrast();}
// Symmetric easing keeps a reversed transition continuous: ease(1-t)==1-ease(t).
inline double ease(double t) {t=std::clamp(t,0.0,1.0);return t*t*(3-2*t);}
inline COLORREF mix(COLORREF from,COLORREF to,double amount) {amount=std::clamp(amount,0.0,1.0);auto channel=[&](int a,int b){return static_cast<BYTE>(a+(b-a)*amount+0.5);};
  return RGB(channel(GetRValue(from),GetRValue(to)),channel(GetGValue(from),GetGValue(to)),channel(GetBValue(from),GetBValue(to)));}
// A two-state transition sampled while painting. Reversing mid-flight resumes
// from the displayed value, so rapid hover changes never jump.
struct Fade {
  ULONGLONG start{};bool on{};
  double progress(int duration) const {return start&&motion()?std::clamp(static_cast<double>(GetTickCount64()-start)/duration,0.0,1.0):1.0;}
  double value(int duration) const {const auto t=ease(progress(duration));return on?t:1-t;}
  bool active(int duration) const {return progress(duration)<1;}
  void set(bool next,int duration){if(next==on)return;const auto done=progress(duration);start=GetTickCount64()-static_cast<ULONGLONG>((1-done)*duration);on=next;}
};
constexpr int hover_duration=140;
// Surfaces a shell motion frame repaints. The tab tree paints the selected pill
// in the same ownership accent as the page frame, so an accent fade repaints
// both; otherwise the pill keeps the color it had when the fade began.
struct ShellRepaint {bool tree{},window{};};
inline ShellRepaint shell_repaint(bool rows_fading,bool gliding,bool accent_fading,bool focus_fading) {
  return {rows_fading||gliding||accent_fading,gliding||accent_fading||focus_fading};
}
// Draw vectors at the window's physical DPI. Keep native HWNDs for input and
// accessibility; no raster assets or scaled copies of the interface are used.
inline ULONG_PTR vector_token{};
class VectorRenderer {
 public:
  VectorRenderer(){Gdiplus::GdiplusStartupInput input;if(Gdiplus::GdiplusStartup(&vector_token,&input,nullptr)!=Gdiplus::Ok)vector_token=0;}
  ~VectorRenderer(){if(vector_token)Gdiplus::GdiplusShutdown(vector_token);vector_token=0;}
  VectorRenderer(const VectorRenderer&)=delete;VectorRenderer& operator=(const VectorRenderer&)=delete;
};
inline bool vector_rendering() {return vector_token!=0;}
inline Gdiplus::Color vector_color(COLORREF color) {return Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color));}
inline void rounded(HDC dc,RECT rect,COLORREF background,COLORREF border,int radius,int thickness=1) {
  if(vector_rendering()){Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const float inset=thickness/2.0f,x=rect.left+inset,y=rect.top+inset,w=rect.right-rect.left-thickness,h=rect.bottom-rect.top-thickness,r=std::max(1.0f,std::min(static_cast<float>(radius),std::min(w,h)/2));if(w<=0||h<=0)return;
    Gdiplus::GraphicsPath path;path.AddArc(x,y,r*2,r*2,180,90);path.AddArc(x+w-r*2,y,r*2,r*2,270,90);path.AddArc(x+w-r*2,y+h-r*2,r*2,r*2,0,90);path.AddArc(x,y+h-r*2,r*2,r*2,90,90);path.CloseFigure();
    Gdiplus::SolidBrush brush(vector_color(background));Gdiplus::Pen pen(vector_color(border),static_cast<float>(thickness));graphics.FillPath(&brush,&path);graphics.DrawPath(&pen,&path);return;}
  auto brush=CreateSolidBrush(background);auto pen=CreatePen(PS_SOLID,thickness,border);auto old_brush=SelectObject(dc,brush),old_pen=SelectObject(dc,pen);
  RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius*2,radius*2);SelectObject(dc,old_pen);SelectObject(dc,old_brush);DeleteObject(pen);DeleteObject(brush);
}
inline void outline(HDC dc,RECT rect,COLORREF color,int radius,int thickness) {
  if(vector_rendering()){Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const float inset=thickness/2.0f,x=rect.left+inset,y=rect.top+inset,w=rect.right-rect.left-thickness,h=rect.bottom-rect.top-thickness,r=std::max(1.0f,std::min(static_cast<float>(radius),std::min(w,h)/2));if(w<=0||h<=0)return;
    Gdiplus::GraphicsPath path;path.AddArc(x,y,r*2,r*2,180,90);path.AddArc(x+w-r*2,y,r*2,r*2,270,90);path.AddArc(x+w-r*2,y+h-r*2,r*2,r*2,0,90);path.AddArc(x,y+h-r*2,r*2,r*2,90,90);path.CloseFigure();
    Gdiplus::Pen pen(vector_color(color),static_cast<float>(thickness));graphics.DrawPath(&pen,&path);return;}
  auto pen=CreatePen(PS_INSIDEFRAME,thickness,color);auto old_pen=SelectObject(dc,pen),old_brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
  RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius*2,radius*2);SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(pen);
}
// Rounds a corner where a connected tab's outline meets a panel's left border,
// so the tab flares into the panel. `border` is the border's left edge; `edge`
// is the outer edge of the tab's top (or bottom) line. The radius must fit in
// the gap between the tab and the panel.
inline void tab_junction(HDC dc,int border,int edge,bool top,int thickness,int radius,COLORREF outside,COLORREF inside,COLORREF line) {
  if(!vector_rendering()||radius<=0||thickness<=0)return;
  const float t=static_cast<float>(thickness),r=static_cast<float>(radius),x=border+t/2,y=top?edge+t/2:edge-t/2,cx=x-r,cy=top?y-r:y+r;
  const float right=static_cast<float>(border+thickness),inner=static_cast<float>(top?edge+thickness:edge-thickness),start=top?90.0f:270.0f,sweep=top?-90.0f:90.0f;
  Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  Gdiplus::SolidBrush back(vector_color(outside)),fill(vector_color(inside));const float box_top=top?edge-r:inner,box_bottom=top?inner:edge+r;
  graphics.FillRectangle(&back,border-r,box_top,r+t,box_bottom-box_top);
  Gdiplus::GraphicsPath flare;flare.AddLine(right,cy,right,inner);flare.AddLine(right,inner,cx,inner);flare.AddLine(cx,inner,cx,y);flare.AddArc(cx-r,cy-r,r*2,r*2,start,sweep);flare.CloseFigure();graphics.FillPath(&fill,&flare);
  // Square caps reach exactly to the straight border and tab line around the arc.
  Gdiplus::Pen pen(vector_color(line),t);pen.SetStartCap(Gdiplus::LineCapSquare);pen.SetEndCap(Gdiplus::LineCapSquare);graphics.DrawArc(&pen,cx-r,cy-r,r*2,r*2,start,sweep);
}
inline void dot(HDC dc,POINT center,int radius,COLORREF color) {
  if(vector_rendering()){Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);Gdiplus::SolidBrush brush(vector_color(color));
    graphics.FillEllipse(&brush,static_cast<float>(center.x-radius),static_cast<float>(center.y-radius),static_cast<float>(radius*2),static_cast<float>(radius*2));return;}
  auto brush=CreateSolidBrush(color);auto pen=CreatePen(PS_SOLID,1,color);auto old_brush=SelectObject(dc,brush),old_pen=SelectObject(dc,pen);
  Ellipse(dc,center.x-radius,center.y-radius,center.x+radius,center.y+radius);SelectObject(dc,old_pen);SelectObject(dc,old_brush);DeleteObject(pen);DeleteObject(brush);
}
inline std::wstring wide(const std::string& value) {if(value.empty())return {};const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);std::wstring out(count,L'\0');if(count)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),count);return out;}
inline std::string utf8(const std::wstring& value) {if(value.empty())return {};const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);std::string out(count,'\0');if(count)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),count,nullptr,nullptr);return out;}
inline std::string tr8(const char* english) {return utf8(tr(wide(english).c_str()));}
inline std::string workspace_label(const std::string& id,const std::string& name,bool private_mode=false) {
  if((id=="native-default"&&name=="Personal")||(private_mode&&name=="Private workspace"))return tr8(name.c_str());
  return name;
}
inline std::string tab_title(const std::string& title,const std::string& url) {
  return title.empty()||((url.empty()||url=="about:blank")&&(title=="New tab"||title=="about:blank"))?tr8("New tab"):title;
}
inline std::wstring error_text(const std::string& english) {
  const auto value=wide(english);const auto translated=tr(value.c_str());
  if(language==Language::simplified_chinese&&translated==value.c_str())
    return std::wstring(tr(L"The operation failed. No secret details were logged."))+L"\n"+value;
  return translated;
}
inline std::string text(HWND control) {const int count=GetWindowTextLengthW(control);std::wstring value(count+1,L'\0');GetWindowTextW(control,value.data(),count+1);value.resize(count);return utf8(value);}
inline void text(HDC dc,RECT rect,const std::wstring& value,HFONT face,COLORREF color,UINT flags=DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS) {const auto old=SelectObject(dc,face);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&rect,flags);SelectObject(dc,old);}
// IsDialogMessage otherwise consumes Return/Escape as dialog commands before
// the edit subclass sees them. Other keys retain ordinary native tab traversal.
inline bool edit_command(MSG& message,HWND control) {
  if(message.hwnd!=control||message.message!=WM_KEYDOWN||(message.wParam!=VK_RETURN&&message.wParam!=VK_ESCAPE))return false;
  SendMessageW(control,message.message,message.wParam,message.lParam);return true;
}
inline bool keyboard_focus(HWND control) {return (SendMessageW(control,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)==0;}
// Keyboard focus is a ring that follows the control's own shape, so it reads
// as focus rather than as a stray separator beside the caption.
inline void focus_mark(HDC dc,RECT rect,HWND control,int radius=6) {
  if(!keyboard_focus(control))return;outline(dc,rect,palette().ink,dip(control,radius),dip(control,2));
}
enum class Icon {none,back,forward,reload,stop,go,controls,menu,plus,up,down,chevron_right,close,extensions,key,star,star_filled,pin,muted,more};
// The browser toolbar's saved-password button. Native credential bubbles
// anchor to it inside their source window when it is visible.
inline constexpr int password_anchor_id=2015;
inline void icon(HDC dc,RECT rect,Icon kind,COLORREF ink,HWND window) {
  if(vector_rendering()){Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);const float scale=GetDpiForWindow(window)/96.0f,cx=(rect.left+rect.right)/2.0f,cy=(rect.top+rect.bottom)/2.0f;
    Gdiplus::Pen pen(vector_color(ink),1.8f*scale);pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);pen.SetLineJoin(Gdiplus::LineJoinRound);Gdiplus::SolidBrush brush(vector_color(ink));
    auto line=[&](float x,float y,float xx,float yy){graphics.DrawLine(&pen,cx+x*scale,cy+y*scale,cx+xx*scale,cy+yy*scale);};
    if(kind==Icon::back||kind==Icon::forward||kind==Icon::go){const float sign=kind==Icon::back?-1.0f:1.0f;line(-7,0,7,0);line(sign*7,0,sign*1,-6);line(sign*7,0,sign*1,6);}
    else if(kind==Icon::reload){graphics.DrawArc(&pen,cx-7*scale,cy-7*scale,14*scale,14*scale,45.0f,270.0f);line(5,-5,7,-3);line(7,-3,7,-8);line(7,-3,2,-3);}
    else if(kind==Icon::stop)graphics.DrawRectangle(&pen,cx-6*scale,cy-6*scale,12*scale,12*scale);
    else if(kind==Icon::plus){line(-7,0,7,0);line(0,-7,0,7);}
    else if(kind==Icon::close){line(-5,-5,5,5);line(-5,5,5,-5);}
    else if(kind==Icon::up||kind==Icon::down){const float sign=kind==Icon::up?-1.0f:1.0f;line(-5,-sign*3,0,sign*3);line(0,sign*3,5,-sign*3);}
    else if(kind==Icon::chevron_right){line(-3,-5,3,0);line(3,0,-3,5);}
    else if(kind==Icon::controls){for(float y:{-6.0f,0.0f,6.0f}){const float x=y==0?4.0f:-3.0f;line(-8,y,x-2,y);line(x+2,y,8,y);graphics.DrawEllipse(&pen,cx+(x-2)*scale,cy+(y-2)*scale,4*scale,4*scale);}}
    else if(kind==Icon::menu)for(float y:{-6.0f,0.0f,6.0f})graphics.FillEllipse(&brush,cx-1.5f*scale,cy+(y-1.5f)*scale,3*scale,3*scale);
    else if(kind==Icon::more)for(float x:{-6.0f,0.0f,6.0f})graphics.FillEllipse(&brush,cx+(x-1.5f)*scale,cy-1.5f*scale,3*scale,3*scale);
    else if(kind==Icon::extensions){
      // Puzzle piece: square body with knobs on its top and right edges.
      line(-6,-3,-3,-3);line(2,-3,5,-3);line(5,-3,5,-0.5f);line(5,4.5f,5,7);line(5,7,-6,7);line(-6,7,-6,-3);
      graphics.DrawArc(&pen,cx-3*scale,cy-5.5f*scale,5*scale,5*scale,180.0f,180.0f);graphics.DrawArc(&pen,cx+2.5f*scale,cy-0.5f*scale,5*scale,5*scale,270.0f,180.0f);}
    else if(kind==Icon::key){graphics.DrawEllipse(&pen,cx-8*scale,cy-3.5f*scale,7*scale,7*scale);line(-1,0,8,0);line(5,0,5,3.5f);line(7.5f,0,7.5f,2.5f);}
    else if(kind==Icon::star||kind==Icon::star_filled){Gdiplus::PointF points[10];for(int n=0;n<10;++n){const double angle=-1.5707963+n*0.6283185;const float radius=(n%2?3.3f:7.5f)*scale;points[n]={cx+radius*static_cast<float>(std::cos(angle)),cy+0.8f*scale+radius*static_cast<float>(std::sin(angle))};}
      if(kind==Icon::star_filled)graphics.FillPolygon(&brush,points,10);graphics.DrawPolygon(&pen,points,10);}
    else if(kind==Icon::pin){graphics.DrawRectangle(&pen,cx-3*scale,cy-7*scale,6*scale,6*scale);line(-5.5f,-1,5.5f,-1);line(0,-1,0,7);}
    else if(kind==Icon::muted){Gdiplus::PointF speaker[]{{cx-7*scale,cy-2.5f*scale},{cx-4*scale,cy-2.5f*scale},{cx,cy-6*scale},{cx,cy+6*scale},{cx-4*scale,cy+2.5f*scale},{cx-7*scale,cy+2.5f*scale}};graphics.DrawPolygon(&pen,speaker,6);line(3,-3,7.5f,3);line(3,3,7.5f,-3);}
    return;}
  const int unit=dip(window,1),cx=(rect.left+rect.right)/2,cy=(rect.top+rect.bottom)/2;auto d=[&](int v){return dip(window,v);};
  auto pen=CreatePen(PS_SOLID,std::max(1,d(2)),ink);auto old_pen=SelectObject(dc,pen),old_brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
  auto line=[&](int x,int y,int xx,int yy){MoveToEx(dc,cx+d(x),cy+d(y),nullptr);LineTo(dc,cx+d(xx),cy+d(yy));};
  if(kind==Icon::back||kind==Icon::forward||kind==Icon::go){const int sign=kind==Icon::back?-1:1;line(-7,0,7,0);line(sign*7,0,sign*1,-6);line(sign*7,0,sign*1,6);}
  else if(kind==Icon::reload){Arc(dc,cx-d(7),cy-d(7),cx+d(7),cy+d(7),cx+d(5),cy-d(5),cx+d(5),cy+d(5));line(5,-5,7,-3);line(7,-3,7,-8);line(7,-3,2,-3);}
  else if(kind==Icon::stop)Rectangle(dc,cx-d(6),cy-d(6),cx+d(6),cy+d(6));
  else if(kind==Icon::plus){line(-7,0,7,0);line(0,-7,0,7);}
  else if(kind==Icon::close){line(-5,-5,5,5);line(-5,5,5,-5);}
  else if(kind==Icon::up||kind==Icon::down){const int sign=kind==Icon::up?-1:1;line(-5,-sign*3,0,sign*3);line(0,sign*3,5,-sign*3);}
  else if(kind==Icon::chevron_right){line(-3,-5,3,0);line(3,0,-3,5);}
  else if(kind==Icon::controls){for(int y:{-6,0,6}){line(-8,y,8,y);const int x=y==0?4:-3;Ellipse(dc,cx+d(x-2),cy+d(y-2),cx+d(x+2)+unit,cy+d(y+2)+unit);}}
  else if(kind==Icon::menu||kind==Icon::more){auto brush=CreateSolidBrush(ink);SelectObject(dc,brush);for(int n:{-6,0,6}){const int x=kind==Icon::more?n:0,y=kind==Icon::more?0:n;Ellipse(dc,cx+d(x-1),cy+d(y-1),cx+d(x+1)+unit,cy+d(y+1)+unit);}SelectObject(dc,old_brush);DeleteObject(brush);}
  else if(kind==Icon::extensions){Rectangle(dc,cx-d(6),cy-d(3),cx+d(5),cy+d(7));Ellipse(dc,cx-d(3),cy-d(7),cx+d(2),cy-d(2));Ellipse(dc,cx+d(4),cy-d(1),cx+d(9),cy+d(4));}
  else if(kind==Icon::key){Ellipse(dc,cx-d(8),cy-d(3),cx-d(1),cy+d(4));line(-1,0,8,0);line(5,0,5,3);line(7,0,7,2);}
  else if(kind==Icon::star||kind==Icon::star_filled){POINT points[10];for(int n=0;n<10;++n){const double angle=-1.5707963+n*0.6283185;const int radius=d(n%2?3:7);points[n]={cx+static_cast<LONG>(radius*std::cos(angle)),cy+d(1)+static_cast<LONG>(radius*std::sin(angle))};}
    auto brush=kind==Icon::star_filled?CreateSolidBrush(ink):nullptr;if(brush)SelectObject(dc,brush);Polygon(dc,points,10);if(brush){SelectObject(dc,GetStockObject(NULL_BRUSH));DeleteObject(brush);}}
  else if(kind==Icon::pin){Rectangle(dc,cx-d(3),cy-d(7),cx+d(3),cy-d(1));line(-5,-1,5,-1);line(0,-1,0,7);}
  else if(kind==Icon::muted){POINT speaker[]{{cx-d(7),cy-d(2)},{cx-d(4),cy-d(2)},{cx,cy-d(6)},{cx,cy+d(6)},{cx-d(4),cy+d(2)},{cx-d(7),cy+d(2)}};Polygon(dc,speaker,6);line(3,-3,7,3);line(3,3,7,-3);}
  SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(pen);
}
// Replaces {0}, {1}... in translated native copy with already-wide values.
inline std::wstring format(std::wstring text,std::initializer_list<std::wstring> values) {
  size_t index=0;for(const auto& value:values){const auto key=L"{"+std::to_wstring(index++)+L"}";for(size_t at=text.find(key);at!=std::wstring::npos;at=text.find(key,at+value.size()))text.replace(at,key.size(),value);}
  return text;
}
// Shared native confirmation for workspace cleanup in the shell and Controls.
inline bool confirm_close_tabs(HWND owner,size_t count,const std::string& workspace) {
  const auto message=format(tr(L"Close all {0} tabs in {1}?\n\nQueued agent actions in these tabs are canceled; actions already started finish first. Unsaved changes in these tabs are lost.\n\nThe workspace, its sign-ins, history, permissions, agent access and saved passwords are kept."),{std::to_wstring(count),wide(workspace)});
  return MessageBoxW(owner,message.c_str(),tr(L"Close workspace tabs"),MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)==IDYES;
}
// Lightweight native popups (account list, save bubble) use Windows 11
// rounded corners where available; older Windows keeps square corners.
inline void popup_corners(HWND window) {const int preference=3;DwmSetWindowAttribute(window,33,&preference,sizeof(preference));}
inline bool hovered(const DRAWITEMSTRUCT& item) {return IsWindowEnabled(item.hwndItem)&&(GetPropW(item.hwndItem,L"XenonHover")||(item.itemState&ODS_HOTLIGHT)||(SendMessageW(item.hwndItem,BM_GETSTATE,0,0)&BST_HOT));}
inline Fade hover_fade(HWND control) {return {static_cast<ULONGLONG>(reinterpret_cast<ULONG_PTR>(GetPropW(control,L"XenonHoverStart"))),GetPropW(control,L"XenonHover")!=nullptr};}
// 0 at rest, 1 fully hovered. System hot-tracking without our pointer state
// paints as fully hovered, matching hovered().
inline double hover_amount(const DRAWITEMSTRUCT& item) {
  if(!IsWindowEnabled(item.hwndItem))return 0;if(hovered(item)&&!GetPropW(item.hwndItem,L"XenonHover"))return 1;return hover_fade(item.hwndItem).value(hover_duration);
}
inline void button_contents(const DRAWITEMSTRUCT& item,HFONT face,Icon glyph,bool active) {auto colors=palette();auto rect=item.rcItem;fill(item.hDC,rect,GetPropW(item.hwndItem,L"XenonSurface")?colors.surface:colors.canvas);InflateRect(&rect,-1,-1);
  const double hover=hover_amount(item);const auto background=(item.itemState&ODS_SELECTED)||active?selection():mix(colors.surface,hover_background(),hover);
  const int radius=GetPropW(item.hwndItem,L"XenonCircle")?16:8;rounded(item.hDC,rect,background,colors.contrast?colors.border:background,dip(item.hwndItem,radius));
  const auto ink=(item.itemState&ODS_DISABLED)?colors.muted:(active||hover>=0.5)&&colors.contrast?selection_ink():colors.ink;
  if(glyph!=Icon::none)icon(item.hDC,rect,glyph,ink,item.hwndItem);
  else {
  wchar_t caption[256]{};GetWindowTextW(item.hwndItem,caption,256);text(item.hDC,rect,caption,face,ink,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  }
  if(item.itemState&ODS_FOCUS)focus_mark(item.hDC,rect,item.hwndItem,radius);}
// Paints an owner-drawn item offscreen, then copies it once, so hover-fade
// frames never show a partly painted control.
template<class Draw> inline void buffered(const DRAWITEMSTRUCT& item,Draw draw) {
  const int width=item.rcItem.right-item.rcItem.left,height=item.rcItem.bottom-item.rcItem.top;auto dc=CreateCompatibleDC(item.hDC);auto bitmap=CreateCompatibleBitmap(item.hDC,width,height);
  if(!dc||!bitmap){if(dc)DeleteDC(dc);if(bitmap)DeleteObject(bitmap);draw(item);return;}
  const auto old=SelectObject(dc,bitmap);auto copy=item;copy.hDC=dc;copy.rcItem={0,0,width,height};draw(copy);
  BitBlt(item.hDC,item.rcItem.left,item.rcItem.top,width,height,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
inline void button(const DRAWITEMSTRUCT& item,HFONT face,Icon glyph=Icon::none,bool active=false) {buffered(item,[&](const DRAWITEMSTRUCT& copy){button_contents(copy,face,glyph,active);});}
inline DWORD styles(const wchar_t* type,DWORD style) {return std::wstring_view(type)==L"LISTBOX"?style|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS:style;}
inline bool list_draw(UINT message,WPARAM,LPARAM lp,HWND parent,LRESULT& result) {
  if(message==WM_MEASUREITEM){auto item=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);if(item&&item->CtlType==ODT_LISTBOX){item->itemHeight=dip(parent,28);result=TRUE;return true;}}
  if(message==WM_DRAWITEM){auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(!item||item->CtlType!=ODT_LISTBOX)return false;const auto colors=palette();auto rect=item->rcItem;fill(item->hDC,rect,colors.canvas);
    const bool selected=(item->itemState&ODS_SELECTED)!=0;if(selected){InflateRect(&rect,-2,-1);rounded(item->hDC,rect,selection(),selection(),dip(parent,6));}
    if(item->itemID!=static_cast<UINT>(-1)){const auto length=SendMessageW(item->hwndItem,LB_GETTEXTLEN,item->itemID,0);if(length>=0&&length<=4096){std::wstring caption(static_cast<size_t>(length)+1,L'\0');SendMessageW(item->hwndItem,LB_GETTEXT,item->itemID,reinterpret_cast<LPARAM>(caption.data()));caption.resize(static_cast<size_t>(length));rect.left+=dip(parent,8);rect.right-=dip(parent,6);text(item->hDC,rect,caption,reinterpret_cast<HFONT>(SendMessageW(item->hwndItem,WM_GETFONT,0,0)),selected?selection_ink():colors.ink);}}
    if((item->itemState&ODS_FOCUS)&&GetFocus()==item->hwndItem){rect=item->rcItem;InflateRect(&rect,-2,-1);focus_mark(item->hDC,rect,item->hwndItem);}result=TRUE;return true;
  }
  if(message!=WM_NOTIFY)return false;auto notice=reinterpret_cast<NMHDR*>(lp);if(!notice||notice->code!=NM_CUSTOMDRAW)return false;
  wchar_t type[64]{};GetClassNameW(notice->hwndFrom,type,64);if(std::wstring_view(type)!=WC_LISTVIEWW)return false;auto draw=reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
  if(draw->nmcd.dwDrawStage==CDDS_PREPAINT){result=CDRF_NOTIFYITEMDRAW;return true;}
  if(draw->nmcd.dwDrawStage!=CDDS_ITEMPREPAINT)return false;
  const auto control=notice->hwndFrom;const int index=static_cast<int>(draw->nmcd.dwItemSpec);const auto colors=palette();const bool selected=(ListView_GetItemState(control,index,LVIS_SELECTED)&LVIS_SELECTED)!=0;
  RECT row{};ListView_GetItemRect(control,index,&row,LVIR_BOUNDS);RECT client{};GetClientRect(control,&client);row.left=0;row.right=client.right;fill(draw->nmcd.hdc,row,colors.canvas);
  if(selected){auto background=row;InflateRect(&background,-2,-1);rounded(draw->nmcd.hdc,background,selection(),selection(),dip(parent,5));}
  const bool checkboxes=(ListView_GetExtendedListViewStyle(control)&LVS_EX_CHECKBOXES)!=0;
  for(int column=0;column<Header_GetItemCount(ListView_GetHeader(control));++column){RECT cell{};ListView_GetSubItemRect(control,index,column,LVIR_BOUNDS,&cell);if(column==0)cell.right=cell.left+ListView_GetColumnWidth(control,0);
    if(column==0&&checkboxes){RECT box{cell.left+dip(parent,4),(row.top+row.bottom)/2-dip(parent,7),cell.left+dip(parent,18),(row.top+row.bottom)/2+dip(parent,7)};DrawFrameControl(draw->nmcd.hdc,&box,DFC_BUTTON,DFCS_BUTTONCHECK|(ListView_GetCheckState(control,index)?DFCS_CHECKED:0));cell.left+=dip(parent,22);}
    wchar_t caption[4096]{};ListView_GetItemText(control,index,column,caption,4096);cell.left+=dip(parent,6);cell.right-=dip(parent,6);text(draw->nmcd.hdc,cell,caption,reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0)),selected?selection_ink():colors.ink);
  }
  if(selected&&GetFocus()==control){InflateRect(&row,-2,-1);focus_mark(draw->nmcd.hdc,row,control,5);}result=CDRF_SKIPDEFAULT;return true;
}
inline bool ctl_color(UINT message,WPARAM wp,LPARAM lp,LRESULT& result) {
  if(message!=WM_CTLCOLORSTATIC&&message!=WM_CTLCOLOREDIT&&message!=WM_CTLCOLORLISTBOX&&message!=WM_CTLCOLORBTN)return false;
  const auto colors=palette();auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,IsWindowEnabled(reinterpret_cast<HWND>(lp))?colors.ink:colors.muted);SetBkColor(dc,colors.canvas);SetDCBrushColor(dc,colors.canvas);result=reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));return true;
}
inline LRESULT CALLBACK header_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
  if(message==WM_PAINT){PAINTSTRUCT paint{};auto dc=BeginPaint(window,&paint);RECT bounds{};GetClientRect(window,&bounds);auto colors=palette();fill(dc,bounds,colors.surface);
    for(int n=0;n<Header_GetItemCount(window);++n){RECT rect{};Header_GetItemRect(window,n,&rect);wchar_t caption[256]{};HDITEMW item{};item.mask=HDI_TEXT;item.pszText=caption;item.cchTextMax=256;Header_GetItem(window,n,&item);
      RECT separator{rect.right-1,rect.top,rect.right,rect.bottom};fill(dc,separator,colors.border);rect.left+=dip(window,6);text(dc,rect,caption,reinterpret_cast<HFONT>(SendMessageW(GetParent(window),WM_GETFONT,0,0)),colors.ink);}
    EndPaint(window,&paint);return 0;}
  if(message==WM_NCDESTROY)RemoveWindowSubclass(window,header_proc,1);
  return DefSubclassProc(window,message,wp,lp);
}
constexpr UINT_PTR hover_timer=0x58E7;
inline void hover_repaint(HWND window) {InvalidateRect(window,nullptr,FALSE);if(GetPropW(window,L"XenonConnected"))InvalidateRect(GetParent(window),nullptr,FALSE);}
inline void hover_state(HWND window,bool on) {
  auto fade=hover_fade(window);if(fade.on==on)return;fade.set(on,hover_duration);
  SetPropW(window,L"XenonHoverStart",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(fade.start)));
  if(on)SetPropW(window,L"XenonHover",reinterpret_cast<HANDLE>(1));else RemovePropW(window,L"XenonHover");
  if(motion())SetTimer(window,hover_timer,15,nullptr);hover_repaint(window);
}
inline LRESULT CALLBACK hover_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
  if(message==WM_ERASEBKGND&&(GetWindowLongPtrW(window,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW)return 1;
  if(message==WM_MOUSEMOVE&&!GetPropW(window,L"XenonHover")){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);hover_state(window,true);}
  if(message==WM_MOUSELEAVE)hover_state(window,false);
  if(message==WM_TIMER&&wp==hover_timer){hover_repaint(window);if(!hover_fade(window).active(hover_duration))KillTimer(window,hover_timer);return 0;}
  if(message==WM_NCDESTROY){KillTimer(window,hover_timer);RemovePropW(window,L"XenonHover");RemovePropW(window,L"XenonHoverStart");RemovePropW(window,L"XenonCircle");RemovePropW(window,L"XenonSurface");RemovePropW(window,L"XenonConnected");RemoveWindowSubclass(window,hover_proc,1);}return DefSubclassProc(window,message,wp,lp);
}
inline void control_theme(HWND control) {auto colors=palette();SetWindowTheme(control,colors.dark?L"DarkMode_Explorer":L"Explorer",nullptr);
  wchar_t type[64]{};GetClassNameW(control,type,64);if(std::wstring_view(type)==WC_LISTVIEWW){SetWindowTheme(control,L"",L"");if(auto header=ListView_GetHeader(control))SetWindowSubclass(header,header_proc,1,0);}
  // Win32 reports built-in names as Button/ListBox even when they were created
  // with uppercase names. Class-name comparisons must be case-insensitive.
  if(_wcsicmp(type,L"BUTTON")==0)SetWindowSubclass(control,hover_proc,1,0);
  if(_wcsicmp(type,L"LISTBOX")==0)SendMessageW(control,LB_SETITEMHEIGHT,0,dip(control,28));}
}
