#include "xenon/browser_shell.hpp"
#include "xenon/broker.hpp"
#include "xenon/cef_engine.hpp"
#include "xenon/browser_data.hpp"
#include "xenon/omnibox.hpp"
#include "xenon/site_info.hpp"
#include "xenon/external_protocol.hpp"
#include "xenon/extension_store.hpp"
#include "xenon/native_ui.hpp"
#include "xenon/ui_theme.hpp"
#include "xenon/agent_overlay.hpp"
#include "xenon/attention_notices.hpp"
#include "xenon/introduction.hpp"
#include "xenon/pointer_motion.hpp"
#include "xenon/pointer_overlay.hpp"
#include "xenon/developer_elements.hpp"
#include "xenon/local_security.hpp"
#include "xenon/version.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <wincrypt.h>
#include <cryptuiapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#include <utility>

namespace xenon {
namespace {
constexpr UINT ShellChanged=WM_APP+90,ShellRun=WM_APP+91,ShellHelpNotice=WM_APP+92;
// Control and command IDs are stable: native test drivers address 2004
// (address), 2006 (Controls) and the theme commands 2111-2113 by number.
enum ShellId {Back=2001,Forward,Reload,Address,Go,Controls,Menu,NewTab,Tree,FindText,FindNext,FindPrevious,FindClose,Extensions,Passwords,BookmarkStar,SiteInfo,
  MenuFind=2100,MenuZoomIn,MenuZoomOut,MenuZoomReset,MenuBookmark,MenuBookmarks,MenuHistory,MenuDownloads,MenuPrint,MenuPdf,
  MenuPermissions,MenuThemeSystem,MenuThemeLight,MenuThemeDark,MenuAbout,MenuNotices,MenuExit,MenuPrivate,MenuCloseTab,
  MenuSidebarNarrower,MenuSidebarWider,MenuSidebarReset,MenuUpdates,MenuQuickTour,MenuDocumentation,MenuDeveloper,
  MenuNewWindow,MenuReopenTab,MenuCloseWorkspaceTabs,MenuCloseWindow,MenuManageExtensions,MenuPasswords,MenuNewTab,
  MenuFullscreen,MenuSettings,MenuClearData,MenuImport,
  MenuLanguageEnglish=2200,MenuLanguageChinese,
  TabNewBelow=2300,TabReload,TabDuplicate,TabPin,TabMute,TabCopyLink,TabBookmark,TabNewWindow,TabTakeOwnership,TabControls,TabClose,TabCloseOthers,TabCloseBelow,TabCloseWorkspace,TabStopSharing,TabDismissHelp,
  GroupNewTab=2350,GroupToggle,GroupCloseAll,GroupConfigure,
  OwnerTake=2370,OwnerControls,OwnerDismissHelp,OwnerHelpNote,
  TabToWorkspace=2400,TabToWindow=2500,OwnerGive=2560,ExtensionItem=2600,
  // Site-information bubble children; SiteSetting+n is the nth site::settings combo.
  SiteClose=2700,SiteBack,SiteConnection,SiteCertificate,SiteCookies,SiteSettings,SiteReset,SiteTitle,SiteSubtitle,SiteText,SiteDetails,SitePermissions,SiteSetting=2720,SiteSettingLabel=2740,
  DeveloperTitle=2800,DeveloperHint,DeveloperFilter,DeveloperList,DeveloperStatus,DeveloperNodeDetails,DeveloperPick};
static_assert(Passwords==ui::password_anchor_id,"Credential bubbles anchor to the toolbar key button");
// Site-settings and cookie panel controls; SitePanelSetting+n is the nth site::settings combo.
enum SitePanelId {SitePanelLeft=21,SitePanelRight,SitePanelMiddle,SitePanelLabel=100,SitePanelSetting=200};
std::string str(const Json& value,const char* key){auto found=value.find(key);return found!=value.end()&&found->is_string()?found->get<std::string>():std::string{};}
// Menus treat '&' as a mnemonic marker; show other parties' text literally.
std::wstring menu_text(std::wstring text){for(size_t at=text.find(L'&');at!=std::wstring::npos;at=text.find(L'&',at+2))text.insert(at,1,L'&');return text;}
std::wstring short_title(const std::string& value,size_t maximum=55){auto text=ui::wide(value);if(text.size()>maximum)text=text.substr(0,maximum-1)+L"…";return text;}
using Clock=std::chrono::steady_clock;
double fraction(Clock::time_point start,int duration){return std::clamp(std::chrono::duration<double,std::milli>(Clock::now()-start).count()/duration,0.0,1.0);}
uint32_t seed(){return static_cast<uint32_t>(std::stoul(local_security::random_hex(4),nullptr,16));}
// Native chrome motion timings in milliseconds. Website pixels and geometry
// never animate; these only change how the shell around the page is painted.
constexpr int GlideDuration=200,AccentDuration=240,FocusDuration=160,LoadingCycle=1300,HelpCycle=1100;
// Desktop toolbar height, row heights and the page frame's corner radius, in DIPs.
constexpr int ToolbarHeight=44,TabRow=34,FrameRadius=8;
// Address suggestion row height and the list's inner top/bottom margin, in DIPs.
constexpr int SuggestionRow=34,SuggestionInset=6;
double elapsed(ULONGLONG start,int duration){return start&&ui::motion()?std::clamp(static_cast<double>(GetTickCount64()-start)/duration,0.0,1.0):1.0;}
// How strongly a tab waiting for the person's help shows orange: a slow pulse,
// or steady full strength when Windows animation effects are off.
double help_pulse(){if(!ui::motion())return 1.0;const double phase=static_cast<double>(GetTickCount64()%HelpCycle)/HelpCycle;return 0.5-0.5*std::cos(phase*2*3.14159265358979323846);}
bool web_address(const std::string& url){return url.rfind("http://",0)==0||url.rfind("https://",0)==0;}
std::string origin_of(const std::string& url){if(!web_address(url))return {};const auto start=url.find("://")+3;return url.substr(0,url.find_first_of("/?#",start));}
// The app Windows registered for a link scheme, for the person's prompt; empty
// without one. Chromium likewise opens nothing for an unregistered scheme.
std::wstring external_app_name(const std::string& scheme){
  const auto key=ui::wide(scheme);wchar_t name[512]{};DWORD size=512;
  if(SUCCEEDED(AssocQueryStringW(ASSOCF_IS_PROTOCOL|ASSOCF_NOTRUNCATE,ASSOCSTR_FRIENDLYAPPNAME,key.c_str(),L"open",name,&size))&&name[0])return omnibox::printable(name);
  wchar_t command[2048]{};size=2048;
  if(SUCCEEDED(AssocQueryStringW(ASSOCF_IS_PROTOCOL|ASSOCF_NOTRUNCATE,ASSOCSTR_COMMAND,key.c_str(),L"open",command,&size))&&command[0])return ui::format(ui::tr(L"the {0} app"),{key});
  return {};
}
// Opens a confirmed link in its registered app, escaped for the command line
// and off the UI thread, as Chromium does.
void launch_external(const std::string& url){
  if(external::scheme(url).empty())return;
  std::thread([target=ui::wide(external::escape(url))]{const bool com=SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE));
    ShellExecuteW(nullptr,L"open",target.c_str(),nullptr,nullptr,SW_SHOWNORMAL);if(com)CoUninitialize();}).detach();
}
// A website's icon, scaled smoothly into `rect`; false when there is none.
bool draw_site_icon(HDC dc,const RECT& rect,const CefEngine::SiteIcon& icon){
  if(icon.pixels.empty()||icon.width<=0||icon.height<=0||!ui::vector_rendering())return false;
  Gdiplus::Bitmap bitmap(icon.width,icon.height,icon.width*4,PixelFormat32bppPARGB,const_cast<BYTE*>(icon.pixels.data()));if(bitmap.GetLastStatus()!=Gdiplus::Ok)return false;
  Gdiplus::Graphics graphics(dc);graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  return graphics.DrawImage(&bitmap,Gdiplus::Rect(rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top))==Gdiplus::Ok;
}
// A human-chosen tab address only; never page text or credentials.
void copy_text(HWND owner,const std::string& value){
  const auto text=ui::wide(value);if(!OpenClipboard(owner))return;EmptyClipboard();
  if(auto memory=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t))){
    if(auto target=static_cast<wchar_t*>(GlobalLock(memory))){std::copy(text.begin(),text.end(),target);target[text.size()]=0;GlobalUnlock(memory);if(!SetClipboardData(CF_UNICODETEXT,memory))GlobalFree(memory);}else GlobalFree(memory);
  }
  CloseClipboard();
}
}
struct BrowserShell::Impl {
  struct Frame;
  struct Host {std::string id,workspace;Frame* frame{};HWND window{},browser{};Json metadata=Json::object();bool human{},agent{},paused{},initialized{},animating{},pinned{};
    PointerPoint display,from,to;Clock::time_point animation;int duration{120};PointerOverlayTransitions transitions;std::string recorded_url;
    // The site's icon (premultiplied BGRA), copied when the engine's revision changes.
    CefEngine::SiteIcon icon;uint64_t icon_revision{};};
  // Where the next human tab for a workspace opens: a window and position.
  struct Placement {std::string workspace;HWND frame{};std::string before;bool select{true};ULONGLONG at{};};
  struct Panel {Impl* owner{};HWND window{},list{},message{},parent{};HFONT font{};std::string kind,workspace,tab,origin;Json rows=Json::array(),info=Json::object();bool private_mode{},answered{};std::function<void(bool)> answer;
    // Library windows: the folder tree, search box, the rows shown and, for
    // history, each row's folded search text. Forms place controls from `places`.
    HWND tree{},search{};HFONT bold{};std::string folder;std::map<HTREEITEM,std::string> folder_items;bool building{},busy{};
    std::vector<size_t> shown;std::vector<std::string> folded;std::vector<browser_import::Source> sources;std::vector<std::tuple<int,int,int,int,int>> places;};
  // A tab being dragged in the sidebar. Targets are recomputed on each move.
  struct Drag {HWND source{};std::string tab;bool active{},finishing{};HWND ghost{};std::wstring title,note;
    HWND target{};std::string workspace,before;bool outside{},blocked{},below{};HTREEITEM line{},group{};POINT point{};};
  struct Mailbox {std::mutex mutex;std::deque<std::function<void()>> calls;};
  struct Closed {std::string workspace,url;};

