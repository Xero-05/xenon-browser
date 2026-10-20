#include "xenon/extension_panel.hpp"
#include "xenon/ui_theme.hpp"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include <windowsx.h>
#include <algorithm>
#include <map>
#include <vector>

namespace xenon {
namespace {
std::string text(const Json& data,const char* key){auto i=data.find(key);return i!=data.end()&&i->is_string()?i->get<std::string>():std::string{};}
std::wstring label(const std::string& value){auto result=omnibox::printable(ui::wide(value));if(result.size()>256)result.resize(256);return result;}
}
struct ExtensionPanel::Impl:std::enable_shared_from_this<Impl> {
  struct Button {HWND window{};int row=-1;std::string command;};
  HWND window{},owner{},rows_window{},toggle{};HFONT font{},small_font{},bold{};
  Json data,rows;Action action;std::map<int,Button> buttons;int next=100,scroll=0,visible_rows=0;bool menu=false;
  std::string closed_reason;
  explicit Impl(HWND parent,const Json& value,Action callback):owner(parent),data(value),rows(value.value("extensions",Json::array())),action(std::move(callback)){}
  ~Impl(){if(font)DeleteObject(font);if(small_font)DeleteObject(small_font);if(bold)DeleteObject(bold);}
  int d(int value)const{return ui::dip(owner,value);}
  int header()const{return data.value("origin",std::string{}).empty()||!data.value("siteToggleAvailable",false)?d(48):d(88);}
  HWND button(HWND parent,const std::wstring& caption,int row,const char* command){
    const auto id=++next;auto child=CreateWindowExW(0,L"BUTTON",caption.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    buttons[id]={child,row,command};SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);SetWindowSubclass(child,ui::hover_proc,1,0);SetWindowSubclass(child,key_proc,2,reinterpret_cast<DWORD_PTR>(this));return child;
  }
  void layout(){RECT rect{};GetClientRect(window,&rect);const int width=rect.right;
    if(toggle)MoveWindow(toggle,d(16),d(45),width-d(32),d(36),TRUE);
    const int list_height=std::max(d(44),visible_rows*d(56));MoveWindow(rows_window,0,header(),width,list_height,TRUE);RECT list{};GetClientRect(rows_window,&list);const int list_width=list.right;
    int footer=header()+list_height+d(8);
    for(const auto& [id,button]:buttons){
      if(button.row>=0){const int y=button.row*d(56)-scroll;
        if(button.command=="activate")MoveWindow(button.window,d(8),y,list_width-d(rows[button.row].value("hasSidePanel",false)?122:88),d(54),TRUE);
        else MoveWindow(button.window,list_width-d(button.command=="side-panel"?110:button.command=="pin"?76:42),y+d(12),d(28),d(30),TRUE);
      }else{MoveWindow(button.window,d(8),footer,width-d(16),d(32),TRUE);footer+=d(34);}
    }
    if(static_cast<int>(rows.size())>visible_rows){SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,static_cast<int>(rows.size())*d(56)-1,static_cast<UINT>(list_height),scroll};SetScrollInfo(rows_window,SB_VERT,&info,TRUE);}
  }
  void draw_text(HDC dc,RECT rect,const std::wstring& caption,HFONT face,COLORREF color,UINT flags=DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS){
    const auto old=SelectObject(dc,face);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,caption.c_str(),-1,&rect,flags|DT_NOPREFIX);SelectObject(dc,old);
  }
  void paint_button(const DRAWITEMSTRUCT& item){
    const auto found=buttons.find(item.CtlID);if(found==buttons.end())return;const auto& button=found->second;const auto colors=ui::palette();
    RECT rect=item.rcItem;const bool hovered=ui::hovered(item)||bool(item.itemState&ODS_SELECTED);ui::rounded(item.hDC,rect,hovered?ui::mix(colors.surface,colors.ink,0.09):colors.canvas,colors.canvas,d(6));
    if(button.row>=0){const auto& row=rows[button.row];
      if(button.command=="activate"){
        RECT icon{d(10),d(15),d(34),d(39)};ExtensionPanel::draw_icon(item.hDC,icon,text(row,"icon"));
        RECT name{d(44),d(5),rect.right-d(4),d(29)};draw_text(item.hDC,name,label(text(row,"name")),font,colors.ink);
        const auto access=text(row,"access");const wchar_t* status=text(data,"origin").empty()?L"Enabled":access=="blocked"?L"Blocked on this site":access=="on_click"?L"Access when you click":access=="allowed"?(row.value("allSites",false)?L"Allowed on all requested sites":L"Allowed on this site"):L"No access to this site";
        RECT detail{d(44),d(28),rect.right-d(4),d(47)};draw_text(item.hDC,detail,ui::tr(status),small_font,colors.muted);
      }else if(button.command=="pin"){
        ui::icon(item.hDC,rect,ui::Icon::pin,row.value("pinned",false)?colors.teal:colors.ink,button.window);
      }else if(button.command=="side-panel"){
        RECT shape=rect;InflateRect(&shape,-d(5),-d(7));ui::rounded(item.hDC,shape,colors.canvas,colors.ink,d(2));
        RECT divider{shape.right-d(5),shape.top,shape.right-d(4),shape.bottom};ui::fill(item.hDC,divider,colors.ink);
      }else ui::icon(item.hDC,rect,ui::Icon::more,colors.ink,button.window);
    }else{RECT title{d(44),0,rect.right-d(8),rect.bottom};if(button.command=="manage"){RECT icon{d(10),d(5),d(32),d(27)};ui::icon(item.hDC,icon,ui::Icon::settings,colors.ink,button.window);}draw_text(item.hDC,title,button.command=="manage"?ui::tr(L"Manage extensions"):button.command=="store"?ui::tr(L"Chrome Web Store"):ui::tr(L"Legacy copied extensions…"),font,colors.ink);}
    if(item.itemState&ODS_FOCUS){InflateRect(&rect,-d(2),-d(2));DrawFocusRect(item.hDC,&rect);}
  }
  void emit(const std::string& id,const std::string& command){auto callback=action;
    if(command!="pin"&&command!="unpin"&&command!="allow-site"&&command!="block-site"&&command!="on-click"&&command!="on-site"&&command!="on-all-sites"){
      if(window)DestroyWindow(window);if(IsWindow(owner))SetForegroundWindow(owner);
    }
    if(callback)callback(id,command);
  }
  void context(int row){
    const auto& value=rows[row];const auto id=text(value,"id");auto popup=CreatePopupMenu();
    auto add=[&](UINT item,const wchar_t* name,bool enabled=true){AppendMenuW(popup,MF_STRING|(enabled?0:MF_GRAYED),item,ui::tr(name));};
    add(1,L"Extension details");add(2,L"Extension options",!text(value,"options").empty());if(value.value("hasSidePanel",false))add(8,L"Open side panel");AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
    const bool site=!text(data,"origin").empty()&&value.value("canChangeSite",false);
    add(3,L"When you click",site&&value.value("canOnClick",true));add(4,L"On this site",site&&value.value("canOnSite",true));add(5,L"On all requested sites",site&&value.value("canAllSites",true));
    const auto access=text(value,"access");if(site)CheckMenuRadioItem(popup,3,5,access=="on_click"?3:value.value("allSites",false)?5:4,MF_BYCOMMAND);
    AppendMenuW(popup,MF_SEPARATOR,0,nullptr);add(6,value.value("pinned",false)?L"Unpin from toolbar":L"Pin to toolbar",value.value("canPin",true));add(7,L"Remove extension…",value.value("canRemove",false));
    RECT anchor{};for(const auto& [key,b]:buttons)if(b.row==row&&b.command=="more")GetWindowRect(b.window,&anchor);
    menu=true;CefSetOSModalLoop(true);const auto picked=TrackPopupMenu(popup,TPM_RETURNCMD|TPM_RIGHTALIGN,anchor.right,anchor.bottom,0,window,nullptr);CefSetOSModalLoop(false);menu=false;DestroyMenu(popup);
    if(!window)return;const char* command=picked==8?"side-panel":picked==1?"details":picked==2?"options":picked==3?"on-click":picked==4?"on-site":picked==5?"on-all-sites":picked==6?(value.value("pinned",false)?"unpin":"pin"):picked==7?"remove":"";
    if(*command)emit(id,command);
  }
  static LRESULT CALLBACK key_proc(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR tag,DWORD_PTR context){auto* self=reinterpret_cast<Impl*>(context);
    if(message==WM_KEYDOWN&&(wp==VK_ESCAPE||wp==VK_TAB)){if(wp==VK_ESCAPE)DestroyWindow(self->window);else SetFocus(GetNextDlgTabItem(self->window,child,(GetKeyState(VK_SHIFT)&0x8000)!=0));return 0;}
    if(message==WM_GETDLGCODE&&wp==VK_TAB)return DLGC_WANTTAB;
    if((message==WM_PAINT||message==WM_PRINTCLIENT)&&child==self->toggle&&!ui::palette().contrast){
      PAINTSTRUCT paint{};auto dc=message==WM_PAINT?BeginPaint(child,&paint):reinterpret_cast<HDC>(wp);RECT rect{};GetClientRect(child,&rect);const auto colors=ui::palette();ui::fill(dc,rect,colors.canvas);
      RECT caption=rect;caption.right-=self->d(48);self->draw_text(dc,caption,label(text(self->data,"siteLabel")),self->font,colors.ink);
      const bool checked=SendMessageW(child,BM_GETCHECK,0,0)==BST_CHECKED;RECT track{rect.right-self->d(36),self->d(8),rect.right,self->d(28)};ui::rounded(dc,track,checked?colors.teal:colors.gray,checked?colors.teal:colors.gray,self->d(10));
      RECT thumb{track.left+self->d(checked?18:2),track.top+self->d(2),track.left+self->d(checked?34:18),track.bottom-self->d(2)};ui::rounded(dc,thumb,RGB(255,255,255),RGB(255,255,255),self->d(8));
      if(GetFocus()==child){InflateRect(&rect,-1,-1);DrawFocusRect(dc,&rect);}if(message==WM_PAINT)EndPaint(child,&paint);return 0;
    }
    if(message==WM_NCDESTROY)RemoveWindowSubclass(child,key_proc,tag);
    return DefSubclassProc(child,message,wp,lp);
  }
  static LRESULT CALLBACK rows_proc(HWND child,UINT message,WPARAM wp,LPARAM lp){auto parent=GetParent(child);
    if(message==WM_COMMAND||message==WM_DRAWITEM||message==WM_MOUSEWHEEL||message==WM_VSCROLL)return SendMessageW(parent,message,wp,lp);
    if(message==WM_ERASEBKGND){RECT rect{};GetClientRect(child,&rect);ui::fill(reinterpret_cast<HDC>(wp),rect,ui::palette().canvas);return 1;}
    return DefWindowProcW(child,message,wp,lp);
  }
  static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp){try{
    auto* holder=reinterpret_cast<std::shared_ptr<Impl>*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){holder=new std::shared_ptr<Impl>(*static_cast<std::shared_ptr<Impl>*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(holder));(*holder)->window=window;}
    if(!holder)return DefWindowProcW(window,message,wp,lp);auto self=*holder;
    switch(message){
      case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE&&!self->menu){self->closed_reason=!IsWindowVisible(reinterpret_cast<HWND>(lp))?"activation-hidden":reinterpret_cast<HWND>(lp)==self->owner?"activation-owner":"activation-other";PostMessageW(window,WM_CLOSE,0,0);}break;
      case WM_ERASEBKGND:return 1;
      case WM_PAINT:{PAINTSTRUCT paint{};auto dc=BeginPaint(window,&paint);RECT rect{};GetClientRect(window,&rect);const auto colors=ui::palette();ui::fill(dc,rect,colors.canvas);
        RECT title{self->d(18),self->d(6),rect.right-self->d(16),self->d(43)};self->draw_text(dc,title,ui::tr(L"Extensions"),self->bold,colors.ink);
        for(const int y:{self->header()-self->d(1),self->header()+std::max(self->d(44),self->visible_rows*self->d(56))+self->d(2)}){RECT line{0,y,rect.right,y+1};ui::fill(dc,line,colors.border);}
        if(self->rows.empty()){RECT note{self->d(18),self->header(),rect.right-self->d(12),self->header()+self->d(44)};self->draw_text(dc,note,self->data.value("private",false)?ui::tr(L"Extensions are off in private tabs"):self->data.value("unavailable",false)?ui::tr(L"Extension list unavailable. Try again."):ui::tr(L"No enabled extensions"),self->small_font,colors.muted);}
        EndPaint(window,&paint);return 0;}
      case WM_DRAWITEM:if(auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);item&&item->CtlType==ODT_BUTTON){self->paint_button(*item);return TRUE;}break;
      case WM_COMMAND:{const int id=LOWORD(wp);if(reinterpret_cast<HWND>(lp)==self->toggle){const bool allow=SendMessageW(self->toggle,BM_GETCHECK,0,0)==BST_CHECKED;EnableWindow(self->toggle,FALSE);self->emit({},allow?"allow-site":"block-site");return 0;}
        const auto it=self->buttons.find(id);if(it==self->buttons.end())break;const auto button=it->second;
        if(button.command=="more")self->context(button.row);
        else if(button.row>=0){const auto& row=self->rows[button.row];if(button.command=="pin")EnableWindow(button.window,FALSE);self->emit(text(row,"id"),button.command=="pin"?(row.value("pinned",false)?"unpin":"pin"):button.command);}
        else self->emit({},button.command);return 0;}
      case WM_VSCROLL:case WM_MOUSEWHEEL:{const int maximum=std::max(0,(static_cast<int>(self->rows.size())-self->visible_rows)*self->d(56));int position=self->scroll;
        if(message==WM_MOUSEWHEEL)position-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*self->d(56);
        else{SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(self->rows_window,SB_VERT,&info);position+=LOWORD(wp)==SB_LINEUP?-self->d(56):LOWORD(wp)==SB_LINEDOWN?self->d(56):LOWORD(wp)==SB_PAGEUP?-self->d(224):LOWORD(wp)==SB_PAGEDOWN?self->d(224):0;if(LOWORD(wp)==SB_THUMBTRACK)position=info.nTrackPos;if(LOWORD(wp)==SB_TOP)position=0;if(LOWORD(wp)==SB_BOTTOM)position=maximum;}
        self->scroll=std::clamp(position,0,maximum);self->layout();InvalidateRect(self->rows_window,nullptr,TRUE);return 0;}
      case WM_CLOSE:DestroyWindow(window);return 0;
      case WM_NCDESTROY:self->window=nullptr;SetWindowLongPtrW(window,GWLP_USERDATA,0);delete holder;return DefWindowProcW(window,message,wp,lp);
    }return DefWindowProcW(window,message,wp,lp);
  }catch(...){return message==WM_NCCREATE?FALSE:0;}}
};

