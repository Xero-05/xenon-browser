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
#include <objidl.h>
#include <gdiplus.h>

namespace xenon::ui {
enum class ThemeMode { system,light,dark };
struct Palette {COLORREF canvas,surface,ink,muted,border,teal,orange,gray;bool dark{},contrast{};bool operator==(const Palette&)const=default;};
inline ThemeMode theme_mode=ThemeMode::system;
inline int sidebar_width=240;
inline std::filesystem::path settings_path;
inline bool introduction_completed{};
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
  introduction_completed=false;
  try{if(std::filesystem::exists(settings_path)&&std::filesystem::file_size(settings_path)<4096){Json value;std::ifstream(settings_path)>>value;
    const auto mode=value.value("theme",std::string("system"));theme_mode=mode=="dark"?ThemeMode::dark:mode=="light"?ThemeMode::light:ThemeMode::system;
    if(auto locale=value.find("language");locale!=value.end()&&locale->is_string())
      language=preferred_language=*locale=="zh-CN"?Language::simplified_chinese:Language::english;
    if(auto completed=value.find("introductionCompleted");completed!=value.end()&&completed->is_boolean())introduction_completed=completed->get<bool>();
    if(auto width=value.find("sidebarWidth");width!=value.end()&&width->is_number_integer())sidebar_width=std::clamp(width->get<int>(),180,480);}}catch(...){}
}
inline bool save_settings() {
  try{auto temporary=settings_path;temporary+=L".tmp";{std::ofstream stream(temporary);stream<<Json{{"version",1},{"theme",theme_mode==ThemeMode::dark?"dark":theme_mode==ThemeMode::light?"light":"system"},{"sidebarWidth",sidebar_width},{"language",language_tag(preferred_language)},{"introductionCompleted",introduction_completed}};stream.flush();if(!stream)return false;}
    local_security::restrict_path(temporary);return MoveFileExW(temporary.c_str(),settings_path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;}catch(...){return false;}
}
inline bool save_theme(ThemeMode mode) {theme_mode=mode;return save_settings();}
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
inline void focus_mark(HDC dc,RECT rect,HWND control) {
  if(!keyboard_focus(control))return;const auto colors=palette();const int width=dip(control,2);
  RECT mark{rect.left,rect.top+dip(control,4),rect.left+width,rect.bottom-dip(control,4)};fill(dc,mark,colors.contrast?colors.ink:colors.gray);
}
enum class Icon {none,back,forward,reload,stop,go,controls,menu,plus,up,down,chevron_right,close};
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
    else if(kind==Icon::menu)for(float x:{-6.0f,0.0f,6.0f})graphics.FillEllipse(&brush,cx+(x-1.5f)*scale,cy-1.5f*scale,3*scale,3*scale);
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
  else if(kind==Icon::menu){auto brush=CreateSolidBrush(ink);SelectObject(dc,brush);for(int x:{-6,0,6})Ellipse(dc,cx+d(x-1),cy-d(1),cx+d(x+1)+unit,cy+d(1)+unit);SelectObject(dc,old_brush);DeleteObject(brush);}
  SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(pen);
}
inline bool hovered(const DRAWITEMSTRUCT& item) {return IsWindowEnabled(item.hwndItem)&&(GetPropW(item.hwndItem,L"XenonHover")||(item.itemState&ODS_HOTLIGHT)||(SendMessageW(item.hwndItem,BM_GETSTATE,0,0)&BST_HOT));}
inline void button_contents(const DRAWITEMSTRUCT& item,HFONT face,Icon glyph,bool active) {auto colors=palette();auto rect=item.rcItem;fill(item.hDC,rect,GetPropW(item.hwndItem,L"XenonSurface")?colors.surface:colors.canvas);InflateRect(&rect,-1,-1);
  const bool hover=hovered(item);const auto background=(item.itemState&ODS_SELECTED)||active?selection():hover?hover_background():colors.surface;
  rounded(item.hDC,rect,background,colors.contrast?colors.border:background,dip(item.hwndItem,GetPropW(item.hwndItem,L"XenonCircle")?16:8));
  const auto ink=(item.itemState&ODS_DISABLED)?colors.muted:(active||hover)&&colors.contrast?selection_ink():colors.ink;
  if(glyph!=Icon::none)icon(item.hDC,rect,glyph,ink,item.hwndItem);
  else {
  wchar_t caption[256]{};GetWindowTextW(item.hwndItem,caption,256);text(item.hDC,rect,caption,face,ink,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  }
  if(item.itemState&ODS_FOCUS){InflateRect(&rect,-3,-3);focus_mark(item.hDC,rect,item.hwndItem);}}
inline void button(const DRAWITEMSTRUCT& item,HFONT face,Icon glyph=Icon::none,bool active=false) {
  const int width=item.rcItem.right-item.rcItem.left,height=item.rcItem.bottom-item.rcItem.top;auto dc=CreateCompatibleDC(item.hDC);auto bitmap=CreateCompatibleBitmap(item.hDC,width,height);
  if(!dc||!bitmap){if(dc)DeleteDC(dc);if(bitmap)DeleteObject(bitmap);button_contents(item,face,glyph,active);return;}
  const auto old=SelectObject(dc,bitmap);auto buffered=item;buffered.hDC=dc;buffered.rcItem={0,0,width,height};button_contents(buffered,face,glyph,active);
  BitBlt(item.hDC,item.rcItem.left,item.rcItem.top,width,height,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
inline DWORD styles(const wchar_t* type,DWORD style) {return std::wstring_view(type)==L"LISTBOX"?style|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS:style;}
inline bool list_draw(UINT message,WPARAM,LPARAM lp,HWND parent,LRESULT& result) {
  if(message==WM_MEASUREITEM){auto item=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);if(item&&item->CtlType==ODT_LISTBOX){item->itemHeight=dip(parent,28);result=TRUE;return true;}}
  if(message==WM_DRAWITEM){auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(!item||item->CtlType!=ODT_LISTBOX)return false;const auto colors=palette();auto rect=item->rcItem;fill(item->hDC,rect,colors.canvas);
    const bool selected=(item->itemState&ODS_SELECTED)!=0;if(selected){InflateRect(&rect,-2,-1);rounded(item->hDC,rect,selection(),selection(),dip(parent,6));}
    if(item->itemID!=static_cast<UINT>(-1)){const auto length=SendMessageW(item->hwndItem,LB_GETTEXTLEN,item->itemID,0);if(length>=0&&length<=4096){std::wstring caption(static_cast<size_t>(length)+1,L'\0');SendMessageW(item->hwndItem,LB_GETTEXT,item->itemID,reinterpret_cast<LPARAM>(caption.data()));caption.resize(static_cast<size_t>(length));rect.left+=dip(parent,8);rect.right-=dip(parent,6);text(item->hDC,rect,caption,reinterpret_cast<HFONT>(SendMessageW(item->hwndItem,WM_GETFONT,0,0)),selected?selection_ink():colors.ink);}}
    if((item->itemState&ODS_FOCUS)&&GetFocus()==item->hwndItem){rect=item->rcItem;InflateRect(&rect,-4,-2);focus_mark(item->hDC,rect,item->hwndItem);}result=TRUE;return true;
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
  if(selected&&GetFocus()==control){InflateRect(&row,-4,-2);focus_mark(draw->nmcd.hdc,row,control);}result=CDRF_SKIPDEFAULT;return true;
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
inline LRESULT CALLBACK hover_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
  if(message==WM_ERASEBKGND&&(GetWindowLongPtrW(window,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW)return 1;
  if(message==WM_MOUSEMOVE&&!GetPropW(window,L"XenonHover")){SetPropW(window,L"XenonHover",reinterpret_cast<HANDLE>(1));TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);InvalidateRect(window,nullptr,FALSE);if(GetPropW(window,L"XenonConnected"))InvalidateRect(GetParent(window),nullptr,FALSE);}
  if(message==WM_MOUSELEAVE){RemovePropW(window,L"XenonHover");InvalidateRect(window,nullptr,FALSE);if(GetPropW(window,L"XenonConnected"))InvalidateRect(GetParent(window),nullptr,FALSE);}
  if(message==WM_NCDESTROY){RemovePropW(window,L"XenonHover");RemovePropW(window,L"XenonCircle");RemovePropW(window,L"XenonSurface");RemovePropW(window,L"XenonConnected");RemoveWindowSubclass(window,hover_proc,1);}return DefSubclassProc(window,message,wp,lp);
}
inline void control_theme(HWND control) {auto colors=palette();SetWindowTheme(control,colors.dark?L"DarkMode_Explorer":L"Explorer",nullptr);
  wchar_t type[64]{};GetClassNameW(control,type,64);if(std::wstring_view(type)==WC_LISTVIEWW){SetWindowTheme(control,L"",L"");if(auto header=ListView_GetHeader(control))SetWindowSubclass(header,header_proc,1,0);}
  // Win32 reports built-in names as Button/ListBox even when they were created
  // with uppercase names. Class-name comparisons must be case-insensitive.
  if(_wcsicmp(type,L"BUTTON")==0)SetWindowSubclass(control,hover_proc,1,0);
  if(_wcsicmp(type,L"LISTBOX")==0)SendMessageW(control,LB_SETITEMHEIGHT,0,dip(control,28));}
}