  struct Frame {
    Impl& shell;HWND window{},tree{},address{},find_bar{},find_text{},cursor{},agent_glow{},tooltips{};HFONT font{},bold{};std::map<int,HWND> controls;
    COLORREF cursor_tint{},glow_tint{};int cursor_dpi{},glow_dpi{};SIZE glow_size{};RECT glow_panel{},glow_tab{};bool cursor_contrast{},glow_fullscreen{};
    std::string selected;std::vector<std::pair<std::string,bool>> tree_keys;Json tree_signature,tree_titles;bool rebuilding{},find_visible{},closing{};ULONGLONG closing_since{};
    RECT page{},panel_bounds{},omnibox{},chip{};std::wstring status,chip_text;ULONGLONG status_until{};
    int sidebar{240},drag_origin{},drag_width{};bool resizing_sidebar{},sidebar_hover{};
    bool address_dirty{},setting_address{};HTREEITEM hovered_row{};std::string pressed_close,hovered_close;
    // When pulsing help rows were last repainted, and whether the taskbar
    // button was flashed for a help request.
    ULONGLONG help_painted{};bool help_flashed{};
    // Address suggestions: a list under the omnibox that never takes focus.
    // `typed` is the person's text without any inline completion.
    HWND suggestions{};HFONT emphasis{};std::vector<omnibox::Match> matches;std::string typed;int suggestion{-1},suggestion_hover{-1},suggestion_pressed{-1};bool typing{};
    // Site information: the selected tab's toolbar security state and the open
    // bubble, which describes one tab's website and closes when it changes.
    struct SiteRow {ui::Icon icon{};std::wstring detail;bool chevron{};COLORREF tint{};};
    std::string security;HWND site_bubble{};std::string site_tab,site_origin;Json site=Json::object();bool site_details{};ULONGLONG site_closed{};HFONT site_title{},site_small{};std::map<int,SiteRow> site_rows;
    // Sampled while painting; animate() only invalidates what is still moving.
    ui::Fade address_focus;std::map<HTREEITEM,ui::Fade> row_fades;RECT glide_from{},pill{};ULONGLONG glide_start{},accent_start{};
    COLORREF accent_from{},accent_to{};bool accent_ready{},pill_valid{},tree_moving{},window_moving{},was_loading{};
    size_t saved_accounts{};bool bookmarked{};std::string checked_url,last_find,update_version;
    // Full screen. F11 hides Xenon's chrome around the page; a page the person
    // controls may also fill the screen, and Esc ends that. The page then
    // occupies the whole client area, outlined only while an agent is available.
    enum class Screen {windowed,browser,page};
    Screen screen_mode{Screen::windowed};bool page_screen{};std::string screen_tab;WINDOWPLACEMENT restore{sizeof(WINDOWPLACEMENT)};LONG_PTR restore_style{};
    HWND notice{};ULONGLONG notice_until{};std::wstring notice_text;bool screen_outline{};
    bool fullscreen() const {return screen_mode!=Screen::windowed;}
    void enter_fullscreen(Screen kind){
      if(fullscreen()||IsIconic(window))return;MONITORINFO monitor{sizeof(monitor)};restore.length=sizeof(restore);
      if(!GetWindowPlacement(window,&restore)||!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))return;
      close_site();hide_suggestions();screen_mode=kind;restore_style=GetWindowLongPtrW(window,GWL_STYLE);
      for(const auto& [id,control]:controls)if(GetParent(control)==window)ShowWindow(control,SW_HIDE);
      SetWindowLongPtrW(window,GWL_STYLE,restore_style&~WS_OVERLAPPEDWINDOW);
      SetWindowPos(window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,SWP_NOOWNERZORDER|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
      layout();
    }
    void leave_fullscreen(){
      if(page_screen){page_screen=false;shell.engine.native_command(screen_tab,"exit-fullscreen");}
      if(!fullscreen())return;screen_mode=Screen::windowed;hide_notice();
      SetWindowLongPtrW(window,GWL_STYLE,restore_style);SetWindowPlacement(window,&restore);
      SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
      for(const auto& [id,control]:controls)if(GetParent(control)==window)ShowWindow(control,SW_SHOWNA);
      layout();sync_toolbar();
    }
    // F11 toggles full screen for the whole window, as in Chromium.
    void toggle_fullscreen(){if(fullscreen()){leave_fullscreen();return;}enter_fullscreen(Screen::browser);if(fullscreen())show_notice(ui::tr(L"Press F11 to exit full screen."));}
    // A page's fullscreen request takes over the window only for the selected
    // tab the person controls. Otherwise the page fills just its tab area.
    void page_fullscreen(const std::string& tab,bool on){
      if(!on){if(page_screen&&screen_tab==tab){page_screen=false;if(screen_mode==Screen::page)leave_fullscreen();else hide_notice();}return;}
      if(tab!=selected||!shell.broker.human_controls(tab))return;
      if(!fullscreen())enter_fullscreen(Screen::page);if(!fullscreen())return;
      page_screen=true;screen_tab=tab;const auto host=shell.hosts.find(tab);const auto name=host==shell.hosts.end()?std::string{}:site::host(site::origin(str(host->second.metadata,"url")));
      show_notice(name.empty()?ui::tr(L"Press Esc to exit full screen."):ui::format(ui::tr(L"{0} is now full screen. Press Esc to exit."),{ui::wide(name)}));
    }
    void leave_page_fullscreen(){if(!page_screen)return;if(screen_mode==Screen::page)leave_fullscreen();else{page_screen=false;shell.engine.native_command(screen_tab,"exit-fullscreen");hide_notice();}}
    // A brief notice at the top of the screen, which never takes input.
    void show_notice(const std::wstring& text){
      if(!notice)notice=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,L"XenonScreenNotice",L"",WS_POPUP,0,0,10,10,window,nullptr,branding_module(),this);
      if(!notice)return;notice_text=text;notice_until=GetTickCount64()+3500;ui::popup_corners(notice);
      MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);const int width=d(text_width(text)+48),height=d(44);
      SetWindowPos(notice,HWND_TOPMOST,(monitor.rcMonitor.left+monitor.rcMonitor.right-width)/2,monitor.rcMonitor.top+d(28),width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);InvalidateRect(notice,nullptr,FALSE);
    }
    void hide_notice(){notice_until=0;if(notice)ShowWindow(notice,SW_HIDE);}
    HWND developer_panel{};bool developer_visible{},developer_pending{},developer_controls_only{};
    int developer_preferred_width{400},developer_split_y{};double developer_split{0.67};bool resizing_developer{},resizing_developer_split{};
    bool developer_rebuilding{},developer_picking{};std::string developer_selection,developer_tab;
    HWND developer_overlay{};Json developer_box=Json::object();
    std::string developer_error;POINT developer_hover_at{};bool developer_hover_dirty{},developer_hover_pending{},developer_hover_inside{};uint64_t developer_hover_version{};Clock::time_point developer_hover_next{};
    std::string developer_hover_error;
    std::string developer_highlight_target;bool developer_highlight_dirty{};uint64_t developer_highlight_version{};Clock::time_point developer_highlight_next{};
    void hover_developer(const POINT* event=nullptr);
    void select_developer(const std::string& diagnostic,bool focus=false){
      if(diagnostic==developer_selection)return;
      if(developer_controls_only&&std::none_of(developer_rows.begin(),developer_rows.end(),[&](const Json& n){return str(n,"diagnosticId")==diagnostic;})){developer_controls_only=false;SendMessageW(controls.at(DeveloperFilter),BM_SETCHECK,BST_UNCHECKED,0);developer_list();}
      auto list=controls.at(DeveloperList);
      for(size_t i=0;i<developer_rows.size();++i)if(str(developer_rows[i],"diagnosticId")==diagnostic){
        developer_rebuilding=true;ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);ListView_SetItemState(list,static_cast<int>(i),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);developer_rebuilding=false;
        ListView_EnsureVisible(list,static_cast<int>(i),FALSE);if(focus)SetFocus(list);developer_details();return;
      }
    }
    void position_developer_overlay(){
      if(!developer_overlay)return;
      const auto host=shell.hosts.find(selected);
      if(!developer_visible||host==shell.hosts.end()||!host->second.browser||(!developer_picking&&(developer_box.empty()||str(host->second.metadata,"developerHighlighted")!=developer_selection))){ShowWindow(developer_overlay,SW_HIDE);return;}
      RECT page{};GetWindowRect(host->second.browser,&page);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&page),2);
      SetWindowLongPtrW(developer_overlay,GWL_EXSTYLE,WS_EX_LAYERED|WS_EX_NOACTIVATE|(developer_picking?0:WS_EX_TRANSPARENT));
      SetLayeredWindowAttributes(developer_overlay,RGB(255,0,255),developer_picking?1:255,developer_picking?LWA_ALPHA:LWA_COLORKEY);
      SetWindowPos(developer_overlay,HWND_TOP,page.left,page.top,page.right-page.left,page.bottom-page.top,SWP_NOACTIVATE|SWP_SHOWWINDOW);
      InvalidateRect(developer_overlay,nullptr,TRUE);
    }
    void highlight_developer(const std::string& diagnostic){
      ++developer_highlight_version;
      developer_error.clear();developer_box=Json::object();ShowWindow(developer_overlay,SW_HIDE);
      developer_highlight_target=diagnostic;developer_highlight_dirty=true;developer_highlight_next=Clock::now()+std::chrono::milliseconds(75);
    }
    void dispatch_developer_highlight(){
      if(!developer_highlight_dirty||developer_hover_pending||Clock::now()<developer_highlight_next)return;
      developer_highlight_dirty=false;const auto diagnostic=developer_highlight_target;
      const auto tab=selected;const auto generation=developer_generation,version=developer_highlight_version;const auto target=window;auto owner=&shell;
      std::weak_ptr<Mailbox> weak=shell.mailbox;const auto messages=shell.messages;
      shell.engine.developer_highlight(tab,diagnostic,false,[weak,messages,owner,target,tab,generation,version,diagnostic](Json value){if(auto box=weak.lock()){
        {std::lock_guard lock(box->mutex);box->calls.push_back([owner,target,tab,generation,version,diagnostic,value=std::move(value)]{
          auto frame=owner->frame_of(target);if(!frame||!frame->developer_visible||frame->selected!=tab||frame->developer_generation!=generation||frame->developer_highlight_version!=version||frame->developer_selection!=diagnostic)return;
          if(!value.value("ok",false)){frame->developer_error=value.dump();frame->developer_details(false);return;}
          frame->developer_error.clear();frame->developer_box=value["result"];frame->position_developer_overlay();frame->developer_details(false);
        });}PostMessageW(messages,ShellRun,0,0);
      }});
    }
    Json developer_nodes=Json::array(),developer_rows=Json::array(),developer_visibility=Json::object(),developer_viewport=Json::object();
    DeveloperElementsIdentity developer_identity;uint64_t developer_generation{};Clock::time_point developer_next{};
    Json current_developer_context() const {
      const auto found=shell.hosts.find(selected);if(found==shell.hosts.end())return Json();
      const auto& m=found->second.metadata;
      return Json{{"tabId",selected},{"documentId",str(m,"documentId")},{"epoch",m.value("evidenceEpoch",uint64_t{})},
        {"human",m.value("humanInputRevision",uint64_t{})},{"protected",m.value("protected",false)},
        {"busy",m.value("humanBusy",false)},{"private",m.value("private",false)}};
    }
    void developer_details(bool highlight=true){
      if(developer_rebuilding)return;
      const int index=ListView_GetNextItem(controls.at(DeveloperList),-1,LVNI_SELECTED);
      std::wstring text;
      std::string diagnostic;
      if(index>=0&&static_cast<size_t>(index)<developer_rows.size()){
        auto node=developer_rows[index];diagnostic=str(node,"diagnosticId");
        node["pageVisibility"]=developer_visibility;
        node["viewport"]=developer_viewport;
        if(str(node,"name").empty())node["labelExplanation"]=node.contains("siteLabel")?ui::tr8("Site-provided label; rendered text is not verified. This untrusted metadata is shown only in the human inspector."):ui::tr8("No verified rendered label. The control may be icon-only or drawn by a native browser widget. Use page picking or visual evidence to identify it.");
        const auto host=shell.hosts.find(selected);node["highlighted"]=host!=shell.hosts.end()&&!diagnostic.empty()&&str(host->second.metadata,"developerHighlighted")==diagnostic;
        if(node["highlighted"]==true&&!developer_box.empty()){node["highlightBounds"]=developer_box;node["highlightVerification"]=developer_box.value("approximate",false)?"approximate_position_only":"verified_geometry";}
        if(!developer_error.empty())node["highlightError"]=developer_error;
        if(!developer_hover_error.empty())node["hoverError"]=developer_hover_error;
        text=ui::wide(node.dump(2));
        for(size_t pos=0;(pos=text.find(L'\n',pos))!=std::wstring::npos;pos+=2)text.insert(pos,L"\r");
      }
      else if(!developer_visibility.empty()){
        text=ui::wide(Json{{"pageVisibility",developer_visibility},{"viewport",developer_viewport}}.dump(2));
        for(size_t pos=0;(pos=text.find(L'\n',pos))!=std::wstring::npos;pos+=2)text.insert(pos,L"\r");
      }
      // A refresh must not reset the user's scroll position or text selection.
      const auto details=controls.at(DeveloperNodeDetails);
      if(ui::text(details)!=ui::utf8(text)){
        const auto line=diagnostic==developer_selection?SendMessageW(details,EM_GETFIRSTVISIBLELINE,0,0):0;DWORD start{},end{};if(diagnostic==developer_selection)SendMessageW(details,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
        SetWindowTextW(details,text.c_str());SendMessageW(details,EM_SETSEL,start,end);SendMessageW(details,EM_LINESCROLL,0,line);
      }
      if(highlight&&!developer_picking&&diagnostic!=developer_selection){developer_selection=diagnostic;developer_tab=selected;
        highlight_developer(diagnostic);
      }
    }
    void developer_list(){
      auto list=controls.at(DeveloperList);developer_rebuilding=true;int next=-1;
      SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);developer_rows=Json::array();
      for(const auto& node:developer_nodes){
        const auto actions=developer_element_actions(node);const bool uncertain=str(node,"visibilityStatus")=="uncertain";if(developer_controls_only&&actions.empty()&&!uncertain)continue;
        const auto index=static_cast<int>(developer_rows.size());developer_rows.push_back(node);
        if(str(node,"diagnosticId")==developer_selection&&!developer_selection.empty())next=index;
        auto role=ui::wide(str(node,"role")),name=ui::wide(str(node,"name"));if(uncertain)role+=L" (?)";const bool site_name=name.empty()&&node.contains("siteLabel");if(site_name)name=ui::wide(str(node["siteLabel"],"text"));if(name.empty())name=ui::tr(L"No verified label");
        auto control=ui::wide(uncertain?ui::tr8("Check screenshot"):actions.empty()?ui::tr8("Read only"):ui::tr8(actions.c_str()));
        LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=index;item.pszText=role.data();ListView_InsertItem(list,&item);
        ListView_SetItemText(list,index,1,name.data());ListView_SetItemText(list,index,2,control.data());
        std::wstring source=ui::tr(site_name?L"Site-provided":str(node,"name").empty()?L"Unlabeled":L"Rendered text");ListView_SetItemText(list,index,3,source.data());
        auto visibility=ui::wide(ui::tr8(uncertain?"Uncertain — check screenshot":"Verified visible"));ListView_SetItemText(list,index,4,visibility.data());
      }
      if(next>=0)ListView_SetItemState(list,next,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
      developer_rebuilding=false;SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,TRUE);developer_details();
    }
    void clear_developer(const wchar_t* message){
      if(!developer_tab.empty())shell.engine.developer_highlight(developer_tab,{},true);
      developer_tab.clear();developer_selection.clear();developer_picking=false;
      developer_error.clear();developer_hover_error.clear();developer_hover_dirty=false;developer_hover_pending=false;developer_hover_inside=false;++developer_hover_version;
      developer_highlight_dirty=false;developer_highlight_target.clear();
      developer_box=Json::object();if(developer_overlay)ShowWindow(developer_overlay,SW_HIDE);
      if(controls.contains(DeveloperPick))SetWindowTextW(controls.at(DeveloperPick),ui::tr(L"Pick from page"));
      developer_generation=developer_identity.reset(current_developer_context());developer_pending=false;developer_next={};
      developer_nodes=Json::array();developer_visibility=Json::object();developer_viewport=Json::object();developer_list();SetWindowTextW(controls.at(DeveloperStatus),message);
    }
    void refresh_developer();
    void pick_developer();
    explicit Frame(Impl& owner):shell(owner){}
    int d(int value) const {return ui::dip(window,value);}
    HWND add(int id,const wchar_t* type,const wchar_t* caption,DWORD style=0,HWND parent=nullptr){
      auto result=CreateWindowExW(0,type,caption,WS_CHILD|WS_VISIBLE|style,0,0,10,10,parent?parent:window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),branding_module(),nullptr);
      SendMessageW(result,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);controls[id]=result;ui::control_theme(result);return result;}
    void tip(HWND control,const wchar_t* title){TOOLINFOW tip{sizeof(tip)};tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tip.hwnd=GetParent(control);tip.uId=reinterpret_cast<UINT_PTR>(control);tip.lpszText=const_cast<LPWSTR>(title);SendMessageW(tooltips,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tip));}
    void build(){font=ui::font(window);bold=ui::font(window,13,FW_SEMIBOLD);ui::icons(window);
      tooltips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,window,nullptr,branding_module(),nullptr);
      SendMessageW(tooltips,TTM_SETDELAYTIME,TTDT_INITIAL,400);SendMessageW(tooltips,TTM_SETMAXTIPWIDTH,0,d(280));
      find_bar=CreateWindowExW(WS_EX_CONTROLPARENT,L"XenonFindBar",L"",WS_CHILD|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,0,0,10,10,window,nullptr,branding_module(),this);
      for(const auto& [id,title]:std::vector<std::pair<int,const wchar_t*>>{{Back,ui::tr(L"Back")},{Forward,ui::tr(L"Forward")},{Reload,ui::tr(L"Reload")},{Extensions,ui::tr(L"Extensions")},{Controls,ui::tr(L"Controls")},{Menu,ui::tr(L"Menu")},
          {Passwords,ui::tr(L"Saved passwords for this site")},{BookmarkStar,ui::tr(L"Bookmark this tab")},{SiteInfo,ui::tr(L"View site information")},{NewTab,ui::tr(L"New tab")},{FindPrevious,ui::tr(L"Previous match")},{FindNext,ui::tr(L"Next match")},{FindClose,ui::tr(L"Close find")}}){
        const bool find=id==FindPrevious||id==FindNext||id==FindClose;auto control=add(id,L"BUTTON",title,BS_OWNERDRAW|WS_TABSTOP,find?find_bar:nullptr);tip(control,title);}
      for(int id:{Passwords,BookmarkStar,SiteInfo,FindPrevious,FindNext,FindClose})SetPropW(controls.at(id),L"XenonSurface",reinterpret_cast<HANDLE>(1));
      site_title=ui::font(window,15,FW_SEMIBOLD);site_small=ui::font(window,12);
      address=add(Address,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL);
      find_text=add(FindText,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,find_bar);
      tree=add(Tree,WC_TREEVIEWW,ui::tr(L"Workspace tabs"),WS_TABSTOP|TVS_HASBUTTONS|TVS_LINESATROOT|TVS_SHOWSELALWAYS|TVS_FULLROWSELECT|TVS_NOHSCROLL);
      SetWindowSubclass(tree,tree_proc,3,reinterpret_cast<DWORD_PTR>(this));TreeView_SetExtendedStyle(tree,TVS_EX_DOUBLEBUFFER,TVS_EX_DOUBLEBUFFER);TreeView_SetItemHeight(tree,d(TabRow));
      // The scrollbar sits on the sidebar's outer edge, so it never separates
      // the selected tab from the page frame it joins.
      SetWindowLongPtrW(tree,GWL_EXSTYLE,GetWindowLongPtrW(tree,GWL_EXSTYLE)|WS_EX_LEFTSCROLLBAR);SetWindowPos(tree,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
      SetWindowSubclass(address,edit_proc,1,reinterpret_cast<DWORD_PTR>(this));SetWindowSubclass(find_text,edit_proc,2,reinterpret_cast<DWORD_PTR>(this));
      emphasis=ui::font(window,14,FW_SEMIBOLD);
      suggestions=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"XenonAddressSuggestions",ui::tr(L"Address suggestions"),WS_POPUP,0,0,10,10,window,nullptr,branding_module(),this);
      if(suggestions){ui::frame(suggestions);ui::popup_corners(suggestions);}
      cursor=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"XenonAgentCursor",ui::tr(L"Agent pointer"),WS_POPUP,0,0,25,30,window,nullptr,branding_module(),this);
      agent_glow=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,L"XenonAgentCursor",L"",WS_CHILD,0,0,1,1,window,nullptr,branding_module(),this);
      developer_panel=CreateWindowExW(WS_EX_CONTROLPARENT,L"XenonElementsPanel",ui::tr(L"Agent page elements"),WS_CHILD|WS_CLIPCHILDREN,0,0,10,10,window,nullptr,branding_module(),this);
      developer_overlay=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,L"XenonElementOverlay",L"",WS_CHILD,0,0,10,10,window,nullptr,branding_module(),this);
      add(DeveloperTitle,L"STATIC",ui::tr(L"Agent page elements"),0,developer_panel);
      add(DeveloperHint,L"STATIC",ui::tr(L"Visible page evidence. Actions require agent permission and a fresh observation."),0,developer_panel);
      add(DeveloperFilter,L"BUTTON",ui::tr(L"Interactable only"),BS_AUTOCHECKBOX|WS_TABSTOP,developer_panel);
      add(DeveloperPick,L"BUTTON",ui::tr(L"Pick from page"),BS_PUSHBUTTON|WS_TABSTOP,developer_panel);
      auto elements=add(DeveloperList,WC_LISTVIEWW,L"",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,developer_panel);
      ListView_SetExtendedListViewStyle(elements,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
      for(const auto& [column,title]:std::vector<std::pair<int,const wchar_t*>>{{0,ui::tr(L"Role")},{1,ui::tr(L"Name")},{2,ui::tr(L"Possible actions")},{3,ui::tr(L"Label source")},{4,ui::tr(L"Visibility")}}){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.cx=d(column==1||column==4?180:95);c.pszText=const_cast<wchar_t*>(title);ListView_InsertColumn(elements,column,&c);}
      add(DeveloperStatus,L"STATIC",ui::tr(L"Waiting for rendered page evidence…"),0,developer_panel);
      add(DeveloperNodeDetails,L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_MULTILINE|ES_READONLY|ES_AUTOHSCROLL|WS_HSCROLL|WS_VSCROLL,developer_panel);
      SendMessageW(controls.at(DeveloperNodeDetails),EM_SETLIMITTEXT,1024*1024,0);
      sidebar=ui::sidebar_width;theme();layout();
    }
    void theme(){auto colors=ui::palette();ui::frame(window);TreeView_SetBkColor(tree,colors.canvas);TreeView_SetTextColor(tree,colors.ink);TreeView_SetLineColor(tree,colors.border);
      if(controls.contains(DeveloperList)){auto list=controls.at(DeveloperList);ListView_SetBkColor(list,colors.surface);ListView_SetTextBkColor(list,colors.surface);ListView_SetTextColor(list,colors.ink);}
      SetWindowTheme(tooltips,L"",L"");SendMessageW(tooltips,TTM_SETTIPBKCOLOR,colors.surface,0);SendMessageW(tooltips,TTM_SETTIPTEXTCOLOR,colors.ink,0);
      for(const auto& [id,control]:controls){ui::control_theme(control);InvalidateRect(control,nullptr,TRUE);}InvalidateRect(window,nullptr,TRUE);InvalidateRect(find_bar,nullptr,TRUE);
      if(suggestions){ui::frame(suggestions);InvalidateRect(suggestions,nullptr,FALSE);}}
    // A moved child discards its copied pixels and repaints whole. Otherwise a
    // live window resize repaints only each right-anchored button's newly
    // exposed strip and leaves slivers of earlier frames across the rest.
    void reposition(HWND target,HWND after,int x,int y,int width,int height,UINT flags){RECT now{};GetWindowRect(target,&now);MapWindowPoints(nullptr,GetParent(target),reinterpret_cast<POINT*>(&now),2);
      if(now.left!=x||now.top!=y)flags|=SWP_NOCOPYBITS;SetWindowPos(target,after,x,y,width,height,flags);}
    void place(int id,int x,int y,int width,int height){reposition(controls.at(id),nullptr,d(x),d(y),d(width),d(height),SWP_NOZORDER|SWP_NOACTIVATE);}
    // Chip text: a transient status message, otherwise the selected tab's owner
    // once the broker reports it. A tab that is still opening shows no chip, so
    // the omnibox does not shrink and regrow for every new tab.
    std::wstring current_chip() const {return !status.empty()&&GetTickCount64()<status_until?status:shell.hosts.contains(selected)&&shell.states.contains(selected)?state_text(selected):std::wstring{};}
    int text_width(const std::wstring& text) const {auto dc=GetDC(window);auto old=SelectObject(dc,font);SIZE size{};GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&size);SelectObject(dc,old);ReleaseDC(window,dc);return MulDiv(size.cx,96,GetDpiForWindow(window));}
    // In full screen the page fills the window; Xenon's controls stay hidden.
    void layout_fullscreen(){RECT rect{};GetClientRect(window,&rect);omnibox=chip=RECT{};panel_bounds=page=rect;
      RECT inner=rect;if(color(selected)!=ui::palette().gray)InflateRect(&inner,-d(2),-d(2));
      reposition(developer_panel,nullptr,0,0,1,1,SWP_NOACTIVATE|SWP_NOZORDER|SWP_HIDEWINDOW);if(developer_overlay)ShowWindow(developer_overlay,SW_HIDE);
      for(auto& [id,host]:shell.hosts)if(host.frame==this){SetWindowPos(host.window,nullptr,inner.left,inner.top,std::max<int>(1,inner.right-inner.left),std::max<int>(1,inner.bottom-inner.top),SWP_NOACTIVATE|SWP_NOZORDER);
        if(host.browser)SetWindowPos(host.browser,nullptr,0,0,std::max<int>(1,inner.right-inner.left),std::max<int>(1,inner.bottom-inner.top),SWP_NOACTIVATE|SWP_NOZORDER);}
      reposition(find_bar,HWND_TOP,inner.right-d(356),inner.top+d(6),d(344),d(40),SWP_NOACTIVATE|(find_visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
      InvalidateRect(window,nullptr,TRUE);}
    void layout(){if(fullscreen()){layout_fullscreen();return;}
      RECT rect{};GetClientRect(window,&rect);const int scale=GetDpiForWindow(window);const int width=MulDiv(rect.right,96,scale),height=MulDiv(rect.bottom,96,scale);
      sidebar=std::clamp(ui::sidebar_width,180,std::max(180,std::min(480,width-400)));
      // Chromium-style toolbar: navigation at the left, the omnibox in the
      // middle, the ownership chip and browser buttons at the right.
      place(Back,8,6,32,32);place(Forward,42,6,32,32);place(Reload,76,6,32,32);
      place(Menu,width-40,6,32,32);place(Controls,width-74,6,32,32);place(Extensions,width-108,6,32,32);
      // Status messages may widen the chip; the omnibox keeps at least 240 DIPs.
      chip_text=current_chip();const int omni_left=116,chip_right=width-116,room=std::max(0,chip_right-8-omni_left-240);
      int chip_width=chip_text.empty()?0:std::min({text_width(chip_text)+30,status.empty()?260:420,room});if(chip_width<60)chip_width=0;
      const int chip_left=chip_right-chip_width,omni_right=chip_width?chip_left-8:chip_right-4;
      omnibox={d(omni_left),d(6),d(omni_right),d(38)};chip={d(chip_left),d(8),d(chip_right),d(36)};
      const bool key=saved_accounts>0&&!is_private();ShowWindow(controls.at(Passwords),key?SW_SHOWNA:SW_HIDE);
      // Keep the complete child surfaces inside the omnibox's curved edge.
      place(BookmarkStar,omni_right-36,10,24,24);place(Passwords,omni_right-64,10,24,24);
      // The site-info button leads the omnibox, as in Chromium.
      const int info=site_button_width();ShowWindow(controls.at(SiteInfo),info?SW_SHOWNA:SW_HIDE);if(info)place(SiteInfo,omni_left+6,10,info,24);
      const int address_left=info?omni_left+12+info:omni_left+16;
      place(Address,address_left,13,std::max(60,omni_right-(key?68:38)-address_left),19);
      // A widened address edit can cover where a moved button was drawn; the
      // edit does not repaint that newly covered strip on its own.
      if(address)RedrawWindow(address,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME);
      place(NewTab,8,ToolbarHeight+4,sidebar-16,32);place(Tree,6,ToolbarHeight+42,sidebar-12,std::max(60,height-ToolbarHeight-50));
      // The tree view repaints only newly exposed pixels on resize, but each
      // row's close button and pill are positioned from its full width.
      InvalidateRect(tree,nullptr,FALSE);
      // Every frame corner is rounded. The page stays rectangular inside the
      // straight edges: equal strips above and below it clear the curves
      // instead of masking website pixels.
      panel_bounds={d(sidebar),d(ToolbarHeight),rect.right-d(8),rect.bottom-d(8)};
      const int developer_width=developer_visible?std::clamp(developer_preferred_width,240,std::max(240,width-sidebar-168)):0;
      if(developer_visible)panel_bounds.right-=d(developer_width+8);
      reposition(developer_panel,nullptr,panel_bounds.right+d(8),d(ToolbarHeight),d(developer_width),std::max<int>(1,rect.bottom-d(ToolbarHeight+8)),SWP_NOACTIVATE|SWP_NOZORDER|(developer_visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
      if(developer_visible){const int panel_height=height-ToolbarHeight-8,space=std::max(140,panel_height-188),list_height=std::clamp(static_cast<int>(space*developer_split),80,space-60);developer_split_y=166+list_height;
        place(DeveloperTitle,12,12,developer_width-24,24);place(DeveloperHint,12,42,developer_width-24,48);
        place(DeveloperFilter,12,96,developer_width/2-12,24);place(DeveloperPick,developer_width/2,96,developer_width/2-12,24);place(DeveloperList,12,128,developer_width-24,list_height);
        place(DeveloperStatus,12,134+list_height,developer_width-24,30);place(DeveloperNodeDetails,12,176+list_height,developer_width-24,std::max(45,panel_height-list_height-188));InvalidateRect(developer_panel,nullptr,TRUE);}
      page={panel_bounds.left,panel_bounds.top+d(FrameRadius),panel_bounds.right,panel_bounds.bottom-d(FrameRadius)};
      RECT inner=page;InflateRect(&inner,-d(2),-d(2));
      for(auto& [id,host]:shell.hosts)if(host.frame==this){SetWindowPos(host.window,nullptr,inner.left,inner.top,std::max<int>(1,inner.right-inner.left),std::max<int>(1,inner.bottom-inner.top),SWP_NOACTIVATE|SWP_NOZORDER);
        if(host.browser)SetWindowPos(host.browser,nullptr,0,0,std::max<int>(1,inner.right-inner.left),std::max<int>(1,inner.bottom-inner.top),SWP_NOACTIVATE|SWP_NOZORDER);}
      // Find floats over the page's top-right corner, as in Chromium; it
      // never resizes the page.
      reposition(find_bar,HWND_TOP,inner.right-d(356),inner.top+d(6),d(344),d(40),SWP_NOACTIVATE|(find_visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
      MoveWindow(find_text,d(12),d(9),d(200),d(22),TRUE);MoveWindow(controls.at(FindPrevious),d(220),d(4),d(32),d(32),TRUE);MoveWindow(controls.at(FindNext),d(256),d(4),d(32),d(32),TRUE);MoveWindow(controls.at(FindClose),d(304),d(4),d(32),d(32),TRUE);
      if(suggesting())place_suggestions();
      InvalidateRect(window,nullptr,TRUE);
    }
    bool sidebar_hit(POINT point) const {RECT bounds{};GetClientRect(window,&bounds);return point.x>=d(sidebar-6)&&point.x<d(sidebar+1)&&point.y>=d(ToolbarHeight)&&point.y<bounds.bottom-d(8);}
    bool developer_width_hit(POINT point) const {return developer_visible&&point.x>=panel_bounds.right&&point.x<panel_bounds.right+d(8)&&point.y>=panel_bounds.top&&point.y<panel_bounds.bottom;}
    bool developer_split_hit(POINT point) const {RECT bounds{};GetClientRect(developer_panel,&bounds);return developer_visible&&point.x>=d(12)&&point.x<bounds.right-d(12)&&point.y>=d(developer_split_y)&&point.y<d(developer_split_y+10);}
    void finish_developer_resize(){resizing_developer=false;resizing_developer_split=false;if(GetCapture()==window||GetCapture()==developer_panel)ReleaseCapture();}
    RECT sidebar_grip() const {RECT bounds{};GetClientRect(window,&bounds);const auto middle=(d(ToolbarHeight)+bounds.bottom)/2;return {d(sidebar-5),middle-d(16),d(sidebar-2),middle+d(16)};}
    void invalidate_grip(){auto rect=sidebar_grip();InvalidateRect(window,&rect,FALSE);}
    void set_address(const std::string& value){if(ui::text(address)==value)return;setting_address=true;SetWindowTextW(address,ui::wide(value).c_str());setting_address=false;}
    // Address text chosen by a suggestion. Text after `selected_from` bytes is
    // the inline completion, left selected so typing replaces it.
    void show_text(const std::string& value,size_t selected_from=std::string::npos){
      setting_address=true;SetWindowTextW(address,ui::wide(value).c_str());setting_address=false;const int length=GetWindowTextLengthW(address);
      const int start=selected_from==std::string::npos?length:static_cast<int>(ui::wide(value.substr(0,selected_from)).size());
      SendMessageW(address,EM_SETSEL,start,length);SendMessageW(address,EM_SCROLLCARET,0,0);
    }
    bool suggesting() const {return suggestions&&IsWindowVisible(suggestions);}
    void hide_suggestions(){suggestion_hover=suggestion_pressed=-1;if(suggesting())ShowWindow(suggestions,SW_HIDE);}
    // The list hangs below the omnibox at its width, over the page frame.
    void place_suggestions(){
      if(!suggestions||matches.empty()||GetFocus()!=address||!IsWindowVisible(window)||IsIconic(window)){hide_suggestions();return;}
      RECT box=omnibox;MapWindowPoints(window,nullptr,reinterpret_cast<POINT*>(&box),2);
      const int height=2*d(SuggestionInset)+static_cast<int>(matches.size())*d(SuggestionRow);
      SetWindowPos(suggestions,HWND_TOP,box.left,box.bottom+d(4),box.right-box.left,height,SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_SHOWWINDOW);InvalidateRect(suggestions,nullptr,FALSE);
    }
    // Suggestions for the address after each edit. Only a character typed at the
    // end completes inline, so deleting, pasting or undoing never brings one back.
    void suggest(){
      const auto text=ui::text(address);DWORD start{},end{};SendMessageW(address,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
      const bool typed_character=std::exchange(typing,false),at_end=start==end&&end==static_cast<DWORD>(GetWindowTextLengthW(address));
      typed=text;suggestion=-1;matches.clear();
      try{matches=shell.data.suggest(workspace(),is_private(),text,typed_character&&at_end);}catch(...){}
      if(!matches.empty()){suggestion=0;if(!matches[0].completion.empty())show_text(matches[0].fill,text.size());}
      place_suggestions();
    }
    // Up and Down move through the list and show the row's address, as in
    // Chromium; on a closed list they open it for the current text.
    void step_suggestion(int step){
      if(!suggesting()){if(GetWindowTextLengthW(address)>0){typing=false;suggest();}return;}
      const int next=std::clamp(suggestion+step,0,static_cast<int>(matches.size())-1);if(next==suggestion)return;suggestion=next;
      const auto& match=matches[static_cast<size_t>(next)];show_text(match.fill,match.completion.empty()?std::string::npos:typed.size());InvalidateRect(suggestions,nullptr,FALSE);
    }
    // Shift+Delete deletes the selected history suggestion's visits.
    bool remove_suggestion(){
      if(!suggesting()||suggestion<0||static_cast<size_t>(suggestion)>=matches.size()||!matches[static_cast<size_t>(suggestion)].removable)return false;
      bool removed=false;try{removed=shell.data.remove_history(workspace(),is_private(),matches[static_cast<size_t>(suggestion)].destination);}catch(...){}
      if(!removed)set_status(ui::tr(L"History could not be updated."));
      show_text(typed);typing=false;suggest();return true;
    }
    int suggestion_at(int y) const {const int top=d(SuggestionInset);if(y<top)return -1;const int row=(y-top)/d(SuggestionRow);return row<static_cast<int>(matches.size())?row:-1;}
    // Draws text left to right with matched characters in bold and advances
    // area.left. Text past `limit` pixels ends in an ellipsis.
    void runs(HDC dc,RECT& area,const std::wstring& text,const std::vector<bool>& marked,COLORREF ink,int limit) const {
      int room=std::min<int>(limit,area.right-area.left);
      for(size_t at=0;at<text.size()&&room>0;){
        const bool strong=marked[at];size_t end=at;while(end<text.size()&&marked[end]==strong)++end;
        const auto face=strong?emphasis:font;const auto old=SelectObject(dc,face);SIZE size{};GetTextExtentPoint32W(dc,text.c_str()+at,static_cast<int>(end-at),&size);SelectObject(dc,old);
        const bool last=size.cx>room;RECT cell{area.left,area.top,area.left+(last?room:size.cx),area.bottom};
        ui::text(dc,cell,last?text.substr(at):text.substr(at,end-at),face,ink,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
        room-=cell.right-cell.left;area.left=cell.right;if(last)break;at=end;
      }
    }
    // Rows read like Chromium's: the URL in link color, then the page title.
    // Titles are website text; they are shown only here, to the person.
    void paint_suggestions(HDC dc,RECT bounds) const {
      const auto colors=ui::palette();ui::fill(dc,bounds,colors.surface);ui::outline(dc,bounds,colors.border,0,1);
      const auto query=ui::wide(typed);const auto plain=[](const std::wstring& text){return std::vector<bool>(text.size());};
      for(size_t n=0;n<matches.size();++n){
        const auto& match=matches[n];const int top=d(SuggestionInset)+static_cast<int>(n)*d(SuggestionRow);RECT row{d(6),top,bounds.right-d(6),top+d(SuggestionRow)};
        const bool chosen=static_cast<int>(n)==suggestion,hot=static_cast<int>(n)==suggestion_hover;
        // High contrast uses the system highlight for hover too, never a blend.
        if(chosen||hot){const auto fill=chosen||colors.contrast?ui::hover_background():ui::mix(colors.surface,ui::hover_background(),0.5);ui::rounded(dc,row,fill,fill,d(6));}
        const bool inverse=(chosen||hot)&&colors.contrast;const auto ink=inverse?ui::selection_ink():colors.ink,muted=inverse?ui::selection_ink():colors.muted,link=inverse?ui::selection_ink():ui::notice_blue();
        RECT glyph{row.left+d(4),row.top,row.left+d(32),row.bottom};
        ui::icon(dc,glyph,match.kind==omnibox::Kind::search?ui::Icon::search:match.kind==omnibox::Kind::bookmark?ui::Icon::star_filled:match.kind==omnibox::Kind::history?ui::Icon::history:ui::Icon::globe,muted,window);
        RECT area{row.left+d(40),row.top,row.right-d(10),row.bottom};const int width=area.right-area.left;const std::wstring dash=L" – ";
        if(match.kind==omnibox::Kind::search){const auto text=omnibox::printable(ui::wide(match.display)),label=dash+ui::format(ui::tr(L"{0} Search"),{omnibox::search_engine().name});runs(dc,area,text,plain(text),ink,width*3/4);runs(dc,area,label,plain(label),muted,width);continue;}
        const auto url=omnibox::printable(ui::wide(match.display)),title=omnibox::printable(ui::wide(match.title));
        runs(dc,area,url,omnibox::emphasis(url,query),link,title.empty()?width:width*3/5);
        if(!title.empty()){runs(dc,area,dash,plain(dash),muted,width);runs(dc,area,title,omnibox::emphasis(title,query),ink,width);}
      }
    }
    // ---- Site information ---------------------------------------------------
    // The toolbar button is an icon, widened into a "Not secure" chip for HTTP
    // and certificate errors, as in Chromium. Blank and internal pages hide it.
    bool not_secure() const {return security=="insecure"||security=="dangerous";}
    int site_button_width() const {return security.empty()||security=="internal"?0:not_secure()?text_width(ui::tr(L"Not secure"))+34:24;}
    ui::Icon site_icon() const {return security=="secure"||security=="protected"?ui::Icon::lock:not_secure()?ui::Icon::warning:ui::Icon::info;}
    void draw_site_button(const DRAWITEMSTRUCT& item){ui::buffered(item,[&](const DRAWITEMSTRUCT& copy){const auto colors=ui::palette();auto rect=copy.rcItem;ui::fill(copy.hDC,rect,colors.surface);
      const double hover=ui::hover_amount(copy);const auto background=site_bubble||(copy.itemState&ODS_SELECTED)?ui::selection():ui::mix(colors.surface,ui::hover_background(),hover);
      ui::rounded(copy.hDC,rect,background,background,d(12));
      const auto ink=colors.contrast&&(hover>=0.5||site_bubble)?ui::selection_ink():security=="dangerous"?ui::danger_red():not_secure()?colors.muted:colors.ink;
      RECT glyph{rect.left,rect.top,rect.left+d(24),rect.bottom};ui::icon(copy.hDC,glyph,site_icon(),ink,copy.hwndItem);
      if(not_secure()){RECT caption{rect.left+d(23),rect.top,rect.right-d(6),rect.bottom};ui::text(copy.hDC,caption,ui::tr(L"Not secure"),font,ink);}
      if(copy.itemState&ODS_FOCUS)ui::focus_mark(copy.hDC,rect,copy.hwndItem,12);});}
    void close_site(){if(!site_bubble)return;const auto bubble=std::exchange(site_bubble,nullptr);site_closed=GetTickCount64();DestroyWindow(bubble);if(controls.contains(SiteInfo))InvalidateRect(controls.at(SiteInfo),nullptr,FALSE);}
    // Site information is read when the bubble opens; the engine rechecks the
    // tab's website before every change made from it.
    void open_site(){if(!shell.hosts.contains(selected))return;const auto tab=selected;const auto target=window;auto& owner=shell;
      shell.engine.site_info(tab,[&owner,target,tab](Json value){owner.post([&owner,target,tab,value=std::move(value)]{auto frame=owner.frame_of(target);if(!frame||frame->selected!=tab)return;
        if(!value.value("ok",false)){frame->set_status(ui::error_text(str(value.value("error",Json::object()),"message")));return;}frame->show_site(value["result"]);});});}
    void show_site(const Json& info){close_site();site=info;site_tab=selected;site_origin=str(info,"origin");site_details=false;
      site_bubble=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,L"XenonSiteInfo",ui::tr(L"Site information"),WS_POPUP|WS_CLIPCHILDREN,0,0,10,10,window,nullptr,branding_module(),this);
      if(!site_bubble)return;ui::frame(site_bubble);ui::popup_corners(site_bubble);
      // Focus rings show when the button was reached with the keyboard; Tab shows them later too.
      SendMessageW(site_bubble,WM_CHANGEUISTATE,MAKEWPARAM(ui::keyboard_focus(controls.at(SiteInfo))?UIS_CLEAR:UIS_SET,UISF_HIDEFOCUS),0);
      build_site();InvalidateRect(controls.at(SiteInfo),nullptr,FALSE);}
    HWND site_control(int id,const wchar_t* type,const std::wstring& text,DWORD style,HFONT face){
      auto control=CreateWindowExW(0,type,text.c_str(),WS_CHILD|WS_VISIBLE|style,0,0,10,10,site_bubble,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),branding_module(),nullptr);
      SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(face),TRUE);ui::control_theme(control);return control;}
    // Wrapped text height in DIPs for a bubble label of `width` DIPs.
    int text_height(const std::wstring& text,HFONT face,int width) const {auto dc=GetDC(window);auto old=SelectObject(dc,face);RECT bounds{0,0,d(width),0};
      DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&bounds,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);ReleaseDC(window,dc);return MulDiv(bounds.bottom,96,GetDpiForWindow(window))+2;}
    static std::wstring date_text(int64_t milliseconds){if(milliseconds<=0)return {};ULARGE_INTEGER value{};value.QuadPart=static_cast<ULONGLONG>(milliseconds+11644473600000LL)*10000;FILETIME utc{value.LowPart,value.HighPart},local{};SYSTEMTIME system{};
      if(!FileTimeToLocalFileTime(&utc,&local)||!FileTimeToSystemTime(&local,&system))return {};wchar_t date[80]{};GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,DATE_SHORTDATE,&system,nullptr,date,80,nullptr);return date;}
    static std::wstring cookie_count(int count){return count<=0?ui::tr(L"None"):count==1?ui::tr(L"1 cookie"):ui::format(ui::tr(L"{0} cookies"),{std::to_wstring(count)});}
    // Chromium's wording for each connection state.
    std::pair<std::wstring,std::wstring> connection_text() const {const auto state=str(site,"state");
      if(state=="secure")return {ui::tr(L"Connection is secure"),ui::tr(L"Your information (for example, passwords or credit card numbers) is private when it is sent to this site.")};
      if(state=="mixed")return {ui::tr(L"Connection is not fully secure"),ui::tr(L"Parts of this page, such as images, are loaded over an insecure connection.")};
      if(state=="dangerous")return {ui::tr(L"Connection is not secure"),ui::tr(L"This site's certificate has a problem. Attackers might be trying to steal your information.")};
      if(state=="local")return {ui::tr(L"Connection is not secure"),ui::tr(L"This page is served from this computer over HTTP, without encryption.")};
      if(state=="pending")return {ui::tr(L"Connection details are not available yet"),ui::tr(L"Reload the page if this does not change.")};
      return {ui::tr(L"Connection is not secure"),ui::tr(L"You should not enter any sensitive information on this site (for example, passwords or credit cards), because it could be stolen by attackers.")};}
    // Lays out the bubble's main or connection view from `site` and shows it
    // below the button. Children are real controls, so Tab, Enter and screen
    // readers work as in any dialog.
    void build_site(){
      for(HWND child=GetWindow(site_bubble,GW_CHILD);child;){const auto next=GetWindow(child,GW_HWNDNEXT);DestroyWindow(child);child=next;}site_rows.clear();
      const int width=360;int y=10;std::vector<std::tuple<HWND,int,int,int,int>> placed;HWND first{};
      // Focus starts on the first row, as in Chromium, not on Close.
      const auto put=[&](HWND control,int x,int top,int w,int h){placed.emplace_back(control,x,top,w,h);if(!first&&(GetWindowLongPtrW(control,GWL_STYLE)&WS_TABSTOP)&&GetDlgCtrlID(control)!=SiteClose)first=control;};
      const auto label=[&](int id,const std::wstring& text,HFONT face,int x,int w){const int h=text_height(text,face,w);put(site_control(id,L"STATIC",text,SS_LEFT|SS_NOPREFIX,face),x,y,w,h);y+=h;};
      const auto row=[&](int id,ui::Icon icon,const std::wstring& text,const std::wstring& detail,bool chevron,COLORREF tint=0){site_rows[id]={icon,detail,chevron,tint};put(site_control(id,L"BUTTON",text,BS_OWNERDRAW|WS_TABSTOP,font),8,y,width-16,40);y+=40;};
      const auto state=str(site,"state");const auto [headline,explanation]=connection_text();
      const auto connection_icon=state=="secure"?ui::Icon::lock:state=="insecure"||state=="dangerous"?ui::Icon::warning:ui::Icon::info;const COLORREF tint=state=="dangerous"?ui::danger_red():0;
      put(site_control(SiteClose,L"BUTTON",ui::tr(L"Close"),BS_OWNERDRAW|WS_TABSTOP,font),width-40,8,28,28);
      if(site_details){
        row(SiteBack,ui::Icon::back,ui::tr(L"Connection security"),{},false);y+=4;
        label(SiteTitle,headline,site_title,16,width-32);y+=6;label(SiteText,explanation,font,16,width-32);
        const auto certificate=site.value("certificate",Json::object());std::wstring details;
        if(!certificate.empty()){details=ui::format(ui::tr(L"Issued to: {0}"),{ui::wide(str(certificate,"subject"))})+L"\n"+ui::format(ui::tr(L"Issued by: {0}"),{ui::wide(str(certificate,"issuer"))})+L"\n"+
          ui::format(ui::tr(L"Valid until: {0}"),{date_text(certificate.value("validTo",int64_t{}))});}
        if(!str(site,"protocol").empty())details+=(details.empty()?L"":L"\n")+ui::format(ui::tr(L"Protocol: {0}"),{ui::wide(str(site,"protocol"))});
        if(!details.empty()){y+=8;label(SiteDetails,details,site_small,16,width-32);}
        if(!str(certificate,"der").empty()){y+=6;row(SiteCertificate,ui::Icon::lock,ui::tr(L"Certificate details"),{},true);}
      }else{
        const auto workspace=ui::wide(ui::tr8("Workspace: ")+(shell.workspace_names.contains(workspace_id())?shell.workspace_names.at(workspace_id()):workspace_id()));
        if(state=="protected"){label(SiteTitle,ui::tr(L"Protected sign-in"),site_title,16,width-64);label(SiteSubtitle,workspace,site_small,16,width-64);y+=10;
          label(SiteText,ui::tr(L"Site controls are unavailable while a protected sign-in is in progress."),font,16,width-32);}
        else{
          label(SiteTitle,ui::wide(str(site,"host")),site_title,16,width-64);label(SiteSubtitle,workspace,site_small,16,width-64);y+=8;
          row(SiteConnection,connection_icon,headline,{},true,tint);
          row(SiteCookies,ui::Icon::cookie,ui::tr(L"Cookies and site data"),cookie_count(site.value("cookieCount",0)),true);
          // As in Chromium, the bubble lists only settings changed from their default.
          const auto settings=site.value("settings",Json::object());bool changed=false;
          for(size_t n=0;n<std::size(site::settings);++n){const auto& item=site::settings[n];const auto value=str(settings,item.name);if(value!="allow"&&value!="block")continue;
            if(!changed){y+=8;label(SitePermissions,ui::tr(L"Permissions"),site_small,16,width-32);y+=2;changed=true;}
            put(site_control(SiteSettingLabel+static_cast<int>(n),L"STATIC",ui::tr(item.label),SS_LEFT|SS_NOPREFIX|SS_CENTERIMAGE,font),16,y,width-200,30);
            auto combo=site_control(SiteSetting+static_cast<int>(n),L"COMBOBOX",{},CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,font);
            for(const char* choice:{"default","allow","block"})SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(ui::tr(site::value_label(item.name,choice))));
            SendMessageW(combo,CB_SETCURSEL,value=="allow"?1:2,0);put(combo,width-176,y+2,160,200);y+=34;}
          if(changed){put(site_control(SiteReset,L"BUTTON",ui::tr(L"Reset permissions"),BS_OWNERDRAW|WS_TABSTOP,font),width-176,y+2,160,30);y+=38;}
          y+=4;row(SiteSettings,ui::Icon::settings,ui::tr(L"Site settings"),{},true);
        }
      }
      const int height=y+10;RECT anchor{};GetWindowRect(controls.at(SiteInfo),&anchor);RECT box=omnibox;MapWindowPoints(window,nullptr,reinterpret_cast<POINT*>(&box),2);
      MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
      const int x=std::clamp<int>(anchor.left-d(4),monitor.rcWork.left,std::max<int>(monitor.rcWork.left,monitor.rcWork.right-d(width))),top=std::clamp<int>(box.bottom+d(4),monitor.rcWork.top,std::max<int>(monitor.rcWork.top,monitor.rcWork.bottom-d(height)));
      for(const auto& [control,cx,cy,cw,ch]:placed)SetWindowPos(control,nullptr,d(cx),d(cy),d(cw),d(ch),SWP_NOZORDER|SWP_NOACTIVATE);
      SetWindowPos(site_bubble,HWND_TOP,x,top,d(width),d(height),SWP_SHOWWINDOW);InvalidateRect(site_bubble,nullptr,TRUE);if(first)SetFocus(first);
    }
    std::string workspace_id() const {auto found=shell.hosts.find(site_tab);return found==shell.hosts.end()?workspace():found->second.workspace;}
    void draw_site_row(const DRAWITEMSTRUCT& item){const auto found=site_rows.find(static_cast<int>(item.CtlID));if(found==site_rows.end())return;const auto row=found->second;
      ui::buffered(item,[&](const DRAWITEMSTRUCT& copy){const auto colors=ui::palette();auto rect=copy.rcItem;ui::fill(copy.hDC,rect,colors.canvas);
        const double hover=ui::hover_amount(copy);const bool pressed=(copy.itemState&ODS_SELECTED)!=0;
        if(hover>0||pressed){const auto fill=pressed?ui::selection():ui::mix(colors.canvas,ui::hover_background(),hover);ui::rounded(copy.hDC,rect,fill,fill,d(6));}
        const bool inverse=colors.contrast&&(hover>=0.5||pressed);const auto ink=inverse?ui::selection_ink():colors.ink,muted=inverse?ui::selection_ink():colors.muted;
        RECT glyph{rect.left+d(4),rect.top,rect.left+d(32),rect.bottom};ui::icon(copy.hDC,glyph,row.icon,row.tint&&!inverse?row.tint:ink,copy.hwndItem);
        RECT text{rect.left+d(40),rect.top,rect.right-d(row.chevron?30:10),rect.bottom};
        if(!row.detail.empty()){const auto old=SelectObject(copy.hDC,font);SIZE size{};GetTextExtentPoint32W(copy.hDC,row.detail.c_str(),static_cast<int>(row.detail.size()),&size);SelectObject(copy.hDC,old);
          RECT detail{std::max<LONG>(text.left,text.right-size.cx),rect.top,text.right,rect.bottom};ui::text(copy.hDC,detail,row.detail,font,muted,DT_SINGLELINE|DT_VCENTER|DT_RIGHT|DT_NOPREFIX|DT_END_ELLIPSIS);text.right=detail.left-d(8);}
        wchar_t caption[256]{};GetWindowTextW(copy.hwndItem,caption,256);ui::text(copy.hDC,text,caption,font,ink,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        if(row.chevron){RECT arrow{rect.right-d(30),rect.top,rect.right-d(8),rect.bottom};ui::icon(copy.hDC,arrow,ui::Icon::chevron_right,muted,copy.hwndItem);}
        if(copy.itemState&ODS_FOCUS){auto ring=rect;ui::focus_mark(copy.hDC,ring,copy.hwndItem,6);}});}
    // Bubble buttons. Rows that open panels or dialogs close the bubble first.
    void site_command(int id){const auto tab=site_tab,origin=site_origin;auto& owner=shell;const auto target=window;
      if(id==SiteClose){close_site();return;}
      if(id==SiteConnection||id==SiteBack){site_details=id==SiteConnection;build_site();return;}
      if(id==SiteCertificate){const auto der=str(site.value("certificate",Json::object()),"der");close_site();show_certificate(der);return;}
      if(id==SiteCookies||id==SiteSettings){close_site();shell.panel(*this,id==SiteCookies?"cookies":"site",{},{},tab);return;}
      if(id==SiteReset)shell.engine.reset_site_settings(tab,origin,[&owner,target,tab](Json value){owner.post([&owner,target,tab,value=std::move(value)]{auto frame=owner.frame_of(target);if(!frame)return;
        if(!value.value("ok",false)){frame->set_status(ui::error_text(str(value.value("error",Json::object()),"message")));return;}
        frame->set_status(ui::tr(L"Site settings reset."));if(frame->site_bubble&&frame->site_tab==tab)frame->open_site();});});
    }
    void change_site_setting(size_t index,HWND combo){const auto choice=SendMessageW(combo,CB_GETCURSEL,0,0);if(choice<0||choice>2||index>=std::size(site::settings))return;
      constexpr const char* values[]{"default","allow","block"};auto& owner=shell;const auto target=window;
      shell.engine.set_site_setting(site_tab,site_origin,site::settings[index].name,values[choice],[&owner,target](Json value){if(value.value("ok",false))return;
        owner.post([&owner,target,value=std::move(value)]{if(auto frame=owner.frame_of(target)){frame->set_status(ui::error_text(str(value.value("error",Json::object()),"message")));frame->close_site();}});});}
    // The site's certificate in the Windows certificate viewer, owned by this window.
    void show_certificate(const std::string& encoded){std::string bytes;if(!site::unhex(encoded,bytes)||bytes.empty())return;
      const auto context=CertCreateCertificateContext(X509_ASN_ENCODING|PKCS_7_ASN_ENCODING,reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()));
      if(!context){set_status(ui::tr(L"The certificate could not be shown."));return;}
      CryptUIDlgViewContext(CERT_STORE_CERTIFICATE_CONTEXT,context,window,ui::tr(L"Certificate"),0,nullptr);CertFreeCertificateContext(context);}
    void set_status(std::wstring text){status=std::move(text);status_until=GetTickCount64()+6000;layout();}
    void finish_sidebar(bool save){if(!resizing_sidebar)return;resizing_sidebar=false;RemovePropW(window,L"XenonSidebarDrag");if(GetCapture()==window)ReleaseCapture();if(save)shell.save_sidebar(*this);}
    void resize_sidebar(int width){RECT bounds{};GetClientRect(window,&bounds);const auto maximum=std::max(180,std::min(480,MulDiv(bounds.right,96,GetDpiForWindow(window))-400));const auto next=std::clamp(width,180,maximum);
      if(ui::sidebar_width!=next){ui::sidebar_width=next;shell.layout_all();}}
    std::string workspace() const {auto found=shell.hosts.find(selected);return found==shell.hosts.end()?"native-default":found->second.workspace;}
    bool is_private() const {auto found=shell.hosts.find(selected);return found!=shell.hosts.end()&&found->second.metadata.value("private",false);}
    bool has_hosts() const {return std::any_of(shell.hosts.begin(),shell.hosts.end(),[&](const auto& entry){return entry.second.frame==this;});}
    bool empty_workspace(const std::string& id) const {return std::none_of(shell.hosts.begin(),shell.hosts.end(),[&](const auto& entry){return entry.second.frame==this&&entry.second.workspace==id;});}
    // Opens a human-owned tab in this window through the native broker API.
    void open_tab(const std::string& workspace,const std::string& url,const std::string& before={},bool select=true){
      shell.placements.push_back({workspace,window,before,select,GetTickCount64()});
      shell.broker.open_human_tab(workspace,url,shell.reply(window,{}));
    }
    void open_workspace(const std::string& id){if(!shell.workspace_names.contains(id)||!empty_workspace(id))return;open_tab(id,"about:blank");}
    RECT close_rect(HTREEITEM item) const {RECT rect{};if(!TreeView_GetItemRect(tree,item,&rect,FALSE))return {};RECT bounds{};GetClientRect(tree,&bounds);rect.left=bounds.right-d(32);rect.right=bounds.right-d(8);rect.top+=d(5);rect.bottom-=d(5);return rect;}
    // Workspace rows keep "+" (new tab, slot 0) in the outer slot at all times;
    // hovering adds "…" (workspace menu, slot 1) beside it. Neither moves.
    enum GroupButton {GroupPlus=0,GroupMore=1};
    RECT group_button(HTREEITEM item,int index) const {auto rect=close_rect(item);if(index==GroupMore)OffsetRect(&rect,-d(26),0);return rect;}
    std::pair<std::string,bool> key_of(HTREEITEM item) const {TVITEMW value{};value.hItem=item;value.mask=TVIF_PARAM;if(!item||!TreeView_GetItem(tree,&value)||value.lParam<=0||static_cast<size_t>(value.lParam)>tree_keys.size())return {};return tree_keys[value.lParam-1];}
    HTREEITEM item_at(POINT point) const {TVHITTESTINFO hit{};hit.pt=point;return TreeView_HitTest(tree,&hit);}
    std::string close_at(POINT point) const {const auto item=item_at(point);const auto [id,group]=key_of(item);if(id.empty()||group)return {};const auto host=shell.hosts.find(id);if(host!=shell.hosts.end()&&host->second.pinned)return {};auto rect=close_rect(item);return PtInRect(&rect,point)?id:std::string{};}
    // Only buttons that are drawn can be hit: "…" exists while its row is hovered.
    int group_button_at(POINT point,std::string& group) const {const auto item=item_at(point);const auto [id,is_group]=key_of(item);if(id.empty()||!is_group)return -1;
      for(int index:{GroupPlus,GroupMore}){if(index==GroupMore&&item!=hovered_row)continue;auto rect=group_button(item,index);if(PtInRect(&rect,point)){group=id;return index;}}return -1;}
    void choose(const std::string& id,bool focus){auto found=shell.hosts.find(id);if(found==shell.hosts.end()||found->second.frame!=this)return;
      if(selected!=id){if(page_screen&&screen_tab!=id)leave_page_fullscreen();clear_developer(ui::tr(L"Waiting for rendered page evidence…"));RECT from{};const bool visible=pill_rect(from);glide_from=from;glide_start=visible&&ui::motion()?GetTickCount64():0;address_dirty=false;hide_suggestions();matches.clear();close_site();set_address(str(found->second.metadata,"url"));
        // Keyboard tab switching can choose a row outside the visible sidebar.
        // A scrolled sidebar has no meaningful glide origin.
        if(auto item=item_of(id)){const auto top=TreeView_GetFirstVisible(tree);TreeView_EnsureVisible(tree,item);if(TreeView_GetFirstVisible(tree)!=top)glide_start=0;}}selected=id;
      status.clear();track_accent();
      SetWindowPos(found->second.window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);if(find_visible)SetWindowPos(find_bar,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
      // Only the foreground window's tab is Chromium's focus target.
      if(shell.active==this||GetAncestor(GetForegroundWindow(),GA_ROOT)==window)shell.engine.select_native_tab(id);
      if(focus&&found->second.browser)shell.engine.native_command(id,"focus");sync_toolbar();refresh_page_state();InvalidateRect(window,nullptr,FALSE);InvalidateRect(tree,nullptr,FALSE);update_cursor();
    }
    // After the selected tab leaves this window, select its right neighbor.
    void select_neighbor(const std::string& gone){
      std::string next,previous;bool passed=false;
      for(const auto& [id,group]:tree_keys){if(group)continue;if(id==gone){passed=true;continue;}auto host=shell.hosts.find(id);if(host==shell.hosts.end()||host->second.frame!=this)continue;if(passed&&next.empty())next=id;if(!passed)previous=id;}
      if(next.empty())next=previous;
      if(next.empty())for(const auto& id:shell.order)if(auto host=shell.hosts.find(id);host!=shell.hosts.end()&&host->second.frame==this&&id!=gone)next=id;
      selected.clear();if(!next.empty())choose(next,false);
    }
    std::vector<std::string> visual_tabs() const {std::vector<std::string> result;for(const auto& [id,group]:tree_keys)if(!group&&shell.hosts.contains(id))result.push_back(id);return result;}
    void cycle(int step){const auto tabs=visual_tabs();if(tabs.empty())return;auto at=std::find(tabs.begin(),tabs.end(),selected);const int index=at==tabs.end()?0:static_cast<int>(at-tabs.begin());const int count=static_cast<int>(tabs.size());choose(tabs[static_cast<size_t>(((index+step)%count+count)%count)],true);}
    void select_number(int number){const auto tabs=visual_tabs();if(tabs.empty())return;choose(number>=9?tabs.back():tabs[static_cast<size_t>(std::min<int>(number-1,static_cast<int>(tabs.size())-1))],true);}
    bool foreground() const {return window&&!IsIconic(window)&&GetAncestor(GetForegroundWindow(),GA_ROOT)==window;}
    bool waiting_for_help(const std::string& id) const {auto found=shell.states.find(id);return found!=shell.states.end()&&AttentionNotices::pending(found->second);}
    bool group_needs_help(const std::string& workspace_id) const {
      return std::any_of(shell.hosts.begin(),shell.hosts.end(),[&](const auto& entry){return entry.second.frame==this&&entry.second.workspace==workspace_id&&waiting_for_help(entry.first);});}
    // Repaints only the rows that pulse, about 30 times a second.
    void pulse_help(){if(!ui::motion()||GetTickCount64()-help_painted<33)return;help_painted=GetTickCount64();RECT rect{};
      for(auto group=TreeView_GetRoot(tree);group;group=TreeView_GetNextSibling(tree,group)){
        if(!(TreeView_GetItemState(tree,group,TVIS_EXPANDED)&TVIS_EXPANDED)){const auto [workspace_id,is_group]=key_of(group);
          if(is_group&&group_needs_help(workspace_id)&&TreeView_GetItemRect(tree,group,&rect,FALSE))InvalidateRect(tree,&rect,FALSE);continue;}
        for(auto item=TreeView_GetChild(tree,group);item;item=TreeView_GetNextSibling(tree,item)){const auto [id,is_group]=key_of(item);
          if(!is_group&&waiting_for_help(id)&&TreeView_GetItemRect(tree,item,&rect,FALSE))InvalidateRect(tree,&rect,FALSE);}}
    }
    COLORREF color(const std::string& id) const {const auto colors=ui::palette();auto found=shell.states.find(id);if(found==shell.states.end()||!found->second.value("agentAvailable",false))return colors.gray;return found->second.value("humanPaused",false)?colors.orange:colors.teal;}
    std::wstring state_text(const std::string& id) const {auto found=shell.states.find(id);if(found==shell.states.end())return ui::tr(L"Opening");const auto& state=found->second;
      auto owner=str(state,"ownerSessionId");std::wstring value=owner=="human"?ui::tr(L"You"):owner.empty()?ui::tr(L"No agent"):ui::wide(shell.names.contains(owner)?shell.names.at(owner):ui::tr8("Agent"));
      if(state.value("agentAvailable",false)&&state.value("humanPaused",false))value+=ui::tr(L" · Paused for you");else if(state.value("handoffPending",false))value+=ui::tr(L" · Handing off");
      else if(owner!="human"&&!owner.empty())value+=state.value("agentAvailable",false)?ui::tr(L" · Agent control"):ui::tr(L" · Unavailable");
      if(state.value("attentionRequested",false))value+=ui::tr(L" · Needs your help");
      if(state.value("protected",false))value+=ui::tr(L" · Protected authentication");return value;
    }
    void retitle(int id,const wchar_t* title){if(ui::text(controls.at(id))==ui::utf8(title))return;SetWindowTextW(controls.at(id),title);TOOLINFOW tip{sizeof(tip)};tip.hwnd=window;tip.uId=reinterpret_cast<UINT_PTR>(controls.at(id));tip.lpszText=const_cast<LPWSTR>(title);SendMessageW(tooltips,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&tip));}
    void sync_toolbar(){auto found=shell.hosts.find(selected);const auto metadata=found==shell.hosts.end()?Json::object():found->second.metadata;
      if(GetFocus()!=address&&!address_dirty)set_address(str(metadata,"url"));
      const auto enable=[&](int id,bool enabled){if((IsWindowEnabled(controls.at(id))!=FALSE)!=enabled)EnableWindow(controls.at(id),enabled);};
      enable(Back,metadata.value("canGoBack",false));enable(Forward,metadata.value("canGoForward",false));enable(Reload,found!=shell.hosts.end());
      enable(BookmarkStar,found!=shell.hosts.end()&&!metadata.value("protected",false)&&web_address(str(metadata,"url")));
      retitle(Reload,metadata.value("loading",false)?ui::tr(L"Stop"):ui::tr(L"Reload"));
      retitle(BookmarkStar,bookmarked?ui::tr(L"Remove bookmark"):ui::tr(L"Bookmark this tab"));
      if(const auto state=found==shell.hosts.end()?std::string{}:str(metadata,"security");state!=security){security=state;layout();InvalidateRect(controls.at(SiteInfo),nullptr,FALSE);}
      // The bubble describes one website and closes once the tab leaves it.
      if(site_bubble&&(selected!=site_tab||site::origin(str(metadata,"url"))!=site_origin))close_site();
      auto title=found==shell.hosts.end()?ui::tr(L"Xenon Browser"):short_title(ui::tab_title(str(metadata,"title"),str(metadata,"url")),100)+L" — Xenon";if(ui::text(window)!=ui::utf8(title))SetWindowTextW(window,title.c_str());
      // An available update marks Controls with a blue dot until it is installed.
      if(auto version=shell.native.available_update();version!=update_version){update_version=std::move(version);InvalidateRect(controls.at(Controls),nullptr,FALSE);}
      auto caption=ui::tr(L"Controls")+(shell.pairing_count?L" ("+std::to_wstring(shell.pairing_count)+L")":std::wstring{});
      if(!update_version.empty())caption+=L" · "+ui::format(ui::tr(L"Xenon {0} is available"),{ui::wide(update_version)});
      if(ui::text(controls.at(Controls))!=ui::utf8(caption))retitle(Controls,caption.c_str());
      if(fullscreen()){
        // Page full screen ends when another tab is chosen or the person no
        // longer controls the page; the outline follows agent availability.
        if(page_screen&&(screen_tab!=selected||!shell.states.contains(selected)||str(shell.states.at(selected),"ownerSessionId")!="human"))leave_page_fullscreen();
        const bool outline=color(selected)!=ui::palette().gray;if(outline!=screen_outline){screen_outline=outline;layout();}
        if(notice_until&&GetTickCount64()>=notice_until)hide_notice();
      }
      else if(current_chip()!=chip_text)layout();
      if(str(metadata,"url")!=checked_url)refresh_page_state();
    }
    // Saved-account key and bookmark star for the selected page. Metadata only.
    void refresh_page_state(){auto found=shell.hosts.find(selected);const auto url=found==shell.hosts.end()?std::string{}:str(found->second.metadata,"url");checked_url=url;
      const auto accounts=found==shell.hosts.end()?size_t{}:shell.engine.saved_account_count(selected);bool marked=false;
      if(found!=shell.hosts.end()&&web_address(url))try{for(const auto& row:shell.data.list(found->second.workspace,is_private()).value("bookmarks",Json::array()))if(str(row,"url")==url){marked=true;break;}}catch(...){}
      if(accounts!=saved_accounts||marked!=bookmarked){saved_accounts=accounts;bookmarked=marked;layout();InvalidateRect(controls.at(BookmarkStar),nullptr,FALSE);}
    }
    ui::Icon button_icon(int id) const {switch(id){case Back:return ui::Icon::back;case Forward:return ui::Icon::forward;case Reload:return ui::text(controls.at(Reload))==ui::tr8("Stop")?ui::Icon::stop:ui::Icon::reload;case Go:return ui::Icon::go;
      case Controls:return ui::Icon::controls;case Menu:return ui::Icon::menu;case NewTab:return ui::Icon::plus;case Extensions:return ui::Icon::extensions;case Passwords:return ui::Icon::key;case BookmarkStar:return bookmarked?ui::Icon::star_filled:ui::Icon::star;
      case FindPrevious:return ui::Icon::up;case FindNext:return ui::Icon::down;case FindClose:return ui::Icon::close;default:return ui::Icon::none;}}
    HTREEITEM item_of(const std::string& id) const {
      for(auto group=TreeView_GetRoot(tree);group;group=TreeView_GetNextSibling(tree,group))for(auto item=TreeView_GetChild(tree,group);item;item=TreeView_GetNextSibling(tree,item)){
        const auto [key,is_group]=key_of(item);if(!is_group&&key==id)return item;}
      return nullptr;
    }
    // The selected tab's pill in tree coordinates. After a selection change it
    // glides from the previous row; the page and its host window never move.
    bool pill_rect(RECT& rect) const {const auto item=item_of(selected);RECT row{},bounds{};if(!item||!TreeView_GetItemRect(tree,item,&row,FALSE))return false;
      GetClientRect(tree,&bounds);if(row.top<0||row.bottom>bounds.bottom)return false;
      rect={d(4),row.top+d(3),bounds.right+d(16),row.bottom-d(3)};
      if(const auto t=elapsed(glide_start,GlideDuration);t<1){const auto height=rect.bottom-rect.top;rect.top=glide_from.top+static_cast<LONG>(std::lround((rect.top-glide_from.top)*ui::ease(t)));rect.bottom=rect.top+height;}
      return true;
    }
    bool gliding() const {return elapsed(glide_start,GlideDuration)<1;}
    // The ownership color crossfades when control changes or another tab is chosen.
    COLORREF accent() const {return accent_ready?ui::mix(accent_from,accent_to,ui::ease(elapsed(accent_start,AccentDuration))):color(selected);}
    void track_accent(){const auto target=color(selected);if(!accent_ready){accent_from=accent_to=target;accent_ready=true;return;}
      if(target==accent_to)return;accent_from=accent();accent_to=target;accent_start=ui::motion()?GetTickCount64():0;}
    double row_hover(HTREEITEM item) const {auto found=row_fades.find(item);return found!=row_fades.end()?found->second.value(ui::hover_duration):item==hovered_row?1.0:0.0;}
    bool loading() const {auto found=shell.hosts.find(selected);return found!=shell.hosts.end()&&found->second.metadata.value("loading",false);}
    // The straight part of the page frame's top edge doubles as the loading bar.
    RECT loading_strip() const {return {panel_bounds.left+d(FrameRadius),panel_bounds.top,panel_bounds.right-d(FrameRadius),panel_bounds.top+d(2)};}
    void paint_loading(HDC dc,COLORREF tint) const {if(!loading())return;const auto strip=loading_strip();const LONG width=strip.right-strip.left;if(width<=0)return;
      if(!ui::motion()){ui::fill(dc,strip,ui::mix(tint,ui::palette().ink,0.35));return;}
      ui::fill(dc,strip,ui::mix(ui::palette().surface,tint,0.2));const double phase=static_cast<double>(GetTickCount64()%LoadingCycle)/LoadingCycle;
      const auto head=ui::ease(phase/0.8),tail=ui::ease((phase-0.2)/0.8);ui::fill(dc,{strip.left+static_cast<LONG>(width*tail),strip.top,strip.left+static_cast<LONG>(width*head),strip.bottom},tint);
    }
    void animate(){track_accent();
      std::erase_if(row_fades,[](const auto& entry){return !entry.second.on&&!entry.second.active(ui::hover_duration);});
      const bool rows=std::any_of(row_fades.begin(),row_fades.end(),[](const auto& entry){return entry.second.active(ui::hover_duration);});
      const auto repaint=ui::shell_repaint(rows,gliding(),elapsed(accent_start,AccentDuration)<1,address_focus.active(FocusDuration));
      const bool tree_now=repaint.tree,window_now=repaint.window,loading_now=loading();
      // Repaint once more after motion stops so the settled frame is exact.
      if(tree_now||tree_moving)InvalidateRect(tree,nullptr,FALSE);
      if(window_now||window_moving)InvalidateRect(window,nullptr,FALSE);else if(loading_now||was_loading){auto strip=loading_strip();InvalidateRect(window,&strip,FALSE);}
      tree_moving=tree_now;window_moving=window_now;was_loading=loading_now;pulse_help();
      if(!status.empty()&&GetTickCount64()>=status_until){status.clear();layout();}
    }
    void draw_tree(NMTVCUSTOMDRAW& draw){const auto index=draw.nmcd.lItemlParam;if(index<=0||static_cast<size_t>(index)>tree_keys.size())return;const auto& [id,group]=tree_keys[index-1];auto colors=ui::palette();const auto dc=draw.nmcd.hdc;const auto item=reinterpret_cast<HTREEITEM>(draw.nmcd.dwItemSpec);
      RECT bounds{};GetClientRect(tree,&bounds);auto rect=draw.nmcd.rc;rect.left=0;rect.right=bounds.right;const auto row=rect;ui::fill(dc,row,colors.canvas);
      RECT label{};TreeView_GetItemRect(tree,item,&label,TRUE);label.right=bounds.right-d(12);
      const double hover=row_hover(item);const bool emphasized=hover>=0.5&&colors.contrast;const auto hover_fill=ui::mix(colors.canvas,ui::hover_background(),hover);
      POINT pointer{};GetCursorPos(&pointer);ScreenToClient(tree,&pointer);
      // Inline actions need their own contrast above the row's settled hover
      // fill; using the same fill makes the individual hit target disappear.
      const auto action_hover=[&](RECT target){const auto fill=colors.contrast?colors.surface:ui::mix(ui::hover_background(),colors.ink,0.14);
        ui::rounded(dc,target,fill,fill,d(5),d(1));};
      // While the selection glides, each row paints its own slice of the moving pill.
      const auto glide=[&]{RECT overlap{};if(!gliding()||!pill_valid||!IntersectRect(&overlap,&pill,&row))return;const int saved=SaveDC(dc);IntersectClipRect(dc,row.left,row.top,row.right,row.bottom);
        ui::rounded(dc,pill,colors.surface,accent(),d(8),d(2));RestoreDC(dc,saved);};
      const auto& drag=shell.drag;const bool drop_here=drag.active&&drag.target==window;
      if(group){auto background=rect;InflateRect(&background,-d(2),-d(3));
        TVITEMW value{};value.hItem=item;value.mask=TVIF_STATE;value.stateMask=TVIS_EXPANDED;TreeView_GetItem(tree,&value);const bool expanded=(value.state&TVIS_EXPANDED)!=0;
        if(drop_here&&drag.group==item)ui::rounded(dc,background,ui::hover_background(),drag.blocked?colors.gray:colors.teal,d(6),d(2));
        // A collapsed workspace pulses for a hidden tab that waits for help.
        else if(!expanded&&group_needs_help(id)){const double pulse=help_pulse();const auto base=hover>0?hover_fill:colors.canvas;
          ui::rounded(dc,background,ui::mix(base,colors.orange,0.16*pulse),ui::mix(base,colors.orange,0.2+0.8*pulse),d(6),d(2));}
        else if(hover>0)ui::rounded(dc,background,hover_fill,hover_fill,d(6));glide();
        RECT arrow=label;arrow.left=d(2);arrow.right=d(22);ui::icon(dc,arrow,expanded?ui::Icon::down:ui::Icon::chevron_right,emphasized?ui::selection_ink():colors.muted,window);
        const bool hovered=item==hovered_row;std::vector<std::pair<int,ui::Icon>> buttons{{GroupPlus,ui::Icon::plus}};
        if(hovered)buttons.push_back({GroupMore,ui::Icon::more});
        for(const auto& [button,glyph]:buttons){auto target=group_button(item,button);const bool hot=hovered&&PtInRect(&target,pointer);if(hot)action_hover(target);
          ui::icon(dc,target,glyph,hot?colors.ink:emphasized?ui::selection_ink():colors.muted,window);label.right=std::min(label.right,target.left-d(4));}
        label.left=d(24);ui::text(dc,label,ui::wide(shell.workspace_names.contains(id)?shell.workspace_names.at(id):ui::tr8("Workspace")),bold,emphasized?ui::selection_ink():colors.muted);return;}
      const bool active=id==selected,settled=active&&!gliding();rect.left=d(4);InflateRect(&rect,0,-d(3));rect.right-=active?0:d(6);auto shape=rect;if(active)shape.right+=d(16);
      const auto state=shell.states.find(id);const bool agent=!active&&state!=shell.states.end()&&state->second.value("agentAvailable",false)&&!state->second.value("humanPaused",false);
      const auto background=settled?colors.surface:hover_fill;
      // A tab whose agent asked for help pulses orange until the person has
      // been present with it in front, then keeps a quiet orange outline.
      const bool help=state!=shell.states.end()&&state->second.value("attentionRequested",false),waiting=help&&AttentionNotices::pending(state->second);
      const double pulse=waiting?help_pulse():0.0;
      const auto edge=waiting?ui::mix(background,colors.orange,0.2+0.8*pulse):settled?accent():help?colors.orange:agent?color(id):background;
      ui::rounded(dc,shape,waiting?ui::mix(background,colors.orange,0.16*pulse):background,edge,d(8),d(settled||waiting?2:1));glide();
      const auto found=shell.hosts.find(id);const bool pinned=found!=shell.hosts.end()&&found->second.pinned,muted=found!=shell.hosts.end()&&found->second.metadata.value("muted",false);
      // Close stays visible on every unpinned tab, quieter until hovered or selected.
      auto close=close_rect(item);
      if(!pinned){const bool hot=item==hovered_row&&PtInRect(&close,pointer);if(hot)action_hover(close);
        const double reveal=colors.contrast||active?1.0:0.45+0.55*hover;
        ui::icon(dc,close,ui::Icon::close,hot?colors.ink:emphasized&&!active?ui::selection_ink():ui::mix(active?colors.surface:background,colors.muted,reveal),window);}
      else close.left=close.right;
      label.left=d(14);label.right=close.left-d(3);
      if(pinned){RECT glyph{label.left-d(2),rect.top,label.left+d(16),rect.bottom};ui::icon(dc,glyph,ui::Icon::pin,colors.muted,window);label.left+=d(20);}
      {const int middle=(rect.top+rect.bottom)/2;const RECT glyph{label.left,middle-d(8),label.left+d(16),middle+d(8)};
        if(found==shell.hosts.end()||!draw_site_icon(dc,glyph,found->second.icon))ui::icon(dc,{glyph.left-d(2),rect.top,glyph.right+d(2),rect.bottom},ui::Icon::globe,colors.muted,window);label.left+=d(24);}
      // Screen sharing, or a camera or microphone in use, shows in red, as in Chromium.
      const bool capturing=found!=shell.hosts.end()&&found->second.metadata.value("screenCapture",false),recording=found!=shell.hosts.end()&&(found->second.metadata.value("mediaVideo",false)||found->second.metadata.value("mediaAudio",false));
      if(capturing||recording){RECT glyph{label.right-d(20),rect.top,label.right,rect.bottom};if(capturing)ui::icon(dc,glyph,ui::Icon::screen,ui::danger_red(),window);else ui::dot(dc,{(glyph.left+glyph.right)/2,(glyph.top+glyph.bottom)/2},d(4),ui::danger_red());label.right-=d(22);}
      if(muted){RECT glyph{label.right-d(20),rect.top,label.right,rect.bottom};ui::icon(dc,glyph,ui::Icon::muted,colors.muted,window);label.right-=d(22);}
      const auto title=found==shell.hosts.end()?ui::tr8("New tab"):ui::tab_title(str(found->second.metadata,"title"),str(found->second.metadata,"url"));ui::text(dc,label,ui::wide(title),font,emphasized&&!active?ui::selection_ink():colors.ink);
      if(drop_here&&drag.line==item){const int y=drag.below?row.bottom-d(1):row.top+d(1);ui::fill(dc,{d(8),y-d(1),bounds.right-d(8),y+d(1)},drag.blocked?colors.gray:colors.teal);}
      if((draw.nmcd.uItemState&CDIS_FOCUS)&&GetFocus()==tree&&!gliding()){auto focus=rect;InflateRect(&focus,-d(3),-d(3));ui::focus_mark(dc,focus,tree,5);}
    }
    void paint(HDC dc){RECT rect{};GetClientRect(window,&rect);const auto colors=ui::palette();const auto tint=accent();
      if(fullscreen()){ui::fill(dc,rect,RGB(0,0,0));if(color(selected)!=colors.gray)ui::outline(dc,rect,tint,0,d(2));return;}
      ui::fill(dc,rect,colors.canvas);
      ui::rounded(dc,omnibox,colors.surface,colors.surface,d(16));
      if(const auto focus=address_focus.value(FocusDuration);focus>0)ui::outline(dc,omnibox,ui::mix(colors.surface,tint,focus),d(16),d(2));
      // Ownership reads as a dot in the frame's color, then the owner or status.
      if(chip.right>chip.left){auto text_rect=chip;const int radius=d(4);ui::dot(dc,{chip.left+d(10),(chip.top+chip.bottom)/2},radius,tint);text_rect.left+=d(20);text_rect.right-=d(4);ui::text(dc,text_rect,chip_text,font,colors.muted);}
      const int thickness=d(2),radius=d(FrameRadius);
      ui::rounded(dc,panel_bounds,colors.surface,tint,radius,thickness);
      paint_loading(dc,tint);
      RECT row{};if(pill_rect(row)){MapWindowPoints(tree,window,reinterpret_cast<POINT*>(&row),2);RECT rail{};GetWindowRect(tree,&rail);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rail),2);RECT bridge{rail.right,row.top,panel_bounds.left+d(2),row.bottom};
        if(bridge.top>panel_bounds.top+radius){ui::fill(dc,bridge,colors.surface);
          ui::fill(dc,{bridge.left,bridge.top,bridge.right,bridge.top+thickness},tint);ui::fill(dc,{bridge.left,bridge.bottom-thickness,bridge.right,bridge.bottom},tint);
          for(const bool top:{true,false})ui::tab_junction(dc,panel_bounds.left,top?bridge.top:bridge.bottom,top,thickness,d(8),colors.canvas,colors.surface,tint);}}
      if(sidebar_hover||resizing_sidebar){auto grip=sidebar_grip();ui::rounded(dc,grip,colors.muted,colors.muted,d(1));}
      if(developer_visible){const int middle=(panel_bounds.top+panel_bounds.bottom)/2;RECT grip{panel_bounds.right+d(3),middle-d(16),panel_bounds.right+d(5),middle+d(16)};ui::fill(dc,grip,colors.muted);}
    }
    void draw_new_tab(const DRAWITEMSTRUCT& item){ui::buffered(item,[&](const DRAWITEMSTRUCT& copy){const auto colors=ui::palette();auto rect=copy.rcItem;ui::fill(copy.hDC,rect,colors.canvas);InflateRect(&rect,-1,-1);
      const double hover=ui::hover_amount(copy);ui::rounded(copy.hDC,rect,ui::mix(colors.canvas,ui::hover_background(),hover),colors.contrast?colors.border:ui::mix(colors.canvas,ui::hover_background(),hover),d(8));
      RECT glyph{rect.left+d(4),rect.top,rect.left+d(28),rect.bottom};ui::icon(copy.hDC,glyph,ui::Icon::plus,colors.ink,copy.hwndItem);
      RECT caption{rect.left+d(32),rect.top,rect.right-d(8),rect.bottom};ui::text(copy.hDC,caption,ui::tr(L"New tab"),font,colors.ink);
      if(copy.itemState&ODS_FOCUS){auto ring=rect;ui::focus_mark(copy.hDC,ring,copy.hwndItem,8);}});}
    HTREEITEM item_for(const std::pair<std::string,bool>& key) const {
      if(!key.second)return item_of(key.first);
      for(auto group=TreeView_GetRoot(tree);group;group=TreeView_GetNextSibling(tree,group))if(key_of(group)==key)return group;
      return nullptr;
    }
    static std::wstring row_label(const Host& host){return short_title(ui::tab_title(str(host.metadata,"title"),str(host.metadata,"url")),37);}
    // Loading pages change titles often. Relabel rows in place rather than
    // rebuilding, so the sidebar neither flashes nor loses its scroll position.
    void relabel_tree(){
      for(auto group=TreeView_GetRoot(tree);group;group=TreeView_GetNextSibling(tree,group))for(auto item=TreeView_GetChild(tree,group);item;item=TreeView_GetNextSibling(tree,item)){
        const auto [id,is_group]=key_of(item);const auto found=shell.hosts.find(id);if(is_group||found==shell.hosts.end())continue;
        auto label=row_label(found->second);TVITEMW value{};value.hItem=item;value.mask=TVIF_TEXT;value.pszText=label.data();TreeView_SetItem(tree,&value);}
      InvalidateRect(tree,nullptr,FALSE);
    }
    void rebuild_tree(){Json signature=Json::array(),titles=Json::array();for(const auto& [id,name]:shell.workspace_names)signature.push_back({id,name});
      for(const auto& id:shell.order)if(auto found=shell.hosts.find(id);found!=shell.hosts.end()&&found->second.frame==this){signature.push_back({id,found->second.workspace,found->second.pinned});titles.push_back(ui::utf8(row_label(found->second)));}
      if(signature==tree_signature){if(titles!=tree_titles){tree_titles=std::move(titles);relabel_tree();}return;}
      tree_signature=signature;tree_titles=std::move(titles);std::set<std::string> expanded;
      // Structural rebuilds keep the row that was at the top of the sidebar.
      const auto anchor=key_of(TreeView_GetFirstVisible(tree));
      for(auto item=TreeView_GetRoot(tree);item;item=TreeView_GetNextSibling(tree,item)){TVITEMW entry{};entry.hItem=item;entry.mask=TVIF_PARAM|TVIF_STATE;entry.stateMask=TVIS_EXPANDED;TreeView_GetItem(tree,&entry);
        if(entry.lParam>0&&static_cast<size_t>(entry.lParam)<=tree_keys.size()&&((entry.state&TVIS_EXPANDED)||!TreeView_GetChild(tree,item)))expanded.insert(tree_keys[entry.lParam-1].first);}
      std::set<std::string> known,previous_tabs;for(const auto& [id,group]:tree_keys)(group?known:previous_tabs).insert(id);
      // A newly opened, selected tab reveals its workspace even if it was
      // collapsed, and scrolls into view. Other rebuilds keep the scroll position.
      const bool fresh=!selected.empty()&&!previous_tabs.contains(selected)&&shell.hosts.contains(selected);
      if(fresh)expanded.insert(shell.hosts.at(selected).workspace);
      const bool first=tree_keys.empty();rebuilding=true;hovered_row=nullptr;hovered_close.clear();row_fades.clear();SendMessageW(tree,WM_SETREDRAW,FALSE,0);TreeView_DeleteAllItems(tree);tree_keys.clear();std::map<std::string,HTREEITEM> groups;HTREEITEM current=nullptr;
      auto add_group=[&](const std::string& id){if(groups.contains(id))return;auto label=ui::wide(shell.workspace_names.contains(id)?shell.workspace_names.at(id):ui::tr8("Workspace"));tree_keys.emplace_back(id,true);TVINSERTSTRUCTW entry{};entry.hParent=TVI_ROOT;entry.hInsertAfter=TVI_LAST;entry.item.mask=TVIF_TEXT|TVIF_PARAM;entry.item.pszText=label.data();entry.item.lParam=static_cast<LPARAM>(tree_keys.size());groups[id]=TreeView_InsertItem(tree,&entry);};
      if(shell.workspace_names.contains("native-default"))add_group("native-default");for(const auto& [id,name]:shell.workspace_names)add_group(id);
      // Pinned tabs lead their workspace group, then tabs in window order.
      for(const bool pinned:{true,false})for(const auto& id:shell.order)if(auto found=shell.hosts.find(id);found!=shell.hosts.end()&&found->second.frame==this&&found->second.pinned==pinned){
        auto& host=found->second;add_group(host.workspace);
        auto label=row_label(host);tree_keys.emplace_back(id,false);
        TVINSERTSTRUCTW entry{};entry.hParent=groups[host.workspace];entry.hInsertAfter=TVI_LAST;entry.item.mask=TVIF_TEXT|TVIF_PARAM;entry.item.pszText=label.data();entry.item.lParam=static_cast<LPARAM>(tree_keys.size());auto item=TreeView_InsertItem(tree,&entry);if(id==selected)current=item;
      }
      for(const auto& [id,group]:groups)if(first||!known.contains(id)||expanded.contains(id))TreeView_Expand(tree,group,TVE_EXPAND);
      if(current&&(first||expanded.contains(shell.hosts.at(selected).workspace)||!known.contains(shell.hosts.at(selected).workspace)))TreeView_SelectItem(tree,current);SendMessageW(tree,WM_SETREDRAW,TRUE,0);
      if(const auto first_row=anchor.first.empty()?nullptr:item_for(anchor))TreeView_SelectSetFirstVisible(tree,first_row);
      if(fresh&&current){const auto top=TreeView_GetFirstVisible(tree);TreeView_EnsureVisible(tree,current);if(TreeView_GetFirstVisible(tree)!=top)glide_start=0;}
      // Every row and the empty area are painted in full; erasing first would flash.
      InvalidateRect(tree,nullptr,FALSE);rebuilding=false;
    }
    PointerPoint parked(const Host& host) const {RECT rect{};GetClientRect(host.window,&rect);const double scale=GetDpiForWindow(host.window)/96.0*host.metadata.value("zoom",1.0);const auto random=seed();return {rect.right/scale*(.76+(random%100)/1000.0),rect.bottom/scale*(.76+((random/100)%100)/1000.0)};}
    void update_agent_glow(){
      const auto colors=ui::palette();const bool visible=IsWindowVisible(window)&&!IsIconic(window)&&shell.hosts.contains(selected)&&color(selected)!=colors.gray&&!colors.contrast&&ui::vector_rendering()&&(!fullscreen()||screen_outline);
      if(!visible){ShowWindow(agent_glow,SW_HIDE);return;}
      RECT bounds{},tab{};GetClientRect(window,&bounds);
      if(!fullscreen()&&pill_rect(tab)){MapWindowPoints(tree,window,reinterpret_cast<POINT*>(&tab),2);if(tab.top<=panel_bounds.top+d(FrameRadius))tab={};}
      const int dpi=GetDpiForWindow(window);const SIZE size{bounds.right,bounds.bottom};const auto tint=accent();
      if(size.cx!=glow_size.cx||size.cy!=glow_size.cy||dpi!=glow_dpi||tint!=glow_tint||glow_fullscreen!=fullscreen()||!EqualRect(&glow_panel,&panel_bounds)||!EqualRect(&glow_tab,&tab)){
        if(!ui::paint_agent_layer(agent_glow,size,[&](Gdiplus::Graphics& graphics){ui::inward_agent_glow(graphics,panel_bounds,dpi/96.0f,fullscreen()?0.0f:static_cast<float>(d(FrameRadius)),tint,tab.bottom>tab.top?&tab:nullptr);})){ShowWindow(agent_glow,SW_HIDE);return;}
        glow_size=size;glow_dpi=dpi;glow_tint=tint;glow_fullscreen=fullscreen();glow_panel=panel_bounds;glow_tab=tab;
      }
      SetWindowPos(agent_glow,HWND_TOP,0,0,size.cx,size.cy,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }
    void update_cursor(){update_agent_glow();auto found=shell.hosts.find(selected);if(found==shell.hosts.end()||found->second.frame!=this||!agent_pointer_visible(found->second.agent,found->second.paused)||!IsWindowVisible(window)||IsIconic(window)){ShowWindow(cursor,SW_HIDE);return;}auto& host=found->second;
      if(host.animating){const auto t=fraction(host.animation,host.duration),f=t*t*(3-2*t);host.display={host.from.x+(host.to.x-host.from.x)*f,host.from.y+(host.to.y-host.from.y)*f};if(t>=1)host.animating=false;}
      RECT rect{};GetClientRect(host.window,&rect);const int dpi=GetDpiForWindow(window);const double scale=GetDpiForWindow(host.window)/96.0*host.metadata.value("zoom",1.0);POINT point{static_cast<LONG>(std::clamp(host.display.x*scale,0.0,std::max(0.0,rect.right-1.0))),static_cast<LONG>(std::clamp(host.display.y*scale,0.0,std::max(0.0,rect.bottom-1.0)))};ClientToScreen(host.window,&point);
      const auto tint=accent();const bool contrast=ui::palette().contrast;const SIZE size{d(ui::agent_pointer_width),d(ui::agent_pointer_height)};
      if(dpi!=cursor_dpi||tint!=cursor_tint||contrast!=cursor_contrast){
        if(!ui::paint_agent_layer(cursor,size,[&](Gdiplus::Graphics& graphics){ui::agent_pointer(graphics,dpi/96.0f,tint,contrast);})){ShowWindow(cursor,SW_HIDE);return;}
        cursor_dpi=dpi;cursor_tint=tint;cursor_contrast=contrast;
      }
      // Padding belongs to the glow, not the pointer's action coordinates.
      SetWindowPos(cursor,nullptr,point.x-d(ui::agent_pointer_hotspot),point.y-d(ui::agent_pointer_hotspot),size.cx,size.cy,SWP_NOACTIVATE|SWP_NOZORDER|SWP_SHOWWINDOW);
    }
    // Enter opens the selected suggestion while its text is shown; otherwise
    // the typed text. Ctrl+Enter completes a single word as www.<word>.com, as
    // in Chromium, from what was typed rather than an inline completion.
    void submit(){const bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0;auto text=ui::text(address);
      const bool shown=suggesting()&&suggestion>=0&&static_cast<size_t>(suggestion)<matches.size()&&text==matches[static_cast<size_t>(suggestion)].fill;
      if(shown&&!control){navigate(matches[static_cast<size_t>(suggestion)].destination);return;}
      if(shown&&!matches[static_cast<size_t>(suggestion)].completion.empty())text=typed;
      navigate(omnibox::typed_destination(text,control));
    }
    void navigate(std::string value){hide_suggestions();matches.clear();address_dirty=false;if(!shell.hosts.contains(selected)){open_tab(workspace(),value);return;}
      const auto target=window;auto& shell_ref=shell;
      shell.engine.native_command(selected,"navigate",value,[&shell_ref,target](Json result){if(!result.value("ok",false))shell_ref.post([&shell_ref,target]{if(auto frame=shell_ref.frame_of(target))frame->set_status(ui::tr(L"This address could not be opened."));});});
      shell.engine.native_command(selected,"focus");}
    void show_find(){find_visible=true;layout();SetFocus(find_text);SendMessageW(find_text,EM_SETSEL,0,-1);}
    void hide_find(){find_visible=false;shell.engine.native_command(selected,"find-close");layout();shell.engine.native_command(selected,"focus");}
    void focus_address(){SetFocus(address);SendMessageW(address,EM_SETSEL,0,-1);}
    void popup(HMENU menu,int id){RECT rect{};GetWindowRect(controls.at(id),&rect);const auto chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN|TPM_TOPALIGN,rect.right,rect.bottom+d(2),0,window,nullptr);DestroyMenu(menu);if(chosen)command(chosen);}
    void menu(){auto popup_menu=CreatePopupMenu();auto item=[&](HMENU target,int id,const wchar_t* caption,bool enabled=true){AppendMenuW(target,MF_STRING|(enabled?0:MF_GRAYED),id,caption);};const auto separator=[&](HMENU target){AppendMenuW(target,MF_SEPARATOR,0,nullptr);};
      // As in Chromium, an available update leads the menu.
      if(!update_version.empty()){const auto caption=ui::format(ui::tr(L"Update Xenon to {0}…"),{ui::wide(update_version)});item(popup_menu,MenuUpdates,caption.c_str());separator(popup_menu);}
      item(popup_menu,MenuNewTab,ui::tr(L"New tab\tCtrl+T"));item(popup_menu,MenuNewWindow,ui::tr(L"New window\tCtrl+N"));item(popup_menu,MenuPrivate,ui::tr(L"New private workspace\tCtrl+Shift+N"));separator(popup_menu);
      item(popup_menu,MenuHistory,ui::tr(L"History\tCtrl+H"));item(popup_menu,MenuClearData,ui::tr(L"Clear browsing data…\tCtrl+Shift+Del"));item(popup_menu,MenuDownloads,ui::tr(L"Downloads\tCtrl+J"));item(popup_menu,MenuBookmarks,ui::tr(L"Bookmarks\tCtrl+Shift+O"));item(popup_menu,MenuImport,ui::tr(L"Import bookmarks and history…"));item(popup_menu,MenuPasswords,ui::tr(L"Passwords"));item(popup_menu,MenuManageExtensions,ui::tr(L"Extensions"));separator(popup_menu);
      item(popup_menu,MenuReopenTab,ui::tr(L"Reopen closed tab\tCtrl+Shift+T"),!shell.closed_tabs.empty());item(popup_menu,MenuCloseWorkspaceTabs,ui::tr(L"Close all tabs in this workspace…"),shell.workspace_tab_count(workspace())>0);separator(popup_menu);
      auto zoom=CreatePopupMenu();item(zoom,MenuZoomIn,ui::tr(L"Zoom in\tCtrl++"));item(zoom,MenuZoomOut,ui::tr(L"Zoom out\tCtrl+-"));item(zoom,MenuZoomReset,ui::tr(L"Reset zoom\tCtrl+0"));AppendMenuW(popup_menu,MF_POPUP,reinterpret_cast<UINT_PTR>(zoom),ui::tr(L"Zoom"));
      item(popup_menu,MenuFullscreen,fullscreen()?ui::tr(L"Exit full screen\tF11"):ui::tr(L"Full screen\tF11"));
      item(popup_menu,MenuFind,ui::tr(L"Find on page\tCtrl+F"));item(popup_menu,MenuBookmark,ui::tr(L"Bookmark this page\tCtrl+D"));item(popup_menu,MenuPrint,ui::tr(L"Print\tCtrl+P"));item(popup_menu,MenuPdf,ui::tr(L"Save as PDF…"));item(popup_menu,MenuPermissions,ui::tr(L"Site settings"));separator(popup_menu);
      auto appearance=CreatePopupMenu();item(appearance,MenuThemeSystem,ui::tr(L"Theme: System"));item(appearance,MenuThemeLight,ui::tr(L"Theme: Light"));item(appearance,MenuThemeDark,ui::tr(L"Theme: Dark"));
      CheckMenuRadioItem(appearance,MenuThemeSystem,MenuThemeDark,ui::theme_mode==ui::ThemeMode::system?MenuThemeSystem:ui::theme_mode==ui::ThemeMode::light?MenuThemeLight:MenuThemeDark,MF_BYCOMMAND);
      AppendMenuW(appearance,MF_SEPARATOR,0,nullptr);item(appearance,MenuSidebarNarrower,ui::tr(L"Narrower sidebar"));item(appearance,MenuSidebarWider,ui::tr(L"Wider sidebar"));item(appearance,MenuSidebarReset,ui::tr(L"Reset sidebar width"));
      AppendMenuW(popup_menu,MF_POPUP,reinterpret_cast<UINT_PTR>(appearance),ui::tr(L"Appearance"));
      auto languages=CreatePopupMenu();AppendMenuW(languages,MF_STRING,MenuLanguageEnglish,L"English");AppendMenuW(languages,MF_STRING,MenuLanguageChinese,L"简体中文");
      CheckMenuRadioItem(languages,MenuLanguageEnglish,MenuLanguageChinese,ui::preferred_language==ui::Language::simplified_chinese?MenuLanguageChinese:MenuLanguageEnglish,MF_BYCOMMAND);
      AppendMenuW(popup_menu,MF_POPUP,reinterpret_cast<UINT_PTR>(languages),L"Language / 语言");item(popup_menu,MenuSettings,ui::tr(L"Settings"));
      auto help=CreatePopupMenu();item(help,MenuQuickTour,ui::tr(L"Quick tour"));item(help,MenuDocumentation,ui::tr(L"GitHub documentation"));item(help,MenuUpdates,ui::tr(L"Check for updates"));item(help,MenuAbout,ui::tr(L"About Xenon"));item(help,MenuNotices,ui::tr(L"Third-party notices"));
      AppendMenuW(popup_menu,MF_POPUP,reinterpret_cast<UINT_PTR>(help),ui::tr(L"Help"));item(popup_menu,MenuDeveloper,ui::tr(L"Developer mode"));separator(popup_menu);
      item(popup_menu,MenuCloseWindow,ui::tr(L"Close window\tCtrl+Shift+W"));item(popup_menu,MenuExit,ui::tr(L"Exit Xenon"));
      popup(popup_menu,Menu);
    }
    // Extension pages open in a new human tab. Toolbar popups that expect
    // Chromium's tab strip may not find this page; options pages work.
    void extensions_menu(){auto menu_handle=CreatePopupMenu();const auto records=shell.extensions.list();int index=0;bool any=false;
      for(const auto& record:records){if(!record.enabled||!shell.extensions.active(record.id)||index>=48){++index;continue;}
        auto submenu=CreatePopupMenu();AppendMenuW(submenu,MF_STRING|(record.popup_page.empty()?MF_GRAYED:0),ExtensionItem+index*2,ui::tr(L"Open popup page in a tab"));AppendMenuW(submenu,MF_STRING|(record.options_page.empty()?MF_GRAYED:0),ExtensionItem+index*2+1,ui::tr(L"Open options"));
        AppendMenuW(menu_handle,MF_POPUP,reinterpret_cast<UINT_PTR>(submenu),ui::wide(record.name).c_str());any=true;++index;}
      if(!any)AppendMenuW(menu_handle,MF_STRING|MF_GRAYED,0,ui::tr(L"No extensions are running"));
      AppendMenuW(menu_handle,MF_SEPARATOR,0,nullptr);AppendMenuW(menu_handle,MF_STRING,MenuManageExtensions,ui::tr(L"Manage extensions…"));
      RECT rect{};GetWindowRect(controls.at(Extensions),&rect);const auto chosen=TrackPopupMenu(menu_handle,TPM_RETURNCMD|TPM_RIGHTALIGN|TPM_TOPALIGN,rect.right,rect.bottom+d(2),0,window,nullptr);DestroyMenu(menu_handle);
      if(chosen>=ExtensionItem&&chosen<ExtensionItem+96){const size_t record=static_cast<size_t>((chosen-ExtensionItem)/2);if(record<records.size())open_extension_page(records[record].id,(chosen-ExtensionItem)%2?records[record].options_page:records[record].popup_page);}
      else if(chosen)command(chosen);
    }
    void open_extension_page(const std::string& id,const std::string& page_path){if(page_path.empty()||!shell.extensions.active(id))return;
      const auto target=is_private()?std::string("native-default"):workspace();const auto url="chrome-extension://"+id+"/"+page_path;auto& shell_ref=shell;const auto frame=window;
      shell.placements.push_back({target,window,{},true,GetTickCount64()});
      shell.broker.open_human_tab(target,"about:blank",[&shell_ref,url,frame](Json value){
        if(!value.value("ok",false)){shell_ref.reply(frame,{})(std::move(value));return;}
        shell_ref.engine.native_command(str(value.value("result",Json::object()),"tabId"),"extension-page",url,shell_ref.reply(frame,{}));});
    }
    // The ownership chip: take control, give it to a connected agent, or
    // open Controls. These are the same native operations as in Controls.
    void owner_menu(){auto found=shell.states.find(selected);if(found==shell.states.end())return;const auto& state=found->second;const auto owner=str(state,"ownerSessionId");
      auto menu_handle=CreatePopupMenu();std::vector<std::string> recipients;
      // The agent's own words, attributed to it; the chip has room for neither.
      if(state.value("attentionRequested",false)){AppendMenuW(menu_handle,MF_STRING|MF_GRAYED,OwnerHelpNote,menu_text(shell.help_text(selected)).c_str());
        AppendMenuW(menu_handle,MF_STRING,OwnerDismissHelp,ui::tr(L"Dismiss help request"));AppendMenuW(menu_handle,MF_SEPARATOR,0,nullptr);}
      AppendMenuW(menu_handle,MF_STRING|(owner=="human"?MF_GRAYED|MF_CHECKED:0),OwnerTake,ui::tr(L"Take ownership"));
      if(owner=="human")for(const auto& worker:shell.workers)if(str(worker,"workspaceId")==workspace()&&worker.value("connected",false)&&str(worker,"state")=="connected"&&recipients.size()<32){
        recipients.push_back(str(worker,"agentSessionId"));AppendMenuW(menu_handle,MF_STRING,OwnerGive+static_cast<int>(recipients.size())-1,ui::format(ui::tr(L"Give to {0}"),{ui::wide(str(worker,"name"))}).c_str());}
      AppendMenuW(menu_handle,MF_SEPARATOR,0,nullptr);AppendMenuW(menu_handle,MF_STRING,OwnerControls,ui::tr(L"Xenon Controls…"));
      POINT at{chip.left,chip.bottom};ClientToScreen(window,&at);const auto chosen=TrackPopupMenu(menu_handle,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN,at.x,at.y+d(2),0,window,nullptr);DestroyMenu(menu_handle);
      if(chosen==OwnerDismissHelp)shell.broker.dismiss_attention(selected);
      else if(chosen==OwnerTake){shell.broker.human_acquire(selected);set_status(ui::tr(L"Ownership requested. Current agent input finishes first."));}
      else if(chosen>=OwnerGive&&chosen<OwnerGive+32&&static_cast<size_t>(chosen-OwnerGive)<recipients.size()){shell.broker.human_release(selected,recipients[static_cast<size_t>(chosen-OwnerGive)]);set_status(ui::tr(L"Control offered to the selected agent."));}
      else if(chosen==OwnerControls)shell.native.show_section(NativeUi::Section::workspaces,workspace());
    }
    void tab_menu(const std::string& id,POINT at);
    void group_menu(const std::string& workspace_id,POINT at);
    void command(int id);
    static LRESULT CALLBACK edit_proc(HWND control,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR owner){auto self=reinterpret_cast<Frame*>(owner);
      if(message==WM_KEYDOWN&&wp==VK_RETURN){if(control==self->address)self->submit();
        else {const auto text=ui::text(control);self->shell.engine.native_command(self->selected,(GetKeyState(VK_SHIFT)&0x8000)?"find-previous":text==self->last_find?"find-next":"find",text);self->last_find=text;}return 0;}
      // The first Escape closes suggestions and restores the typed text.
      if(message==WM_KEYDOWN&&wp==VK_ESCAPE){if(control==self->find_text)self->hide_find();else if(self->suggesting()){self->hide_suggestions();self->show_text(self->typed);}else {self->address_dirty=false;auto found=self->shell.hosts.find(self->selected);self->set_address(found==self->shell.hosts.end()?std::string{}:str(found->second.metadata,"url"));self->shell.engine.native_command(self->selected,"focus");}return 0;}
      if(control==self->address){
        if(message==WM_KEYDOWN&&(wp==VK_UP||wp==VK_DOWN)){self->step_suggestion(wp==VK_DOWN?1:-1);return 0;}
        if(message==WM_KEYDOWN&&wp==VK_DELETE&&(GetKeyState(VK_SHIFT)&0x8000)&&self->remove_suggestion())return 0;
        // Only a printable character permits inline completion of the next change.
        if(message==WM_CHAR)self->typing=wp>=0x20&&wp!=0x7f;
        else if((message==WM_KEYDOWN&&(wp==VK_DELETE||wp==VK_INSERT))||message==WM_PASTE||message==WM_CUT||message==WM_CLEAR||message==WM_UNDO||message==EM_UNDO||message==WM_SETTEXT)self->typing=false;
      }
      // The first click into the address selects all of it, as in Chromium.
      if(message==WM_LBUTTONDOWN&&control==self->address&&GetFocus()!=control){SetFocus(control);SendMessageW(control,EM_SETSEL,0,-1);return 0;}
      if(message==WM_NCDESTROY)RemoveWindowSubclass(control,edit_proc,control==self->address?1:2);
      return DefSubclassProc(control,message,wp,lp);
    }
    static LRESULT CALLBACK tree_proc(HWND control,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR owner){auto self=reinterpret_cast<Frame*>(owner);auto& shell=self->shell;
      // Custom mouse paths can bypass Windows' normal focus-cue update.
      // A reclick keeps the ownership outline; keyboard navigation restores
      // the separate focus cue for accessibility.
      if(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK)SendMessageW(self->window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEFOCUS),0);
      if(message==WM_KEYDOWN)SendMessageW(self->window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);
      if(message==WM_KEYDOWN&&wp==VK_ESCAPE&&shell.drag.active){shell.drag_cancel();return 0;}
      if(message==WM_KEYDOWN&&(wp==VK_DELETE||wp==VK_RETURN)){const auto [id,group]=self->key_of(TreeView_GetSelection(control));if(!id.empty()){if(wp==VK_DELETE&&!group){shell.engine.native_command(id,"close");return 0;}if(wp==VK_RETURN&&group&&self->empty_workspace(id)){self->open_workspace(id);return 0;}}}
      // A quick second click arrives as a double-click. On a row button it is
      // another press of that button; the tree would otherwise toggle the
      // workspace's expansion.
      if(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK){const POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        self->pressed_close=self->close_at(point);if(!self->pressed_close.empty()){SetCapture(control);return 0;}
        std::string group;const int button=self->group_button_at(point,group);
        if(button>=0){POINT screen=point;ClientToScreen(control,&screen);if(button==GroupMore)self->group_menu(group,screen);else self->open_tab(group,"about:blank");return 0;}}
      if(message==WM_LBUTTONUP&&!self->pressed_close.empty()){auto id=std::exchange(self->pressed_close,{});if(GetCapture()==control)ReleaseCapture();if(id==self->close_at({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}))shell.engine.native_command(id,"close");return 0;}
      if(message==WM_CAPTURECHANGED||message==WM_CANCELMODE)self->pressed_close.clear();
      if(message==WM_MBUTTONUP){const auto [id,group]=self->key_of(self->item_at({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}));if(!id.empty()&&!group)shell.engine.native_command(id,"close");return 0;}
      if(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK){const auto [id,group]=self->key_of(self->item_at({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}));if(group&&self->empty_workspace(id)){self->open_workspace(id);return 0;}}
      // Keyboard context menu (Shift+F10 or the menu key) for the selected row.
      if(message==WM_CONTEXTMENU&&GET_X_LPARAM(lp)==-1&&GET_Y_LPARAM(lp)==-1){const auto item=TreeView_GetSelection(control);const auto [id,group]=self->key_of(item);RECT rect{};if(!id.empty()&&TreeView_GetItemRect(control,item,&rect,FALSE)){POINT at{rect.left+self->d(24),rect.bottom};ClientToScreen(control,&at);if(group)self->group_menu(id,at);else self->tab_menu(id,at);}return 0;}
      auto result=DefSubclassProc(control,message,wp,lp);
      if(message==WM_MOUSEMOVE||message==WM_MOUSELEAVE){TVHITTESTINFO hit{};hit.pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};const auto next=message==WM_MOUSELEAVE?nullptr:TreeView_HitTest(control,&hit);const auto close=message==WM_MOUSELEAVE?std::string{}:self->close_at(hit.pt);
        if(next!=self->hovered_row||close!=self->hovered_close){if(next!=self->hovered_row){if(self->hovered_row)self->row_fades[self->hovered_row].set(false,ui::hover_duration);if(next)self->row_fades[next].set(true,ui::hover_duration);}self->hovered_row=next;self->hovered_close=close;}
        InvalidateRect(control,nullptr,FALSE);if(message==WM_MOUSEMOVE){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,control,0};TrackMouseEvent(&track);}}
      if(message==WM_VSCROLL||message==WM_MOUSEWHEEL||message==WM_KEYDOWN)InvalidateRect(self->window,nullptr,FALSE);if(message==WM_NCDESTROY)RemoveWindowSubclass(control,tree_proc,3);return result;}
  };

  Broker& broker;CefEngine& engine;NativeUi& native;ExtensionStore& extensions;BrowserData data;std::filesystem::path root;
  HWND messages{},ghost{};std::vector<std::unique_ptr<Frame>> frames;Frame* active{};
  std::map<std::string,Host> hosts;std::vector<std::string> order;
  std::map<std::string,Json> states;std::map<std::string,std::string> names,workspace_names;std::set<std::string> private_workspaces;Json workers=Json::array();
  size_t pairing_count{};Clock::time_point next_state{},next_flush{},next_slow{};bool shutting_down{};
  std::deque<Placement> placements;std::deque<Closed> closed_tabs;
  std::vector<std::unique_ptr<Panel>> panels;Drag drag;
  // Agents' help requests: arrivals, acknowledgment and alert limits, and the
  // tray icon whose Windows notification opens `help_tab`.
  AttentionNotices help;std::string help_tab;bool help_icon{};
  std::shared_ptr<Mailbox> mailbox=std::make_shared<Mailbox>();
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
  std::filesystem::path fixture_destination;std::map<int,std::pair<uint64_t,uint64_t>> fixture_paints;
  void fixture_snapshot(const std::filesystem::path& destination);
