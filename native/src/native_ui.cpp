#include "xenon/native_ui.hpp"
#include "xenon/broker.hpp"
#include "xenon/cef_engine.hpp"
#include "xenon/vault.hpp"
#include "xenon/file_policy.hpp"
#include "xenon/dialog_notices.hpp"
#include "xenon/branding.hpp"
#include <windows.h>
#include <commdlg.h>
#include <wtsapi32.h>
#include <shobjidl.h>
#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace xenon {
namespace {
std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);std::wstring v(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),v.data(),n);return v;}
std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string v(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),v.data(),n,nullptr,nullptr);return v;}
std::string str(const Json& j,const char* key){auto i=j.find(key);return i!=j.end()&&i->is_string()?i->get<std::string>():"";}
std::string compact(const std::string& text,size_t limit){if(text.size()<=limit)return text;while(limit&&(static_cast<unsigned char>(text[limit])&0xc0)==0x80)--limit;return text.substr(0,limit)+"…";}
enum Id { Pairings=101,Approve,Deny,Clients,Workspaces,Share,Tabs,Take,Workers,Give,Stop,ResumeAuth,
          Origin,Username,Password,Label,Save,Import,Accounts,DeleteAccount,Grant,NewWorkspace,UploadGrant,Status,Revoke,PrivateWorkspace,RestoreSession,DialogAccept,DialogDismiss,DialogText,RemoveWorkspace,FillSavedAccount };