ExtensionPanel::ExtensionPanel(HWND owner,RECT anchor,const Json& data,Action action):impl_(std::make_shared<Impl>(owner,data,std::move(action))){
  static const bool registered=[](){WNDCLASSW value{};value.hInstance=GetModuleHandleW(nullptr);value.hCursor=LoadCursorW(nullptr,IDC_ARROW);value.lpfnWndProc=Impl::proc;value.lpszClassName=L"XenonExtensionsPanel";value.style=CS_DROPSHADOW;RegisterClassW(&value);value.lpfnWndProc=Impl::rows_proc;value.lpszClassName=L"XenonExtensionsRows";value.style=0;RegisterClassW(&value);return true;}();(void)registered;
  auto& state=*impl_;state.visible_rows=std::min<int>(6,static_cast<int>(state.rows.size()));
  const int width=state.d(360),height=state.header()+std::max(state.d(44),state.visible_rows*state.d(56))+state.d(data.value("legacy",false)?118:84);
  MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromRect(&anchor,MONITOR_DEFAULTTONEAREST),&monitor);
  const int x=std::clamp<int>(anchor.right-width,monitor.rcWork.left,std::max<int>(monitor.rcWork.left,monitor.rcWork.right-width));const int y=std::clamp<int>(anchor.bottom+state.d(4),monitor.rcWork.top,std::max<int>(monitor.rcWork.top,monitor.rcWork.bottom-height));
  const auto window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,L"XenonExtensionsPanel",ui::tr(L"Extensions"),WS_POPUP|WS_BORDER,x,y,width,height,owner,nullptr,GetModuleHandleW(nullptr),&impl_);if(!window)return;
  state.font=ui::font(window,13);state.small_font=ui::font(window,12);state.bold=ui::font(window,13,FW_SEMIBOLD);ui::frame(window);ui::popup_corners(window);
  state.rows_window=CreateWindowExW(WS_EX_CONTROLPARENT,L"XenonExtensionsRows",L"",WS_CHILD|WS_VISIBLE|(state.rows.size()>6?WS_VSCROLL:0),0,0,1,1,window,nullptr,GetModuleHandleW(nullptr),nullptr);
  if(state.rows.empty())ShowWindow(state.rows_window,SW_HIDE);
  if(!text(data,"origin").empty()&&data.value("siteToggleAvailable",false)){
    state.toggle=CreateWindowExW(0,L"BUTTON",label(text(data,"siteLabel")).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,0,0,1,1,window,reinterpret_cast<HMENU>(99),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(state.toggle,WM_SETFONT,reinterpret_cast<WPARAM>(state.font),FALSE);SendMessageW(state.toggle,BM_SETCHECK,data.value("siteBlocked",false)?BST_UNCHECKED:BST_CHECKED,0);SetWindowSubclass(state.toggle,Impl::key_proc,2,reinterpret_cast<DWORD_PTR>(&state));
  }
  for(size_t i=0;i<state.rows.size();++i){const auto& row=state.rows[i];const auto name=label(text(row,"name"));state.button(state.rows_window,name,static_cast<int>(i),"activate");auto pin=state.button(state.rows_window,(row.value("pinned",false)?ui::tr(L"Unpin from toolbar"):ui::tr(L"Pin to toolbar"))+std::wstring(L": ")+name,static_cast<int>(i),"pin");EnableWindow(pin,row.value("canPin",true));state.button(state.rows_window,std::wstring(ui::tr(L"More options"))+L": "+name,static_cast<int>(i),"more");}
  for(size_t i=0;i<state.rows.size();++i)if(state.rows[i].value("hasSidePanel",false))state.button(state.rows_window,std::wstring(ui::tr(L"Open side panel"))+L": "+label(text(state.rows[i],"name")),static_cast<int>(i),"side-panel");
  state.button(window,ui::tr(L"Manage extensions"),-1,"manage");state.button(window,ui::tr(L"Chrome Web Store"),-1,"store");if(data.value("legacy",false))state.button(window,ui::tr(L"Legacy copied extensions…"),-1,"legacy");
  state.layout();ShowWindow(window,SW_SHOW);SetForegroundWindow(window);SetFocus(state.toggle?state.toggle:state.buttons.begin()->second.window);
}
ExtensionPanel::~ExtensionPanel(){close();}
void ExtensionPanel::close(){if(impl_&&impl_->window){impl_->closed_reason="native-close";DestroyWindow(impl_->window);}}
HWND ExtensionPanel::window()const{return impl_?impl_->window:nullptr;}
void ExtensionPanel::draw_icon(HDC dc,RECT bounds,const std::string& data){
  constexpr char prefix[]="data:image/png;base64,";if(!data.starts_with(prefix)||data.size()>131072){ui::icon(dc,bounds,ui::Icon::extensions,ui::palette().muted,WindowFromDC(dc));return;}
  const auto decoded=CefBase64Decode(data.substr(sizeof(prefix)-1));if(!decoded||decoded->GetSize()>98304)return;
  unsigned char header[24]{};if(decoded->GetSize()<sizeof(header)||decoded->GetData(header,sizeof(header),0)!=sizeof(header))return;
  constexpr unsigned char png_header[]{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82};
  if(!std::equal(std::begin(png_header),std::end(png_header),header))return;
  auto dimension=[&](int at){return (uint32_t(header[at])<<24)|(uint32_t(header[at+1])<<16)|(uint32_t(header[at+2])<<8)|header[at+3];};
  if(dimension(16)<1||dimension(16)>128||dimension(20)<1||dimension(20)>128)return;
  auto memory=GlobalAlloc(GMEM_MOVEABLE,decoded->GetSize());if(!memory)return;auto bytes=GlobalLock(memory);if(!bytes){GlobalFree(memory);return;}decoded->GetData(bytes,decoded->GetSize(),0);GlobalUnlock(memory);
  IStream* stream{};if(FAILED(CreateStreamOnHGlobal(memory,TRUE,&stream))){GlobalFree(memory);return;}
  {Gdiplus::Image image(stream);Gdiplus::Graphics graphics(dc);graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);if(image.GetLastStatus()==Gdiplus::Ok)graphics.DrawImage(&image,static_cast<int>(bounds.left),static_cast<int>(bounds.top),static_cast<int>(bounds.right-bounds.left),static_cast<int>(bounds.bottom-bounds.top));}
  stream->Release();
}
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
Json ExtensionPanel::fixture_state()const{
  if(!impl_->window)return {{"visible",false},{"closedReason",impl_->closed_reason}};RECT rect{};GetWindowRect(impl_->window,&rect);const auto dpi=GetDpiForWindow(impl_->window);size_t icons{};
  for(const auto& row:impl_->rows)if(text(row,"icon").starts_with("data:image/png;base64,"))++icons;
  return {{"visible",IsWindowVisible(impl_->window)!=FALSE},{"rows",impl_->rows.size()},{"iconRows",icons},{"siteToggle",impl_->toggle!=nullptr},
    {"siteAllowed",impl_->toggle&&SendMessageW(impl_->toggle,BM_GETCHECK,0,0)==BST_CHECKED},
    {"widthDip",MulDiv(rect.right-rect.left,96,dpi)},{"heightDip",MulDiv(rect.bottom-rect.top,96,dpi)}};
}
bool ExtensionPanel::fixture_action(const std::string& command){
  if(!impl_->window)return false;
  if(command=="site-toggle"){if(!impl_->toggle)return false;SendMessageW(impl_->toggle,BM_CLICK,0,0);return true;}
  for(const auto& [id,button]:impl_->buttons)if(button.row>=0&&text(impl_->rows[button.row],"name")=="Synthetic action popup"&&button.command==command){SendMessageW(button.window,BM_CLICK,0,0);return true;}
  return false;
}
#endif
}