#endif
  Impl(Broker& b,CefEngine& e,NativeUi& n,ExtensionStore& x,std::filesystem::path path):broker(b),engine(e),native(n),extensions(x),data(path),root(std::move(path)){}
  // Broker and engine replies may arrive on other threads. Shell state is
  // changed only on the UI thread through this queue.
  void post(std::function<void()> call){{std::lock_guard lock(mailbox->mutex);mailbox->calls.push_back(std::move(call));}PostMessageW(messages,ShellRun,0,0);}
  void run_posted(){std::deque<std::function<void()>> calls;{std::lock_guard lock(mailbox->mutex);calls.swap(mailbox->calls);}for(auto& call:calls)try{call();}catch(...){}}
  Reply reply(HWND frame,std::wstring success_text){auto box=mailbox;const auto target=messages;
    return [this,box,target,frame,success_text=std::move(success_text)](Json value){
      const bool ok=value.value("ok",false);const auto message=ok?success_text:ui::error_text(str(value.value("error",Json::object()),"message"));
      {std::lock_guard lock(box->mutex);box->calls.push_back([this,frame,message]{if(auto found=frame_of(frame);found&&!message.empty())found->set_status(message);});}PostMessageW(target,ShellRun,0,0);};}
  Frame* frame_of(HWND window) const {if(!window)return nullptr;for(const auto& frame:frames)if(frame->window==window)return frame.get();return nullptr;}
  Frame* primary() const {for(const auto& frame:frames)if(frame->window&&!frame->closing)return frame.get();return nullptr;}
  Frame* live_active() const {return active&&active->window&&!active->closing?active:primary();}
  void layout_all(){for(auto& frame:frames)if(frame->window)frame->layout();}
  void save_sidebar(Frame& frame){if(!ui::save_settings())frame.set_status(ui::tr(L"Sidebar resized for this run; saving failed."));InvalidateRect(frame.window,nullptr,FALSE);}
  // ---- Agent help requests ----------------------------------------------------
  // The requesting agent's name and note are its own words, never Xenon's or
  // the website's; both are made printable and attributed to the agent.
  std::wstring help_agent(const std::string& tab) const {const auto state=states.find(tab);const auto owner=state==states.end()?std::string{}:str(state->second,"ownerSessionId");
    auto name=omnibox::printable(ui::wide(names.contains(owner)&&!names.at(owner).empty()?names.at(owner):ui::tr8("Agent")));if(name.size()>60)name=name.substr(0,59)+L"…";return name;}
  std::wstring help_text(const std::string& tab) const {const auto state=states.find(tab);const auto note=state==states.end()?std::string{}:str(state->second,"attentionReason");
    return note.empty()?ui::format(ui::tr(L"{0} needs your help in this tab."),{help_agent(tab)}):ui::format(ui::tr(L"{0} needs your help: “{1}”"),{help_agent(tab),omnibox::printable(ui::wide(note))});}
  // A request flashes its tab until the person has been present with it in
  // front. A new one also flashes the window's taskbar button and, when the
  // person allowed it, shows a Windows notification, unless they are already
  // in that window. None of this sends page input or changes ownership.
  void update_help(){
    if(shutting_down){hide_help_notice();return;}
    for(const auto& arrival:help.update(states,GetTickCount())){
      const auto host=hosts.find(arrival.tab_id);if(host==hosts.end()||!host->second.frame||!host->second.frame->window)continue;auto& frame=*host->second.frame;
      if((frame.foreground()&&!frame.fullscreen())||!help.allow_alert(arrival.tab_id,GetTickCount64()))continue;
      if(!frame.foreground()){FLASHWINFO flash{sizeof(flash),frame.window,FLASHW_TRAY,3,0};FlashWindowEx(&flash);frame.help_flashed=true;}
      if(ui::agent_help_notifications)show_help_notice(arrival.tab_id);
    }
    LASTINPUTINFO input{sizeof(input)};
    if(GetLastInputInfo(&input))for(auto& frame:frames)if(frame->window&&!frame->closing&&frame->foreground()&&frame->waiting_for_help(frame->selected)&&help.acknowledge(frame->selected,input.dwTime)){
      broker.attention_seen(frame->selected);frame->set_status(help_text(frame->selected));}
    if(help_icon&&(!ui::agent_help_notifications||!states.contains(help_tab)||!AttentionNotices::pending(states.at(help_tab))))hide_help_notice();
    for(auto& frame:frames)if(frame->window&&frame->help_flashed&&std::none_of(hosts.begin(),hosts.end(),[&](const auto& entry){return entry.second.frame==frame.get()&&frame->waiting_for_help(entry.first);})){
      FLASHWINFO stop{sizeof(stop),frame->window,FLASHW_STOP,0,0};FlashWindowEx(&stop);frame->help_flashed=false;}
  }
  // Explorer shows a tray icon's balloon as a Windows notification. Selecting
  // it, or the icon while it remains, opens the tab. The notification names
  // the agent and the tab's site, never the page title, which the site controls.
  void show_help_notice(const std::string& tab){
    const auto state=states.find(tab);const auto host=hosts.find(tab);if(state==states.end()||host==hosts.end())return;
    NOTIFYICONDATAW data{};data.cbSize=sizeof(data);data.hWnd=messages;data.uID=1;data.uFlags=NIF_ICON|NIF_TIP|NIF_MESSAGE|NIF_INFO|NIF_SHOWTIP;
    data.uCallbackMessage=ShellHelpNotice;data.hIcon=branding_icon(GetSystemMetrics(SM_CXSMICON));
    const auto title=ui::format(ui::tr(L"{0} needs your help"),{help_agent(tab)});
    auto note=omnibox::printable(ui::wide(str(state->second,"attentionReason")));if(note.size()>180)note=note.substr(0,179)+L"…";
    auto body=note.empty()?std::wstring(ui::tr(L"A tab is waiting for you.")):L"“"+note+L"”";
    if(const auto site=origin_of(str(host->second.metadata,"url"));!site.empty())body+=L"\n"+omnibox::printable(ui::wide(site.size()>70?site.substr(0,70):site));
    wcsncpy_s(data.szTip,title.c_str(),_TRUNCATE);wcsncpy_s(data.szInfoTitle,title.c_str(),_TRUNCATE);wcsncpy_s(data.szInfo,body.c_str(),_TRUNCATE);
    data.dwInfoFlags=NIIF_USER|NIIF_LARGE_ICON;data.hBalloonIcon=branding_icon(GetSystemMetrics(SM_CXICON)*2);
    // Explorer may have restarted and dropped the icon; add it again.
    if(help_icon&&!Shell_NotifyIconW(NIM_MODIFY,&data))help_icon=false;
    if(!help_icon){if(!Shell_NotifyIconW(NIM_ADD,&data))return;data.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&data);help_icon=true;}
    help_tab=tab;
  }
  void hide_help_notice(){if(!help_icon)return;NOTIFYICONDATAW data{};data.cbSize=sizeof(data);data.hWnd=messages;data.uID=1;Shell_NotifyIconW(NIM_DELETE,&data);help_icon=false;help_tab.clear();}
  void open_help_tab(){const auto tab=help_tab;hide_help_notice();const auto host=hosts.find(tab);if(host==hosts.end()||!host->second.frame||!host->second.frame->window||host->second.frame->closing)return;
    auto& frame=*host->second.frame;if(IsIconic(frame.window))ShowWindow(frame.window,SW_RESTORE);SetForegroundWindow(frame.window);frame.choose(tab,true);}
  size_t workspace_tab_count(const std::string& workspace) const {return static_cast<size_t>(std::count_if(states.begin(),states.end(),[&](const auto& entry){return str(entry.second,"workspaceId")==workspace;}));}
  void theme_all(){const auto colors=ui::palette();for(auto& frame:frames)if(frame->window)frame->theme();
    for(auto& panel:panels)if(panel->window){ui::icons(panel->window);for(auto child=GetWindow(panel->window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))ui::control_theme(child);if(panel->list){ListView_SetBkColor(panel->list,colors.canvas);ListView_SetTextBkColor(panel->list,colors.canvas);ListView_SetTextColor(panel->list,colors.ink);}
      if(panel->tree){TreeView_SetBkColor(panel->tree,colors.canvas);TreeView_SetTextColor(panel->tree,colors.ink);}InvalidateRect(panel->window,nullptr,TRUE);}
    engine.native_command("","theme");}
  Frame* create_frame(const RECT* bounds=nullptr){
    auto frame=std::make_unique<Frame>(*this);auto raw=frame.get();frames.push_back(std::move(frame));
    const int x=bounds?bounds->left:CW_USEDEFAULT,y=bounds?bounds->top:CW_USEDEFAULT,width=bounds?bounds->right-bounds->left:1200,height=bounds?bounds->bottom-bounds->top:800;
    CreateWindowExW(0,L"XenonBrowserShell",ui::tr(L"Xenon Browser"),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,x,y,width,height,nullptr,nullptr,branding_module(),raw);
    if(!raw->window){frames.pop_back();return nullptr;}
    return raw;
  }
  // A new window beside an existing one, as Ctrl+N and tab tear-off create.
  Frame* new_window(const Frame* beside,POINT* at=nullptr){RECT bounds{};
    if(beside&&beside->window){GetWindowRect(beside->window,&bounds);if(IsZoomed(beside->window)){MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(beside->window,MONITOR_DEFAULTTONEAREST),&monitor);bounds=monitor.rcWork;InflateRect(&bounds,-ui::dip(beside->window,60),-ui::dip(beside->window,40));}OffsetRect(&bounds,ui::dip(beside->window,32),ui::dip(beside->window,32));}
    if(at&&bounds.right>bounds.left)OffsetRect(&bounds,at->x-ui::dip(beside->window,120)-bounds.left,at->y-ui::dip(beside->window,20)-bounds.top);
    auto frame=create_frame(bounds.right>bounds.left?&bounds:nullptr);if(frame){ShowWindow(frame->window,SW_SHOWNORMAL);SetForegroundWindow(frame->window);active=frame;}return frame;}
  // Window for a new tab: an explicit placement, its opener's window, the
  // active window for human tabs, else a window already showing its workspace.
  Frame* frame_for(const std::string& workspace,bool human,const std::string& opener,std::string& before,bool& select){
    if(auto found=hosts.find(opener);!opener.empty()&&found!=hosts.end()&&found->second.frame&&!found->second.frame->closing){
      const auto next=std::find(order.begin(),order.end(),opener);before=next!=order.end()&&std::next(next)!=order.end()?*std::next(next):std::string{};return found->second.frame;}
    const auto now=GetTickCount64();std::erase_if(placements,[&](const auto& placement){return now-placement.at>15000;});
    for(auto placement=placements.begin();placement!=placements.end();++placement)if(placement->workspace==workspace){
      auto frame=frame_of(placement->frame);before=placement->before;select=placement->select;placements.erase(placement);if(frame&&!frame->closing)return frame;break;}
    if(human)return live_active();
    for(auto it=order.rbegin();it!=order.rend();++it)if(auto found=hosts.find(*it);found!=hosts.end()&&found->second.workspace==workspace&&found->second.frame&&!found->second.frame->closing)return found->second.frame;
    return live_active();
  }
  void reorder(const std::string& id,const std::string& before){std::erase(order,id);auto at=before.empty()?order.end():std::find(order.begin(),order.end(),before);order.insert(at,id);}
  // Human tab move between windows of the same workspace. The page and its
  // profile are untouched; only its native host changes parent window.
  void move_host(Host& host,Frame& target,const std::string& before){
    auto source=host.frame;if(before!=host.id)reorder(host.id,before);
    if(source!=&target){SetParent(host.window,target.window);host.frame=&target;host.initialized=false;
      if(source&&source->selected==host.id)source->select_neighbor(host.id);
      target.layout();target.choose(host.id,true);engine.native_command(host.id,"reparented");
      if(source&&!source->has_hosts()&&frames.size()>1)DestroyWindow(source->window);
      if(source&&source->window){source->tree_signature=nullptr;source->rebuild_tree();InvalidateRect(source->window,nullptr,FALSE);}}
    target.tree_signature=nullptr;target.rebuild_tree();
  }
  void close_tabs(Frame& frame,std::vector<std::string> ids){if(ids.empty())return;broker.close_tabs(ids,reply(frame.window,{}));}
  void close_workspace(Frame& frame,const std::string& workspace){
    const auto count=workspace_tab_count(workspace);if(!count)return;
    if(!ui::confirm_close_tabs(frame.window,count,workspace_names.contains(workspace)?workspace_names.at(workspace):workspace))return;
    frame.set_status(ui::tr(L"Closing tabs after active agent input finishes…"));
    broker.close_workspace_tabs(workspace,reply(frame.window,ui::tr(L"Workspace tabs closed. The workspace and its data were kept.")));
  }
  std::wstring move_reason(const std::string& code) const {
    if(code=="AGENT_CONNECTED")return ui::tr(L"An agent controls this tab. Take ownership before moving it to another workspace.");
    if(code=="PRIVATE_WORKSPACE")return ui::tr(L"Private workspace tabs cannot move between workspaces.");
    if(code=="PROTECTED_AUTH")return ui::tr(L"Finish signing in before moving this tab.");
    if(code=="SAME_WORKSPACE")return {};
    return ui::tr(L"This tab cannot move to that workspace now.");
  }
  // A profile cannot adopt a live page: the address reopens in the target
  // workspace with that workspace's cookies, and the original then closes.
  void move_to_workspace(Frame& frame,const std::string& tab,const std::string& workspace,const std::string& before,bool confirm=true){
    const auto host=hosts.find(tab);if(host==hosts.end())return;
    if(const auto blocker=broker.tab_move_blocker(tab,workspace);!blocker.empty()){frame.set_status(move_reason(blocker));return;}
    const auto name=workspace_names.contains(workspace)?workspace_names.at(workspace):workspace;
    if(confirm&&MessageBoxW(frame.window,ui::format(ui::tr(L"Move this tab to {0}?\n\nThe page reloads using {0}'s cookies and sign-ins. Text entered on the page is not carried over."),{ui::wide(name)}).c_str(),ui::tr(L"Move tab to workspace"),MB_OKCANCEL|MB_ICONQUESTION)!=IDOK)return;
    placements.push_back({workspace,frame.window,before,true,GetTickCount64()});
    broker.move_tab_to_workspace(tab,workspace,str(host->second.metadata,"url"),reply(frame.window,ui::format(ui::tr(L"Tab moved to {0}."),{ui::wide(name)})));
  }
  // ---- Tab drag and drop -------------------------------------------------
  void drag_begin(Frame& frame,const std::string& tab){
    const auto host=hosts.find(tab);if(host==hosts.end()||drag.active)return;
    drag=Drag{};drag.source=frame.window;drag.tab=tab;drag.active=true;drag.title=short_title(ui::tab_title(str(host->second.metadata,"title"),str(host->second.metadata,"url")),48);
    SetPropW(frame.window,L"XenonSidebarDrag",reinterpret_cast<HANDLE>(1));SetCapture(frame.window);
    ghost=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"XenonTabDrag",L"",WS_POPUP,0,0,10,10,frame.window,nullptr,branding_module(),this);
    if(ghost)SetLayeredWindowAttributes(ghost,0,235,LWA_ALPHA);
    POINT point{};GetCursorPos(&point);drag_move(point);
  }
  HWND window_at(POINT point) const {
    for(HWND window=GetTopWindow(nullptr);window;window=GetWindow(window,GW_HWNDNEXT)){
      if(window==ghost||!IsWindowVisible(window)||IsIconic(window))continue;wchar_t name[64]{};GetClassNameW(window,name,64);
      if(wcscmp(name,L"XenonAgentCursor")==0||wcscmp(name,TOOLTIPS_CLASSW)==0)continue;RECT rect{};GetWindowRect(window,&rect);if(PtInRect(&rect,point))return window;}
    return nullptr;
  }
  void drag_move(POINT point){
    if(!drag.active)return;drag.point=point;const auto host=hosts.find(drag.tab);if(host==hosts.end()){drag_cancel();return;}
    const auto previous_target=drag.target;drag.target=nullptr;drag.outside=false;drag.blocked=false;drag.line=nullptr;drag.group=nullptr;drag.before.clear();drag.workspace=host->second.workspace;
    auto frame=frame_of(window_at(point));if(frame&&frame->closing)frame=nullptr;
    if(!frame)drag.outside=true;
    else{drag.target=frame->window;RECT tree{};GetWindowRect(frame->tree,&tree);
      if(PtInRect(&tree,point)){POINT local=point;ScreenToClient(frame->tree,&local);const auto item=frame->item_at(local);const auto [id,group]=frame->key_of(item);
        if(group){drag.group=item;drag.workspace=id;}
        else if(!id.empty()&&hosts.contains(id)){RECT row{};TreeView_GetItemRect(frame->tree,item,&row,FALSE);drag.line=item;drag.below=local.y>(row.top+row.bottom)/2;drag.workspace=hosts.at(id).workspace;
          if(drag.below){const auto tabs=frame->visual_tabs();auto at=std::find(tabs.begin(),tabs.end(),id);drag.before=at!=tabs.end()&&std::next(at)!=tabs.end()&&hosts.at(*std::next(at)).workspace==drag.workspace?*std::next(at):std::string{};}
          else drag.before=id;
          if(drag.before==drag.tab){drag.line=nullptr;}}}}
    std::wstring note;
    if(drag.outside)note=ui::tr(L"Open in a new window");
    else if(drag.workspace!=host->second.workspace){const auto blocker=broker.tab_move_blocker(drag.tab,drag.workspace);drag.blocked=!blocker.empty();
      note=drag.blocked?move_reason(blocker):ui::format(ui::tr(L"Move to {0} (reloads)"),{ui::wide(workspace_names.contains(drag.workspace)?workspace_names.at(drag.workspace):drag.workspace)});}
    else if(drag.target!=drag.source)note=ui::tr(L"Move to this window");
    if(note!=drag.note){drag.note=note;if(ghost)InvalidateRect(ghost,nullptr,FALSE);}
    SetCursor(LoadCursorW(nullptr,drag.blocked?IDC_NO:IDC_ARROW));
    if(ghost){const auto owner=frame_of(drag.source);const int scale=owner?GetDpiForWindow(owner->window):96;SetWindowPos(ghost,HWND_TOPMOST,point.x+MulDiv(14,scale,96),point.y+MulDiv(10,scale,96),MulDiv(280,scale,96),MulDiv(drag.note.empty()?36:56,scale,96),SWP_NOACTIVATE|SWP_SHOWWINDOW);}
    for(const auto window:{previous_target,drag.target})if(auto target=frame_of(window))InvalidateRect(target->tree,nullptr,FALSE);
  }
  void drag_end(){
    const auto source=frame_of(drag.source),target=frame_of(drag.target);
    if(ghost){DestroyWindow(ghost);ghost=nullptr;}
    if(source){RemovePropW(source->window,L"XenonSidebarDrag");drag.finishing=true;if(GetCapture()==source->window)ReleaseCapture();drag.finishing=false;InvalidateRect(source->tree,nullptr,FALSE);}
    if(target)InvalidateRect(target->tree,nullptr,FALSE);
    drag.active=false;
  }
  void drag_cancel(){if(drag.active)drag_end();}
  void drag_drop(POINT point){
    if(!drag.active)return;drag_move(point);const auto result=drag;drag_end();
    auto source=frame_of(result.source);auto host=hosts.find(result.tab);if(!source||host==hosts.end())return;
    if(result.outside){
      // Dragging a window's only tab moves the window, as in Chromium.
      if(std::count_if(hosts.begin(),hosts.end(),[&](const auto& entry){return entry.second.frame==source;})<=1){RECT rect{};GetWindowRect(source->window,&rect);if(IsZoomed(source->window))ShowWindow(source->window,SW_RESTORE);
        SetWindowPos(source->window,nullptr,point.x-ui::dip(source->window,120),point.y-ui::dip(source->window,20),0,0,SWP_NOSIZE|SWP_NOZORDER);return;}
      POINT at=point;if(auto frame=new_window(source,&at))move_host(host->second,*frame,{});return;}
    auto target=frame_of(result.target);if(!target)return;
    if(result.workspace!=host->second.workspace){if(result.blocked){source->set_status(result.note);return;}move_to_workspace(*target,result.tab,result.workspace,result.before);return;}
    move_host(host->second,*target,result.before);
  }
  static LRESULT CALLBACK ghost_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(message==WM_NCHITTEST)return HTTRANSPARENT;if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT&&self){PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT rect{};GetClientRect(h,&rect);const auto colors=ui::palette();ui::fill(dc,rect,colors.canvas);
      ui::rounded(dc,rect,colors.surface,self->drag.blocked?colors.gray:colors.teal,ui::dip(h,8),ui::dip(h,1));const auto source=self->frame_of(self->drag.source);const HFONT face=source?source->font:reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
      RECT title{rect.left+ui::dip(h,12),rect.top+ui::dip(h,6),rect.right-ui::dip(h,10),rect.top+ui::dip(h,28)};ui::text(dc,title,self->drag.title,face,colors.ink);
      if(!self->drag.note.empty()){RECT note{title.left,title.bottom,title.right,rect.bottom-ui::dip(h,4)};ui::text(dc,note,self->drag.note,face,colors.muted);}
      EndPaint(h,&paint);return 0;}
    return DefWindowProcW(h,message,wp,lp);
  }
  // ---- Polling ---------------------------------------------------------------
  void refresh(){
    for(const auto& workspace:broker.removed_workspaces())data.forget(workspace);
    if(Clock::now()>=next_state){next_state=Clock::now()+std::chrono::milliseconds(75);auto snapshot=broker.state();std::map<std::string,Json> next;names.clear();workspace_names.clear();private_workspaces.clear();
      for(const auto& worker:snapshot["workers"])names[str(worker,"agentSessionId")]=str(worker,"name");workers=snapshot["workers"];
      for(const auto& workspace:snapshot["workspaces"]){const auto id=str(workspace,"workspaceId");if(workspace.value("removing",false))continue;workspace_names[id]=ui::workspace_label(id,str(workspace,"displayName"),workspace.value("private",false));if(workspace.value("private",false))private_workspaces.insert(id);}
      for(auto tab:snapshot["tabs"]){const auto tab_id=str(tab,"tabId");next[tab_id]=std::move(tab);}
      if(next!=states){states=std::move(next);for(auto& frame:frames)if(frame->window){InvalidateRect(frame->window,nullptr,FALSE);InvalidateRect(frame->tree,nullptr,FALSE);}}
      pairing_count=snapshot["pairings"].size();
    }
    for(const auto& row:engine.native_tabs())if(auto found=hosts.find(str(row,"tabId"));found!=hosts.end()){
      auto& host=found->second;
      const auto media=[](const Json& value){return std::array<bool,3>{value.value("screenCapture",false),value.value("mediaVideo",false),value.value("mediaAudio",false)};};
      if(media(host.metadata)!=media(row)&&host.frame)InvalidateRect(host.frame->tree,nullptr,FALSE);
      if(const auto revision=row.value("faviconRevision",uint64_t{});revision!=host.icon_revision){host.icon_revision=revision;host.icon={};engine.site_icon(host.id,host.icon);if(host.frame)InvalidateRect(host.frame->tree,nullptr,FALSE);}
      host.metadata=row;host.metadata["title"]=ui::tab_title(str(row,"title"),str(row,"url"));const auto state=states.contains(host.id)?states.at(host.id):Json::object();const bool agent=state.value("agentAvailable",false),paused=state.value("humanPaused",false);
      const PointerPoint actual{row.value("pointerX",0.0),row.value("pointerY",0.0)};
      if(agent&&!host.initialized){host.display=host.frame?host.frame->parked(host):PointerPoint{};host.initialized=true;}
      const auto human_revision=row.value("humanPointerRevision",uint64_t{}),sync_revision=row.value("pointerSyncRevision",uint64_t{}),agent_revision=row.value("agentPointerRevision",uint64_t{});
      const auto start=[&](PointerPoint target,int duration){host.from=host.display;host.to=target;host.animation=Clock::now();host.duration=duration;host.animating=true;};
      switch(host.transitions.update(agent,paused,human_revision,sync_revision,agent_revision)){
        case PointerVisual::human:start(actual,100);break;
        case PointerVisual::park:if(host.frame)start(host.frame->parked(host),120);break;
        case PointerVisual::synchronize:start(actual,80);break;
        case PointerVisual::follow:host.animating=false;host.display=actual;break;
        default:break;
      }
      host.agent=agent;host.paused=paused;
      if(!row.value("loading",true)&&!row.value("protected",false)&&row.value("privacyReady",false)&&str(row,"url")!=host.recorded_url){host.recorded_url=str(row,"url");try{data.visit(host.workspace,row.value("private",false),false,host.recorded_url,str(row,"title"));}catch(...){if(host.frame)host.frame->set_status(ui::tr(L"Browser history could not be saved."));}}
    }
    update_help();
    const bool slow=Clock::now()>=next_slow;if(slow)next_slow=Clock::now()+std::chrono::seconds(1);
    for(auto& frame:frames)if(frame->window){frame->rebuild_tree();frame->sync_toolbar();if(slow)frame->refresh_page_state();frame->update_cursor();frame->animate();
      frame->refresh_developer();
      // A closing window whose tabs survived (a page refused to close) returns.
      if(frame->closing&&GetTickCount64()-frame->closing_since>10000&&frame->has_hosts()){frame->closing=false;ShowWindow(frame->window,SW_SHOWNORMAL);frame->set_status(ui::tr(L"Some tabs did not close."));}}
    if(Clock::now()>=next_flush){next_flush=Clock::now()+std::chrono::seconds(3);if(!data.flush())if(auto frame=live_active())frame->set_status(ui::tr(L"Browser metadata could not be saved. Existing data is preserved."));}
    std::erase_if(panels,[](const auto& panel){return !panel->window;});
    if(active&&!active->window)active=nullptr;
    std::erase_if(frames,[&](const auto& frame){return !frame->window&&std::none_of(hosts.begin(),hosts.end(),[&](const auto& entry){return entry.second.frame==frame.get();});});
  }
  // `like` opens the panel for another panel's workspace and tab.
  void panel(Frame& frame,const std::string& kind,const std::string& description={},std::function<void(bool)> answer={},const std::string& source_tab={},const Panel* like=nullptr);
  // ---- Library and form windows (bookmarks, history, settings, import, clear data)
  static bool library_kind(const std::string& kind){return kind=="bookmarks"||kind=="history";}
  static bool form_kind(const std::string& kind){return kind=="settings"||kind=="import"||kind=="clear";}
  Frame* panel_frame(const Panel& target) const {auto frame=frame_of(target.parent);return frame&&!frame->closing?frame:live_active();}
  std::wstring workspace_label(const Panel& target) const {return ui::wide(workspace_names.contains(target.workspace)?workspace_names.at(target.workspace):target.workspace);}
  void bookmarks_tree(Panel& target);
  void bookmarks_rows(Panel& target);
  void history_rows(Panel& target,bool reload);
  void library_command(Panel& target,int id);
  void library_refresh(const std::string& workspace,const std::string& kind);
  void library_open(Panel& target,const std::vector<std::string>& urls);
  void form_build(Panel& target);
  void form_command(Panel& target,int id,int code);
  void settings_status(Panel& target);
  void clear_data(Panel& target);
  void start_import(Panel& target);
  static LRESULT CALLBACK library_proc(HWND,UINT,WPARAM,LPARAM);
  static LRESULT CALLBACK form_proc(HWND,UINT,WPARAM,LPARAM);
  // Website requests the person answers: a permission, or a link for another app.
  static bool prompt_kind(const std::string& kind){return kind=="permission"||kind=="external";}
  // Site settings and cookie panels describe one tab's website. Replies may
  // arrive after the panel closed, so each checks that it is still open.
  bool panel_alive(const Panel* raw,HWND window) const {return window&&std::any_of(panels.begin(),panels.end(),[&](const auto& item){return item.get()==raw&&item->window==window;});}
  void load_site(Panel* raw,std::wstring notice={});
  void fill_site(Panel& target,const Json& value,const std::wstring& notice);
  void site_panel_command(Panel& target,int id);
  void panel_setting(Panel& target,size_t index,HWND combo);
  static LRESULT CALLBACK panel_proc(HWND,UINT,WPARAM,LPARAM);
  static LRESULT CALLBACK host_proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){if(msg==WM_NCHITTEST)return HTCLIENT;return DefWindowProcW(h,msg,wp,lp);}
  static LRESULT CALLBACK elements_proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    auto frame=reinterpret_cast<Frame*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(msg==WM_NCCREATE){frame=static_cast<Frame*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(frame));}
    if(frame&&(msg==WM_COMMAND||msg==WM_NOTIFY||msg==WM_CTLCOLORSTATIC||msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORBTN))return SendMessageW(frame->window,msg,wp,lp);
    if(frame){
      if(msg==WM_SETCURSOR){POINT point{};GetCursorPos(&point);ScreenToClient(h,&point);if(frame->resizing_developer_split||frame->developer_split_hit(point)){SetCursor(LoadCursorW(nullptr,IDC_SIZENS));return TRUE;}}
      if(msg==WM_LBUTTONDOWN&&frame->developer_split_hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)})){frame->resizing_developer_split=true;SetCapture(h);return 0;}
      if(msg==WM_MOUSEMOVE&&frame->resizing_developer_split){RECT rect{};GetClientRect(h,&rect);const int space=std::max(140,MulDiv(rect.bottom,96,GetDpiForWindow(h))-188);frame->developer_split=std::clamp(static_cast<double>(MulDiv(GET_Y_LPARAM(lp),96,GetDpiForWindow(h))-171)/space,80.0/space,1.0-60.0/space);frame->layout();return 0;}
      if(msg==WM_LBUTTONUP||msg==WM_CAPTURECHANGED||msg==WM_CANCELMODE){if(frame->resizing_developer_split){frame->finish_developer_resize();return 0;}}
      if(msg==WM_LBUTTONDBLCLK&&frame->developer_split_hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)})){frame->developer_split=0.67;frame->layout();return 0;}
    }
    if(msg==WM_PAINT){PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT rect{};GetClientRect(h,&rect);ui::fill(dc,rect,ui::palette().surface);if(frame){const int middle=rect.right/2;RECT grip{middle-frame->d(16),frame->d(frame->developer_split_y+4),middle+frame->d(16),frame->d(frame->developer_split_y+6)};ui::fill(dc,grip,ui::palette().muted);}EndPaint(h,&paint);return 0;}
    return DefWindowProcW(h,msg,wp,lp);
  }
  static LRESULT CALLBACK element_overlay_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){auto self=reinterpret_cast<Frame*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Frame*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(h,message,wp,lp);
    if(message==WM_NCHITTEST)return self->developer_picking?HTCLIENT:HTTRANSPARENT;
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_SETCURSOR&&self->developer_picking){SetCursor(LoadCursorW(nullptr,IDC_CROSS));return TRUE;}
    if(message==WM_LBUTTONDOWN&&self->developer_picking){SetCapture(h);return 0;}
    if(message==WM_LBUTTONUP&&self->developer_picking){if(GetCapture()==h)ReleaseCapture();
      const auto host=self->shell.hosts.find(self->selected);if(host!=self->shell.hosts.end()){
        const auto scale=GetDpiForWindow(host->second.browser)/96.0*host->second.metadata.value("zoom",1.0);
        self->shell.engine.developer_pick_point(self->selected,GET_X_LPARAM(lp)/scale,GET_Y_LPARAM(lp)/scale);
      }return 0;
    }
    if(message==WM_MOUSEWHEEL||message==WM_RBUTTONDOWN||message==WM_RBUTTONUP||message==WM_MBUTTONDOWN||message==WM_MBUTTONUP)return 0;
    if(message==WM_PAINT){PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT rect{};GetClientRect(h,&rect);ui::fill(dc,rect,RGB(255,0,255));
      const auto host=self->shell.hosts.find(self->selected);
      if(!self->developer_box.empty()&&host!=self->shell.hosts.end()){
        const auto scale=GetDpiForWindow(host->second.browser)/96.0*host->second.metadata.value("zoom",1.0);const auto& b=self->developer_box;
        RECT box{static_cast<LONG>(std::floor(b.value("x",0.0)*scale)),static_cast<LONG>(std::floor(b.value("y",0.0)*scale)),static_cast<LONG>(std::ceil((b.value("x",0.0)+b.value("width",0.0))*scale)),static_cast<LONG>(std::ceil((b.value("y",0.0)+b.value("height",0.0))*scale))};
        const bool approximate=b.value("approximate",false);
        auto pen=CreatePen(approximate?PS_DASH:PS_SOLID,approximate?1:self->d(2),approximate?RGB(235,160,45):RGB(35,140,255));auto old=SelectObject(dc,pen),brush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));Rectangle(dc,box.left,box.top,box.right,box.bottom);SelectObject(dc,old);SelectObject(dc,brush);DeleteObject(pen);
      }EndPaint(h,&paint);return 0;}
    return DefWindowProcW(h,message,wp,lp);
  }
  static LRESULT CALLBACK cursor_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){auto self=reinterpret_cast<Frame*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Frame*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(message==WM_NCHITTEST)return HTTRANSPARENT;if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT){PAINTSTRUCT paint{};BeginPaint(h,&paint);EndPaint(h,&paint);return 0;}
    return DefWindowProcW(h,message,wp,lp);
  }
  // The full-screen notice at the top of the screen. It never takes input.
  static LRESULT CALLBACK notice_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){auto self=reinterpret_cast<Frame*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Frame*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(message==WM_NCHITTEST)return HTTRANSPARENT;if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT&&self){PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT rect{};GetClientRect(h,&rect);const auto colors=ui::palette();ui::fill(dc,rect,colors.surface);ui::outline(dc,rect,colors.border,0,1);
      RECT text=rect;InflateRect(&text,-ui::dip(h,20),0);ui::text(dc,text,self->notice_text,self->font,colors.ink,DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_NOPREFIX|DT_END_ELLIPSIS);EndPaint(h,&paint);return 0;}
    if(message==WM_NCDESTROY&&self&&self->notice==h)self->notice=nullptr;
    return DefWindowProcW(h,message,wp,lp);
  }
  // The floating find box: a rounded surface whose children forward to the frame.
  static LRESULT CALLBACK find_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){auto self=reinterpret_cast<Frame*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Frame*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(h,message,wp,lp);
    if(message==WM_COMMAND||message==WM_DRAWITEM||message==WM_CTLCOLOREDIT||message==WM_CTLCOLORSTATIC||message==WM_CTLCOLORBTN)return SendMessageW(self->window,message,wp,lp);
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT){PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT rect{};GetClientRect(h,&rect);const auto colors=ui::palette();ui::fill(dc,rect,colors.surface);ui::outline(dc,rect,colors.border,ui::dip(h,8),ui::dip(h,1));EndPaint(h,&paint);return 0;}
    return DefWindowProcW(h,message,wp,lp);
  }
  // Address suggestions. The list never activates, so keyboard focus and
  // typing stay in the address box while rows respond to the pointer.
  static LRESULT CALLBACK suggestions_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){try{auto self=reinterpret_cast<Frame*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Frame*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(h,message,wp,lp);
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_MOUSEMOVE){const int row=self->suggestion_at(GET_Y_LPARAM(lp));if(row!=self->suggestion_hover){self->suggestion_hover=row;InvalidateRect(h,nullptr,FALSE);}
      TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);return 0;}
    if(message==WM_MOUSELEAVE){if(self->suggestion_hover>=0){self->suggestion_hover=-1;InvalidateRect(h,nullptr,FALSE);}return 0;}
    if(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK){self->suggestion_pressed=self->suggestion_at(GET_Y_LPARAM(lp));SetCapture(h);return 0;}
    if(message==WM_LBUTTONUP){const int row=std::exchange(self->suggestion_pressed,-1);if(GetCapture()==h)ReleaseCapture();
      if(row>=0&&row==self->suggestion_at(GET_Y_LPARAM(lp)))self->navigate(self->matches[static_cast<size_t>(row)].destination);return 0;}
    if(message==WM_CAPTURECHANGED){self->suggestion_pressed=-1;return 0;}
    if(message==WM_PAINT){PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT bounds{};GetClientRect(h,&bounds);auto memory=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,std::max<LONG>(1,bounds.right),std::max<LONG>(1,bounds.bottom));
      if(memory&&bitmap){auto old=SelectObject(memory,bitmap);self->paint_suggestions(memory,bounds);BitBlt(dc,0,0,bounds.right,bounds.bottom,memory,0,0,SRCCOPY);SelectObject(memory,old);}else self->paint_suggestions(dc,bounds);
      if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);EndPaint(h,&paint);return 0;}
    if(message==WM_PRINTCLIENT){RECT bounds{};GetClientRect(h,&bounds);self->paint_suggestions(reinterpret_cast<HDC>(wp),bounds);return 0;}
    if(message==WM_NCDESTROY)self->suggestions=nullptr;
    return DefWindowProcW(h,message,wp,lp);
  }catch(...){return message==WM_NCCREATE?FALSE:0;}}
  // The site-information bubble. Like Chromium's, it takes focus for keyboard
  // use and closes when it loses activation or on Escape.
  static LRESULT CALLBACK site_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){try{auto self=reinterpret_cast<Frame*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Frame*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(h,message,wp,lp);
    LRESULT result{};
    if(message==WM_CTLCOLORSTATIC){ui::ctl_color(message,wp,lp,result);const int id=GetDlgCtrlID(reinterpret_cast<HWND>(lp));if(id==SiteSubtitle||id==SiteDetails||id==SitePermissions)SetTextColor(reinterpret_cast<HDC>(wp),ui::palette().muted);return result;}
    if(ui::ctl_color(message,wp,lp,result))return result;
    switch(message){
      case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE)PostMessageW(h,WM_CLOSE,0,0);return 0;
      case WM_CLOSE:if(self->site_bubble==h)self->close_site();else DestroyWindow(h);return 0;
      case WM_ERASEBKGND:return 1;
      case WM_PAINT:case WM_PRINTCLIENT:{PAINTSTRUCT paint{};auto dc=message==WM_PAINT?BeginPaint(h,&paint):reinterpret_cast<HDC>(wp);RECT rect{};GetClientRect(h,&rect);const auto colors=ui::palette();
        ui::fill(dc,rect,colors.canvas);ui::outline(dc,rect,colors.border,0,1);if(message==WM_PAINT)EndPaint(h,&paint);return 0;}
      case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(!item||item->CtlType!=ODT_BUTTON)break;
        if(self->site_rows.contains(static_cast<int>(item->CtlID)))self->draw_site_row(*item);else ui::button(*item,self->font,item->CtlID==SiteClose?ui::Icon::close:ui::Icon::none);return TRUE;}
      case WM_COMMAND:{const int id=LOWORD(wp);
        if(id==IDCANCEL){self->close_site();return 0;}
        // Enter activates the focused row or button.
        if(id==IDOK){const auto focus=GetFocus();wchar_t type[16]{};if(focus&&IsChild(h,focus)&&GetClassNameW(focus,type,16)&&_wcsicmp(type,L"BUTTON")==0)PostMessageW(h,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(focus),BN_CLICKED),reinterpret_cast<LPARAM>(focus));return 0;}
        if(HIWORD(wp)==CBN_SELCHANGE&&id>=SiteSetting&&id<SiteSetting+static_cast<int>(std::size(site::settings))){self->change_site_setting(static_cast<size_t>(id-SiteSetting),reinterpret_cast<HWND>(lp));return 0;}
        // Posted, so rebuilding or closing never destroys the clicked control inside its own notification.
        if(HIWORD(wp)==BN_CLICKED){PostMessageW(h,WM_APP,static_cast<WPARAM>(id),0);return 0;}break;}
      case WM_APP:self->site_command(static_cast<int>(wp));return 0;
      case WM_NCDESTROY:if(self->site_bubble==h)self->site_bubble=nullptr;break;
    }
    return DefWindowProcW(h,message,wp,lp);
  }catch(...){return message==WM_NCCREATE?FALSE:0;}}
  static LRESULT CALLBACK message_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(h,message,wp,lp);
    try{
      if(message==ShellRun){self->run_posted();return 0;}
      if(message==ShellHelpNotice){const auto event=LOWORD(lp);if(event==NIN_BALLOONUSERCLICK||event==NIN_SELECT||event==NIN_KEYSELECT)self->open_help_tab();return 0;}
      if(message==WM_TIMER||message==ShellChanged){self->refresh();
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
        if(message==WM_TIMER&&wp==2&&!self->fixture_destination.empty())self->fixture_snapshot(self->fixture_destination);
#endif
        return 0;}
    }catch(...){return 0;}
    return DefWindowProcW(h,message,wp,lp);
  }
  static LRESULT CALLBACK proc(HWND h,UINT message,WPARAM wp,LPARAM lp){try{return dispatch_proc(h,message,wp,lp);}catch(...){return message==WM_NCCREATE?FALSE:message==WM_CREATE?-1:0;}}
  static LRESULT CALLBACK dispatch_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){auto self=reinterpret_cast<Frame*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Frame*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(h,message,wp,lp);
    auto& shell=self->shell;
    // The omnibox edit sits on the omnibox surface, not the window canvas.
    if(message==WM_CTLCOLOREDIT&&reinterpret_cast<HWND>(lp)==self->address){const auto colors=ui::palette();auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,colors.ink);SetBkColor(dc,colors.surface);SetDCBrushColor(dc,colors.surface);return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));}
    LRESULT result{};if(ui::ctl_color(message,wp,lp,result))return result;
    switch(message){case WM_CREATE:self->build();return 0;case WM_SIZE:self->close_site();self->layout();return 0;
      case WM_MOVE:self->close_site();if(self->suggesting())self->place_suggestions();break;
      case WM_ACTIVATE:if(LOWORD(wp)!=WA_INACTIVE){shell.active=self;if(!self->selected.empty())shell.engine.select_native_tab(self->selected);}break;
      case WM_SETCURSOR:{POINT point{};GetCursorPos(&point);ScreenToClient(h,&point);if(LOWORD(lp)==HTCLIENT&&(self->resizing_sidebar||self->sidebar_hit(point))){SetCursor(LoadCursorW(nullptr,IDC_SIZEWE));return TRUE;}
        if(LOWORD(lp)==HTCLIENT&&(self->resizing_developer||self->developer_width_hit(point))){SetCursor(LoadCursorW(nullptr,IDC_SIZEWE));return TRUE;}
        if(LOWORD(lp)==HTCLIENT&&PtInRect(&self->chip,point)){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}break;}
      case WM_LBUTTONDOWN:{const POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if(self->developer_width_hit(point)){self->drag_origin=point.x;RECT bounds{};GetClientRect(self->developer_panel,&bounds);self->drag_width=MulDiv(bounds.right,96,GetDpiForWindow(h));self->clear_developer(ui::tr(L"Waiting for rendered page evidence…"));self->resizing_developer=true;SetCapture(h);return 0;}
        if(self->sidebar_hit(point)){self->drag_origin=point.x;self->drag_width=self->sidebar;self->resizing_sidebar=true;SetPropW(h,L"XenonSidebarDrag",reinterpret_cast<HANDLE>(1));SetCapture(h);return 0;}
        if(PtInRect(&self->chip,point)){self->owner_menu();return 0;}
        if(PtInRect(&self->omnibox,point)){self->focus_address();return 0;}break;}
      case WM_MOUSEMOVE:if(shell.drag.active&&shell.drag.source==h){POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(h,&point);shell.drag_move(point);return 0;}
        if(self->resizing_sidebar){self->resize_sidebar(self->drag_width+MulDiv(GET_X_LPARAM(lp)-self->drag_origin,96,GetDpiForWindow(h)));return 0;}
        if(self->resizing_developer){RECT bounds{};GetClientRect(h,&bounds);self->developer_preferred_width=std::clamp(self->drag_width-MulDiv(GET_X_LPARAM(lp)-self->drag_origin,96,GetDpiForWindow(h)),240,std::max(240,MulDiv(bounds.right,96,GetDpiForWindow(h))-self->sidebar-168));self->layout();return 0;}
        {POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(PtInRect(&self->page,point)&&!(wp&(MK_LBUTTON|MK_RBUTTON|MK_MBUTTON|MK_XBUTTON1|MK_XBUTTON2))){ClientToScreen(h,&point);self->hover_developer(&point);}}
        {const bool hover=self->sidebar_hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});if(hover!=self->sidebar_hover){self->sidebar_hover=hover;self->invalidate_grip();}if(hover){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);}}break;
      case WM_MOUSELEAVE:if(self->sidebar_hover){self->sidebar_hover=false;self->invalidate_grip();}return 0;
      case WM_LBUTTONUP:if(shell.drag.active&&shell.drag.source==h){POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(h,&point);shell.drag_drop(point);return 0;}
        if(self->resizing_sidebar){self->finish_sidebar(true);return 0;}if(self->resizing_developer){self->finish_developer_resize();return 0;}break;
      case WM_CAPTURECHANGED:if(shell.drag.active&&shell.drag.source==h&&!shell.drag.finishing){shell.drag_cancel();return 0;}self->finish_sidebar(true);self->finish_developer_resize();return 0;
      case WM_CANCELMODE:if(shell.drag.active&&shell.drag.source==h)shell.drag_cancel();self->finish_sidebar(true);self->finish_developer_resize();break;
      case WM_KEYDOWN:if(wp==VK_ESCAPE&&self->resizing_sidebar){self->finish_sidebar(true);return 0;}if(wp==VK_ESCAPE&&shell.drag.active){shell.drag_cancel();return 0;}break;
      case WM_LBUTTONDBLCLK:if(self->sidebar_hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)})){self->finish_sidebar(false);self->resize_sidebar(240);shell.save_sidebar(*self);return 0;}break;
      case WM_DPICHANGED:{const auto rect=reinterpret_cast<RECT*>(lp);SetWindowPos(h,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);DeleteObject(self->font);DeleteObject(self->bold);DeleteObject(self->emphasis);DeleteObject(self->site_title);DeleteObject(self->site_small);self->close_site();
        self->font=ui::font(h);self->bold=ui::font(h,13,FW_SEMIBOLD);self->emphasis=ui::font(h,14,FW_SEMIBOLD);self->site_title=ui::font(h,15,FW_SEMIBOLD);self->site_small=ui::font(h,12);
        for(const auto& [id,c]:self->controls)SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(self->font),TRUE);TreeView_SetItemHeight(self->tree,self->d(TabRow));self->layout();return 0;}
      case WM_SETTINGCHANGE:case WM_THEMECHANGED:shell.theme_all();return 0;
      case WM_GETMINMAXINFO:{auto value=reinterpret_cast<MINMAXINFO*>(lp);value->ptMinTrackSize={ui::dip(h,640),ui::dip(h,420)};return 0;}
      case WM_COMMAND:if(LOWORD(wp)==DeveloperPick&&HIWORD(wp)==BN_CLICKED){self->pick_developer();}
        else if(LOWORD(wp)==DeveloperFilter&&HIWORD(wp)==BN_CLICKED){self->developer_controls_only=SendMessageW(self->controls.at(DeveloperFilter),BM_GETCHECK,0,0)==BST_CHECKED;self->developer_list();}
        else if(LOWORD(wp)==Address&&HIWORD(wp)==EN_CHANGE&&!self->setting_address){self->address_dirty=true;self->suggest();}
        else if(LOWORD(wp)==Address&&(HIWORD(wp)==EN_SETFOCUS||HIWORD(wp)==EN_KILLFOCUS)){self->address_focus.set(HIWORD(wp)==EN_SETFOCUS,FocusDuration);InvalidateRect(h,&self->omnibox,FALSE);if(HIWORD(wp)==EN_KILLFOCUS)self->hide_suggestions();}else if(HIWORD(wp)==BN_CLICKED)self->command(LOWORD(wp));return 0;
      case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item&&item->CtlType==ODT_BUTTON){
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
        if(self==shell.primary()&&self->controls.contains(static_cast<int>(item->CtlID))){auto& counts=shell.fixture_paints[static_cast<int>(item->CtlID)];++counts.first;if(ui::hovered(*item))++counts.second;}
#endif
        if(item->CtlID==NewTab){self->draw_new_tab(*item);return TRUE;}
        if(item->CtlID==SiteInfo){self->draw_site_button(*item);return TRUE;}
        ui::button(*item,self->font,self->button_icon(static_cast<int>(item->CtlID)));
        // Controls carries small dots: orange for a pending pairing request,
        // blue for an available update (below the orange one when both show).
        if(item->CtlID==Controls){const int radius=self->d(4);const LONG x=item->rcItem.right-self->d(7);
          if(shell.pairing_count)ui::dot(item->hDC,{x,item->rcItem.top+self->d(7)},radius,ui::palette().orange);
          if(!self->update_version.empty())ui::dot(item->hDC,{x,shell.pairing_count?item->rcItem.bottom-self->d(7):item->rcItem.top+self->d(7)},radius,ui::notice_blue());}
        return TRUE;}break;}
      case WM_NOTIFY:{auto notice=reinterpret_cast<NMHDR*>(lp);
        if(notice->idFrom==DeveloperList){if(ui::list_draw(message,wp,lp,h,result))return result;if(notice->code==LVN_ITEMCHANGED)self->developer_details();return 0;}
        if(notice->hwndFrom!=self->tree)break;
        if(notice->code==TVN_SELCHANGEDW&&!self->rebuilding){auto item=reinterpret_cast<NMTREEVIEWW*>(lp);const auto index=item->itemNew.lParam;if(index>0&&static_cast<size_t>(index)<=self->tree_keys.size()&&!self->tree_keys[index-1].second)self->choose(self->tree_keys[index-1].first,true);return 0;}
        if(notice->code==TVN_ITEMEXPANDEDW){InvalidateRect(h,nullptr,FALSE);return 0;}
        if(notice->code==TVN_BEGINDRAGW){auto item=reinterpret_cast<NMTREEVIEWW*>(lp);const auto [id,group]=self->key_of(item->itemNew.hItem);if(!id.empty()&&!group)shell.drag_begin(*self,id);return 0;}
        if(notice->code==NM_RCLICK){POINT screen{};GetCursorPos(&screen);POINT local=screen;ScreenToClient(self->tree,&local);const auto [id,group]=self->key_of(self->item_at(local));
          if(!id.empty()){if(group)self->group_menu(id,screen);else self->tab_menu(id,screen);}return 1;}
        if(notice->code==NM_CUSTOMDRAW){auto draw=reinterpret_cast<NMTVCUSTOMDRAW*>(lp);if(draw->nmcd.dwDrawStage==CDDS_PREPAINT){self->pill_valid=self->pill_rect(self->pill);return CDRF_NOTIFYITEMDRAW;}
          if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){self->draw_tree(*draw);return CDRF_SKIPDEFAULT;}}
        break;}
      case WM_ERASEBKGND:return 1;
      case WM_PAINT:{PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT bounds{};GetClientRect(h,&bounds);auto memory=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,std::max<LONG>(1,bounds.right),std::max<LONG>(1,bounds.bottom));if(memory&&bitmap){auto old=SelectObject(memory,bitmap);self->paint(memory);BitBlt(dc,paint.rcPaint.left,paint.rcPaint.top,paint.rcPaint.right-paint.rcPaint.left,paint.rcPaint.bottom-paint.rcPaint.top,memory,paint.rcPaint.left,paint.rcPaint.top,SRCCOPY);SelectObject(memory,old);}else self->paint(dc);if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);EndPaint(h,&paint);return 0;}
      // Closing the last window exits Xenon. Closing another window closes
      // its tabs through the broker, then the window itself.
      case WM_CLOSE:{if(shell.shutting_down)return 0;
        const bool others=std::any_of(shell.frames.begin(),shell.frames.end(),[&](const auto& frame){return frame.get()!=self&&frame->window&&!frame->closing;});
        if(!others){shell.request_exit();return 0;}
        std::vector<std::string> ids;for(const auto& [id,host]:shell.hosts)if(host.frame==self)ids.push_back(id);
        if(ids.empty()){DestroyWindow(h);return 0;}
        self->closing=true;self->closing_since=GetTickCount64();ShowWindow(h,SW_HIDE);if(shell.active==self)shell.active=nullptr;shell.close_tabs(*self,ids);return 0;}
      case WM_DESTROY:self->clear_developer(ui::tr(L"Waiting for rendered page evidence…"));self->finish_sidebar(false);if(shell.drag.active&&(shell.drag.source==h||shell.drag.target==h))shell.drag_cancel();if(shell.active==self)shell.active=nullptr;return 0;
      case WM_NCDESTROY:if(self->font)DeleteObject(self->font);if(self->bold)DeleteObject(self->bold);for(auto face:{self->emphasis,self->site_title,self->site_small})if(face)DeleteObject(face);self->font=self->bold=self->emphasis=self->site_title=self->site_small=nullptr;self->window=nullptr;return DefWindowProcW(h,message,wp,lp);
    }return DefWindowProcW(h,message,wp,lp);
  }
  void request_exit(){if(shutting_down)return;shutting_down=true;hide_help_notice();data.flush();engine.shutdown();}
};