constexpr UINT LoginNotice=WM_APP+41;
constexpr UINT DialogNotice=WM_APP+42;
constexpr UINT RemovalNotice=WM_APP+43;
constexpr UINT AutofillNotice=WM_APP+44;
enum class TextTone : INT_PTR { normal=0, muted=1, heading=2, brand=3, brand_muted=4, status=5 };
enum class ButtonTone : INT_PTR { normal=0, primary=1, caution=2 };
constexpr wchar_t TextToneProperty[]=L"Xenon.TextTone";
constexpr wchar_t ButtonToneProperty[]=L"Xenon.ButtonTone";
constexpr COLORREF Canvas=RGB(238,245,246),Surface=RGB(255,255,255),Ink=RGB(23,49,61),Muted=RGB(81,108,119);
constexpr COLORREF Navy=RGB(9,37,50),Teal=RGB(5,119,125),Border=RGB(206,222,226),PaleTeal=RGB(184,232,230);
bool high_contrast(){HIGHCONTRASTW value{sizeof(value)};return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(value),&value,0)&&(value.dwFlags&HCF_HIGHCONTRASTON);}
}
struct NativeUi::Impl {
  Broker& broker;CefEngine& engine;Vault& vault;FilePolicy& files;
  HWND window{};HFONT font{},heading_font{},brand_font{},small_font{};
  HBRUSH canvas_brush=CreateSolidBrush(Canvas),surface_brush=CreateSolidBrush(Surface),navy_brush=CreateSolidBrush(Navy);
  std::map<int,HWND> controls;std::map<int,std::vector<Json>> rows;
  HWND file_window{},file_list{};std::string file_scope;std::vector<Json> file_rows;
  struct LoginNoticeEntry {std::string id;HWND owner{};};
  std::deque<LoginNoticeEntry> login_notices;
  HWND login_window{},login_accept{},login_status{};
  std::string login_candidate;
  struct AutofillNoticeEntry {Json offer;HWND owner{};bool manual{};};
  std::deque<AutofillNoticeEntry> autofill_notices;
  std::set<std::string> manual_autofill_tabs;
  HWND autofill_window{},autofill_list{},autofill_accept{},autofill_status{};
  Json autofill_offer=Json::object();bool autofill_pending=false;
  struct AutofillResult {std::string offer_id,tab_id;bool request{};Json value;};
  struct AutofillResults {std::mutex mutex;bool alive=true;std::deque<AutofillResult> values;};
  std::shared_ptr<AutofillResults> autofill_results=std::make_shared<AutofillResults>();
  DialogNotices dialog_notices;
  struct RemovalResults {std::mutex mutex;std::deque<Json> values;};
  std::shared_ptr<RemovalResults> removal_results=std::make_shared<RemovalResults>();
  Impl(Broker& b,CefEngine& e,Vault& v,FilePolicy& f):broker(b),engine(e),vault(v),files(f){}
  void text_style(HWND control,TextTone tone,HFONT face=nullptr){
    SetPropW(control,TextToneProperty,reinterpret_cast<HANDLE>(static_cast<INT_PTR>(tone)));
    if(face)SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(face),TRUE);
  }
  void button_style(HWND control,ButtonTone tone=ButtonTone::normal){
    // Keep the native BUTTON window and its keyboard/accessibility behavior.
    // Only its paint is customized, including focus and disabled states.
    auto style=GetWindowLongPtrW(control,GWL_STYLE);
    SetWindowLongPtrW(control,GWL_STYLE,(style&~BS_TYPEMASK)|BS_OWNERDRAW);
    SetPropW(control,ButtonToneProperty,reinterpret_cast<HANDLE>(static_cast<INT_PTR>(tone)));
  }
  void window_icon(HWND target){
    SendMessageW(target,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(branding_icon(GetSystemMetrics(SM_CXSMICON))));
    SendMessageW(target,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(branding_icon(GetSystemMetrics(SM_CXICON))));
  }
  void paint_surface(HWND target,HDC dc){
    RECT client{};GetClientRect(target,&client);
    const bool contrast=high_contrast();
    FillRect(dc,&client,contrast?GetSysColorBrush(COLOR_WINDOW):canvas_brush);
    auto card=[&](RECT bounds){
      const auto pen=CreatePen(PS_SOLID,1,contrast?GetSysColor(COLOR_WINDOWFRAME):Border);
      const auto previous_pen=SelectObject(dc,pen);
      const auto previous_brush=SelectObject(dc,contrast?GetSysColorBrush(COLOR_WINDOW):surface_brush);
      RoundRect(dc,bounds.left,bounds.top,bounds.right,bounds.bottom,12,12);
      SelectObject(dc,previous_brush);SelectObject(dc,previous_pen);DeleteObject(pen);
    };
    if(target==window){
      card({10,50,958,186});card({10,194,958,337});card({10,345,958,507});card({10,515,958,728});
      RECT header{0,0,client.right,42};FillRect(dc,&header,contrast?GetSysColorBrush(COLOR_WINDOW):navy_brush);
      if(auto icon=branding_icon(28))DrawIconEx(dc,18,7,icon,28,28,0,nullptr,DI_NORMAL);
    }else{
      // Prompts have a branded, noninteractive header above native controls.
      RECT header{0,0,client.right,54};FillRect(dc,&header,contrast?GetSysColorBrush(COLOR_WINDOW):navy_brush);
      if(auto icon=branding_icon(28))DrawIconEx(dc,20,13,icon,28,28,0,nullptr,DI_NORMAL);
      const auto edge=target==file_window?client.bottom-60:client.bottom-76;
      card({12,66,client.right-12,edge});
    }
  }
  void draw_button(const DRAWITEMSTRUCT& item){
    const auto saved_dc=SaveDC(item.hDC);
    const bool contrast=high_contrast(),disabled=(item.itemState&ODS_DISABLED)!=0,pressed=(item.itemState&ODS_SELECTED)!=0;
    const auto tone=static_cast<ButtonTone>(reinterpret_cast<INT_PTR>(GetPropW(item.hwndItem,ButtonToneProperty)));
    COLORREF background=Surface,foreground=Ink,outline=Border;
    if(tone==ButtonTone::primary){background=pressed?RGB(4,89,96):Teal;foreground=Surface;outline=background;}
    else if(tone==ButtonTone::caution){background=pressed?RGB(248,225,224):RGB(255,248,247);foreground=RGB(143,51,49);outline=RGB(226,196,193);}
    else if(pressed)background=RGB(223,238,240);
    if(disabled){background=RGB(232,239,241);foreground=RGB(129,146,152);outline=RGB(218,229,232);}
    if(contrast){background=GetSysColor(pressed?COLOR_HIGHLIGHT:COLOR_BTNFACE);foreground=GetSysColor(disabled?COLOR_GRAYTEXT:pressed?COLOR_HIGHLIGHTTEXT:COLOR_BTNTEXT);outline=GetSysColor(COLOR_WINDOWFRAME);}
    FillRect(item.hDC,&item.rcItem,contrast?GetSysColorBrush(COLOR_WINDOW):GetParent(item.hwndItem)==window?surface_brush:canvas_brush);
    auto brush=CreateSolidBrush(background);auto pen=CreatePen(PS_SOLID,1,outline);
    const auto old_brush=SelectObject(item.hDC,brush),old_pen=SelectObject(item.hDC,pen);
    const auto& bounds=item.rcItem;RoundRect(item.hDC,bounds.left,bounds.top,bounds.right,bounds.bottom,7,7);
    SelectObject(item.hDC,old_pen);SelectObject(item.hDC,old_brush);DeleteObject(pen);DeleteObject(brush);
    wchar_t caption[256]{};GetWindowTextW(item.hwndItem,caption,static_cast<int>(std::size(caption)));
    auto text_bounds=bounds;InflateRect(&text_bounds,-6,-1);if(pressed)OffsetRect(&text_bounds,0,1);
    SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,foreground);
    auto old_font=SelectObject(item.hDC,font);DrawTextW(item.hDC,caption,-1,&text_bounds,DT_CENTER|DT_VCENTER|DT_SINGLELINE|((item.itemState&ODS_NOACCEL)?DT_HIDEPREFIX:0));SelectObject(item.hDC,old_font);
    if((item.itemState&ODS_FOCUS)&&!(item.itemState&ODS_NOFOCUSRECT)){auto focus=bounds;InflateRect(&focus,-4,-4);DrawFocusRect(item.hDC,&focus);}
    if(saved_dc)RestoreDC(item.hDC,saved_dc);
  }
  bool theme_message(HWND target,UINT message,WPARAM wp,LPARAM lp,LRESULT& result){
    if(message==WM_ERASEBKGND){result=1;return true;}
    if(message==WM_PAINT){PAINTSTRUCT paint{};auto dc=BeginPaint(target,&paint);paint_surface(target,dc);EndPaint(target,&paint);result=0;return true;}
    if(message==WM_DRAWITEM){const auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item&&item->CtlType==ODT_BUTTON){draw_button(*item);result=TRUE;return true;}}
    if(message==WM_CTLCOLORSTATIC||message==WM_CTLCOLOREDIT||message==WM_CTLCOLORLISTBOX){
      auto dc=reinterpret_cast<HDC>(wp);auto control=reinterpret_cast<HWND>(lp);
      if(high_contrast()){SetTextColor(dc,GetSysColor(IsWindowEnabled(control)?COLOR_WINDOWTEXT:COLOR_GRAYTEXT));SetBkColor(dc,GetSysColor(COLOR_WINDOW));result=reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));return true;}
      const auto tone=static_cast<TextTone>(reinterpret_cast<INT_PTR>(GetPropW(control,TextToneProperty)));
      auto background=Surface,foreground=Ink;auto brush=surface_brush;
      if(tone==TextTone::brand||tone==TextTone::brand_muted){background=Navy;foreground=tone==TextTone::brand?Surface:PaleTeal;brush=navy_brush;}
      else if(tone==TextTone::status){background=Canvas;foreground=Muted;brush=canvas_brush;}
      else if(tone==TextTone::muted)foreground=Muted;
      else if(tone==TextTone::heading)foreground=Teal;
      SetTextColor(dc,foreground);SetBkColor(dc,background);result=reinterpret_cast<LRESULT>(brush);return true;
    }
    return false;
  }
  void close_login_prompt(bool dismiss){
    auto id=std::exchange(login_candidate,{});auto h=std::exchange(login_window,nullptr);login_accept=nullptr;login_status=nullptr;
    if(dismiss&&!id.empty()){try{vault.dismiss_login(id);}catch(...){}}
    if(h&&IsWindow(h))DestroyWindow(h);
  }
  void clear_login_prompts(){
    close_login_prompt(true);
    for(const auto& notice:login_notices){try{vault.dismiss_login(notice.id);}catch(...){}}
    login_notices.clear();
  }
  void close_autofill(bool dismiss){
    const auto id=str(autofill_offer,"offerId");
    auto h=std::exchange(autofill_window,nullptr);autofill_list=nullptr;autofill_accept=nullptr;autofill_status=nullptr;
    autofill_offer=Json::object();autofill_pending=false;
    if(dismiss&&!id.empty())engine.dismiss_autofill(id);
    if(h&&IsWindow(h))DestroyWindow(h);
  }
  void clear_autofill(){
    close_autofill(true);
    for(const auto& notice:autofill_notices)engine.dismiss_autofill(str(notice.offer,"offerId"));
    autofill_notices.clear();manual_autofill_tabs.clear();
  }
  void notify_autofill(const Json& offer,HWND source){
    const auto id=str(offer,"offerId"),tab=str(offer,"tabId"),origin=str(offer,"origin");
    if(id.empty()||tab.empty()||origin.empty()||!offer.contains("accounts")||!offer["accounts"].is_array())return;
    const bool manual=manual_autofill_tabs.erase(tab)!=0;
    if(id==str(autofill_offer,"offerId"))return;
    if(std::any_of(autofill_notices.begin(),autofill_notices.end(),[&](const auto& notice){return str(notice.offer,"offerId")==id;}))return;
    auto owner=GetAncestor(source,GA_ROOT);DWORD process{};if(owner)GetWindowThreadProcessId(owner,&process);
    if(!owner||process!=GetCurrentProcessId()||autofill_notices.size()>=64){engine.dismiss_autofill(id);return;}
    autofill_notices.push_back({offer,owner,manual});PostMessageW(window,AutofillNotice,0,0);
  }
  Reply autofill_reply(std::string offer_id,std::string tab_id,bool request){
    auto queue=autofill_results;const auto target=window;
    return [queue,target,offer_id=std::move(offer_id),tab_id=std::move(tab_id),request](Json value){
      std::lock_guard lock(queue->mutex);if(!queue->alive)return;
      queue->values.push_back({offer_id,tab_id,request,std::move(value)});PostMessageW(target,AutofillNotice,0,0);
    };
  }
  void request_autofill(){
    const auto tab=str(selected(Tabs),"tabId");
    if(tab.empty()){status(L"Select the login tab before filling a saved account.");return;}
    if(vault.locked()){status(L"Unlock Windows before using saved accounts.");return;}
    if(manual_autofill_tabs.contains(tab)){status(L"The selected login tab is still being checked…");return;}
    if(autofill_window&&autofill_pending){status(L"A saved-account fill is already in progress.");return;}
    if(autofill_window&&str(autofill_offer,"tabId")==tab&&engine.autofill_offer_valid(str(autofill_offer,"offerId"))){
      SetWindowPos(autofill_window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);return;
    }
    if(autofill_window)close_autofill(true);
    manual_autofill_tabs.insert(tab);
    status(L"Checking the selected tab for a supported HTTPS login form…");
    engine.request_autofill(tab,autofill_reply({},tab,true));
  }
  void autofill_command(int id){
    if(autofill_pending)return;
    if(id==2){close_autofill(true);PostMessageW(window,AutofillNotice,0,0);return;}
    if(id!=1||autofill_offer.empty())return;
    const auto offer=str(autofill_offer,"offerId"),tab=str(autofill_offer,"tabId");
    if(vault.locked()||!engine.autofill_offer_valid(offer)){close_autofill(true);status(L"That login page changed. Open saved accounts again on the current login form.");return;}
    const auto index=SendMessageW(autofill_list,LB_GETCURSEL,0,0);
    const auto& accounts=autofill_offer["accounts"];
    if(index<0||static_cast<size_t>(index)>=accounts.size()){SetWindowTextW(autofill_status,L"Choose a saved account first.");return;}
    const auto account=str(accounts[static_cast<size_t>(index)],"accountId");
    if(account.empty())return;
    autofill_pending=true;EnableWindow(autofill_accept,FALSE);EnableWindow(GetDlgItem(autofill_window,2),FALSE);
    EnableMenuItem(GetSystemMenu(autofill_window,FALSE),SC_CLOSE,MF_BYCOMMAND|MF_GRAYED);
    SetWindowTextW(autofill_status,L"Filling the selected account…");
    try{engine.fill_saved_account(offer,account,autofill_reply(offer,tab,false));}
    catch(...){close_autofill(true);status(L"Filling could not be confirmed. Check the login page before trying again.");}
  }
  static LRESULT CALLBACK autofill_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){
    auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(h,message,wp,lp);
    LRESULT painted{};if(self->theme_message(h,message,wp,lp,painted))return painted;
    if(message==WM_COMMAND&&HIWORD(wp)==BN_CLICKED){self->autofill_command(LOWORD(wp));return 0;}
    if(message==WM_CLOSE||(message==WM_KEYDOWN&&wp==VK_ESCAPE)){if(!self->autofill_pending){self->close_autofill(true);PostMessageW(self->window,AutofillNotice,0,0);}return 0;}
    if(message==WM_DESTROY&&self->autofill_window==h){
      const auto id=str(self->autofill_offer,"offerId");self->autofill_window=nullptr;self->autofill_list=nullptr;self->autofill_accept=nullptr;self->autofill_status=nullptr;
      self->autofill_offer=Json::object();self->autofill_pending=false;if(!id.empty())self->engine.dismiss_autofill(id);
      PostMessageW(self->window,AutofillNotice,0,0);return 0;
    }
    return DefWindowProcW(h,message,wp,lp);
  }
  void show_autofill(const Json& offer,HWND owner){
    WNDCLASSW wc{};wc.lpfnWndProc=autofill_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"XenonFillSavedAccount";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,extended=WS_EX_TOOLWINDOW;
    RECT bounds{0,0,480,390};AdjustWindowRectEx(&bounds,style,FALSE,extended);
    const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    RECT source{};GetWindowRect(owner,&source);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
    const int x=std::clamp(source.right-width-18,monitor.rcWork.left,std::max(monitor.rcWork.left,monitor.rcWork.right-width));
    const int y=std::clamp(source.top+80,monitor.rcWork.top,std::max(monitor.rcWork.top,monitor.rcWork.bottom-height));
    autofill_offer=offer;autofill_pending=false;
    autofill_window=CreateWindowExW(extended,wc.lpszClassName,L"Fill saved account",style,x,y,width,height,owner,nullptr,wc.hInstance,this);
    if(!autofill_window){engine.dismiss_autofill(str(offer,"offerId"));autofill_offer=Json::object();return;}
    window_icon(autofill_window);
    auto add=[&](int id,const wchar_t* type,const wchar_t* title,int x,int y,int w,int h,DWORD extra=0){
      auto control=CreateWindowExW(0,type,title,WS_CHILD|WS_VISIBLE|extra,x,y,w,h,autofill_window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),wc.hInstance,nullptr);SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);if(std::wstring(type)==L"BUTTON")button_style(control,id==1?ButtonTone::primary:ButtonTone::normal);return control;
    };
    text_style(add(0,L"STATIC",L"Xenon  /  Saved accounts",60,16,394,27),TextTone::brand,heading_font);
    text_style(add(0,L"STATIC",L"Choose an account for this login website",26,80,428,24),TextTone::heading,heading_font);
    const auto origin=wide(str(offer,"origin"));add(10,L"EDIT",origin.c_str(),26,112,428,50,WS_BORDER|ES_READONLY|ES_MULTILINE|WS_TABSTOP);
    autofill_list=add(11,L"LISTBOX",L"",26,174,428,80,WS_BORDER|WS_VSCROLL|LBS_NOTIFY|WS_TABSTOP);
    for(const auto& account:offer["accounts"]){
      auto label=str(account,"label");if(label.empty())label="Saved account";
      const auto row=wide(compact(label,100)+"  ·  "+str(account,"accountId").substr(0,8));
      SendMessageW(autofill_list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str()));
    }
    if(!offer["accounts"].empty())SendMessageW(autofill_list,LB_SETCURSEL,0,0);
    text_style(add(0,L"STATIC",L"Fills this form without submitting. You finish signing in. Agent observations stay protected during login.",26,268,428,38),TextTone::muted,small_font);
    autofill_status=add(0,L"STATIC",L"",26,331,190,48);text_style(autofill_status,TextTone::status,small_font);
    autofill_accept=add(1,L"BUTTON",L"Fill",230,335,104,34,WS_TABSTOP|BS_PUSHBUTTON);
    add(2,L"BUTTON",L"Not now",346,335,108,34,WS_TABSTOP|BS_PUSHBUTTON);
    SetWindowPos(autofill_window,HWND_TOP,x,y,width,height,SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_SHOWWINDOW);
    ShowWindow(autofill_window,SW_SHOWNOACTIVATE);
  }
  void poll_autofill(){
    try{
      std::deque<AutofillResult> results;
      {std::lock_guard lock(autofill_results->mutex);results.swap(autofill_results->values);}
      for(const auto& outcome:results){
        const bool ok=outcome.value.value("ok",false);
        if(outcome.request){
          // Success may be delivered before its native notice. Keep the manual
          // marker until notify_autofill consumes it, whichever arrives first.
          if(!ok){manual_autofill_tabs.erase(outcome.tab_id);status(L"Saved-account fill is unavailable for this tab. Use an HTTPS login form with an account saved for this exact website.");}
        }else{
          if(outcome.offer_id==str(autofill_offer,"offerId"))close_autofill(false);
          const auto phase=str(outcome.value.value("result",Json::object()),"phase");
          if(!ok)status(L"Filling could not be confirmed. Check the login page before trying again.");
          else if(phase=="username")status(L"Saved username filled. Continue on the website, then use Fill saved account again at the password step.");
          else if(phase=="password")status(L"Saved password filled without submitting. Return to the login tab to continue signing in.");
          else status(L"Saved account filled without submitting. Return to the login tab to continue signing in.");
        }
      }
      if(vault.locked()){clear_autofill();return;}
      if(autofill_window){
        // A fill consumes its offer before its asynchronous completion arrives.
        // Keep the disabled picker until then; lock/owner destruction still close.
        if(!autofill_pending&&!engine.autofill_offer_valid(str(autofill_offer,"offerId")))close_autofill(false);
        else return;
      }
      for(auto notice=autofill_notices.begin();notice!=autofill_notices.end();){
        if(!IsWindow(notice->owner)||!engine.autofill_offer_valid(str(notice->offer,"offerId"))){engine.dismiss_autofill(str(notice->offer,"offerId"));notice=autofill_notices.erase(notice);}else ++notice;
      }
      if(physical_input_held())return;
      const auto foreground=GetAncestor(GetForegroundWindow(),GA_ROOT);
      for(auto notice=autofill_notices.begin();notice!=autofill_notices.end();++notice){
        if(IsIconic(notice->owner)||(!notice->manual&&notice->owner!=foreground))continue;
        const auto offer=notice->offer;const auto owner=notice->owner;autofill_notices.erase(notice);show_autofill(offer,owner);break;
      }
    }catch(...){clear_autofill();status(L"Saved-account filling is temporarily unavailable.");}
  }
  void notify_login(const std::string& id,HWND source){
    if(id.empty()||id==login_candidate||std::any_of(login_notices.begin(),login_notices.end(),[&](const auto& notice){return notice.id==id;}))return;
    auto owner=GetAncestor(source,GA_ROOT);DWORD process{};if(owner)GetWindowThreadProcessId(owner,&process);
    if(!owner||process!=GetCurrentProcessId()||login_notices.size()>=64){vault.dismiss_login(id);return;}
    login_notices.push_back({id,owner});PostMessageW(window,LoginNotice,0,0);
  }
  static bool physical_input_held(){
    for(int key=1;key<256;++key)if(GetAsyncKeyState(key)&0x8000)return true;
    return false;
  }
  void notify_dialog(const std::string& tab_id){
    dialog_notices.notify(tab_id);PostMessageW(window,DialogNotice,0,0);
  }
  void poll_dialog_notices(){
    // OnJSDialog itself never enters the broker. This runs from a posted
    // native message or timer, after CEF and broker dispatch have returned.
    try{
      const auto notice=dialog_notices.take(broker.state(),engine.native_dialogs(),physical_input_held());
      if(!notice)return;
      refresh();
      auto& tabs=rows[Tabs];
      for(size_t i=0;i<tabs.size();++i)if(str(tabs[i],"tabId")==notice->tab_id){
        SendMessageW(controls.at(Tabs),LB_SETCURSEL,static_cast<WPARAM>(i),0);
        SendMessageW(controls.at(Tabs),LB_SETTOPINDEX,static_cast<WPARAM>(i),0);
        SetWindowTextW(controls.at(DialogText),L"");
        const auto origin=notice->origin.empty()?"unknown origin":notice->origin;
        status(wide((notice->protected_auth?"Protected authentication. ":"")+std::string("Website ")+notice->type+" from "+origin+" in the selected tab. Review its message above, then Accept dialog or Dismiss. Prompt response goes to the website."));
        // Surface the pending human dialog without stealing keyboard focus or
        // interrupting another tab with a modal Chromium dialog.
        ShowWindow(window,SW_SHOWNOACTIVATE);
        SetWindowPos(window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        return;
      }
    }catch(const std::exception&){}
  }
  void login_command(int id){
    if(id==2){close_login_prompt(true);PostMessageW(window,LoginNotice,0,0);return;}
    if(id!=1||login_candidate.empty())return;
    // This handler exists only in this owned native window. No page or MCP
    // command is connected to confirmation, and no plaintext is fetched here.
    try{
      if(vault.locked()){clear_login_prompts();return;}
      auto result=vault.accept_login(login_candidate);
      if(result.value("ok",false)){
        const bool updated=str(result.value("result",Json::object()),"status")=="updated";
        close_login_prompt(false);status(updated?L"Saved password updated in Xenon.":L"Password saved in Xenon.");refresh();PostMessageW(window,LoginNotice,0,0);
      }else{
        EnableWindow(login_accept,FALSE);
        SetWindowTextW(login_status,L"This login could not be saved. Close this prompt and try the login again.");
      }
    }catch(...){if(login_status)SetWindowTextW(login_status,L"The login could not be saved. No password details were displayed or logged.");}
  }
  static LRESULT CALLBACK login_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){
    auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(h,message,wp,lp);
    LRESULT painted{};if(self->theme_message(h,message,wp,lp,painted))return painted;
    if(message==WM_COMMAND&&HIWORD(wp)==BN_CLICKED){self->login_command(LOWORD(wp));return 0;}
    if(message==WM_CLOSE){self->close_login_prompt(true);PostMessageW(self->window,LoginNotice,0,0);return 0;}
    if(message==WM_KEYDOWN&&wp==VK_ESCAPE){self->close_login_prompt(true);PostMessageW(self->window,LoginNotice,0,0);return 0;}
    if(message==WM_DESTROY&&self->login_window==h){
      // An owned popup also disappears if its Chromium owner is destroyed.
      auto id=std::exchange(self->login_candidate,{});self->login_window=nullptr;self->login_accept=nullptr;self->login_status=nullptr;
      if(!id.empty()){try{self->vault.dismiss_login(id);}catch(...){}}
      PostMessageW(self->window,LoginNotice,0,0);return 0;
    }
    return DefWindowProcW(h,message,wp,lp);
  }
  void show_login_prompt(const PendingCredential& candidate,HWND owner){
    WNDCLASSW wc{};wc.lpfnWndProc=login_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"XenonSavePassword";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN;
    constexpr DWORD extended=WS_EX_TOOLWINDOW;
    RECT bounds{0,0,480,362};AdjustWindowRectEx(&bounds,style,FALSE,extended);
    const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    RECT source{};GetWindowRect(owner,&source);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
    const int x=std::clamp(source.right-width-18,monitor.rcWork.left,std::max(monitor.rcWork.left,monitor.rcWork.right-width));
    const int y=std::clamp(source.top+80,monitor.rcWork.top,std::max(monitor.rcWork.top,monitor.rcWork.bottom-height));
    login_candidate=candidate.candidate_id;
    login_window=CreateWindowExW(extended,wc.lpszClassName,candidate.update?L"Update saved password?":L"Save password in Xenon?",style,x,y,width,height,owner,nullptr,wc.hInstance,this);
    if(!login_window){vault.dismiss_login(login_candidate);login_candidate.clear();return;}
    window_icon(login_window);
    auto add=[&](int id,const wchar_t* type,const wchar_t* title,int x,int y,int w,int h,DWORD extra=0){
      auto c=CreateWindowExW(0,type,title,WS_CHILD|WS_VISIBLE|extra,x,y,w,h,login_window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),wc.hInstance,nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);if(std::wstring(type)==L"BUTTON")button_style(c,id==1?ButtonTone::primary:ButtonTone::normal);return c;
    };
    text_style(add(0,L"STATIC",L"Xenon  /  Saved accounts",60,16,394,27),TextTone::brand,heading_font);
    text_style(add(0,L"STATIC",candidate.update?L"Update with the password you just submitted?":L"Save the password you just submitted?",26,80,428,42),TextTone::heading,heading_font);
    text_style(add(0,L"STATIC",L"Login website — this account will be saved for this origin",26,122,428,21),TextTone::muted,small_font);
    auto origin=wide(candidate.origin);add(10,L"EDIT",origin.c_str(),26,149,428,55,WS_BORDER|WS_VSCROLL|ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_TABSTOP);
    auto account=wide("Account: "+candidate.username_label);add(0,L"STATIC",account.c_str(),26,214,428,22);
    text_style(add(0,L"STATIC",L"Sign-in may still require verification. Save only if this password is correct. Saving does not grant agent access.",26,243,428,36),TextTone::muted,small_font);
    login_accept=add(1,L"BUTTON",candidate.update?L"Update":L"Save",230,308,104,34,WS_TABSTOP|BS_PUSHBUTTON);
    add(2,L"BUTTON",L"Not now",346,308,108,34,WS_TABSTOP|BS_PUSHBUTTON);
    login_status=add(0,L"STATIC",L"",26,300,190,54);text_style(login_status,TextTone::status,small_font);
    // Appearance changes neither browser focus nor agent ownership. Clicking a
    // native button later is an explicit human action.
    SetWindowPos(login_window,HWND_TOP,x,y,width,height,SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_SHOWWINDOW);
    ShowWindow(login_window,SW_SHOWNOACTIVATE);
  }
  void poll_login_prompts(){
    try{
      if(vault.locked()){clear_login_prompts();return;}
      const auto pending=vault.pending_logins();
      const auto find=[&](const std::string& id){return std::find_if(pending.begin(),pending.end(),[&](const auto& p){return p.candidate_id==id;});};
      if(login_window){if(find(login_candidate)==pending.end())close_login_prompt(false);else return;}
      for(auto it=login_notices.begin();it!=login_notices.end();){
        if(find(it->id)==pending.end()||!IsWindow(it->owner)){vault.dismiss_login(it->id);it=login_notices.erase(it);}else ++it;
      }
      if(physical_input_held())return;
      const auto foreground=GetAncestor(GetForegroundWindow(),GA_ROOT);
      for(auto it=login_notices.begin();it!=login_notices.end();++it){
        if(IsIconic(it->owner)||it->owner!=foreground)continue;
        const auto candidate=find(it->id);const auto owner=it->owner;login_notices.erase(it);
        if(candidate!=pending.end())show_login_prompt(*candidate,owner);
        break;
      }
    }catch(...){clear_login_prompts();}
  }
  HWND add(int id,const wchar_t* type,const wchar_t* title,int x,int y,int w,int h,DWORD style=0){
    HWND c=CreateWindowExW(0,type,title,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);controls[id]=c;return c;
  }
  HWND label(const wchar_t* text,int x,int y,int w,TextTone tone=TextTone::normal){auto c=add(0,L"STATIC",text,x,y,w,20);text_style(c,tone,tone==TextTone::heading?heading_font:tone==TextTone::muted?small_font:font);return c;}
  void button(int id,const wchar_t* text,int x,int y,int w=125,ButtonTone tone=ButtonTone::normal){button_style(add(id,L"BUTTON",text,x,y,w,28,WS_TABSTOP|BS_PUSHBUTTON),tone);}
  std::string text(int id){auto c=controls.at(id);int n=GetWindowTextLengthW(c);std::wstring s(n+1,L'\0');GetWindowTextW(c,s.data(),n+1);s.resize(n);auto value=utf8(s);SecureZeroMemory(s.data(),s.size()*sizeof(wchar_t));return value;}
  Json selected(int id){int n=static_cast<int>(SendMessageW(controls.at(id),LB_GETCURSEL,0,0));if(n<0||static_cast<size_t>(n)>=rows[id].size())return Json::object();return rows[id][n];}
  void status(const std::wstring& s){SetWindowTextW(controls.at(Status),s.c_str());}
  void result(const Json& r){if(r.value("ok",false))status(L"Done.");else status(wide(str(r.value("error",Json::object()),"message")));}
  void list(int id,const Json& values,const char* key,std::function<std::string(const Json&)> render){
    auto old=selected(id);auto old_id=str(old,key);auto c=controls.at(id);
    std::vector<Json> next;for(const auto& value:values)next.push_back(value);
    if(rows[id]==next)return;
    SendMessageW(c,WM_SETREDRAW,FALSE,0);SendMessageW(c,LB_RESETCONTENT,0,0);rows[id]=std::move(next);
    int match=-1;for(size_t i=0;i<rows[id].size();++i){auto s=wide(render(rows[id][i]));SendMessageW(c,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(s.c_str()));if(str(rows[id][i],key)==old_id)match=static_cast<int>(i);}
    if(match<0&&!rows[id].empty())match=0;SendMessageW(c,LB_SETCURSEL,match,0);SendMessageW(c,WM_SETREDRAW,TRUE,0);InvalidateRect(c,nullptr,TRUE);
  }
  void refresh(){
    try {
      auto s=broker.state();
      std::map<std::string,std::string> worker_names,workspace_names;
      for(const auto& worker:s["workers"])worker_names[str(worker,"agentSessionId")]=str(worker,"name");
      for(auto& workspace:s["workspaces"]){
        const auto id=str(workspace,"workspaceId");auto name=id=="native-default"?"Personal":workspace.value("private",false)?"Private workspace":"Workspace";
        workspace["displayName"]=std::string(name)+(id=="native-default"?"":" · "+id.substr(0,10))+(workspace.value("removing",false)?" — "+workspace.value("removalStatus",std::string("removing")):"");workspace_names[id]=workspace["displayName"].get<std::string>();
      }
      for(auto& tab:s["tabs"]){auto owner=str(tab,"ownerSessionId");tab["ownerDisplay"]=owner=="human"?"You":owner.empty()?"No owner":worker_names.contains(owner)?worker_names[owner]:"Agent "+owner.substr(0,8);if(tab.value("humanPaused",false))tab["ownerDisplay"]=tab["ownerDisplay"].get<std::string>()+" (paused for you)";tab["workspaceDisplay"]=workspace_names[str(tab,"workspaceId")];}
      for(const auto& cached:engine.native_tabs())for(auto& tab:s["tabs"])if(str(tab,"tabId")==str(cached,"tabId"))tab["title"]=cached["title"];
      list(Pairings,s["pairings"],"requestId",[](auto& j){return str(j,"name");});
      auto clients=s["clients"];
      for(auto pending:s.value("pendingRevocations",Json::array())){pending["revocationPending"]=true;clients.push_back(std::move(pending));}
      list(Clients,clients,"clientId",[](auto& j){return (j.value("revocationPending",false)?"NOT SAVED — retry revocation: ":"")+str(j,"name")+"  "+str(j,"clientId");});
      list(Workspaces,s["workspaces"],"workspaceId",[](auto& j){return str(j,"displayName");});
      for(const auto& dialog:engine.native_dialogs())for(auto& tab:s["tabs"])if(str(tab,"tabId")==str(dialog,"tabId"))tab["dialog"]=dialog;
      list(Tabs,s["tabs"],"tabId",[](auto& j){return compact(str(j,"title"),60)+"  |  "+str(j,"workspaceDisplay")+"  |  "+str(j,"ownerDisplay")+"  |  "+str(j,"tabId").substr(0,10)+(j.value("protected",false)?"  |  Protected authentication":"")+(j.contains("dialog")?"  |  Website "+str(j["dialog"],"type")+": "+str(j["dialog"],"message"):"");});
      list(Workers,s["workers"],"agentSessionId",[](auto& j){return str(j,"name")+"  ·  "+str(j,"agentSessionId").substr(0,10)+(str(j,"state")=="retiring"?" (retiring)":j.value("connected",false)?"":" (offline)");});
      auto accounts=vault.list_accounts();if(accounts.value("ok",false))list(Accounts,accounts["result"]["accounts"],"accountId",[](auto& j){return str(j,"label")+"  |  "+str(j,"origin");});
    }catch(const std::exception&){status(L"Some controls are unavailable while the vault is locked.");}
  }
  std::filesystem::path pick(bool csv){
    wchar_t path[32768]{};OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=window;ofn.lpstrFile=path;ofn.nMaxFile=32768;
    ofn.lpstrFilter=csv?L"Password CSV\0*.csv\0All files\0*.*\0":L"Files\0*.*\0";
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;return GetOpenFileNameW(&ofn)?std::filesystem::path(path):std::filesystem::path{};
  }
  std::filesystem::path pick_folder(){
    IFileOpenDialog* dialog=nullptr;std::filesystem::path path;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return path;
    DWORD options{};dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);
    if(SUCCEEDED(dialog->Show(file_window))){IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR name=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&name))){path=name;CoTaskMemFree(name);}item->Release();}}
    dialog->Release();return path;
  }
  void refresh_files(){
    if(!file_list)return;SendMessageW(file_list,LB_RESETCONTENT,0,0);file_rows.clear();
    auto folders=files.list_folders(file_scope),singles=files.list_files(file_scope);
    if(folders.value("ok",false))for(auto entry:folders["result"]["folders"]){entry["kind"]="Folder";file_rows.push_back(std::move(entry));}
    if(singles.value("ok",false))for(auto entry:singles["result"]["files"]){entry["kind"]="File";file_rows.push_back(std::move(entry));}
    for(const auto& entry:file_rows){auto name=wide(str(entry,"kind")+": "+str(entry,"name")+"  ["+str(entry,entry.contains("folderId")?"folderId":"fileId")+"]");SendMessageW(file_list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}
    if(!file_rows.empty())SendMessageW(file_list,LB_SETCURSEL,0,0);
  }
  void file_command(int id){
    try{
      if(id==1){auto path=pick(false);if(!path.empty())result(files.grant_upload(file_scope,path));}
      else if(id==2){auto path=pick_folder();if(!path.empty())result(files.grant_folder(file_scope,path));}
      else if(id==3){int n=static_cast<int>(SendMessageW(file_list,LB_GETCURSEL,0,0));if(n>=0&&static_cast<size_t>(n)<file_rows.size()){const auto& entry=file_rows[n];files.revoke_grant(file_scope,str(entry,entry.contains("folderId")?"folderId":"fileId"));status(L"Selected file or folder grant revoked.");}}
      else if(id==4){ShowWindow(file_window,SW_HIDE);return;}
      refresh_files();
    }catch(const std::exception&){status(L"The file permission could not be changed.");}
  }
  static LRESULT CALLBACK file_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){
    auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(h,message,wp,lp);
    LRESULT painted{};if(self->theme_message(h,message,wp,lp,painted))return painted;
    if(message==WM_COMMAND&&HIWORD(wp)==BN_CLICKED){self->file_command(LOWORD(wp));return 0;}
    if(message==WM_CLOSE){ShowWindow(h,SW_HIDE);return 0;}
    return DefWindowProcW(h,message,wp,lp);
  }
  void show_files(){
    auto workspace=selected(Workspaces);if(workspace.value("private",false)){status(L"Private workspaces are human-only and do not accept agent file grants.");return;}
    file_scope=str(workspace,"workspaceId");if(file_scope.empty()){status(L"Select a workspace first.");return;}
    if(!file_window){
      WNDCLASSW wc{};wc.lpfnWndProc=file_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"XenonFilePermissions";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
      constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN;
      RECT bounds{0,0,860,420};AdjustWindowRectEx(&bounds,style,FALSE,0);
      file_window=CreateWindowExW(0,wc.lpszClassName,L"Xenon File Permissions",style,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,window,nullptr,wc.hInstance,this);
      window_icon(file_window);
      auto make=[&](int id,const wchar_t* type,const wchar_t* name,int x,int y,int w,int h,DWORD style){auto child=CreateWindowExW(0,type,name,WS_CHILD|WS_VISIBLE|style,x,y,w,h,file_window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),wc.hInstance,nullptr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);if(std::wstring(type)==L"BUTTON")button_style(child,id==1?ButtonTone::primary:id==3?ButtonTone::caution:ButtonTone::normal);return child;};
      text_style(make(0,L"STATIC",L"Xenon  /  File permissions",60,16,770,27,0),TextTone::brand,heading_font);
      text_style(make(0,L"STATIC",L"Only files and folders you approve here are available to this workspace's agents.",26,82,808,24,0),TextTone::muted);
      file_list=make(10,L"LISTBOX",L"",26,114,808,230,WS_BORDER|WS_VSCROLL|WS_HSCROLL|WS_TABSTOP);
      make(1,L"BUTTON",L"Grant file",26,376,150,30,WS_TABSTOP);make(2,L"BUTTON",L"Grant folder",188,376,150,30,WS_TABSTOP);
      make(3,L"BUTTON",L"Revoke selected",350,376,170,30,WS_TABSTOP);make(4,L"BUTTON",L"Close",684,376,150,30,WS_TABSTOP);
    }
    SetWindowTextW(file_window,wide("Xenon File Permissions — "+file_scope).c_str());refresh_files();ShowWindow(file_window,SW_SHOWNORMAL);SetForegroundWindow(file_window);
  }
  void poll_removal_results(){
    std::deque<Json> results;{std::lock_guard lock(removal_results->mutex);results.swap(removal_results->values);}
    for(const auto& value:results){
      if(value.value("ok",false)){
        const auto detail=value.value("result",Json::object());
        status(detail.value("profileCleanup",std::string{})=="memory_only"?
          L"Private workspace removed. Saved passwords and downloaded files were kept.":
          L"Workspace removed. Its site data will be deleted on the next launch; locked files are retried later. Saved passwords and downloaded files were kept.");
      }else result(value);
    }
    if(!results.empty())refresh();
  }
  void remove_selected_workspace(){
    const auto workspace=selected(Workspaces);const auto id=str(workspace,"workspaceId");
    if(id.empty()){status(L"Select the workspace to remove.");return;}
    if(id=="native-default"){status(L"Personal is the default workspace and cannot be removed. Other workspaces can be removed here.");return;}
    if(workspace.value("removing",false)&&str(workspace,"removalStatus")!="retry"){
      status(L"Workspace removal is already in progress. Finish any held input or answer its pending website dialog.");return;
    }
    const auto name=str(workspace,"displayName");
    const auto message=wide("Remove "+name+"?\n\nThis closes all its tabs, stops its agents and downloads, and removes its agent and file permissions. Unsaved work in those tabs will be lost.\n\n"+
      std::string(workspace.value("private",false)?"Its private browsing session will be discarded.":"Its cookies, sign-in sessions, history and other profile data will be deleted on the next launch. Files that are still locked will be retried on later launches.")+
      "\n\nSaved passwords in the shared vault, downloaded files and files from approved folders will be kept. Tabs and stored data in other workspaces are kept.");
    if(MessageBoxW(window,message.c_str(),L"Remove workspace",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
    if(file_window&&file_scope==id){DestroyWindow(file_window);file_window=nullptr;file_list=nullptr;file_rows.clear();file_scope.clear();}
    const auto notices=removal_results;const auto target=window;
    status(L"Removing workspace. Waiting for active input to finish before closing its tabs…");
    broker.remove_workspace(id,[notices,target](Json value){
      {std::lock_guard lock(notices->mutex);notices->values.push_back(std::move(value));}
      PostMessageW(target,RemovalNotice,0,0);
    });
  }
  void command(int id){
    try {
      if(id==Approve){if(broker.approve_pairing(str(selected(Pairings),"requestId")))status(L"Client paired. Choose a workspace and share it to grant access.");}
      else if(id==Deny)broker.deny_pairing(str(selected(Pairings),"requestId"));
      else if(id==Revoke){
        const auto revoked=broker.revoke_client(str(selected(Clients),"clientId"));
        if(revoked==Broker::RevocationStatus::durable)status(L"Client access revoked and saved. Its queued actions were canceled.");
        else if(revoked==Broker::RevocationStatus::pending)status(L"Client blocked for this run, but revocation could not be saved. Restarting can restore access. Select its NOT SAVED row and click Revoke / retry.");
        else status(L"Select a paired client or an unsaved revocation to retry.");
      }
      else if(id==Share){status(broker.share_workspace(str(selected(Workspaces),"workspaceId"),str(selected(Clients),"clientId"))?L"Workspace shared with the selected client.":L"Select a client and workspace.");}
      else if(id==Take){broker.human_acquire(str(selected(Tabs),"tabId"));status(L"Ownership requested for you until explicitly given to an agent. Current input will finish.");}
      else if(id==Give){broker.human_release(str(selected(Tabs),"tabId"),str(selected(Workers),"agentSessionId"));status(L"Control offered to the selected connected agent in this workspace.");}
      else if(id==Stop){broker.stop_all();status(L"All agent control stopped. Restart Xenon to accept clients again.");}
      else if(id==ResumeAuth){
        if(MessageBoxW(window,L"Resume agent observations for this tab? Confirm that login is finished and no password, code, recovery key, or other secret is visible.",L"Resume observations",MB_YESNO|MB_ICONQUESTION)==IDYES){engine.release_protection(str(selected(Tabs),"tabId"));status(L"Protected authentication ended for the selected tab.");}
      }
      else if(id==DialogAccept||id==DialogDismiss){engine.answer_native_dialog(str(selected(Tabs),"tabId"),id==DialogAccept,text(DialogText));SetWindowTextW(controls[DialogText],L"");status(L"Selected tab's script dialog answered.");}
      else if(id==FillSavedAccount)request_autofill();
      else if(id==Save){
        auto user=text(Username),pass=text(Password);result(vault.save(text(Origin),user,pass,text(Label)));SecureZeroMemory(user.data(),user.size());SecureZeroMemory(pass.data(),pass.size());SetWindowTextW(controls[Password],L"");SetWindowTextW(controls[Username],L"");
      }
      else if(id==Import){
        auto path=pick(true);
        if(!path.empty()){
          files.deny_source(path);
          auto preview=vault.preview_csv(path);
          if(!preview.value("ok",false)){result(preview);return;}
          const auto& data=preview.at("result");
          std::wstring details=L"Review this local password import. Usernames and passwords are masked.\n\nValid rows: "+std::to_wstring(data.at("validRows").get<size_t>())+
            L"\nInvalid rows to skip: "+std::to_wstring(data.at("invalidRows").get<size_t>())+
            L"\nDuplicate/existing rows: "+std::to_wstring(data.at("duplicates").get<size_t>())+
            L"\nConflicting rows: "+std::to_wstring(data.at("conflicts").get<size_t>())+L"\n\n";
          size_t shown=0;
          for(const auto& item:data.at("preview")){
            if(shown++==12){details+=L"Additional accounts are included in the counts above.\n";break;}
            details+=wide(str(item,"origin"))+L" | "+wide(str(item,"usernameLabel"))+L" | "+wide(str(item,"status"))+L"\n";
          }
          if(data.at("validRows").get<size_t>()==0){status(L"The CSV contains no supported HTTPS logins. Nothing was imported.");return;}
          details+=L"\nImport these logins? The source CSV remains unencrypted on disk until you remove it.";
          if(MessageBoxW(window,details.c_str(),L"Preview password import",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES){status(L"Import canceled. Nothing was imported.");return;}
          bool replace=false;
          if(data.at("conflicts").get<size_t>()>0){
            const auto answer=MessageBoxW(window,L"Some accounts have different passwords in the CSV.\n\nYes: replace conflicting saved passwords with the CSV values.\nNo: keep existing passwords and skip conflicting rows.\nCancel: import nothing.",L"Resolve password conflicts",MB_YESNOCANCEL|MB_ICONQUESTION|MB_DEFBUTTON2);
            if(answer==IDCANCEL){status(L"Import canceled. Nothing was imported.");return;}
            replace=answer==IDYES;
          }
          auto imported=vault.import_csv(path,replace,str(data,"fileDigest"));result(imported);
          if(imported.value("ok",false)){
            const auto& counts=imported.at("result");
            status(L"Imported "+std::to_wstring(counts.at("created").get<size_t>())+L", updated "+std::to_wstring(counts.at("updated").get<size_t>())+
              L", skipped conflicts "+std::to_wstring(counts.at("conflicts").get<size_t>())+L". Remove the source CSV when finished.");
          }
        }
      }
      else if(id==DeleteAccount){auto account=selected(Accounts);if(!account.empty()&&MessageBoxW(window,L"Remove this saved account from Xenon?",L"Remove account",MB_YESNO|MB_ICONQUESTION)==IDYES)result(vault.remove(str(account,"accountId")));}
      else if(id==Grant){auto a=selected(Accounts);status(broker.grant_account(str(selected(Clients),"clientId"),str(selected(Workspaces),"workspaceId"),str(a,"accountId"),str(a,"origin"))?L"Selected account allowed for this client, workspace, and exact origin.":L"Select an account, a paired client, and a shared workspace.");}
      else if(id==NewWorkspace){broker.open_human_workspace("about:blank",[this](Json r){result(r);});}
      else if(id==RemoveWorkspace)remove_selected_workspace();
      else if(id==PrivateWorkspace){broker.open_human_workspace("about:blank",[this](Json r){result(r);},true);}
      else if(id==RestoreSession){
        auto state=broker.state();std::vector<std::string> allowed;for(const auto& workspace:state["workspaces"])if(!workspace.value("private",false))allowed.push_back(str(workspace,"workspaceId"));
        engine.restore_session(allowed,[this](Json r){if(r.value("ok",false))status(L"Restored "+std::to_wstring(r["result"].value("restored",0))+L" tabs with human control. Closed, private and protected tabs are excluded.");else result(r);});
      }
      else if(id==UploadGrant)show_files();
      refresh();
    }catch(const std::exception&){status(L"The operation failed. No secret details were logged.");}
  }
  void build(){
    auto face=[](int height,int weight){return CreateFontW(height,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");};
    font=face(-15,FW_NORMAL);heading_font=face(-16,FW_SEMIBOLD);brand_font=face(-23,FW_SEMIBOLD);small_font=face(-14,FW_NORMAL);
    window_icon(window);
    text_style(add(0,L"STATIC",L"Xenon",58,6,106,30),TextTone::brand,brand_font);
    text_style(add(0,L"STATIC",L"Browser controls",179,13,310,22),TextTone::brand_muted,small_font);
    text_style(add(0,L"STATIC",L"Your browser. Your agents.",714,13,230,22,SS_RIGHT),TextTone::brand_muted,small_font);
    label(L"Pending pairing requests",24,58,340,TextTone::heading);label(L"Paired clients and unsaved revocations",389,58,554,TextTone::heading);
    add(Pairings,L"LISTBOX",L"",24,82,347,59,WS_BORDER|WS_VSCROLL|LBS_NOTIFY|WS_TABSTOP);
    add(Clients,L"LISTBOX",L"",389,82,554,59,WS_BORDER|WS_VSCROLL|LBS_NOTIFY|WS_TABSTOP);
    button(Approve,L"Approve",24,149,125,ButtonTone::primary);button(Deny,L"Deny",161,149);button(Revoke,L"Revoke / retry",389,149,150);button(Stop,L"Stop all agents",793,149,150,ButtonTone::caution);
    label(L"Workspaces",24,202,347,TextTone::heading);label(L"Connected agents",389,202,554,TextTone::heading);
    add(Workspaces,L"LISTBOX",L"",24,226,347,63,WS_BORDER|WS_VSCROLL|LBS_NOTIFY|WS_TABSTOP);
    add(Workers,L"LISTBOX",L"",389,226,554,63,WS_BORDER|WS_VSCROLL|LBS_NOTIFY|WS_TABSTOP);
    button(Share,L"Share workspace",24,297,140);button(NewWorkspace,L"New workspace",172,297,135);button(RemoveWorkspace,L"Remove workspace",315,297,145);
    button(UploadGrant,L"File permissions…",468,297,145);button(PrivateWorkspace,L"Private workspace",621,297,150);button(RestoreSession,L"Restore last session",779,297,165);
    label(L"Live tabs and control ownership",24,353,919,TextTone::heading);
    add(Tabs,L"LISTBOX",L"",24,377,919,86,WS_BORDER|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY|WS_TABSTOP);
    button(Take,L"Take ownership",24,471,140);button(Give,L"Give to agent",176,471,135);button(ResumeAuth,L"Resume after login",323,471,178);
    button(DialogAccept,L"Accept dialog",513,471,120);button(DialogDismiss,L"Dismiss",645,471,95);add(DialogText,L"EDIT",L"",752,473,191,25,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);
    label(L"Saved accounts",24,523,155,TextTone::heading);label(L"Shared encrypted vault · Agent permission is per client and workspace",185,525,758,TextTone::muted);
    add(Accounts,L"LISTBOX",L"",24,547,550,72,WS_BORDER|WS_VSCROLL|LBS_NOTIFY|WS_TABSTOP);
    button(Import,L"Import CSV",592,549,150);button(DeleteAccount,L"Remove account",754,549,189);button(Grant,L"Allow selected client to use account",592,587,351);
    label(L"HTTPS origin",24,627,210,TextTone::muted);label(L"Username",248,627,210,TextTone::muted);label(L"Password",472,627,210,TextTone::muted);label(L"Account label",696,627,247,TextTone::muted);
    add(Origin,L"EDIT",L"",24,649,210,25,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);
    add(Username,L"EDIT",L"",248,649,210,25,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);
    add(Password,L"EDIT",L"",472,649,210,25,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL|ES_PASSWORD);
    add(Label,L"EDIT",L"",696,649,247,25,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);
    button(Save,L"Save account",24,686,145,ButtonTone::primary);
    button(FillSavedAccount,L"Fill saved account…",181,686,220);label(L"Uses the selected login tab above.",416,691,527,TextTone::muted);
    text_style(add(Status,L"STATIC",L"Right-click a webpage and choose Xenon Controls to return here. Handoff preserves the live page.",24,740,919,40),TextTone::status,small_font);
    SetTimer(window,1,750,nullptr);WTSRegisterSessionNotification(window,NOTIFY_FOR_THIS_SESSION);refresh();
  }
  static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(msg==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(h,msg,wp,lp);
    LRESULT painted{};if(self->theme_message(h,msg,wp,lp,painted))return painted;
    switch(msg){
      case WM_CREATE:self->build();return 0;
      case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED)self->command(LOWORD(wp));return 0;
      case WM_TIMER:self->poll_login_prompts();self->poll_autofill();self->poll_dialog_notices();self->poll_removal_results();self->refresh();return 0;
      case LoginNotice:self->poll_login_prompts();return 0;
      case DialogNotice:self->poll_dialog_notices();return 0;
      case RemovalNotice:self->poll_removal_results();return 0;
      case AutofillNotice:self->poll_autofill();return 0;
      case WM_CLOSE:ShowWindow(h,SW_HIDE);return 0;
      case WM_WTSSESSION_CHANGE:if(wp==WTS_SESSION_LOCK){self->clear_login_prompts();self->clear_autofill();self->vault.set_locked(true);self->engine.cancel_login_prompts();self->broker.stop_all();self->status(L"Windows session locked. Agent control stopped.");}else if(wp==WTS_SESSION_UNLOCK)self->vault.set_locked(false);return 0;
      case WM_DESTROY:KillTimer(h,1);WTSUnRegisterSessionNotification(h);return 0;
      default:return DefWindowProcW(h,msg,wp,lp);
    }
  }
};
NativeUi::NativeUi(Broker& b,CefEngine& e,Vault& v,FilePolicy& f):impl_(std::make_unique<Impl>(b,e,v,f)){
  WNDCLASSW wc{};wc.lpfnWndProc=Impl::proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"XenonControlCenter";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
  CreateWindowExW(0,wc.lpszClassName,L"Xenon Controls",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,990,825,nullptr,nullptr,wc.hInstance,impl_.get());
  e.set_save_prompt_callback([self=impl_.get()](const std::string& candidate,CefWindowHandle source){self->notify_login(candidate,source);});
  e.set_autofill_prompt_callback([self=impl_.get()](const Json& offer,CefWindowHandle source){self->notify_autofill(offer,source);});
  e.set_dialog_callback([self=impl_.get()](const std::string& tab_id){self->notify_dialog(tab_id);});
}
NativeUi::~NativeUi(){
  impl_->engine.set_save_prompt_callback({});impl_->engine.set_autofill_prompt_callback({});impl_->engine.set_dialog_callback({});
  {std::lock_guard lock(impl_->autofill_results->mutex);impl_->autofill_results->alive=false;impl_->autofill_results->values.clear();}
  impl_->clear_login_prompts();impl_->clear_autofill();if(impl_->file_window)DestroyWindow(impl_->file_window);if(impl_->window)DestroyWindow(impl_->window);
  for(auto face:{impl_->font,impl_->heading_font,impl_->brand_font,impl_->small_font})if(face)DeleteObject(face);
  for(auto brush:{impl_->canvas_brush,impl_->surface_brush,impl_->navy_brush})if(brush)DeleteObject(brush);
}
void NativeUi::show(){impl_->refresh();ShowWindow(impl_->window,SW_SHOWNORMAL);SetForegroundWindow(impl_->window);}
}