void BrowserShell::Impl::Frame::tab_menu(const std::string& id,POINT at){
  const auto found=shell.hosts.find(id);if(found==shell.hosts.end())return;auto& host=found->second;
  const auto url=str(host.metadata,"url");const bool protected_page=host.metadata.value("protected",false),web=web_address(url)&&!protected_page;
  const auto state=shell.states.contains(id)?shell.states.at(id):Json::object();const auto owner=str(state,"ownerSessionId");const bool agent_owned=!owner.empty()&&owner!="human";
  auto menu_handle=CreatePopupMenu();auto item=[&](HMENU target,int command_id,const std::wstring& caption,bool enabled=true,bool checked=false){AppendMenuW(target,MF_STRING|(enabled?0:MF_GRAYED)|(checked?MF_CHECKED:0),command_id,caption.c_str());};
  item(menu_handle,TabNewBelow,ui::tr(L"New tab below"));AppendMenuW(menu_handle,MF_SEPARATOR,0,nullptr);
  item(menu_handle,TabReload,ui::tr(L"Reload"));item(menu_handle,TabDuplicate,ui::tr(L"Duplicate"),web||url=="about:blank");
  item(menu_handle,TabPin,host.pinned?ui::tr(L"Unpin"):ui::tr(L"Pin"));item(menu_handle,TabMute,host.metadata.value("muted",false)?ui::tr(L"Unmute site"):ui::tr(L"Mute site"));
  item(menu_handle,TabCopyLink,ui::tr(L"Copy link"),web);item(menu_handle,TabBookmark,ui::tr(L"Bookmark tab"),web);AppendMenuW(menu_handle,MF_SEPARATOR,0,nullptr);
  // Window moves keep the live page. Workspace moves reopen the address in
  // another profile, so the broker only allows them without an agent owner.
  auto windows=CreatePopupMenu();item(windows,TabNewWindow,ui::tr(L"New window"),std::count_if(shell.hosts.begin(),shell.hosts.end(),[&](const auto& entry){return entry.second.frame==this;})>1);
  std::vector<HWND> targets;for(const auto& frame:shell.frames)if(frame.get()!=this&&frame->window&&!frame->closing&&targets.size()<32){targets.push_back(frame->window);
    const auto selected_host=shell.hosts.find(frame->selected);item(windows,TabToWindow+static_cast<int>(targets.size())-1,selected_host==shell.hosts.end()?ui::tr(L"Xenon window"):short_title(str(selected_host->second.metadata,"title"),48));}
  AppendMenuW(menu_handle,MF_POPUP,reinterpret_cast<UINT_PTR>(windows),ui::tr(L"Move tab to window"));
  auto spaces=CreatePopupMenu();std::vector<std::string> destinations;
  if(agent_owned)item(spaces,0,ui::tr(L"An agent controls this tab. Take ownership to move it."),false);
  for(const auto& [workspace_id,name]:shell.workspace_names)if(workspace_id!=host.workspace&&!shell.private_workspaces.contains(workspace_id)&&destinations.size()<64){
    destinations.push_back(workspace_id);item(spaces,TabToWorkspace+static_cast<int>(destinations.size())-1,ui::wide(name),shell.broker.tab_move_blocker(id,workspace_id).empty());}
  if(destinations.empty())item(spaces,0,ui::tr(L"No other workspace"),false);
  AppendMenuW(menu_handle,MF_POPUP|(host.metadata.value("private",false)?MF_GRAYED:0),reinterpret_cast<UINT_PTR>(spaces),ui::tr(L"Move tab to workspace"));
  AppendMenuW(menu_handle,MF_SEPARATOR,0,nullptr);
  if(state.value("attentionRequested",false))item(menu_handle,TabDismissHelp,ui::tr(L"Dismiss help request"));
  item(menu_handle,TabTakeOwnership,ui::tr(L"Take ownership"),owner!="human");item(menu_handle,TabControls,ui::tr(L"Xenon Controls…"));AppendMenuW(menu_handle,MF_SEPARATOR,0,nullptr);
  // Chromium cannot end a page's capture on its own; reloading the page does.
  if(host.metadata.value("screenCapture",false)){item(menu_handle,TabStopSharing,ui::tr(L"Stop sharing your screen (reloads the page)"));AppendMenuW(menu_handle,MF_SEPARATOR,0,nullptr);}
  item(menu_handle,TabClose,ui::tr(L"Close tab\tCtrl+W"));item(menu_handle,TabCloseOthers,ui::tr(L"Close other tabs in workspace"));item(menu_handle,TabCloseBelow,ui::tr(L"Close tabs below"));
  item(menu_handle,TabCloseWorkspace,ui::format(ui::tr(L"Close all tabs in {0}…"),{ui::wide(shell.workspace_names.contains(host.workspace)?shell.workspace_names.at(host.workspace):host.workspace)}));
  const auto chosen=TrackPopupMenu(menu_handle,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RIGHTBUTTON,at.x,at.y,0,window,nullptr);DestroyMenu(menu_handle);
  if(!chosen||!shell.hosts.contains(id))return;
  const auto workspace_id=host.workspace;
  const auto same_group=[&]{std::vector<std::string> tabs;for(const auto& tab:visual_tabs())if(shell.hosts.at(tab).workspace==workspace_id)tabs.push_back(tab);return tabs;};
  try{
    if(chosen==TabNewBelow){const auto tabs=same_group();auto next=std::find(tabs.begin(),tabs.end(),id);open_tab(workspace_id,"about:blank",next!=tabs.end()&&std::next(next)!=tabs.end()?*std::next(next):std::string{});}
    else if(chosen==TabReload||chosen==TabStopSharing)shell.engine.native_command(id,"reload");
    else if(chosen==TabDuplicate){const auto tabs=same_group();auto next=std::find(tabs.begin(),tabs.end(),id);open_tab(workspace_id,url,next!=tabs.end()&&std::next(next)!=tabs.end()?*std::next(next):std::string{});}
    else if(chosen==TabPin){host.pinned=!host.pinned;tree_signature=nullptr;rebuild_tree();}
    else if(chosen==TabMute)shell.engine.native_command(id,host.metadata.value("muted",false)?"unmute":"mute");
    else if(chosen==TabCopyLink)copy_text(window,url);
    else if(chosen==TabBookmark)set_status(shell.data.bookmark(workspace_id,host.metadata.value("private",false),url,str(host.metadata,"title"))?ui::tr(L"Bookmark saved."):ui::tr(L"Bookmark could not be saved."));
    else if(chosen==TabNewWindow){if(auto frame=shell.new_window(this))shell.move_host(host,*frame,{});}
    else if(chosen>=TabToWindow&&chosen<TabToWindow+32&&static_cast<size_t>(chosen-TabToWindow)<targets.size()){if(auto frame=shell.frame_of(targets[static_cast<size_t>(chosen-TabToWindow)]))shell.move_host(host,*frame,{});}
    else if(chosen>=TabToWorkspace&&chosen<TabToWorkspace+64&&static_cast<size_t>(chosen-TabToWorkspace)<destinations.size())shell.move_to_workspace(*this,id,destinations[static_cast<size_t>(chosen-TabToWorkspace)],{});
    else if(chosen==TabDismissHelp)shell.broker.dismiss_attention(id);
    else if(chosen==TabTakeOwnership){shell.broker.human_acquire(id);set_status(ui::tr(L"Ownership requested. Current agent input finishes first."));}
    else if(chosen==TabControls)shell.native.show_section(NativeUi::Section::workspaces,workspace_id);
    else if(chosen==TabClose)shell.engine.native_command(id,"close");
    else if(chosen==TabCloseOthers){std::vector<std::string> ids;for(const auto& tab:same_group())if(tab!=id&&!shell.hosts.at(tab).pinned)ids.push_back(tab);shell.close_tabs(*this,ids);}
    else if(chosen==TabCloseBelow){std::vector<std::string> ids;bool after=false;for(const auto& tab:same_group()){if(after)ids.push_back(tab);if(tab==id)after=true;}shell.close_tabs(*this,ids);}
    else if(chosen==TabCloseWorkspace)shell.close_workspace(*this,workspace_id);
  }catch(...){set_status(ui::tr(L"The browser action could not be completed. Existing data is preserved."));}
}
void BrowserShell::Impl::Frame::group_menu(const std::string& workspace_id,POINT at){
  const auto item=[&]{for(auto group=TreeView_GetRoot(tree);group;group=TreeView_GetNextSibling(tree,group))if(key_of(group).first==workspace_id)return group;return HTREEITEM{};}();
  TVITEMW value{};value.hItem=item;value.mask=TVIF_STATE;value.stateMask=TVIS_EXPANDED;if(item)TreeView_GetItem(tree,&value);const bool expanded=(value.state&TVIS_EXPANDED)!=0;
  const auto count=shell.workspace_tab_count(workspace_id);const bool private_mode=shell.private_workspaces.contains(workspace_id);
  auto menu_handle=CreatePopupMenu();
  AppendMenuW(menu_handle,MF_STRING,GroupNewTab,ui::tr(L"New tab in this workspace"));
  AppendMenuW(menu_handle,MF_STRING|(item&&TreeView_GetChild(tree,item)?0:MF_GRAYED),GroupToggle,expanded?ui::tr(L"Collapse"):ui::tr(L"Expand"));
  AppendMenuW(menu_handle,MF_SEPARATOR,0,nullptr);
  AppendMenuW(menu_handle,MF_STRING|(count?0:MF_GRAYED),GroupCloseAll,ui::format(ui::tr(L"Close all {0} tabs…"),{std::to_wstring(count)}).c_str());
  AppendMenuW(menu_handle,MF_STRING|(private_mode?MF_GRAYED:0),GroupConfigure,ui::tr(L"Workspace settings…"));
  const auto chosen=TrackPopupMenu(menu_handle,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RIGHTBUTTON,at.x,at.y,0,window,nullptr);DestroyMenu(menu_handle);
  if(chosen==GroupNewTab)open_tab(workspace_id,"about:blank");
  else if(chosen==GroupToggle&&item){TreeView_Expand(tree,item,expanded?TVE_COLLAPSE:TVE_EXPAND);InvalidateRect(window,nullptr,FALSE);}
  else if(chosen==GroupCloseAll)shell.close_workspace(*this,workspace_id);
  else if(chosen==GroupConfigure)shell.native.show_section(NativeUi::Section::workspaces,workspace_id);
}
void BrowserShell::Impl::Frame::pick_developer(){
  if(developer_picking){shell.engine.developer_pick(selected,false,[](Json){});return;}
  if(!developer_visible||developer_nodes.empty())return;
  developer_picking=true;developer_tab=selected;
  position_developer_overlay();
  SetWindowTextW(controls.at(DeveloperPick),ui::tr(L"Cancel picking"));
  SetWindowTextW(controls.at(DeveloperStatus),ui::tr(L"Click a page element to select it. Escape cancels."));
  const auto tab=selected;const auto target=window;const auto generation=developer_generation;auto owner=&shell;
  std::weak_ptr<Mailbox> weak=shell.mailbox;const auto messages=shell.messages;
  shell.engine.developer_pick(tab,true,[weak,messages,owner,target,tab,generation](Json value){if(auto box=weak.lock()){
    {std::lock_guard lock(box->mutex);box->calls.push_back([owner,target,tab,generation,value=std::move(value)]{
      auto frame=owner->frame_of(target);if(!frame||frame->selected!=tab||frame->developer_generation!=generation)return;
      frame->developer_picking=false;SetWindowTextW(frame->controls.at(DeveloperPick),ui::tr(L"Pick from page"));
      frame->position_developer_overlay();
      if(!value.value("ok",false)){SetWindowTextW(frame->controls.at(DeveloperStatus),ui::tr(L"Selection canceled or unavailable. Choose a listed element."));return;}
      const auto diagnostic=str(value["result"],"diagnosticId");
      if(frame->developer_controls_only){frame->developer_controls_only=false;SendMessageW(frame->controls.at(DeveloperFilter),BM_SETCHECK,BST_UNCHECKED,0);frame->developer_list();}
      frame->developer_selection.clear();
      auto list=frame->controls.at(DeveloperList);
      for(size_t i=0;i<frame->developer_rows.size();++i)if(str(frame->developer_rows[i],"diagnosticId")==diagnostic){
        ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);ListView_SetItemState(list,static_cast<int>(i),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        ListView_EnsureVisible(list,static_cast<int>(i),FALSE);SetFocus(list);frame->developer_details();break;
      }
    });}PostMessageW(messages,ShellRun,0,0);
  }});
}
void BrowserShell::Impl::Frame::hover_developer(const POINT* event){
  if(!developer_visible||developer_picking||resizing_developer||developer_nodes.empty())return;
  const auto host=shell.hosts.find(selected);if(host==shell.hosts.end()||!host->second.browser)return;
  // Read only this window's current hover, without intercepting page messages.
  // Chromium child windows may receive input outside our message-hook thread.
  if(event){developer_hover_at=*event;developer_hover_dirty=true;++developer_hover_version;}
  else {POINT screen{};GetCursorPos(&screen);const auto under=WindowFromPoint(screen);
  if(GetAncestor(GetForegroundWindow(),GA_ROOT)!=window||!(under==host->second.browser||IsChild(host->second.browser,under)||under==developer_overlay)||(GetAsyncKeyState(VK_LBUTTON)&0x8000)||(GetAsyncKeyState(VK_RBUTTON)&0x8000)||(GetAsyncKeyState(VK_MBUTTON)&0x8000)){
    if(developer_hover_inside)++developer_hover_version;developer_hover_inside=false;developer_hover_dirty=false;return;
  }
  if(!developer_hover_inside||screen.x!=developer_hover_at.x||screen.y!=developer_hover_at.y){developer_hover_at=screen;developer_hover_dirty=true;++developer_hover_version;}developer_hover_inside=true;}
  if(developer_hover_pending||!developer_hover_dirty||Clock::now()<developer_hover_next)return;
  const auto context=current_developer_context();if(context.is_null()||context.value("protected",false)||context.value("busy",false)||context.value("private",false))return;
  POINT point=developer_hover_at;ScreenToClient(host->second.browser,&point);const double scale=GetDpiForWindow(host->second.browser)/96.0*host->second.metadata.value("zoom",1.0);
  developer_hover_pending=true;developer_hover_dirty=false;developer_hover_next=Clock::now()+std::chrono::milliseconds(120);
  const auto generation=developer_generation,version=developer_hover_version;const auto tab=selected;const auto target=window;auto owner=&shell;std::weak_ptr<Mailbox> weak=shell.mailbox;const auto messages=shell.messages;
  shell.engine.developer_hover_point(tab,point.x/scale,point.y/scale,[weak,messages,owner,target,tab,generation,version,context](Json value){if(auto box=weak.lock()){
    {std::lock_guard lock(box->mutex);box->calls.push_back([owner,target,tab,generation,version,context,value=std::move(value)]{
      auto frame=owner->frame_of(target);if(!frame||frame->selected!=tab||frame->developer_generation!=generation)return;
      frame->developer_hover_pending=false;
      if(frame->developer_hover_version!=version||frame->current_developer_context()!=context||!frame->developer_visible||frame->developer_picking)return;
      frame->developer_hover_error=value.value("ok",false)?std::string{}:value.dump();
      if(!value.value("ok",false))return;
      frame->select_developer(str(value["result"],"diagnosticId"));
    });}PostMessageW(messages,ShellRun,0,0);
  }});
}
void BrowserShell::Impl::Frame::refresh_developer(){
  const bool enabled=shell.broker.developer_mode_enabled();
  if(enabled!=developer_visible){developer_visible=enabled;clear_developer(ui::tr(L"Waiting for rendered page evidence…"));layout();}
  if(!enabled)return;
  developer_details(false);
  position_developer_overlay();
  const auto context=current_developer_context();
  if(developer_identity.changed(context))clear_developer(ui::tr(L"Waiting for rendered page evidence…"));
  if(context.is_null()||context.value("protected",false)||context.value("busy",false)||context.value("private",false)){
    if(!developer_nodes.empty())clear_developer(ui::tr(L"Page evidence is unavailable for this tab right now."));
    SetWindowTextW(controls.at(DeveloperStatus),ui::tr(L"Page evidence is unavailable for this tab right now."));return;
  }
  if(developer_picking||resizing_developer)return;
  hover_developer();
  dispatch_developer_highlight();
  if(developer_pending||Clock::now()<developer_next||IsIconic(window)||!IsWindowVisible(window))return;
  developer_pending=true;developer_next=Clock::now()+std::chrono::milliseconds(1500);
  const auto generation=developer_generation;const auto tab=selected;const auto target=window;
  std::weak_ptr<Mailbox> weak=shell.mailbox;const auto messages=shell.messages;auto owner=&shell;
  shell.engine.developer_elements(tab,[weak,messages,owner,target,tab,generation,context](Json value){
    if(auto box=weak.lock()){
      {std::lock_guard lock(box->mutex);box->calls.push_back([owner,target,tab,generation,context,value=std::move(value)]{
        auto frame=owner->frame_of(target);
        if(!frame||!frame->developer_visible||!owner->broker.developer_mode_enabled()||frame->selected!=tab||
          !frame->developer_identity.matches(generation,frame->current_developer_context())||frame->current_developer_context()!=context)return;
        frame->developer_pending=false;
        if(!value.value("ok",false)){frame->developer_nodes=Json::array();frame->developer_list();
          SetWindowTextW(frame->controls.at(DeveloperStatus),ui::tr(L"Page evidence is unavailable for this tab right now."));return;}
        const auto& result=value["result"];const auto& nodes=result["nodes"];
        frame->developer_visibility=result.value("visibility",Json::object());
        const auto viewport=result.value("viewport",Json::object());
        const bool moved=viewport!=frame->developer_viewport;
        frame->developer_viewport=viewport;
        const bool changed=nodes!=frame->developer_nodes;
        // Scrolling can move a different element under a stationary cursor,
        // even when document coordinates and the admitted node set are equal.
        if(moved||changed){++frame->developer_hover_version;frame->developer_hover_dirty=frame->developer_hover_inside;
          frame->developer_box=Json::object();if(frame->developer_overlay)ShowWindow(frame->developer_overlay,SW_HIDE);}
        if(changed){frame->developer_nodes=nodes;frame->developer_list();}
        const auto host=owner->hosts.find(tab);
        if(!frame->developer_picking&&!frame->developer_selection.empty()&&(moved||changed||host==owner->hosts.end()||str(host->second.metadata,"developerHighlighted")!=frame->developer_selection))frame->highlight_developer(frame->developer_selection);
        size_t interactive=0;for(const auto& node:nodes)if(!developer_element_actions(node).empty())++interactive;
        const auto status=std::to_wstring(nodes.size())+L" "+ui::tr(L"elements")+L" · "+std::to_wstring(interactive)+L" "+ui::tr(L"interactable")+
          (result.value("truncated",false)?L" · "+std::wstring(ui::tr(L"Truncated")):L"")+
          (str(result,"coverage")=="partial"?L" · "+std::wstring(ui::tr(L"Partial coverage")):L"");
        SetWindowTextW(frame->controls.at(DeveloperStatus),status.c_str());
      });}
      PostMessageW(messages,ShellRun,0,0);
    }
  });
}
void BrowserShell::Impl::Frame::command(int id){try{
  auto& engine=shell.engine;auto& hosts=shell.hosts;
  if(id==Back||id==Forward||id==Reload)engine.native_command(selected,id==Back?"back":id==Forward?"forward":hosts.contains(selected)&&hosts.at(selected).metadata.value("loading",false)?"stop":"reload");
  else if(id==Go)submit();else if(id==Controls)engine.show_controls();else if(id==Menu)menu();else if(id==Extensions)extensions_menu();
  else if(id==Passwords){if(hosts.contains(selected))shell.native.request_autofill(selected);}
  // A click on the button while its bubble is open first closes the bubble
  // by deactivating it; that click must not reopen it.
  else if(id==SiteInfo){if(site_bubble)close_site();else if(GetTickCount64()-site_closed>300)open_site();}
  else if(id==BookmarkStar){auto found=hosts.find(selected);if(found!=hosts.end()&&!found->second.metadata.value("protected",false)){const auto url=str(found->second.metadata,"url");
      if(bookmarked)set_status(shell.data.remove_bookmark(workspace(),is_private(),url)?ui::tr(L"Bookmark removed."):ui::tr(L"Bookmark could not be removed."));
      else set_status(shell.data.bookmark(workspace(),is_private(),url,str(found->second.metadata,"title"))?ui::tr(L"Bookmark saved."):ui::tr(L"Bookmark could not be saved."));
      checked_url.clear();refresh_page_state();}}
  else if(id==NewTab||id==MenuNewTab)open_tab(workspace(),"about:blank");
  else if(id==MenuNewWindow){if(auto frame=shell.new_window(this))frame->open_tab(workspace(),"about:blank");}
  else if(id==MenuCloseWindow)PostMessageW(window,WM_CLOSE,0,0);
  else if(id==MenuPrivate)shell.broker.open_human_workspace("about:blank",[](Json){},true);
  else if(id==MenuCloseTab)engine.native_command(selected,"close");
  else if(id==MenuReopenTab){if(!shell.closed_tabs.empty()){const auto closed=shell.closed_tabs.back();shell.closed_tabs.pop_back();open_tab(closed.workspace,closed.url);}}
  else if(id==MenuCloseWorkspaceTabs)shell.close_workspace(*this,workspace());
  else if(id==MenuManageExtensions)shell.native.show_section(NativeUi::Section::extensions);
  else if(id==MenuPasswords){auto found=hosts.find(selected);shell.native.show_section(NativeUi::Section::passwords,found==hosts.end()?std::string{}:origin_of(str(found->second.metadata,"url")));}
  else if(id==MenuUpdates)engine.show_updates();
  else if(id==MenuDeveloper){if(!shell.broker.developer_mode_enabled())shell.broker.set_developer_mode(true);refresh_developer();shell.native.show_section(NativeUi::Section::developer);}
  else if(id==MenuDocumentation||(id==MenuQuickTour&&ui::show_introduction(window,false)==ui::IntroductionResult::documentation))open_tab("native-default",ui::utf8(ui::documentation_url));
  else if(id==MenuFullscreen)toggle_fullscreen();
  else if(id==MenuSettings||id==MenuClearData||id==MenuImport)shell.panel(*this,id==MenuSettings?"settings":id==MenuClearData?"clear":"import");
  else if(id==MenuFind)show_find();else if(id==FindClose)hide_find();
  else if(id==FindNext||id==FindPrevious)engine.native_command(selected,id==FindNext?"find-next":"find-previous",ui::text(find_text));
  else if(id==MenuZoomIn||id==MenuZoomOut||id==MenuZoomReset){double zoom=hosts.contains(selected)?hosts.at(selected).metadata.value("zoom",1.0):1;zoom=id==MenuZoomReset?1:id==MenuZoomIn?zoom*1.2:zoom/1.2;engine.native_command(selected,"zoom",std::to_string(zoom));}
  else if(id==MenuBookmark){auto found=hosts.find(selected);if(found!=hosts.end()&&!found->second.metadata.value("protected",false)){set_status(shell.data.bookmark(workspace(),is_private(),str(found->second.metadata,"url"),str(found->second.metadata,"title"))?ui::tr(L"Bookmark saved."):ui::tr(L"Bookmark could not be saved."));checked_url.clear();}}
  else if(id==MenuBookmarks||id==MenuHistory||id==MenuDownloads||id==MenuPermissions||id==MenuAbout||id==MenuNotices)shell.panel(*this,id==MenuBookmarks?"bookmarks":id==MenuHistory?"history":id==MenuDownloads?"downloads":id==MenuPermissions?"site":id==MenuNotices?"notices":"about");
  else if(id==MenuPrint)engine.native_command(selected,"print");
  else if(id==MenuPdf){wchar_t path[32768]=L"Xenon page.pdf";OPENFILENAMEW picker{};picker.lStructSize=sizeof(picker);picker.hwndOwner=window;picker.lpstrFile=path;picker.nMaxFile=32768;picker.lpstrFilter=ui::pdf_filter();picker.lpstrDefExt=L"pdf";picker.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetSaveFileNameW(&picker))engine.native_command(selected,"pdf",ui::utf8(path),[&shell=shell,target=window](Json value){const bool ok=value.value("ok",false);shell.post([&shell,target,ok]{if(auto frame=shell.frame_of(target))frame->set_status(ok?ui::tr(L"PDF saved."):ui::tr(L"PDF could not be saved."));});});}
  else if(id==MenuThemeSystem||id==MenuThemeLight||id==MenuThemeDark){if(!ui::save_theme(id==MenuThemeDark?ui::ThemeMode::dark:id==MenuThemeLight?ui::ThemeMode::light:ui::ThemeMode::system))set_status(ui::tr(L"Theme changed for this run; saving failed."));
    else if(ui::theme_mode!=ui::web_theme_mode)set_status(ui::tr(L"Theme saved. Web pages use it after Xenon restarts."));shell.theme_all();}
  else if(id==MenuSidebarNarrower||id==MenuSidebarWider||id==MenuSidebarReset){resize_sidebar(id==MenuSidebarReset?240:sidebar+(id==MenuSidebarWider?32:-32));shell.save_sidebar(*this);}
  else if(id==MenuLanguageEnglish||id==MenuLanguageChinese){
    if(!ui::save_language(id==MenuLanguageChinese?ui::Language::simplified_chinese:ui::Language::english))set_status(ui::tr(L"The language preference could not be saved. Try again."));
    else {const std::wstring message=ui::preferred_language==ui::language?ui::tr(L"Language preference saved."):ui::tr(L"Restart Xenon to apply the selected language. Save unfinished website work before exiting.");set_status(message);
      if(ui::preferred_language!=ui::language)MessageBoxW(window,message.c_str(),L"Language / 语言",MB_OK|MB_ICONINFORMATION);}
  }
  else if(id==MenuExit)shell.request_exit();
  InvalidateRect(window,nullptr,FALSE);
}catch(...){set_status(ui::tr(L"The browser action could not be completed. Existing data is preserved."));}}
void BrowserShell::Impl::panel(Frame& frame,const std::string& kind,const std::string& description,std::function<void(bool)> answer,const std::string& source_tab,const Panel* like){
  if(prompt_kind(kind)&&std::count_if(panels.begin(),panels.end(),[](const auto& panel){return prompt_kind(panel->kind)&&panel->window;})>=8){if(answer)answer(false);return;}
  auto panel=std::make_unique<Panel>();panel->owner=this;panel->parent=frame.window;panel->kind=kind;panel->workspace=frame.workspace();panel->private_mode=frame.is_private();panel->tab=source_tab.empty()?frame.selected:source_tab;if(const auto host=hosts.find(panel->tab);host!=hosts.end()){panel->workspace=host->second.workspace;panel->private_mode=host->second.metadata.value("private",false);}panel->answer=std::move(answer);
  if(like){panel->workspace=like->workspace;panel->private_mode=like->private_mode;panel->tab=like->tab;panel->parent=like->parent;}
  // One library or form window per workspace; asking again brings it forward.
  if(library_kind(kind)||form_kind(kind))for(const auto& open:panels)if(open->window&&open->kind==kind&&(kind=="settings"||open->workspace==panel->workspace)){
    if(IsIconic(open->window))ShowWindow(open->window,SW_RESTORE);SetForegroundWindow(open->window);return;}
  auto raw=panel.get();panels.push_back(std::move(panel));
  const auto title=ui::wide(kind=="permission"?ui::tr8("Website permission request"):kind=="external"?ui::tr8("Open link in another app"):kind=="bookmarks"?ui::tr8("Xenon Bookmarks"):kind=="history"?ui::tr8("Xenon History"):kind=="downloads"?ui::tr8("Xenon Downloads"):kind=="site"?ui::tr8("Xenon Site Settings"):kind=="cookies"?ui::tr8("Xenon Cookies and Site Data"):kind=="notices"?ui::tr8("Xenon Third-party Notices"):
    kind=="settings"?ui::tr8("Xenon Settings"):kind=="import"?ui::tr8("Import bookmarks and history"):kind=="clear"?ui::tr8("Clear browsing data"):ui::tr8("About Xenon"));
  CreateWindowExW(0,library_kind(kind)?L"XenonLibrary":form_kind(kind)?L"XenonForm":L"XenonBrowserPanel",title.c_str(),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,780,480,frame.window,nullptr,branding_module(),raw);
  // Sizes are in DIPs of the monitor the panel opened on, so every row fits on scaled displays.
  const auto size=library_kind(kind)?SIZE{900,600}:kind=="settings"?SIZE{640,640}:kind=="import"?SIZE{600,440}:kind=="clear"?SIZE{560,470}:kind=="site"?SIZE{660,520}:SIZE{780,480};
  if(raw->window)SetWindowPos(raw->window,nullptr,0,0,ui::dip(raw->window,size.cx),ui::dip(raw->window,size.cy),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
  if(!description.empty())SetWindowTextW(raw->message,ui::wide(description).c_str());ShowWindow(raw->window,prompt_kind(kind)?SW_SHOWNOACTIVATE:SW_SHOW);
}
void BrowserShell::Impl::load_site(Panel* raw,std::wstring notice){const auto window=raw->window;
  engine.site_info(raw->tab,[this,raw,window,notice](Json value){post([this,raw,window,notice,value=std::move(value)]{if(panel_alive(raw,window))fill_site(*raw,value,notice);});});}
void BrowserShell::Impl::fill_site(Panel& target,const Json& value,const std::wstring& notice){
  const auto h=target.window;const auto result=value.value("result",Json::object());target.info=result;target.origin=str(result,"origin");
  const bool usable=value.value("ok",false)&&!target.origin.empty();const auto workspace=workspace_names.contains(target.workspace)?workspace_names.at(target.workspace):target.workspace;
  std::wstring text;
  if(!value.value("ok",false))text=ui::error_text(str(value.value("error",Json::object()),"message"));
  else if(str(result,"state")=="protected")text=ui::tr(L"Site controls are unavailable while a protected sign-in is in progress.");
  else if(target.origin.empty())text=ui::tr(L"Site settings are available for websites.");
  else{text=ui::wide(ui::tr8("Site: ")+target.origin)+L"\n"+ui::wide(ui::tr8("Workspace: ")+workspace)+L" · "+Frame::cookie_count(result.value("cookieCount",0));
    if(result.value("storageBytes",0.0)>0)text+=L" · "+ui::format(ui::tr(L"Stored on this device: {0}"),{ui::wide(site::bytes_text(static_cast<uint64_t>(std::max(0.0,result.value("storageBytes",0.0)))))});
    text+=L"\n"+std::wstring(target.kind=="cookies"?ui::tr(L"Cookie values are never shown."):ui::tr(L"Settings apply to this site in this workspace only."));}
  if(!notice.empty())text=notice+L"\n"+text;
  SetWindowTextW(target.message,text.c_str());
  if(target.list){SendMessageW(target.list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(target.list);target.rows=usable?result.value("cookies",Json::array()):Json::array();int index=0;
    for(const auto& row:target.rows){auto name=omnibox::printable(ui::wide(str(row,"name")));LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=index;item.pszText=name.data();ListView_InsertItem(target.list,&item);
      auto domain=omnibox::printable(ui::wide(str(row,"domain"))),path=omnibox::printable(ui::wide(str(row,"path")));
      auto expires=row.value("session",false)?std::wstring(ui::tr(L"Session")):Frame::date_text(row.value("expires",int64_t{}));
      std::wstring flags;if(row.value("secure",false))flags=ui::tr(L"Secure");if(row.value("httpOnly",false))flags+=(flags.empty()?L"":L", ")+std::wstring(L"HttpOnly");
      ListView_SetItemText(target.list,index,1,domain.data());ListView_SetItemText(target.list,index,2,path.data());ListView_SetItemText(target.list,index,3,expires.data());ListView_SetItemText(target.list,index,4,flags.data());++index;}
    SendMessageW(target.list,WM_SETREDRAW,TRUE,0);InvalidateRect(target.list,nullptr,TRUE);}
  if(target.kind=="site"){const auto settings=result.value("settings",Json::object());
    for(size_t n=0;n<std::size(site::settings);++n)if(auto combo=GetDlgItem(h,SitePanelSetting+static_cast<int>(n))){const auto current=str(settings,site::settings[n].name);
      SendMessageW(combo,CB_SETCURSEL,current=="allow"?1:current=="block"?2:0,0);EnableWindow(combo,usable);}}
  for(int id:{SitePanelLeft,SitePanelMiddle,SitePanelRight})if(auto button=GetDlgItem(h,id))EnableWindow(button,usable&&(target.kind!="cookies"||id!=SitePanelLeft||!target.rows.empty()));
}
void BrowserShell::Impl::site_panel_command(Panel& target,int id){Panel* raw=&target;const auto window=target.window;
  const auto done=[this,raw,window](std::wstring success){return [this,raw,window,success=std::move(success)](Json value){post([this,raw,window,success,value=std::move(value)]{if(!panel_alive(raw,window))return;
    load_site(raw,value.value("ok",false)?success:ui::error_text(str(value.value("error",Json::object()),"message")));});};};
  if(id==SitePanelMiddle){if(auto frame=frame_of(target.parent))panel(*frame,"cookies",{},{},target.tab);return;}
  if(id==SitePanelLeft&&target.kind=="site"){engine.reset_site_settings(target.tab,target.origin,done(ui::tr(L"Site settings reset.")));return;}
  if(id==SitePanelLeft){std::vector<std::string> keys;
    for(int index=ListView_GetNextItem(target.list,-1,LVNI_SELECTED);index>=0;index=ListView_GetNextItem(target.list,index,LVNI_SELECTED))if(static_cast<size_t>(index)<target.rows.size())keys.push_back(str(target.rows[index],"key"));
    // An empty key list would mean every cookie of the site.
    if(!keys.empty())engine.delete_site_cookies(target.tab,target.origin,keys,done(ui::tr(L"Selected cookies deleted.")));return;}
  const auto workspace=workspace_names.contains(target.workspace)?workspace_names.at(target.workspace):target.workspace;
  if(MessageBoxW(window,ui::format(ui::tr(L"Delete all cookies and stored data for {0} in {1}?\n\nYou may be signed out of this site, and its open pages may need to reload. Agent input in this tab pauses briefly, and agents must observe the page again."),
      {ui::wide(target.origin),ui::wide(workspace)}).c_str(),ui::tr(L"Delete site data"),MB_OKCANCEL|MB_ICONWARNING|MB_DEFBUTTON2)!=IDOK||!panel_alive(raw,window))return;
  engine.clear_site_data(target.tab,target.origin,done(ui::tr(L"Site data deleted. Reload the page to apply.")));
}
void BrowserShell::Impl::panel_setting(Panel& target,size_t index,HWND combo){const auto choice=SendMessageW(combo,CB_GETCURSEL,0,0);if(choice<0||choice>2||index>=std::size(site::settings))return;
  constexpr const char* values[]{"default","allow","block"};Panel* raw=&target;const auto window=target.window;
  engine.set_site_setting(target.tab,target.origin,site::settings[index].name,values[choice],[this,raw,window](Json value){if(value.value("ok",false))return;
    post([this,raw,window,value=std::move(value)]{if(panel_alive(raw,window))load_site(raw,ui::error_text(str(value.value("error",Json::object()),"message")));});});}
LRESULT CALLBACK BrowserShell::Impl::panel_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){try{auto panel=reinterpret_cast<Panel*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(message==WM_NCCREATE){panel=static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);panel->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(panel));}if(!panel)return DefWindowProcW(h,message,wp,lp);auto owner=panel->owner;
  LRESULT result{};if(ui::list_draw(message,wp,lp,h,result)||ui::ctl_color(message,wp,lp,result))return result;
  if(message==WM_CREATE){panel->font=ui::font(h);ui::icons(h);auto make=[&](int id,const wchar_t* type,const wchar_t* text,DWORD style){auto control=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,10,10,h,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),branding_module(),nullptr);SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(panel->font),TRUE);ui::control_theme(control);return control;};
    // Site panels use button IDs beyond IDOK/IDCANCEL, so Escape closes them
    // instead of pressing "Delete all site data".
    const bool site_panel=panel->kind=="site"||panel->kind=="cookies";
    panel->message=make(10,L"STATIC",L"",SS_LEFT|(site_panel||prompt_kind(panel->kind)?SS_NOPREFIX:0));
    if(!site_panel){make(1,L"BUTTON",panel->kind=="permission"?ui::tr(L"Allow"):panel->kind=="external"?ui::tr(L"Open app"):ui::tr(L"Open selected"),BS_OWNERDRAW|WS_TABSTOP);
      make(2,L"BUTTON",panel->kind=="permission"?ui::tr(L"Deny"):panel->kind=="external"?ui::tr(L"Cancel"):ui::tr(L"Close"),BS_OWNERDRAW|WS_TABSTOP);}
    const bool listing=panel->kind=="downloads";
    if(listing){panel->list=make(11,WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|WS_TABSTOP|WS_BORDER);ListView_SetExtendedListViewStyle(panel->list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
      auto colors=ui::palette();ListView_SetBkColor(panel->list,colors.canvas);ListView_SetTextBkColor(panel->list,colors.canvas);ListView_SetTextColor(panel->list,colors.ink);
      LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=220;column.pszText=const_cast<LPWSTR>(panel->kind=="downloads"?ui::tr(L"Name"):ui::tr(L"Title"));ListView_InsertColumn(panel->list,0,&column);column.cx=460;column.pszText=const_cast<LPWSTR>(panel->kind=="downloads"?ui::tr(L"Status / bytes"):L"URL");ListView_InsertColumn(panel->list,1,&column);
      for(const auto& tab:owner->engine.native_tabs())if(str(tab,"workspaceId")==panel->workspace){owner->engine.execute("files.downloads",{{"workspaceId",panel->workspace}},[panel](Json value){if(value.value("ok",false))panel->rows=value["result"]["downloads"];});break;}ShowWindow(GetDlgItem(h,1),SW_HIDE);
      int index=0;for(const auto& row:panel->rows){auto title=ui::wide(str(row,panel->kind=="downloads"?"name":"title"));LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=index;item.pszText=title.data();ListView_InsertItem(panel->list,&item);
        auto detail=panel->kind=="downloads"?std::wstring(row.value("complete",false)?ui::tr(L"Complete"):row.value("canceled",false)?ui::tr(L"Canceled"):ui::tr(L"Downloading"))+L" · "+std::to_wstring(row.value("receivedBytes",uint64_t{}))+ui::tr(L" bytes"):ui::wide(str(row,"url"));ListView_SetItemText(panel->list,index++,1,detail.data());}
      SetWindowTextW(panel->message,ui::wide(ui::tr8("Workspace: ")+(owner->workspace_names.contains(panel->workspace)?owner->workspace_names[panel->workspace]:panel->workspace)).c_str());
    }else if(panel->kind=="about"){SetWindowTextW(panel->message,ui::wide(std::string(ui::tr8("Xenon Browser\n"))+std::string(kVersion)+ui::tr8("\n\nA local browser for people and MCP agents.\nCEF supplies the sandboxed web engine.\n\nThird-party licenses and notices are available from the Menu.")).c_str());ShowWindow(GetDlgItem(h,1),SW_HIDE);}
    else if(panel->kind=="notices"){wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);const auto directory=std::filesystem::path(module).parent_path();std::string notice;for(const auto& name:{"THIRD_PARTY_NOTICES.md","CEF-LICENSE.txt","NOTICE"}){auto file=directory/name;if(!std::filesystem::exists(file))file=std::filesystem::path(name);if(std::filesystem::exists(file)&&std::filesystem::file_size(file)<1024*1024){std::ifstream input(file);notice.append(std::istreambuf_iterator<char>(input),{});notice+="\n\n";}}
      if(notice.empty())notice="Xenon: Apache License 2.0.\nCEF and Chromium: see CEF-LICENSE.txt and Chromium-CREDITS.html beside Xenon.exe.\nSQLite: public domain. nlohmann/json: MIT.\nMCP SDK: MIT.";
      DestroyWindow(panel->message);panel->message=make(10,L"EDIT",ui::wide(notice).c_str(),ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_TABSTOP);SetWindowTextW(GetDlgItem(h,1),ui::tr(L"Open engine credits"));EnableWindow(GetDlgItem(h,1),std::filesystem::is_regular_file(directory/L"Chromium-CREDITS.html"));}
    else if(site_panel){
      make(SitePanelLeft,L"BUTTON",panel->kind=="site"?ui::tr(L"Reset permissions"):ui::tr(L"Delete selected"),BS_OWNERDRAW|WS_TABSTOP);make(SitePanelRight,L"BUTTON",ui::tr(L"Delete all site data…"),BS_OWNERDRAW|WS_TABSTOP);
      if(panel->kind=="cookies"){panel->list=make(11,WC_LISTVIEWW,L"",LVS_REPORT|LVS_SHOWSELALWAYS|WS_TABSTOP|WS_BORDER);ListView_SetExtendedListViewStyle(panel->list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
        auto colors=ui::palette();ListView_SetBkColor(panel->list,colors.canvas);ListView_SetTextBkColor(panel->list,colors.canvas);ListView_SetTextColor(panel->list,colors.ink);int column_index=0;
        for(const auto& [width,title]:std::vector<std::pair<int,const wchar_t*>>{{170,ui::tr(L"Name")},{170,ui::tr(L"Domain")},{80,ui::tr(L"Path")},{110,ui::tr(L"Expires")},{120,ui::tr(L"Flags")}}){
          LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=ui::dip(h,width);column.pszText=const_cast<LPWSTR>(title);ListView_InsertColumn(panel->list,column_index++,&column);}}
      else{make(SitePanelMiddle,L"BUTTON",ui::tr(L"Manage cookies…"),BS_OWNERDRAW|WS_TABSTOP);
        for(size_t n=0;n<std::size(site::settings);++n){const auto& item=site::settings[n];make(SitePanelLabel+static_cast<int>(n),L"STATIC",ui::tr(item.label),SS_LEFT|SS_CENTERIMAGE|SS_NOPREFIX);
          auto combo=make(SitePanelSetting+static_cast<int>(n),L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
          for(const char* choice:{"default","allow","block"})SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(ui::tr(site::value_label(item.name,choice))));EnableWindow(combo,FALSE);}}
      for(int id:{SitePanelLeft,SitePanelMiddle,SitePanelRight})if(auto button=GetDlgItem(h,id))EnableWindow(button,FALSE);
      SetWindowTextW(panel->message,ui::tr(L"Loading…"));owner->load_site(panel);
    }
    return 0;
  }
  if(message==WM_SIZE&&(panel->kind=="site"||panel->kind=="cookies")){RECT rect{};GetClientRect(h,&rect);auto d=[&](int value){return ui::dip(h,value);};
    MoveWindow(panel->message,d(16),d(14),rect.right-d(32),d(80),TRUE);if(panel->list)MoveWindow(panel->list,d(16),d(104),rect.right-d(32),std::max<int>(d(60),rect.bottom-d(168)),TRUE);
    for(size_t n=0;n<std::size(site::settings);++n){const int y=d(104)+static_cast<int>(n)*d(38);
      if(auto label=GetDlgItem(h,SitePanelLabel+static_cast<int>(n)))MoveWindow(label,d(16),y,d(220),d(30),TRUE);if(auto combo=GetDlgItem(h,SitePanelSetting+static_cast<int>(n)))MoveWindow(combo,d(244),y+d(2),d(220),d(200),TRUE);}
    MoveWindow(GetDlgItem(h,SitePanelLeft),d(16),rect.bottom-d(48),d(180),d(30),TRUE);if(auto middle=GetDlgItem(h,SitePanelMiddle))MoveWindow(middle,d(206),rect.bottom-d(48),d(170),d(30),TRUE);
    MoveWindow(GetDlgItem(h,SitePanelRight),rect.right-d(216),rect.bottom-d(48),d(200),d(30),TRUE);return 0;}
  if(message==WM_SIZE){RECT rect{};GetClientRect(h,&rect);auto d=[&](int value){return ui::dip(h,value);};MoveWindow(panel->message,d(16),d(14),rect.right-d(32),panel->list?d(35):rect.bottom-d(80),TRUE);if(panel->list)MoveWindow(panel->list,d(16),d(56),rect.right-d(32),rect.bottom-d(120),TRUE);MoveWindow(GetDlgItem(h,1),d(16),rect.bottom-d(48),d(200),d(30),TRUE);MoveWindow(GetDlgItem(h,2),rect.right-d(176),rect.bottom-d(48),d(160),d(30),TRUE);return 0;}
  if(message==WM_DPICHANGED){auto rect=reinterpret_cast<RECT*>(lp);auto old=panel->font;panel->font=ui::font(h);for(auto child=GetWindow(h,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(panel->font),TRUE);DeleteObject(old);SetWindowPos(h,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOACTIVATE|SWP_NOZORDER);return 0;}
  if(message==WM_GETMINMAXINFO){auto value=reinterpret_cast<MINMAXINFO*>(lp);const bool site_panel=panel->kind=="site"||panel->kind=="cookies";value->ptMinTrackSize={ui::dip(h,site_panel?620:540),ui::dip(h,panel->kind=="site"?460:360)};return 0;}
  if(message==WM_COMMAND&&HIWORD(wp)==CBN_SELCHANGE&&panel->kind=="site"){const int index=LOWORD(wp)-SitePanelSetting;
    if(index>=0&&index<static_cast<int>(std::size(site::settings)))owner->panel_setting(*panel,static_cast<size_t>(index),reinterpret_cast<HWND>(lp));return 0;}
  if(message==WM_COMMAND&&HIWORD(wp)==BN_CLICKED&&(panel->kind=="site"||panel->kind=="cookies")){const int id=LOWORD(wp);
    if(id==IDCANCEL){DestroyWindow(h);return 0;}
    // Enter presses the focused button; it never starts a deletion by default.
    if(id==IDOK){const auto focus=GetFocus();wchar_t type[16]{};if(focus&&IsChild(h,focus)&&GetClassNameW(focus,type,16)&&_wcsicmp(type,L"BUTTON")==0)PostMessageW(h,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(focus),BN_CLICKED),reinterpret_cast<LPARAM>(focus));return 0;}
    if(id==SitePanelLeft||id==SitePanelMiddle||id==SitePanelRight)owner->site_panel_command(*panel,id);return 0;}
  if(message==WM_DRAWITEM){auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item&&item->CtlType==ODT_BUTTON){ui::button(*item,panel->font);return TRUE;}}
  if(message==WM_COMMAND&&HIWORD(wp)==BN_CLICKED){const int id=LOWORD(wp);
    if(prompt_kind(panel->kind)){panel->answered=true;if(panel->answer)panel->answer(id==1);DestroyWindow(h);return 0;}
    if(id==2){DestroyWindow(h);return 0;}
    if(panel->kind=="notices"&&id==1){wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);const auto credits=std::filesystem::path(module).parent_path()/L"Chromium-CREDITS.html";if(std::filesystem::is_regular_file(credits))ShellExecuteW(h,L"open",credits.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;}
    const int index=panel->list?ListView_GetNextItem(panel->list,-1,LVNI_SELECTED):-1;
    if(index>=0&&static_cast<size_t>(index)<panel->rows.size()){const auto row=panel->rows[index];if(id==1){if(auto frame=owner->frame_of(panel->parent))owner->engine.native_command(frame->selected,"navigate",str(row,"url"));DestroyWindow(h);}}
    return 0;
  }
  if(message==WM_CLOSE){DestroyWindow(h);return 0;}if(message==WM_DESTROY){if(panel->answer&&!panel->answered){panel->answered=true;panel->answer(false);}DeleteObject(panel->font);return 0;}
  if(message==WM_NCDESTROY){panel->window=nullptr;return DefWindowProcW(h,message,wp,lp);}
  if(message==WM_PAINT){PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT rect{};GetClientRect(h,&rect);ui::fill(dc,rect,ui::palette().canvas);EndPaint(h,&paint);return 0;}
  return DefWindowProcW(h,message,wp,lp);
}catch(...){return message==WM_NCCREATE?FALSE:message==WM_CREATE?-1:0;}
}
// ---- Library and form windows ------------------------------------------------
// Bookmarks, history, settings, import and Clear browsing data are native
// human chrome for one workspace. None of it is reachable through MCP.
namespace {
enum LibraryId {LibraryOpen=1,LibraryClose=2,LibraryNewFolder=3,LibraryRename,LibraryMove,LibraryDelete,LibraryImport,LibraryClear,LibraryMessage=10,LibraryList,LibraryTree,LibrarySearch,LibraryMoveTarget=3000};
enum FormId {FormPrimary=1,FormClose=2,FormMessage=10,FormSearchEngine=20,FormDefaultStatus,FormMakeDefault,FormTheme,FormLanguage,FormImport,FormClearData,FormPasswords,FormSource,FormBookmarks,FormHistory,
  FormRange,FormClearHistory,FormClearCookies,FormClearStorage,FormClearCache,FormNote,FormStatus,FormAgentHelp,FormHeading=60,FormLabel=80};
HWND child(HWND parent,HFONT font,int id,const wchar_t* type,const std::wstring& text,DWORD style){
  auto control=CreateWindowExW(0,type,text.c_str(),WS_CHILD|WS_VISIBLE|style,0,0,10,10,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),branding_module(),nullptr);
  SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);ui::control_theme(control);return control;}
bool checked(HWND window,int id){return SendMessageW(GetDlgItem(window,id),BM_GETCHECK,0,0)==BST_CHECKED;}
// Case-folded words of a search; every word must appear in a row.
std::vector<std::string> words(const std::string& text){std::vector<std::string> result;std::string current;
  for(const char c:omnibox::fold(text)){if(c==' '||c=='\t'){if(!current.empty())result.push_back(std::move(current));current.clear();}else current+=c;}
  if(!current.empty())result.push_back(std::move(current));return result;}
bool matches(const std::string& text,const std::vector<std::string>& query){return std::all_of(query.begin(),query.end(),[&](const std::string& word){return text.find(word)!=std::string::npos;});}
std::wstring visit_time(int64_t milliseconds){if(milliseconds<=0)return {};ULARGE_INTEGER value{};value.QuadPart=static_cast<ULONGLONG>(milliseconds+11644473600000LL)*10000;FILETIME utc{value.LowPart,value.HighPart},local{};SYSTEMTIME system{};
  if(!FileTimeToLocalFileTime(&utc,&local)||!FileTimeToSystemTime(&local,&system))return {};wchar_t date[80]{},time[80]{};
  GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,DATE_SHORTDATE,&system,nullptr,date,80,nullptr);GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,TIME_NOSECONDS,&system,nullptr,time,80);return std::wstring(date)+L" "+time;}
// A folder and every folder inside it, from a bookmarks document's folders.
std::set<std::string> folder_subtree(const Json& folders,const std::vector<std::string>& roots){std::set<std::string> result(roots.begin(),roots.end());
  for(bool grew=true;grew;){grew=false;for(const auto& folder:folders)if(result.contains(str(folder,"parent"))&&result.insert(str(folder,"id")).second)grew=true;}return result;}
std::wstring registry_text(const wchar_t* key,const wchar_t* value){wchar_t buffer[4096]{};DWORD size=sizeof(buffer);
  return RegGetValueW(HKEY_CURRENT_USER,key,value,RRF_RT_REG_SZ,nullptr,buffer,&size)==ERROR_SUCCESS?std::wstring(buffer):std::wstring{};}
// The installer registers this executable with Windows as a browser. A
// portable copy, or another installation, is not this copy's registration.
bool browser_registered(){
  if(registry_text(L"Software\\RegisteredApplications",L"Xenon Browser").empty())return false;
  wchar_t module[32768]{};if(!GetModuleFileNameW(nullptr,module,32768))return false;std::wstring executable(module),command=registry_text(L"Software\\Classes\\XenonURL\\shell\\open\\command",nullptr);
  if(command.empty())return false;CharLowerBuffW(executable.data(),static_cast<DWORD>(executable.size()));CharLowerBuffW(command.data(),static_cast<DWORD>(command.size()));
  return command.find(L"\""+executable+L"\"")!=std::wstring::npos;}
// Windows keeps the person's browser choice; Xenon only reads it.
bool browser_default(){
  for(const auto* key:{L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\https\\UserChoiceLatest",L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\https\\UserChoice"})
    if(const auto program=registry_text(key,L"ProgId");!program.empty())return program==L"XenonURL";
  return false;}
std::filesystem::path local_app_data(){PWSTR folder{};if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&folder)))return {};std::filesystem::path result(folder);CoTaskMemFree(folder);return result;}
}
void BrowserShell::Impl::library_open(Panel& target,const std::vector<std::string>& urls){
  auto frame=panel_frame(target);if(!frame||urls.empty())return;const size_t count=std::min<size_t>(urls.size(),20);
  for(size_t n=0;n<count;++n)frame->open_tab(target.workspace,urls[n],{},n==0);
  if(urls.size()>count)SetWindowTextW(target.message,ui::format(ui::tr(L"Opened the first {0} pages."),{std::to_wstring(count)}).c_str());
}
void BrowserShell::Impl::library_refresh(const std::string& workspace,const std::string& kind){
  for(const auto& open:panels)if(open->window&&open->kind==kind&&open->workspace==workspace){if(kind=="bookmarks")bookmarks_tree(*open);else history_rows(*open,true);}
}
void BrowserShell::Impl::bookmarks_tree(Panel& target){
  Json value=Json::object();try{value=data.list(target.workspace,target.private_mode);}catch(...){}
  const auto folders=value.value("folders",Json::array());
  if(!target.folder.empty()&&std::none_of(folders.begin(),folders.end(),[&](const Json& folder){return str(folder,"id")==target.folder;}))target.folder.clear();
  target.building=true;SendMessageW(target.tree,WM_SETREDRAW,FALSE,0);TreeView_DeleteAllItems(target.tree);target.folder_items.clear();HTREEITEM chosen{};
  const auto insert=[&](HTREEITEM parent,std::wstring label,const std::string& id){TVINSERTSTRUCTW entry{};entry.hParent=parent;entry.hInsertAfter=TVI_LAST;entry.item.mask=TVIF_TEXT;entry.item.pszText=label.data();
    const auto item=TreeView_InsertItem(target.tree,&entry);target.folder_items[item]=id;if(id==target.folder)chosen=item;return item;};
  const auto top=insert(TVI_ROOT,ui::tr(L"Bookmarks"),"");
  std::function<void(HTREEITEM,const std::string&,size_t)> add=[&](HTREEITEM parent,const std::string& id,size_t depth){if(depth>browser_import::max_depth)return;
    for(const auto& folder:folders)if(str(folder,"parent")==id){const auto item=insert(parent,omnibox::printable(ui::wide(str(folder,"name"))),str(folder,"id"));add(item,str(folder,"id"),depth+1);}};
  add(top,"",1);
  for(const auto& [item,id]:target.folder_items)TreeView_Expand(target.tree,item,TVE_EXPAND);
  TreeView_SelectItem(target.tree,chosen?chosen:top);target.building=false;SendMessageW(target.tree,WM_SETREDRAW,TRUE,0);InvalidateRect(target.tree,nullptr,TRUE);
  bookmarks_rows(target);
}
void BrowserShell::Impl::bookmarks_rows(Panel& target){
  Json value=Json::object();try{value=data.list(target.workspace,target.private_mode);}catch(...){}
  const auto folders=value.value("folders",Json::array()),bookmarks=value.value("bookmarks",Json::array());const auto query=words(ui::text(target.search));
  std::set<std::string> known;for(const auto& folder:folders)known.insert(str(folder,"id"));
  target.rows=Json::array();const auto add=[&](const Json& bookmark){target.rows.push_back({{"kind","bookmark"},{"url",str(bookmark,"url")},{"title",str(bookmark,"title")}});};
  if(query.empty()){
    for(const auto& folder:folders)if(str(folder,"parent")==target.folder)target.rows.push_back({{"kind","folder"},{"id",str(folder,"id")},{"title",str(folder,"name")}});
    // Bookmarks whose folder is gone appear at the top level.
    for(const auto& bookmark:bookmarks){const auto folder=str(bookmark,"folder");if(folder==target.folder||(target.folder.empty()&&!known.contains(folder)))add(bookmark);}
  }else for(const auto& bookmark:bookmarks)if(target.rows.size()<5000&&matches(omnibox::fold(str(bookmark,"title"))+"\n"+omnibox::lower_ascii(str(bookmark,"url")),query))add(bookmark);
  const auto list=target.list;SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);int index=0;
  for(const auto& row:target.rows){const bool folder=str(row,"kind")=="folder";auto name=omnibox::printable(ui::wide(str(row,"title").empty()?str(row,"url"):str(row,"title")));
    LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=index;item.pszText=name.data();ListView_InsertItem(list,&item);
    auto detail=folder?std::wstring(ui::tr(L"Folder")):omnibox::printable(ui::wide(str(row,"url")));ListView_SetItemText(list,index++,1,detail.data());}
  SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,TRUE);
  SetWindowTextW(target.message,ui::format(ui::tr(L"Workspace: {0} · Bookmarks: {1}"),{workspace_label(target),std::to_wstring(bookmarks.size())}).c_str());
}
void BrowserShell::Impl::history_rows(Panel& target,bool reload){
  if(reload){Json value=Json::object();try{value=data.list(target.workspace,target.private_mode);}catch(...){}
    target.rows=value.value("history",Json::array());target.folded.clear();target.folded.reserve(target.rows.size());
    for(const auto& row:target.rows)target.folded.push_back(omnibox::fold(str(row,"title"))+"\n"+omnibox::lower_ascii(str(row,"url")));}
  const auto query=words(ui::text(target.search));target.shown.clear();
  for(size_t n=0;n<target.rows.size()&&n<target.folded.size();++n)if(query.empty()||matches(target.folded[n],query))target.shown.push_back(n);
  ListView_SetItemState(target.list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);ListView_SetItemCountEx(target.list,static_cast<int>(target.shown.size()),0);InvalidateRect(target.list,nullptr,TRUE);
  SetWindowTextW(target.message,ui::format(query.empty()?ui::tr(L"Workspace: {0} · Pages: {1}"):ui::tr(L"Workspace: {0} · Matching pages: {1}"),{workspace_label(target),std::to_wstring(target.shown.size())}).c_str());
}
void BrowserShell::Impl::library_command(Panel& target,int id){try{
  Panel* raw=&target;const auto window=target.window;const bool bookmarks=target.kind=="bookmarks";const bool tree_focus=bookmarks&&GetFocus()==target.tree;
  std::vector<int> selected;for(int index=ListView_GetNextItem(target.list,-1,LVNI_SELECTED);index>=0&&selected.size()<10000;index=ListView_GetNextItem(target.list,index,LVNI_SELECTED))selected.push_back(index);
  const auto row=[&](int index)->Json{const auto at=static_cast<size_t>(index);if(bookmarks)return at<target.rows.size()?target.rows[at]:Json::object();
    return at<target.shown.size()&&target.shown[at]<target.rows.size()?target.rows[target.shown[at]]:Json::object();};
  const auto tree_folder=[&]{const auto found=target.folder_items.find(TreeView_GetSelection(target.tree));return found==target.folder_items.end()?std::string{}:found->second;};
  if(id==LibraryOpen){if(tree_focus)return;std::vector<std::string> urls;
    for(const int index:selected){const auto item=row(index);if(str(item,"kind")=="folder"){target.folder=str(item,"id");bookmarks_tree(target);return;}if(const auto url=str(item,"url");web_address(url))urls.push_back(url);}
    library_open(target,urls);return;}
  if(id==LibraryNewFolder&&bookmarks){const auto folder=data.add_folder(target.workspace,target.private_mode,target.folder,ui::tr8("New folder"));
    if(folder.empty()){SetWindowTextW(target.message,ui::tr(L"The folder could not be created."));return;}
    if(GetWindowTextLengthW(target.search)){target.building=true;SetWindowTextW(target.search,L"");target.building=false;}
    bookmarks_tree(target);
    for(size_t n=0;n<target.rows.size();++n)if(str(target.rows[n],"id")==folder){SetFocus(target.list);ListView_SetItemState(target.list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
      ListView_SetItemState(target.list,static_cast<int>(n),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_EditLabel(target.list,static_cast<int>(n));break;}
    return;}
  if(id==LibraryRename&&bookmarks){if(tree_focus){if(!tree_folder().empty())TreeView_EditLabel(target.tree,TreeView_GetSelection(target.tree));return;}
    if(!selected.empty()){SetFocus(target.list);ListView_EditLabel(target.list,selected.front());}return;}
  if(id==LibraryMove&&bookmarks){std::vector<std::string> moving_folders,moving_urls;
    if(tree_focus){if(const auto folder=tree_folder();!folder.empty())moving_folders.push_back(folder);}
    else for(const int index:selected){const auto item=row(index);if(str(item,"kind")=="folder")moving_folders.push_back(str(item,"id"));else if(!str(item,"url").empty())moving_urls.push_back(str(item,"url"));}
    if(moving_folders.empty()&&moving_urls.empty())return;
    // Destinations indented by depth; a folder cannot move into itself.
    const auto folders=data.list(target.workspace,target.private_mode).value("folders",Json::array());const auto excluded=folder_subtree(folders,moving_folders);
    std::vector<std::string> destinations{""};auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,LibraryMoveTarget,ui::tr(L"Bookmarks"));
    std::function<void(const std::string&,int)> add=[&](const std::string& parent,int depth){for(const auto& folder:folders)if(str(folder,"parent")==parent&&destinations.size()<500){const auto folder_id=str(folder,"id");if(excluded.contains(folder_id))continue;
      destinations.push_back(folder_id);const auto label=std::wstring(static_cast<size_t>(depth)*4,L' ')+omnibox::printable(ui::wide(str(folder,"name")));AppendMenuW(menu,MF_STRING,LibraryMoveTarget+static_cast<int>(destinations.size())-1,label.c_str());if(depth<32)add(folder_id,depth+1);}};
    add("",1);RECT rect{};GetWindowRect(GetDlgItem(window,LibraryMove),&rect);
    const auto chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_BOTTOMALIGN,rect.left,rect.top,0,window,nullptr);DestroyMenu(menu);
    if(!panel_alive(raw,window)||chosen<LibraryMoveTarget||static_cast<size_t>(chosen-LibraryMoveTarget)>=destinations.size())return;
    const auto destination=destinations[static_cast<size_t>(chosen-LibraryMoveTarget)];bool ok=true;
    for(const auto& folder:moving_folders)if(!data.move_folder(target.workspace,target.private_mode,folder,destination))ok=false;
    for(const auto& url:moving_urls)if(!data.move_bookmark(target.workspace,target.private_mode,url,destination))ok=false;
    bookmarks_tree(target);if(!ok)SetWindowTextW(target.message,ui::tr(L"Some items could not be moved."));return;}
  if(id==LibraryDelete){
    if(!bookmarks){std::vector<std::pair<std::string,int64_t>> visits;for(const int index:selected){const auto item=row(index);visits.push_back({str(item,"url"),item.value("visitedAt",int64_t{})});}
      if(visits.empty())return;const bool ok=data.remove_visits(target.workspace,target.private_mode,visits);history_rows(target,true);if(!ok)SetWindowTextW(target.message,ui::tr(L"History could not be updated."));return;}
    std::vector<std::string> folders,urls;
    if(tree_focus){if(const auto folder=tree_folder();!folder.empty())folders.push_back(folder);}
    else for(const int index:selected){const auto item=row(index);if(str(item,"kind")=="folder")folders.push_back(str(item,"id"));else if(!str(item,"url").empty())urls.push_back(str(item,"url"));}
    if(folders.empty()&&urls.empty())return;
    // Deleting a folder deletes what it contains, so the person confirms that.
    if(!folders.empty()){const auto document=data.list(target.workspace,target.private_mode);const auto inside=folder_subtree(document.value("folders",Json::array()),folders);size_t contained=0;
      for(const auto& bookmark:document.value("bookmarks",Json::array()))if(inside.contains(str(bookmark,"folder")))++contained;
      if(contained){const auto text=ui::format(folders.size()==1?ui::tr(L"Delete this folder and the {0} bookmarks in it?"):ui::tr(L"Delete these folders and the {0} bookmarks in them?"),{std::to_wstring(contained)});
        if(MessageBoxW(window,text.c_str(),ui::tr(L"Delete bookmarks"),MB_OKCANCEL|MB_ICONWARNING|MB_DEFBUTTON2)!=IDOK||!panel_alive(raw,window))return;}}
    bool ok=true;for(const auto& folder:folders)if(!data.remove_folder(target.workspace,target.private_mode,folder))ok=false;
    for(const auto& url:urls)if(!data.remove_bookmark(target.workspace,target.private_mode,url))ok=false;
    bookmarks_tree(target);if(!ok)SetWindowTextW(target.message,ui::tr(L"Some bookmarks could not be deleted."));return;}
  if(id==LibraryImport||id==LibraryClear){if(auto frame=panel_frame(target))panel(*frame,id==LibraryImport?"import":"clear",{},{},{},&target);return;}
}catch(...){if(target.message)SetWindowTextW(target.message,ui::tr(L"The change could not be saved. Existing data is preserved."));}}
LRESULT CALLBACK BrowserShell::Impl::library_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){try{auto panel=reinterpret_cast<Panel*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(message==WM_NCCREATE){panel=static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);panel->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(panel));}
  if(!panel)return DefWindowProcW(h,message,wp,lp);auto owner=panel->owner;const bool bookmarks=panel->kind=="bookmarks";
  // The history list is virtual: its text comes from the shown rows on demand.
  if(message==WM_NOTIFY&&reinterpret_cast<NMHDR*>(lp)->code==LVN_GETDISPINFOW&&reinterpret_cast<NMHDR*>(lp)->hwndFrom==panel->list&&!bookmarks){
    auto info=reinterpret_cast<NMLVDISPINFOW*>(lp);const auto index=static_cast<size_t>(info->item.iItem);
    if((info->item.mask&LVIF_TEXT)&&info->item.pszText&&info->item.cchTextMax>0){std::wstring text;
      if(index<panel->shown.size()&&panel->shown[index]<panel->rows.size()){const auto& row=panel->rows[panel->shown[index]];const auto url=str(row,"url");
        text=info->item.iSubItem==0?omnibox::printable(ui::wide(str(row,"title").empty()?url:str(row,"title"))):info->item.iSubItem==1?omnibox::printable(ui::wide(url)):visit_time(row.value("visitedAt",int64_t{}));}
      wcsncpy_s(info->item.pszText,static_cast<size_t>(info->item.cchTextMax),text.c_str(),_TRUNCATE);}
    return 0;}
  LRESULT result{};if(ui::list_draw(message,wp,lp,h,result)||ui::ctl_color(message,wp,lp,result))return result;
  switch(message){
    case WM_CREATE:{panel->font=ui::font(h);panel->bold=ui::font(h,14,FW_SEMIBOLD);ui::icons(h);const auto colors=ui::palette();
      panel->message=child(h,panel->font,LibraryMessage,L"STATIC",{},SS_LEFT|SS_NOPREFIX|SS_CENTERIMAGE|SS_ENDELLIPSIS);
      panel->search=child(h,panel->font,LibrarySearch,L"EDIT",{},WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL);
      SendMessageW(panel->search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(bookmarks?ui::tr(L"Search bookmarks"):ui::tr(L"Search history")));
      if(bookmarks){panel->tree=child(h,panel->font,LibraryTree,WC_TREEVIEWW,{},WS_TABSTOP|WS_BORDER|TVS_HASBUTTONS|TVS_LINESATROOT|TVS_SHOWSELALWAYS|TVS_EDITLABELS|TVS_FULLROWSELECT|TVS_NOHSCROLL);
        TreeView_SetBkColor(panel->tree,colors.canvas);TreeView_SetTextColor(panel->tree,colors.ink);TreeView_SetItemHeight(panel->tree,ui::dip(h,28));}
      panel->list=child(h,panel->font,LibraryList,WC_LISTVIEWW,{},WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SHOWSELALWAYS|(bookmarks?LVS_EDITLABELS:LVS_OWNERDATA));
      ListView_SetExtendedListViewStyle(panel->list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);ListView_SetBkColor(panel->list,colors.canvas);ListView_SetTextBkColor(panel->list,colors.canvas);ListView_SetTextColor(panel->list,colors.ink);
      int column=0;for(const auto& [width,title]:bookmarks?std::vector<std::pair<int,const wchar_t*>>{{280,ui::tr(L"Name")},{340,L"URL"}}:std::vector<std::pair<int,const wchar_t*>>{{300,ui::tr(L"Title")},{300,L"URL"},{150,ui::tr(L"Visited")}}){
        LVCOLUMNW item{};item.mask=LVCF_TEXT|LVCF_WIDTH;item.cx=ui::dip(h,width);item.pszText=const_cast<LPWSTR>(title);ListView_InsertColumn(panel->list,column++,&item);}
      child(h,panel->font,LibraryOpen,L"BUTTON",ui::tr(L"Open"),BS_OWNERDRAW|WS_TABSTOP);
      if(bookmarks){child(h,panel->font,LibraryNewFolder,L"BUTTON",ui::tr(L"New folder"),BS_OWNERDRAW|WS_TABSTOP);child(h,panel->font,LibraryRename,L"BUTTON",ui::tr(L"Rename"),BS_OWNERDRAW|WS_TABSTOP);
        child(h,panel->font,LibraryMove,L"BUTTON",ui::tr(L"Move to…"),BS_OWNERDRAW|WS_TABSTOP);}
      child(h,panel->font,LibraryDelete,L"BUTTON",ui::tr(L"Delete"),BS_OWNERDRAW|WS_TABSTOP);
      child(h,panel->font,bookmarks?LibraryImport:LibraryClear,L"BUTTON",bookmarks?ui::tr(L"Import…"):ui::tr(L"Clear browsing data…"),BS_OWNERDRAW|WS_TABSTOP);
      child(h,panel->font,LibraryClose,L"BUTTON",ui::tr(L"Close"),BS_OWNERDRAW|WS_TABSTOP);
      if(bookmarks)owner->bookmarks_tree(*panel);else owner->history_rows(*panel,true);
      return 0;}
    case WM_SIZE:{RECT rect{};GetClientRect(h,&rect);auto d=[&](int value){return ui::dip(h,value);};const int top=d(52),bottom=rect.bottom-d(60);
      MoveWindow(panel->message,d(16),d(12),std::max<int>(d(80),rect.right-d(348)),d(28),TRUE);MoveWindow(panel->search,rect.right-d(316),d(12),d(300),d(28),TRUE);
      const int left=panel->tree?d(264):d(16);if(panel->tree)MoveWindow(panel->tree,d(16),top,d(240),std::max<int>(d(60),bottom-top),TRUE);
      MoveWindow(panel->list,left,top,std::max<int>(d(80),rect.right-d(16)-left),std::max<int>(d(60),bottom-top),TRUE);
      // Columns share the list width, leaving room for its vertical scroll bar.
      {RECT area{};GetClientRect(panel->list,&area);const int width=std::max<int>(d(120),area.right-GetSystemMetrics(SM_CXVSCROLL)),visited=bookmarks?0:d(170),name=(width-visited)*(bookmarks?2:1)/(bookmarks?5:2);
        ListView_SetColumnWidth(panel->list,0,name);ListView_SetColumnWidth(panel->list,1,width-visited-name);if(!bookmarks)ListView_SetColumnWidth(panel->list,2,visited);}
      int x=d(16);for(int id:{LibraryOpen,LibraryNewFolder,LibraryRename,LibraryMove,LibraryDelete,LibraryImport,LibraryClear})if(auto button=GetDlgItem(h,id)){
        const int width=d(id==LibraryClear?190:id==LibraryOpen||id==LibraryDelete||id==LibraryRename?96:120);MoveWindow(button,x,rect.bottom-d(46),width,d(30),TRUE);x+=width+d(8);}
      MoveWindow(GetDlgItem(h,LibraryClose),rect.right-d(112),rect.bottom-d(46),d(96),d(30),TRUE);return 0;}
    case WM_GETMINMAXINFO:{auto value=reinterpret_cast<MINMAXINFO*>(lp);value->ptMinTrackSize={ui::dip(h,860),ui::dip(h,420)};return 0;}
    case WM_COMMAND:{const int id=LOWORD(wp);
      if(id==LibrarySearch){if(HIWORD(wp)==EN_CHANGE&&!panel->building){if(bookmarks)owner->bookmarks_rows(*panel);else owner->history_rows(*panel,false);}return 0;}
      if(id==IDCANCEL||id==LibraryClose){DestroyWindow(h);return 0;}
      // Enter in the search box moves to the results instead of opening them.
      if(id==IDOK&&GetFocus()==panel->search){SetFocus(panel->list);return 0;}
      if(HIWORD(wp)==BN_CLICKED)owner->library_command(*panel,id);return 0;}
    case WM_NOTIFY:{auto notice=reinterpret_cast<NMHDR*>(lp);
      if(panel->tree&&notice->hwndFrom==panel->tree){
        if(notice->code==TVN_SELCHANGEDW&&!panel->building){const auto found=panel->folder_items.find(reinterpret_cast<NMTREEVIEWW*>(lp)->itemNew.hItem);panel->folder=found==panel->folder_items.end()?std::string{}:found->second;
          if(GetWindowTextLengthW(panel->search)){panel->building=true;SetWindowTextW(panel->search,L"");panel->building=false;}owner->bookmarks_rows(*panel);return 0;}
        // The top level is not a folder of its own and cannot be renamed.
        if(notice->code==TVN_BEGINLABELEDITW){const auto found=panel->folder_items.find(reinterpret_cast<NMTVDISPINFOW*>(lp)->item.hItem);return found==panel->folder_items.end()||found->second.empty();}
        if(notice->code==TVN_ENDLABELEDITW){const auto& item=reinterpret_cast<NMTVDISPINFOW*>(lp)->item;const auto found=panel->folder_items.find(item.hItem);
          if(item.pszText&&found!=panel->folder_items.end()&&!found->second.empty()){if(!owner->data.rename_folder(panel->workspace,panel->private_mode,found->second,ui::utf8(item.pszText)))SetWindowTextW(panel->message,ui::tr(L"The name could not be changed."));PostMessageW(h,WM_APP,0,0);}
          return FALSE;}
        if(notice->code==TVN_KEYDOWN){const auto key=reinterpret_cast<NMTVKEYDOWN*>(lp)->wVKey;if(key==VK_DELETE||key==VK_F2){owner->library_command(*panel,key==VK_DELETE?LibraryDelete:LibraryRename);return TRUE;}}
        break;}
      if(notice->hwndFrom==panel->list){
        if(notice->code==NM_DBLCLK){owner->library_command(*panel,LibraryOpen);return 0;}
        if(notice->code==LVN_KEYDOWN){const auto key=reinterpret_cast<NMLVKEYDOWN*>(lp)->wVKey;if(key==VK_DELETE)owner->library_command(*panel,LibraryDelete);else if(key==VK_F2&&bookmarks)owner->library_command(*panel,LibraryRename);return 0;}
        if(notice->code==LVN_BEGINLABELEDITW)return !bookmarks;
        if(notice->code==LVN_ENDLABELEDITW){const auto& item=reinterpret_cast<NMLVDISPINFOW*>(lp)->item;
          if(item.pszText&&item.iItem>=0&&static_cast<size_t>(item.iItem)<panel->rows.size()){const auto row=panel->rows[static_cast<size_t>(item.iItem)];const auto label=ui::utf8(item.pszText);
            const bool ok=str(row,"kind")=="folder"?owner->data.rename_folder(panel->workspace,panel->private_mode,str(row,"id"),label):owner->data.rename_bookmark(panel->workspace,panel->private_mode,str(row,"url"),label);
            if(!ok)SetWindowTextW(panel->message,ui::tr(L"The name could not be changed."));PostMessageW(h,WM_APP,0,0);}
          return FALSE;}}
      break;}
    // Rebuilt after label editing ends, never inside its notification.
    case WM_APP:if(bookmarks)owner->bookmarks_tree(*panel);return 0;
    case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item&&item->CtlType==ODT_BUTTON){ui::button(*item,panel->font);return TRUE;}break;}
    case WM_DPICHANGED:{auto rect=reinterpret_cast<RECT*>(lp);auto old=panel->font;panel->font=ui::font(h);for(auto control=GetWindow(h,GW_CHILD);control;control=GetWindow(control,GW_HWNDNEXT))SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(panel->font),TRUE);DeleteObject(old);
      if(panel->tree)TreeView_SetItemHeight(panel->tree,ui::dip(h,28));SetWindowPos(h,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOACTIVATE|SWP_NOZORDER);return 0;}
    case WM_CLOSE:DestroyWindow(h);return 0;
    case WM_DESTROY:if(panel->font)DeleteObject(panel->font);if(panel->bold)DeleteObject(panel->bold);panel->font=panel->bold=nullptr;return 0;
    case WM_NCDESTROY:panel->window=nullptr;return DefWindowProcW(h,message,wp,lp);
    case WM_PAINT:{PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT rect{};GetClientRect(h,&rect);ui::fill(dc,rect,ui::palette().canvas);EndPaint(h,&paint);return 0;}
  }
  return DefWindowProcW(h,message,wp,lp);
}catch(...){return message==WM_NCCREATE?FALSE:message==WM_CREATE?-1:0;}}
void BrowserShell::Impl::form_build(Panel& target){const auto h=target.window;auto& places=target.places;int y=16;const bool settings=target.kind=="settings";
  const int label_width=settings?250:110,field=settings?280:140,field_width=settings?300:target.kind=="import"?420:380;
  const auto put=[&](int id,const wchar_t* type,const std::wstring& text,DWORD style,int x,int width,int height,HFONT face=nullptr){auto control=child(h,face?face:target.font,id,type,text,style);places.emplace_back(id,x,y,width,height);return control;};
  int heading=FormHeading,label=FormLabel;
  const auto section=[&](const wchar_t* text){if(y>16)y+=14;put(heading++,L"STATIC",text,SS_LEFT|SS_NOPREFIX,20,580,24,target.bold);y+=30;};
  const auto row_label=[&](const wchar_t* text){put(label++,L"STATIC",text,SS_LEFT|SS_NOPREFIX|SS_CENTERIMAGE,20,label_width,30);};
  const auto combo=[&](int id,const std::vector<std::wstring>& items,int selected){auto control=put(id,L"COMBOBOX",{},CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,field,field_width,260);
    for(const auto& item:items)SendMessageW(control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item.c_str()));SendMessageW(control,CB_SETCURSEL,selected,0);return control;};
  const auto check=[&](int id,const wchar_t* text,bool on){auto control=put(id,L"BUTTON",text,BS_AUTOCHECKBOX|WS_TABSTOP,20,560,26);SendMessageW(control,BM_SETCHECK,on?BST_CHECKED:BST_UNCHECKED,0);y+=30;return control;};
  const auto bottom=[&](int id,const wchar_t* text,int x,int width){child(h,target.font,id,L"BUTTON",text,BS_OWNERDRAW|WS_TABSTOP);places.emplace_back(id,x,-46,width,30);};
  if(settings){
    section(ui::tr(L"Search engine"));row_label(ui::tr(L"Searches from the address box use"));
    std::vector<std::wstring> engines;for(const auto& engine_choice:omnibox::search_engines)engines.emplace_back(engine_choice.name);combo(FormSearchEngine,engines,static_cast<int>(omnibox::search_choice));y+=38;
    section(ui::tr(L"Default browser"));put(FormDefaultStatus,L"STATIC",{},SS_LEFT|SS_NOPREFIX,20,580,44);y+=50;
    put(FormMakeDefault,L"BUTTON",ui::tr(L"Make Xenon your default browser…"),BS_OWNERDRAW|WS_TABSTOP,20,300,30);y+=38;
    section(ui::tr(L"Appearance"));row_label(ui::tr(L"Theme"));combo(FormTheme,{ui::tr(L"System"),ui::tr(L"Light"),ui::tr(L"Dark")},ui::theme_mode==ui::ThemeMode::light?1:ui::theme_mode==ui::ThemeMode::dark?2:0);y+=38;
    row_label(ui::tr(L"Language"));combo(FormLanguage,{L"English",L"简体中文"},ui::preferred_language==ui::Language::simplified_chinese?1:0);y+=38;
    // Tabs where an agent needs the person always pulse; this adds a notification.
    section(ui::tr(L"Agents"));check(FormAgentHelp,ui::tr(L"Show a Windows notification when an agent needs your help"),ui::agent_help_notifications);
    section(ui::tr(L"Your data"));put(FormImport,L"BUTTON",ui::tr(L"Import bookmarks and history…"),BS_OWNERDRAW|WS_TABSTOP,20,270,30);put(FormClearData,L"BUTTON",ui::tr(L"Clear browsing data…"),BS_OWNERDRAW|WS_TABSTOP,300,220,30);y+=38;
    put(FormPasswords,L"BUTTON",ui::tr(L"Passwords…"),BS_OWNERDRAW|WS_TABSTOP,20,270,30);
    child(h,target.font,FormStatus,L"STATIC",{},SS_LEFT|SS_NOPREFIX|SS_CENTERIMAGE|SS_ENDELLIPSIS);places.emplace_back(FormStatus,20,-46,460,30);
    bottom(FormClose,ui::tr(L"Close"),-112,96);settings_status(target);return;}
  if(target.kind=="import"){
    target.message=put(FormMessage,L"STATIC",ui::format(ui::tr(L"Import bookmarks and history into the {0} workspace."),{workspace_label(target)}),SS_LEFT|SS_NOPREFIX,20,560,44);y+=52;
    row_label(ui::tr(L"From"));target.sources=browser_import::chromium_sources(local_app_data());std::vector<std::wstring> items;
    for(const auto& source:target.sources)items.push_back(ui::wide(source.browser)+L" — "+omnibox::printable(ui::wide(source.profile_name)));items.emplace_back(ui::tr(L"Bookmarks HTML file…"));
    combo(FormSource,items,0);y+=44;check(FormBookmarks,ui::tr(L"Bookmarks"),true);check(FormHistory,ui::tr(L"History"),true);y+=8;
    put(FormNote,L"STATIC",ui::tr(L"Passwords, cookies and settings are not imported. To bring passwords, export them from the other browser as a CSV file and use Import CSV in Xenon Controls → Passwords. If history cannot be read, close the other browser and try again."),SS_LEFT|SS_NOPREFIX,20,560,80);
    bottom(FormPrimary,ui::tr(L"Import"),-216,96);bottom(FormClose,ui::tr(L"Close"),-112,96);form_command(target,FormSource,CBN_SELCHANGE);return;}
  target.message=put(FormMessage,L"STATIC",ui::format(ui::tr(L"Clear browsing data in the {0} workspace."),{workspace_label(target)}),SS_LEFT|SS_NOPREFIX,20,520,44);y+=52;
  row_label(ui::tr(L"Time range"));combo(FormRange,{ui::tr(L"Last hour"),ui::tr(L"Last 24 hours"),ui::tr(L"Last 7 days"),ui::tr(L"Last 4 weeks"),ui::tr(L"All time")},4);y+=44;
  check(FormClearHistory,ui::tr(L"Browsing history"),true);check(FormClearCookies,ui::tr(L"Cookies, which may sign you out of websites"),true);
  check(FormClearStorage,ui::tr(L"Other site data, for all time"),true);check(FormClearCache,ui::tr(L"Cached images and files, for all time"),true);y+=8;
  put(FormNote,L"STATIC",ui::tr(L"Saved passwords, bookmarks and downloads are kept. Agents working in this workspace pause and must observe their pages again."),SS_LEFT|SS_NOPREFIX,20,520,60);
  bottom(FormPrimary,ui::tr(L"Clear data"),-216,96);bottom(FormClose,ui::tr(L"Cancel"),-112,96);
}
void BrowserShell::Impl::settings_status(Panel& target){if(!target.window)return;const bool registered=browser_registered(),current=registered&&browser_default();
  SetWindowTextW(GetDlgItem(target.window,FormDefaultStatus),current?ui::tr(L"Xenon is your default browser."):registered?ui::tr(L"Xenon is not your default browser. Windows asks you to choose it in Settings → Apps → Default apps."):
    ui::tr(L"This copy of Xenon is not registered with Windows. Install Xenon with its installer to make it your default browser."));
  EnableWindow(GetDlgItem(target.window,FormMakeDefault),registered&&!current);}
void BrowserShell::Impl::form_command(Panel& target,int id,int code){try{const auto window=target.window;
  const auto status=[&](const wchar_t* text){if(auto line=GetDlgItem(window,FormStatus))SetWindowTextW(line,text);};
  if((id==IDCANCEL||id==FormClose)&&code==BN_CLICKED){if(!target.busy)DestroyWindow(window);return;}
  if(target.kind=="settings"){
    if(code==CBN_SELCHANGE){const auto choice=SendMessageW(GetDlgItem(window,id),CB_GETCURSEL,0,0);if(choice<0)return;
      if(id==FormSearchEngine)status(ui::save_search_engine(static_cast<size_t>(choice))?ui::tr(L"Search engine saved."):ui::tr(L"The search engine could not be saved."));
      else if(id==FormTheme){if(!ui::save_theme(choice==2?ui::ThemeMode::dark:choice==1?ui::ThemeMode::light:ui::ThemeMode::system))status(ui::tr(L"Theme changed for this run; saving failed."));
        else status(ui::theme_mode!=ui::web_theme_mode?ui::tr(L"Theme saved. Web pages use it after Xenon restarts."):ui::tr(L"Theme saved."));theme_all();}
      else if(id==FormLanguage){if(!ui::save_language(choice==1?ui::Language::simplified_chinese:ui::Language::english))status(ui::tr(L"The language preference could not be saved. Try again."));
        else status(ui::preferred_language==ui::language?ui::tr(L"Language preference saved."):ui::tr(L"Restart Xenon to apply the selected language. Save unfinished website work before exiting."));}
      return;}
    if(code!=BN_CLICKED)return;
    if(id==FormAgentHelp){const bool on=checked(window,FormAgentHelp);
      if(!ui::save_agent_help_notifications(on)){SendMessageW(GetDlgItem(window,FormAgentHelp),BM_SETCHECK,ui::agent_help_notifications?BST_CHECKED:BST_UNCHECKED,0);status(ui::tr(L"The notification preference could not be saved."));}
      else status(on?ui::tr(L"Agent help notifications are on."):ui::tr(L"Agent help notifications are off."));
      if(!ui::agent_help_notifications)hide_help_notice();return;}
    // Windows lets only the person choose a default browser, in its own Settings.
    if(id==FormMakeDefault){ShellExecuteW(window,L"open",L"ms-settings:defaultapps?registeredAppUser=Xenon%20Browser",nullptr,nullptr,SW_SHOWNORMAL);return;}
    if(id==FormImport||id==FormClearData){if(auto frame=panel_frame(target))panel(*frame,id==FormImport?"import":"clear");return;}
    if(id==FormPasswords){native.show_section(NativeUi::Section::passwords,{});return;}
    return;}
  if(target.kind=="import"){
    if(id==FormSource&&code==CBN_SELCHANGE){const auto choice=static_cast<size_t>(SendMessageW(GetDlgItem(window,FormSource),CB_GETCURSEL,0,0));const bool file=choice>=target.sources.size();
      const bool bookmarks=file||target.sources[choice].bookmarks,history=!file&&target.sources[choice].history;
      EnableWindow(GetDlgItem(window,FormBookmarks),bookmarks&&!file);EnableWindow(GetDlgItem(window,FormHistory),history);
      SendMessageW(GetDlgItem(window,FormBookmarks),BM_SETCHECK,bookmarks?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(GetDlgItem(window,FormHistory),BM_SETCHECK,history?BST_CHECKED:BST_UNCHECKED,0);return;}
    if(id==FormPrimary&&code==BN_CLICKED)start_import(target);return;}
  if(target.kind=="clear"&&id==FormPrimary&&code==BN_CLICKED)clear_data(target);
}catch(...){if(target.message)SetWindowTextW(target.message,ui::tr(L"The browser action could not be completed. Existing data is preserved."));}}
void BrowserShell::Impl::clear_data(Panel& target){const auto window=target.window;
  const bool history=checked(window,FormClearHistory),cookies=checked(window,FormClearCookies),storage=checked(window,FormClearStorage),cache=checked(window,FormClearCache);
  if(!history&&!cookies&&!storage&&!cache)return;
  constexpr int64_t hour=3600000;constexpr int64_t spans[]{hour,24*hour,7*24*hour,28*24*hour};const auto range=SendMessageW(GetDlgItem(window,FormRange),CB_GETCURSEL,0,0);
  const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();const int64_t since=range>=0&&range<4?now-spans[range]:0;
  // Stored data is cleared for every site in this workspace's history or tabs.
  std::vector<std::string> origins;std::set<std::string> seen;
  if(storage){try{for(const auto& row:data.list(target.workspace,target.private_mode).value("history",Json::array())){const auto origin=site::origin(str(row,"url"));if(!origin.empty()&&origins.size()<1000&&seen.insert(origin).second)origins.push_back(origin);}}catch(...){}
    for(const auto& [id,host]:hosts)if(host.workspace==target.workspace){const auto origin=site::origin(str(host.metadata,"url"));if(!origin.empty()&&seen.insert(origin).second)origins.push_back(origin);}}
  bool saved=true;if(history){try{saved=data.clear_history(target.workspace,target.private_mode,since);}catch(...){saved=false;}library_refresh(target.workspace,"history");}
  const auto frame=panel_frame(target);const auto frame_window=frame?frame->window:nullptr;
  if(cookies||storage||cache)engine.clear_browsing_data(target.workspace,since,cookies,storage,cache,origins,[this,frame_window,saved](Json value){post([this,frame_window,saved,value=std::move(value)]{
    if(auto owner=frame_of(frame_window))owner->set_status(!value.value("ok",false)?ui::error_text(str(value.value("error",Json::object()),"message")):saved?ui::tr(L"Browsing data cleared."):ui::tr(L"Site data cleared, but history could not be saved."));});});
  else if(frame)frame->set_status(saved?ui::tr(L"Browsing history cleared."):ui::tr(L"History could not be saved."));
  DestroyWindow(window);
}
void BrowserShell::Impl::start_import(Panel& target){if(target.busy)return;Panel* raw=&target;const auto window=target.window;
  const auto choice=static_cast<size_t>(SendMessageW(GetDlgItem(window,FormSource),CB_GETCURSEL,0,0));const bool bookmarks=checked(window,FormBookmarks),history=checked(window,FormHistory);
  std::function<browser_import::Data()> read;std::string folder,browser;bool want_bookmarks=bookmarks,want_history=history;
  if(choice<target.sources.size()){const auto source=target.sources[choice];want_bookmarks=bookmarks&&source.bookmarks;want_history=history&&source.history;if(!want_bookmarks&&!want_history)return;
    browser=source.browser;folder=ui::utf8(ui::format(ui::tr(L"Imported from {0}"),{ui::wide(source.browser)}));
    read=[source,want_bookmarks,want_history,staging=root]{return browser_import::read_chromium_profile(source.profile,want_bookmarks,want_history,staging);};}
  else{wchar_t path[32768]{};OPENFILENAMEW picker{};picker.lStructSize=sizeof(picker);picker.hwndOwner=window;picker.lpstrFile=path;picker.nMaxFile=32768;picker.lpstrFilter=ui::bookmark_filter();picker.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameW(&picker)||!panel_alive(raw,window))return;const std::filesystem::path file(path);want_bookmarks=true;want_history=false;folder=ui::tr8("Imported bookmarks");read=[file]{return browser_import::read_bookmarks_html(file);};}
  target.busy=true;for(int id:{FormPrimary,FormClose,FormSource,FormBookmarks,FormHistory})EnableWindow(GetDlgItem(window,id),FALSE);SetWindowTextW(target.message,ui::tr(L"Importing…"));
  // Files are read off the UI thread; the merge into Xenon's data runs on it.
  std::thread([this,raw,window,read,folder,browser,want_bookmarks,want_history,box=mailbox,target_window=messages]{browser_import::Data result;try{result=read();}catch(...){}
    {std::lock_guard lock(box->mutex);box->calls.push_back([this,raw,window,result=std::move(result),folder,browser,want_bookmarks,want_history]{if(!panel_alive(raw,window))return;raw->busy=false;
      std::wstring text;if(want_bookmarks&&!result.bookmarks_read)text=browser.empty()?ui::tr(L"The bookmark file could not be read."):ui::format(ui::tr(L"Bookmarks could not be read from {0}."),{ui::wide(browser)});
      if(want_history&&!result.history_read)text+=(text.empty()?L"":L" ")+ui::format(ui::tr(L"History could not be read. Close {0} and try again."),{ui::wide(browser)});
      BrowserData::ImportCounts counts;try{counts=data.import(raw->workspace,raw->private_mode,result,folder);}catch(...){}
      const auto summary=ui::format(ui::tr(L"Imported {0} bookmarks in {1} folders and {2} history pages."),{std::to_wstring(counts.bookmarks),std::to_wstring(counts.folders),std::to_wstring(counts.visits)});
      text=text.empty()?summary:summary+L" "+text;if(!counts.ok)text+=L" "+std::wstring(ui::tr(L"Browser metadata could not be saved. Existing data is preserved."));
      SetWindowTextW(raw->message,text.c_str());for(int id:{FormPrimary,FormClose,FormSource})EnableWindow(GetDlgItem(window,id),TRUE);form_command(*raw,FormSource,CBN_SELCHANGE);
      library_refresh(raw->workspace,"bookmarks");library_refresh(raw->workspace,"history");});}
    PostMessageW(target_window,ShellRun,0,0);}).detach();
}
LRESULT CALLBACK BrowserShell::Impl::form_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){try{auto panel=reinterpret_cast<Panel*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(message==WM_NCCREATE){panel=static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);panel->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(panel));}
  if(!panel)return DefWindowProcW(h,message,wp,lp);auto owner=panel->owner;LRESULT result{};
  if(message==WM_CTLCOLORSTATIC){ui::ctl_color(message,wp,lp,result);const int id=GetDlgCtrlID(reinterpret_cast<HWND>(lp));if(id==FormNote||id==FormStatus)SetTextColor(reinterpret_cast<HDC>(wp),ui::palette().muted);return result;}
  if(ui::ctl_color(message,wp,lp,result))return result;
  switch(message){
    case WM_CREATE:panel->font=ui::font(h);panel->bold=ui::font(h,15,FW_SEMIBOLD);ui::icons(h);owner->form_build(*panel);return 0;
    case WM_SIZE:{RECT rect{};GetClientRect(h,&rect);for(const auto& [id,x,y,width,height]:panel->places)if(auto control=GetDlgItem(h,id)){
        MoveWindow(control,x<0?rect.right+ui::dip(h,x):ui::dip(h,x),y<0?rect.bottom+ui::dip(h,y):ui::dip(h,y),ui::dip(h,width),ui::dip(h,height),TRUE);}return 0;}
    case WM_GETMINMAXINFO:{auto value=reinterpret_cast<MINMAXINFO*>(lp);value->ptMinTrackSize={ui::dip(h,560),ui::dip(h,400)};return 0;}
    // Returning from Windows Settings shows the current default-browser choice.
    case WM_ACTIVATE:if(LOWORD(wp)!=WA_INACTIVE&&panel->kind=="settings")owner->settings_status(*panel);break;
    case WM_COMMAND:owner->form_command(*panel,LOWORD(wp),HIWORD(wp));return 0;
    case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item&&item->CtlType==ODT_BUTTON){ui::button(*item,panel->font);return TRUE;}break;}
    case WM_DPICHANGED:{auto rect=reinterpret_cast<RECT*>(lp);auto old=panel->font,old_bold=panel->bold;panel->font=ui::font(h);panel->bold=ui::font(h,15,FW_SEMIBOLD);
      for(auto control=GetWindow(h,GW_CHILD);control;control=GetWindow(control,GW_HWNDNEXT)){const int id=GetDlgCtrlID(control);SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(id>=FormHeading&&id<FormLabel?panel->bold:panel->font),TRUE);}
      DeleteObject(old);DeleteObject(old_bold);SetWindowPos(h,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOACTIVATE|SWP_NOZORDER);return 0;}
    case WM_CLOSE:if(!panel->busy)DestroyWindow(h);return 0;
    case WM_DESTROY:if(panel->font)DeleteObject(panel->font);if(panel->bold)DeleteObject(panel->bold);panel->font=panel->bold=nullptr;return 0;
    case WM_NCDESTROY:panel->window=nullptr;return DefWindowProcW(h,message,wp,lp);
    case WM_PAINT:{PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT rect{};GetClientRect(h,&rect);ui::fill(dc,rect,ui::palette().canvas);EndPaint(h,&paint);return 0;}
  }
  return DefWindowProcW(h,message,wp,lp);
}catch(...){return message==WM_NCCREATE?FALSE:message==WM_CREATE?-1:0;}}
BrowserShell::BrowserShell(Broker& broker,CefEngine& engine,NativeUi& native,ExtensionStore& extensions,const std::filesystem::path& root):impl_(std::make_unique<Impl>(broker,engine,native,extensions,root)){
  INITCOMMONCONTROLSEX common{sizeof(common),ICC_TREEVIEW_CLASSES|ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&common);
  for(const auto& [name,proc]:std::vector<std::pair<const wchar_t*,WNDPROC>>{{L"XenonBrowserShell",Impl::proc},{L"XenonTabHost",Impl::host_proc},{L"XenonAgentCursor",Impl::cursor_proc},{L"XenonBrowserPanel",Impl::panel_proc},
      {L"XenonFindBar",Impl::find_proc},{L"XenonElementsPanel",Impl::elements_proc},{L"XenonElementOverlay",Impl::element_overlay_proc},{L"XenonTabDrag",Impl::ghost_proc},{L"XenonShellMessages",Impl::message_proc},
      {L"XenonAddressSuggestions",Impl::suggestions_proc},{L"XenonSiteInfo",Impl::site_proc},{L"XenonScreenNotice",Impl::notice_proc},{L"XenonLibrary",Impl::library_proc},{L"XenonForm",Impl::form_proc}}){
    const bool shadow=wcscmp(name,L"XenonTabDrag")==0||wcscmp(name,L"XenonAddressSuggestions")==0||wcscmp(name,L"XenonSiteInfo")==0||wcscmp(name,L"XenonScreenNotice")==0;
    WNDCLASSW type{};type.style=wcscmp(name,L"XenonBrowserShell")==0?CS_DBLCLKS:shadow?CS_DROPSHADOW:0;type.lpfnWndProc=proc;type.hInstance=branding_module();type.lpszClassName=name;type.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&type);}
  impl_->messages=CreateWindowExW(0,L"XenonShellMessages",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,branding_module(),impl_.get());
  if(!impl_->messages||!impl_->create_frame())throw std::runtime_error("Cannot create native browser shell");
  impl_->active=impl_->frames.front().get();
  SetTimer(impl_->messages,1,16,nullptr);SetTimer(impl_->messages,2,1000,nullptr);
}
BrowserShell::~BrowserShell(){impl_->data.flush();impl_->hide_help_notice();if(impl_->messages){KillTimer(impl_->messages,1);KillTimer(impl_->messages,2);}
  for(auto& panel:impl_->panels)if(panel->window)DestroyWindow(panel->window);if(impl_->ghost)DestroyWindow(impl_->ghost);
  for(auto& frame:impl_->frames)if(frame->window)DestroyWindow(frame->window);if(impl_->messages)DestroyWindow(impl_->messages);}
void BrowserShell::show(){auto frame=impl_->primary();if(!frame)return;ShowWindow(frame->window,SW_SHOWNORMAL);SetWindowPos(frame->window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);SetForegroundWindow(frame->window);}
void BrowserShell::request_exit(){impl_->request_exit();}
HWND BrowserShell::create_host(const std::string& workspace,const std::string& id,bool human,const std::string& opener){if(auto found=impl_->hosts.find(id);found!=impl_->hosts.end())return found->second.window;
  std::string before;bool select=human;auto frame=impl_->frame_for(workspace,human,opener,before,select);
  if(!frame)frame=impl_->new_window(nullptr);if(!frame)return nullptr;
  Impl::Host host;host.id=id;host.workspace=workspace;host.human=human;host.frame=frame;host.window=CreateWindowExW(0,L"XenonTabHost",ui::tr(L"Web page"),WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,100,100,frame->window,nullptr,branding_module(),nullptr);
  if(!host.window)return nullptr;impl_->hosts.emplace(id,std::move(host));impl_->order.push_back(id);if(!before.empty())impl_->reorder(id,before);frame->layout();
  // Agent-created tabs stay beneath the selected page.
  SetWindowPos(impl_->hosts.at(id).window,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
  if(select||frame->selected.empty())frame->choose(id,false);
  return impl_->hosts.at(id).window;
}
void BrowserShell::tab_created(const std::string& id,HWND browser){if(auto found=impl_->hosts.find(id);found!=impl_->hosts.end()){found->second.browser=browser;auto frame=found->second.frame;if(frame){frame->layout();if(found->second.human&&frame->selected==id)frame->choose(id,true);}}impl_->refresh();}
void BrowserShell::tab_closed(const std::string& id){auto& shell=*impl_;Impl::Frame* frame=nullptr;
  if(auto found=shell.hosts.find(id);found!=shell.hosts.end()){frame=found->second.frame;
    // Recently closed ordinary pages can be reopened with Ctrl+Shift+T.
    const auto url=str(found->second.metadata,"url");if(!shell.shutting_down&&web_address(url)&&!found->second.metadata.value("protected",false)){shell.closed_tabs.push_back({found->second.workspace,url});if(shell.closed_tabs.size()>25)shell.closed_tabs.pop_front();}
    if(frame&&frame->selected==id)frame->select_neighbor(id);
    DestroyWindow(found->second.window);shell.hosts.erase(found);std::erase(shell.order,id);}
  if(shell.drag.active&&shell.drag.tab==id)shell.drag_cancel();
  // A window whose last tab closes goes away unless it is the only window.
  if(frame&&frame->window&&!shell.shutting_down&&!frame->has_hosts()&&std::count_if(shell.frames.begin(),shell.frames.end(),[](const auto& entry){return entry->window!=nullptr;})>1)DestroyWindow(frame->window);
  PostMessageW(shell.messages,ShellChanged,0,0);
}
void BrowserShell::refresh(){PostMessageW(impl_->messages,ShellChanged,0,0);}
bool BrowserShell::pretranslate(MSG& message){
  if(message.message<WM_KEYFIRST||message.message>WM_KEYLAST)return false;const auto root=GetAncestor(message.hwnd,GA_ROOT);
  for(const auto& panel:impl_->panels)if(panel->window==root)return IsDialogMessageW(root,&message)!=FALSE;
  for(const auto& owner:impl_->frames)if(owner->site_bubble&&owner->site_bubble==root)return IsDialogMessageW(root,&message)!=FALSE;
  auto frame=impl_->frame_of(root);if(!frame)return false;
  if(message.message==WM_KEYDOWN||message.message==WM_SYSKEYDOWN){const bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0,alt=(GetKeyState(VK_MENU)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;const auto key=message.wParam;
    const auto run=[&](int id){frame->command(id);return true;};
    if(key==VK_ESCAPE&&frame->developer_picking){frame->pick_developer();return true;}
    if(frame->developer_picking){const auto host=impl_->hosts.find(frame->selected);if(host!=impl_->hosts.end()&&(message.hwnd==host->second.window||IsChild(host->second.window,message.hwnd)))return true;}
    if(key==VK_F11&&!control&&!alt){frame->toggle_fullscreen();return true;}
    if(key==VK_ESCAPE&&message.message==WM_KEYDOWN&&frame->page_screen){frame->leave_page_fullscreen();return true;}
    if(control&&shift&&key=='X')return run(Controls);
    if((control&&key=='L')||(alt&&key=='D')||key==VK_F6){if(frame->fullscreen())frame->leave_fullscreen();frame->focus_address();return true;}
    if(control&&key=='F')return run(MenuFind);
    if(key==VK_F3&&frame->find_visible)return run(shift?FindPrevious:FindNext);
    if(control&&shift&&key=='T')return run(MenuReopenTab);
    if(control&&key=='T')return run(NewTab);
    if(control&&shift&&key=='N')return run(MenuPrivate);
    if(control&&key=='N')return run(MenuNewWindow);
    if(control&&shift&&key=='W')return run(MenuCloseWindow);
    if(control&&(key=='W'||key==VK_F4))return run(MenuCloseTab);
    if(control&&key==VK_TAB){frame->cycle(shift?-1:1);return true;}
    if(control&&(key==VK_NEXT||key==VK_PRIOR)){frame->cycle(key==VK_NEXT?1:-1);return true;}
    if(control&&!alt&&key>='1'&&key<='9'){frame->select_number(static_cast<int>(key-'0'));return true;}
    if(control&&key=='D')return run(MenuBookmark);
    if(control&&key=='P')return run(MenuPrint);
    if(control&&key=='H')return run(MenuHistory);
    if(control&&shift&&key==VK_DELETE)return run(MenuClearData);
    if(control&&shift&&key=='O')return run(MenuBookmarks);
    if(control&&key=='J')return run(MenuDownloads);
    if(control&&key==VK_OEM_PLUS)return run(MenuZoomIn);
    if(control&&key==VK_OEM_MINUS)return run(MenuZoomOut);
    if(control&&key=='0')return run(MenuZoomReset);
    if(key==VK_F5||(control&&key=='R')){if(shift||(control&&key==VK_F5))impl_->engine.native_command(frame->selected,"reload-hard");else frame->command(Reload);return true;}
    if(alt&&key==VK_LEFT)return run(Back);
    if(alt&&key==VK_RIGHT)return run(Forward);
  }
  if(ui::edit_command(message,frame->address)||ui::edit_command(message,frame->find_text))return true;
  for(const auto& [id,control]:frame->controls)if(message.hwnd==control)return IsDialogMessageW(frame->window,&message)!=FALSE;
  return false;
}
void BrowserShell::permission(const std::string& tab,const std::string& origin,const std::string& description,std::function<void(bool)> answer){
  auto found=impl_->hosts.find(tab);auto frame=found!=impl_->hosts.end()&&found->second.frame?found->second.frame:impl_->live_active();
  if(!frame){if(answer)answer(false);return;}
  impl_->panel(*frame,"permission",ui::tr8("Website: ")+origin+ui::tr8("\n\nRequested access: ")+description+ui::tr8("\n\nAllow only if you intended to give this website access."),std::move(answer),tab);}
void BrowserShell::page_fullscreen(const std::string& tab,bool fullscreen){
  if(auto found=impl_->hosts.find(tab);found!=impl_->hosts.end()&&found->second.frame&&found->second.frame->window)found->second.frame->page_fullscreen(tab,fullscreen);}
void BrowserShell::external_link(const std::string& tab,const std::string& origin,const std::string& url,const std::string& scheme,bool permitted,std::function<void(bool)> done){
  const auto found=impl_->hosts.find(tab);auto frame=found!=impl_->hosts.end()?found->second.frame:nullptr;
  // Only the page the person is looking at may ask; background requests are dropped.
  if(!frame||!frame->window||frame->closing||frame->selected!=tab){done(false);return;}
  if(!permitted){frame->set_status(ui::tr(L"A link to another app was blocked because it did not come from you."));done(false);return;}
  const auto app=external_app_name(scheme);
  if(app.empty()){frame->set_status(ui::format(ui::tr(L"No app is set up to open {0}: links."),{ui::wide(scheme)}));done(false);return;}
  auto shown=omnibox::printable(ui::wide(url));if(shown.size()>200)shown=shown.substr(0,199)+L"…";
  const auto text=ui::format(ui::tr(L"Website: {0}\n\nThis site wants to open {1} for a {2}: link:\n{3}\n\nOpen it only if you expected this. The app receives the whole link."),{ui::wide(origin.empty()?ui::tr8("this page"):origin),app,ui::wide(scheme),shown});
  impl_->panel(*frame,"external",ui::utf8(text),[done,url](bool open){if(open)launch_external(url);done(open);},tab);
}
void BrowserShell::screen_share(const std::string& tab,const std::string& origin,bool audio,std::function<void(bool)> answer){
  const auto found=impl_->hosts.find(tab);auto frame=found!=impl_->hosts.end()?found->second.frame:nullptr;
  if(!frame||!frame->window||frame->closing||frame->selected!=tab){answer(false);return;}
  impl_->panel(*frame,"permission",ui::tr8("Website: ")+origin+ui::tr8("\n\nRequested access: ")+(audio?ui::tr8("Your entire screen and the sound playing on this computer"):ui::tr8("Your entire screen"))+
    ui::tr8("\n\nXenon cannot share a single window or tab. The site sees everything on all of your displays, including other apps and notifications, until sharing stops. Agents cannot take screenshots of this tab while it shares your screen.\n\nAllow only if you intended to share your screen."),std::move(answer),tab);
}
void BrowserShell::open_external_url(const std::string& url){
  if(!web_address(url))return;auto frame=impl_->live_active();if(!frame)frame=impl_->new_window(nullptr);if(!frame)return;
  if(IsIconic(frame->window))ShowWindow(frame->window,SW_RESTORE);SetForegroundWindow(frame->window);
  frame->open_tab("native-default",url);
}
void BrowserShell::open_link(const std::string& tab,const std::string& url,bool window){
  auto found=impl_->hosts.find(tab);if(found==impl_->hosts.end()||!web_address(url))return;auto frame=found->second.frame;if(!frame)return;
  if(window){if(auto created=impl_->new_window(frame))created->open_tab(found->second.workspace,url);return;}
  // A link opened in a new tab lands beside its source and stays in the background.
  const auto tabs=frame->visual_tabs();auto next=std::find(tabs.begin(),tabs.end(),tab);
  frame->open_tab(found->second.workspace,url,next!=tabs.end()&&std::next(next)!=tabs.end()?*std::next(next):std::string{},false);
}
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
void BrowserShell::fixture_snapshot(const std::filesystem::path& destination){impl_->fixture_snapshot(destination);}
void BrowserShell::Impl::fixture_snapshot(const std::filesystem::path& destination){
  if(destination.parent_path().filename()!=L".cache"||!destination.filename().wstring().starts_with(L"shell-ui-"))return;
  std::ifstream marker(destination/"SYNTHETIC_TEST_PROFILE");std::string contents;std::getline(marker,contents);if(contents!="XENON_SYNTHETIC_UI_FIXTURE")return;
  fixture_destination=destination;auto frame=primary();if(!frame)return;const auto window=frame->window;
  RECT rect{};GetWindowRect(window,&rect);wchar_t title[256]{},type[128]{},desktop[128]{};GetWindowTextW(window,title,256);GetClassNameW(window,type,128);DWORD size{};GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof(desktop),&size);
  Json buttons=Json::array();for(const auto& [id,control]:frame->controls)if((GetWindowLongPtrW(control,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW){const auto counts=fixture_paints[id];buttons.push_back({{"id",id},{"hover",GetPropW(control,L"XenonHover")!=nullptr},{"paints",counts.first},{"hoverPaints",counts.second}});}
  int footer_clearance{};if(auto found=hosts.find(frame->selected);found!=hosts.end()){RECT host{};GetWindowRect(found->second.window,&host);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&host),2);footer_clearance=frame->panel_bounds.bottom-host.bottom;}
  const auto colors=ui::palette();auto rgb=[](COLORREF value){return Json::array({GetRValue(value),GetGValue(value),GetBValue(value)});};
  Json value={{"visible",IsWindowVisible(window)!=FALSE},{"class",ui::utf8(type)},{"title",ui::utf8(title)},{"desktop",ui::utf8(desktop)},
    {"width",rect.right-rect.left},{"height",rect.bottom-rect.top},{"hosts",hosts.size()},{"windows",frames.size()},{"selected",frame->selected},{"style",GetWindowLongPtrW(window,GWL_STYLE)},
    {"dpi",GetDpiForWindow(window)},{"dpiAwareness",GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(window))},
    {"perMonitorV2",AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(window),DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)!=FALSE},{"antialiasedVectors",ui::vector_rendering()},
    {"sidebarWidth",frame->sidebar},{"preferredSidebarWidth",ui::sidebar_width},{"sidebarDragging",frame->resizing_sidebar},{"footerClearance",footer_clearance},{"buttons",buttons},
    {"palette",{{"canvas",rgb(colors.canvas)},{"surface",rgb(colors.surface)},{"hover",rgb(ui::hover_background())},{"selection",rgb(ui::selection())}}}};
  const auto target=destination/"native-shell-state.json",temporary=destination/"native-shell-state.json.tmp";
  {std::ofstream output(temporary);output<<value.dump(2);}local_security::restrict_path(temporary);MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
#endif
}
