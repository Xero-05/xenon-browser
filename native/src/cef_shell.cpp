#include "xenon/browser_shell.hpp"
#include "xenon/broker.hpp"
#include "xenon/cef_engine.hpp"
#include "xenon/browser_data.hpp"
#include "xenon/extension_store.hpp"
#include "xenon/native_ui.hpp"
#include "xenon/ui_theme.hpp"
#include "xenon/introduction.hpp"
#include "xenon/pointer_motion.hpp"
#include "xenon/pointer_overlay.hpp"
#include "xenon/local_security.hpp"
#include "xenon/version.hpp"
#include "include/cef_parser.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <vector>
#include <utility>

namespace xenon {
namespace {
constexpr UINT ShellChanged=WM_APP+90,ShellRun=WM_APP+91;
// Control and command IDs are stable: native test drivers address 2004
// (address), 2006 (Controls) and the theme commands 2111-2113 by number.
enum ShellId {Back=2001,Forward,Reload,Address,Go,Controls,Menu,NewTab,Tree,FindText,FindNext,FindPrevious,FindClose,Extensions,Passwords,BookmarkStar,
  MenuFind=2100,MenuZoomIn,MenuZoomOut,MenuZoomReset,MenuBookmark,MenuBookmarks,MenuHistory,MenuDownloads,MenuPrint,MenuPdf,
  MenuPermissions,MenuThemeSystem,MenuThemeLight,MenuThemeDark,MenuAbout,MenuNotices,MenuExit,MenuPrivate,MenuCloseTab,
  MenuSidebarNarrower,MenuSidebarWider,MenuSidebarReset,MenuUpdates,MenuQuickTour,MenuDocumentation,
  MenuNewWindow,MenuReopenTab,MenuCloseWorkspaceTabs,MenuCloseWindow,MenuManageExtensions,MenuPasswords,MenuNewTab,
  MenuLanguageEnglish=2200,MenuLanguageChinese,
  TabNewBelow=2300,TabReload,TabDuplicate,TabPin,TabMute,TabCopyLink,TabBookmark,TabNewWindow,TabTakeOwnership,TabControls,TabClose,TabCloseOthers,TabCloseBelow,TabCloseWorkspace,
  GroupNewTab=2350,GroupToggle,GroupCloseAll,GroupConfigure,
  OwnerTake=2370,OwnerControls,
  TabToWorkspace=2400,TabToWindow=2500,OwnerGive=2560,ExtensionItem=2600};
static_assert(Passwords==ui::password_anchor_id,"Credential bubbles anchor to the toolbar key button");
std::string str(const Json& value,const char* key){auto found=value.find(key);return found!=value.end()&&found->is_string()?found->get<std::string>():std::string{};}
std::wstring short_title(const std::string& value,size_t maximum=55){auto text=ui::wide(value);if(text.size()>maximum)text=text.substr(0,maximum-1)+L"…";return text;}
using Clock=std::chrono::steady_clock;
double fraction(Clock::time_point start,int duration){return std::clamp(std::chrono::duration<double,std::milli>(Clock::now()-start).count()/duration,0.0,1.0);}
uint32_t seed(){return static_cast<uint32_t>(std::stoul(local_security::random_hex(4),nullptr,16));}
// Native chrome motion timings in milliseconds. Website pixels and geometry
// never animate; these only change how the shell around the page is painted.
constexpr int GlideDuration=200,AccentDuration=240,FocusDuration=160,LoadingCycle=1300;
// Desktop toolbar height, row heights and the page frame's corner radius, in DIPs.
constexpr int ToolbarHeight=44,TabRow=34,FrameRadius=8;
double elapsed(ULONGLONG start,int duration){return start&&ui::motion()?std::clamp(static_cast<double>(GetTickCount64()-start)/duration,0.0,1.0):1.0;}
bool web_address(const std::string& url){return url.rfind("http://",0)==0||url.rfind("https://",0)==0;}
std::string origin_of(const std::string& url){if(!web_address(url))return {};const auto start=url.find("://")+3;return url.substr(0,url.find_first_of("/?#",start));}
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
    PointerPoint display,from,to;Clock::time_point animation;int duration{120};PointerOverlayTransitions transitions;std::string recorded_url;};
  // Where the next human tab for a workspace opens: a window and position.
  struct Placement {std::string workspace;HWND frame{};std::string before;bool select{true};ULONGLONG at{};};
  struct Panel {Impl* owner{};HWND window{},list{},message{},parent{};HFONT font{};std::string kind,workspace,tab;Json rows=Json::array();bool private_mode{},answered{};std::function<void(bool)> answer;};
  // A tab being dragged in the sidebar. Targets are recomputed on each move.
  struct Drag {HWND source{};std::string tab;bool active{},finishing{};HWND ghost{};std::wstring title,note;
    HWND target{};std::string workspace,before;bool outside{},blocked{},below{};HTREEITEM line{},group{};POINT point{};};
  struct Mailbox {std::mutex mutex;std::deque<std::function<void()>> calls;};
  struct Closed {std::string workspace,url;};

  struct Frame {
    Impl& shell;HWND window{},tree{},address{},find_bar{},find_text{},cursor{},tooltips{};HFONT font{},bold{};std::map<int,HWND> controls;
    std::string selected;std::vector<std::pair<std::string,bool>> tree_keys;Json tree_signature,tree_titles;bool rebuilding{},find_visible{},closing{};ULONGLONG closing_since{};
    RECT page{},panel_bounds{},omnibox{},chip{};std::wstring status,chip_text;ULONGLONG status_until{};
    int sidebar{240},drag_origin{},drag_width{};bool resizing_sidebar{},sidebar_hover{};
    bool address_dirty{},setting_address{};HTREEITEM hovered_row{};std::string pressed_close,hovered_close;
    // Sampled while painting; animate() only invalidates what is still moving.
    ui::Fade address_focus;std::map<HTREEITEM,ui::Fade> row_fades;RECT glide_from{},pill{};ULONGLONG glide_start{},accent_start{};
    COLORREF accent_from{},accent_to{};bool accent_ready{},pill_valid{},tree_moving{},window_moving{},was_loading{};
    size_t saved_accounts{};bool bookmarked{};std::string checked_url,last_find,update_version;
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
          {Passwords,ui::tr(L"Saved passwords for this site")},{BookmarkStar,ui::tr(L"Bookmark this tab")},{NewTab,ui::tr(L"New tab")},{FindPrevious,ui::tr(L"Previous match")},{FindNext,ui::tr(L"Next match")},{FindClose,ui::tr(L"Close find")}}){
        const bool find=id==FindPrevious||id==FindNext||id==FindClose;auto control=add(id,L"BUTTON",title,BS_OWNERDRAW|WS_TABSTOP,find?find_bar:nullptr);tip(control,title);}
      for(int id:{Passwords,BookmarkStar,FindPrevious,FindNext,FindClose})SetPropW(controls.at(id),L"XenonSurface",reinterpret_cast<HANDLE>(1));
      address=add(Address,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL);
      find_text=add(FindText,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,find_bar);
      tree=add(Tree,WC_TREEVIEWW,ui::tr(L"Workspace tabs"),WS_TABSTOP|TVS_HASBUTTONS|TVS_LINESATROOT|TVS_SHOWSELALWAYS|TVS_FULLROWSELECT|TVS_NOHSCROLL);
      SetWindowSubclass(tree,tree_proc,3,reinterpret_cast<DWORD_PTR>(this));TreeView_SetExtendedStyle(tree,TVS_EX_DOUBLEBUFFER,TVS_EX_DOUBLEBUFFER);TreeView_SetItemHeight(tree,d(TabRow));
      // The scrollbar sits on the sidebar's outer edge, so it never separates
      // the selected tab from the page frame it joins.
      SetWindowLongPtrW(tree,GWL_EXSTYLE,GetWindowLongPtrW(tree,GWL_EXSTYLE)|WS_EX_LEFTSCROLLBAR);SetWindowPos(tree,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
      SetWindowSubclass(address,edit_proc,1,reinterpret_cast<DWORD_PTR>(this));SetWindowSubclass(find_text,edit_proc,2,reinterpret_cast<DWORD_PTR>(this));
      cursor=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"XenonAgentCursor",ui::tr(L"Agent pointer"),WS_POPUP,0,0,25,30,window,nullptr,branding_module(),this);
      SetLayeredWindowAttributes(cursor,RGB(255,0,255),255,LWA_COLORKEY);
      sidebar=ui::sidebar_width;theme();layout();
    }
    void theme(){auto colors=ui::palette();ui::frame(window);TreeView_SetBkColor(tree,colors.canvas);TreeView_SetTextColor(tree,colors.ink);TreeView_SetLineColor(tree,colors.border);
      SetWindowTheme(tooltips,L"",L"");SendMessageW(tooltips,TTM_SETTIPBKCOLOR,colors.surface,0);SendMessageW(tooltips,TTM_SETTIPTEXTCOLOR,colors.ink,0);
      for(const auto& [id,control]:controls){ui::control_theme(control);InvalidateRect(control,nullptr,TRUE);}InvalidateRect(window,nullptr,TRUE);InvalidateRect(find_bar,nullptr,TRUE);}
    void place(int id,int x,int y,int width,int height){MoveWindow(controls.at(id),d(x),d(y),d(width),d(height),TRUE);}
    // Chip text: a transient status message, otherwise the selected tab's owner
    // once the broker reports it. A tab that is still opening shows no chip, so
    // the omnibox does not shrink and regrow for every new tab.
    std::wstring current_chip() const {return !status.empty()&&GetTickCount64()<status_until?status:shell.hosts.contains(selected)&&shell.states.contains(selected)?state_text(selected):std::wstring{};}
    int text_width(const std::wstring& text) const {auto dc=GetDC(window);auto old=SelectObject(dc,font);SIZE size{};GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&size);SelectObject(dc,old);ReleaseDC(window,dc);return MulDiv(size.cx,96,GetDpiForWindow(window));}
    void layout(){RECT rect{};GetClientRect(window,&rect);const int scale=GetDpiForWindow(window);const int width=MulDiv(rect.right,96,scale),height=MulDiv(rect.bottom,96,scale);
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
      place(BookmarkStar,omni_right-33,8,28,28);place(Passwords,omni_right-63,8,28,28);
      place(Address,omni_left+16,13,std::max(60,omni_right-(key?68:38)-(omni_left+16)),19);
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
      page={panel_bounds.left,panel_bounds.top+d(FrameRadius),panel_bounds.right,panel_bounds.bottom-d(FrameRadius)};
      RECT inner=page;InflateRect(&inner,-d(2),-d(2));
      for(auto& [id,host]:shell.hosts)if(host.frame==this){SetWindowPos(host.window,nullptr,inner.left,inner.top,std::max<int>(1,inner.right-inner.left),std::max<int>(1,inner.bottom-inner.top),SWP_NOACTIVATE|SWP_NOZORDER);
        if(host.browser)SetWindowPos(host.browser,nullptr,0,0,std::max<int>(1,inner.right-inner.left),std::max<int>(1,inner.bottom-inner.top),SWP_NOACTIVATE|SWP_NOZORDER);}
      // Find floats over the page's top-right corner, as in Chromium; it
      // never resizes the page.
      SetWindowPos(find_bar,HWND_TOP,inner.right-d(356),inner.top+d(6),d(344),d(40),SWP_NOACTIVATE|(find_visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
      MoveWindow(find_text,d(12),d(9),d(200),d(22),TRUE);MoveWindow(controls.at(FindPrevious),d(220),d(4),d(32),d(32),TRUE);MoveWindow(controls.at(FindNext),d(256),d(4),d(32),d(32),TRUE);MoveWindow(controls.at(FindClose),d(304),d(4),d(32),d(32),TRUE);
      InvalidateRect(window,nullptr,TRUE);
    }
    bool sidebar_hit(POINT point) const {RECT bounds{};GetClientRect(window,&bounds);return point.x>=d(sidebar-6)&&point.x<d(sidebar+1)&&point.y>=d(ToolbarHeight)&&point.y<bounds.bottom-d(8);}
    RECT sidebar_grip() const {RECT bounds{};GetClientRect(window,&bounds);const auto middle=(d(ToolbarHeight)+bounds.bottom)/2;return {d(sidebar-5),middle-d(16),d(sidebar-2),middle+d(16)};}
    void invalidate_grip(){auto rect=sidebar_grip();InvalidateRect(window,&rect,FALSE);}
    void set_address(const std::string& value){if(ui::text(address)==value)return;setting_address=true;SetWindowTextW(address,ui::wide(value).c_str());setting_address=false;}
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
      if(selected!=id){RECT from{};const bool visible=pill_rect(from);glide_from=from;glide_start=visible&&ui::motion()?GetTickCount64():0;address_dirty=false;set_address(str(found->second.metadata,"url"));
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
    COLORREF color(const std::string& id) const {const auto colors=ui::palette();auto found=shell.states.find(id);if(found==shell.states.end()||!found->second.value("agentAvailable",false))return colors.gray;return found->second.value("humanPaused",false)?colors.orange:colors.teal;}
    std::wstring state_text(const std::string& id) const {auto found=shell.states.find(id);if(found==shell.states.end())return ui::tr(L"Opening");const auto& state=found->second;
      auto owner=str(state,"ownerSessionId");std::wstring value=owner=="human"?ui::tr(L"You"):owner.empty()?ui::tr(L"No agent"):ui::wide(shell.names.contains(owner)?shell.names.at(owner):ui::tr8("Agent"));
      if(state.value("agentAvailable",false)&&state.value("humanPaused",false))value+=ui::tr(L" · Paused for you");else if(state.value("handoffPending",false))value+=ui::tr(L" · Handing off");
      else if(owner!="human"&&!owner.empty())value+=state.value("agentAvailable",false)?ui::tr(L" · Agent control"):ui::tr(L" · Unavailable");
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
      auto title=found==shell.hosts.end()?ui::tr(L"Xenon Browser"):short_title(ui::tab_title(str(metadata,"title"),str(metadata,"url")),100)+L" — Xenon";if(ui::text(window)!=ui::utf8(title))SetWindowTextW(window,title.c_str());
      // An available update marks Controls with a blue dot until it is installed.
      if(auto version=shell.native.available_update();version!=update_version){update_version=std::move(version);InvalidateRect(controls.at(Controls),nullptr,FALSE);}
      auto caption=ui::tr(L"Controls")+(shell.pairing_count?L" ("+std::to_wstring(shell.pairing_count)+L")":std::wstring{});
      if(!update_version.empty())caption+=L" · "+ui::format(ui::tr(L"Xenon {0} is available"),{ui::wide(update_version)});
      if(ui::text(controls.at(Controls))!=ui::utf8(caption))retitle(Controls,caption.c_str());
      if(current_chip()!=chip_text)layout();
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
      tree_moving=tree_now;window_moving=window_now;was_loading=loading_now;
      if(!status.empty()&&GetTickCount64()>=status_until){status.clear();layout();}
    }
    void draw_tree(NMTVCUSTOMDRAW& draw){const auto index=draw.nmcd.lItemlParam;if(index<=0||static_cast<size_t>(index)>tree_keys.size())return;const auto& [id,group]=tree_keys[index-1];auto colors=ui::palette();const auto dc=draw.nmcd.hdc;const auto item=reinterpret_cast<HTREEITEM>(draw.nmcd.dwItemSpec);
      RECT bounds{};GetClientRect(tree,&bounds);auto rect=draw.nmcd.rc;rect.left=0;rect.right=bounds.right;const auto row=rect;ui::fill(dc,row,colors.canvas);
      RECT label{};TreeView_GetItemRect(tree,item,&label,TRUE);label.right=bounds.right-d(12);
      const double hover=row_hover(item);const bool emphasized=hover>=0.5&&colors.contrast;const auto hover_fill=ui::mix(colors.canvas,ui::hover_background(),hover);
      POINT pointer{};GetCursorPos(&pointer);ScreenToClient(tree,&pointer);
      // While the selection glides, each row paints its own slice of the moving pill.
      const auto glide=[&]{RECT overlap{};if(!gliding()||!pill_valid||!IntersectRect(&overlap,&pill,&row))return;const int saved=SaveDC(dc);IntersectClipRect(dc,row.left,row.top,row.right,row.bottom);
        ui::rounded(dc,pill,colors.surface,accent(),d(8),d(2));RestoreDC(dc,saved);};
      const auto& drag=shell.drag;const bool drop_here=drag.active&&drag.target==window;
      if(group){auto background=rect;InflateRect(&background,-d(2),-d(3));
        if(drop_here&&drag.group==item)ui::rounded(dc,background,ui::hover_background(),drag.blocked?colors.gray:colors.teal,d(6),d(2));
        else if(hover>0)ui::rounded(dc,background,hover_fill,hover_fill,d(6));glide();
        TVITEMW value{};value.hItem=item;value.mask=TVIF_STATE;value.stateMask=TVIS_EXPANDED;TreeView_GetItem(tree,&value);RECT arrow=label;arrow.left=d(2);arrow.right=d(22);ui::icon(dc,arrow,value.state&TVIS_EXPANDED?ui::Icon::down:ui::Icon::chevron_right,emphasized?ui::selection_ink():colors.muted,window);
        const bool hovered=item==hovered_row;std::vector<std::pair<int,ui::Icon>> buttons{{GroupPlus,ui::Icon::plus}};
        if(hovered)buttons.push_back({GroupMore,ui::Icon::more});
        for(const auto& [button,glyph]:buttons){auto target=group_button(item,button);if(hovered&&PtInRect(&target,pointer))ui::rounded(dc,target,ui::hover_background(),ui::hover_background(),d(5));
          ui::icon(dc,target,glyph,colors.muted,window);label.right=std::min(label.right,target.left-d(4));}
        label.left=d(24);ui::text(dc,label,ui::wide(shell.workspace_names.contains(id)?shell.workspace_names.at(id):ui::tr8("Workspace")),bold,emphasized?ui::selection_ink():colors.muted);return;}
      const bool active=id==selected,settled=active&&!gliding();rect.left=d(4);InflateRect(&rect,0,-d(3));rect.right-=active?0:d(6);auto shape=rect;if(active)shape.right+=d(16);
      const auto state=shell.states.find(id);const bool agent=!active&&state!=shell.states.end()&&state->second.value("agentAvailable",false)&&!state->second.value("humanPaused",false);
      const auto background=settled?colors.surface:hover_fill;
      ui::rounded(dc,shape,background,settled?accent():agent?color(id):background,d(8),d(settled?2:1));glide();
      const auto found=shell.hosts.find(id);const bool pinned=found!=shell.hosts.end()&&found->second.pinned,muted=found!=shell.hosts.end()&&found->second.metadata.value("muted",false);
      // Close stays visible on every unpinned tab, quieter until hovered or selected.
      auto close=close_rect(item);
      if(!pinned){if(item==hovered_row&&PtInRect(&close,pointer))ui::rounded(dc,close,ui::hover_background(),ui::hover_background(),d(5));
        const double reveal=colors.contrast||active?1.0:0.45+0.55*hover;
        ui::icon(dc,close,ui::Icon::close,emphasized&&!active?ui::selection_ink():ui::mix(active?colors.surface:background,colors.muted,reveal),window);}
      else close.left=close.right;
      label.left=d(14);label.right=close.left-d(3);
      if(pinned){RECT glyph{label.left-d(2),rect.top,label.left+d(16),rect.bottom};ui::icon(dc,glyph,ui::Icon::pin,colors.muted,window);label.left+=d(20);}
      if(muted){RECT glyph{label.right-d(20),rect.top,label.right,rect.bottom};ui::icon(dc,glyph,ui::Icon::muted,colors.muted,window);label.right-=d(22);}
      const auto title=found==shell.hosts.end()?ui::tr8("New tab"):ui::tab_title(str(found->second.metadata,"title"),str(found->second.metadata,"url"));ui::text(dc,label,ui::wide(title),font,emphasized&&!active?ui::selection_ink():colors.ink);
      if(drop_here&&drag.line==item){const int y=drag.below?row.bottom-d(1):row.top+d(1);ui::fill(dc,{d(8),y-d(1),bounds.right-d(8),y+d(1)},drag.blocked?colors.gray:colors.teal);}
      if((draw.nmcd.uItemState&CDIS_FOCUS)&&GetFocus()==tree&&!gliding()){auto focus=rect;InflateRect(&focus,-d(3),-d(3));ui::focus_mark(dc,focus,tree,5);}
    }
    void paint(HDC dc){RECT rect{};GetClientRect(window,&rect);const auto colors=ui::palette();const auto tint=accent();ui::fill(dc,rect,colors.canvas);
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
    void update_cursor(){auto found=shell.hosts.find(selected);if(found==shell.hosts.end()||found->second.frame!=this||!found->second.agent||!IsWindowVisible(window)||IsIconic(window)){ShowWindow(cursor,SW_HIDE);return;}auto& host=found->second;
      if(host.animating){const auto t=fraction(host.animation,host.duration),f=t*t*(3-2*t);host.display={host.from.x+(host.to.x-host.from.x)*f,host.from.y+(host.to.y-host.from.y)*f};if(t>=1)host.animating=false;}
      RECT rect{};GetClientRect(host.window,&rect);const double scale=GetDpiForWindow(host.window)/96.0*host.metadata.value("zoom",1.0);POINT point{static_cast<LONG>(std::clamp(host.display.x*scale,0.0,std::max(0.0,rect.right-25.0))),static_cast<LONG>(std::clamp(host.display.y*scale,0.0,std::max(0.0,rect.bottom-30.0)))};ClientToScreen(host.window,&point);
      SetWindowPos(cursor,nullptr,point.x,point.y,d(25),d(30),SWP_NOACTIVATE|SWP_NOZORDER|SWP_SHOWWINDOW);InvalidateRect(cursor,nullptr,FALSE);
    }
    std::string navigate_value(){auto input=ui::text(address);if(input=="about:blank"||input.rfind("http://",0)==0||input.rfind("https://",0)==0)return input;
      // Ctrl+Enter completes a single word as www.<word>.com, as in Chromium.
      if((GetKeyState(VK_CONTROL)&0x8000)&&!input.empty()&&input.find_first_of(" .:/")==std::string::npos)return "https://www."+input+".com";
      if(input.find(' ')==std::string::npos&&input.find('.')!=std::string::npos&&input.find(':')==std::string::npos)return "https://"+input;
      return "https://www.google.com/search?q="+CefURIEncode(input,true).ToString();
    }
    void navigate(){if(!shell.hosts.contains(selected)){open_tab(workspace(),navigate_value());address_dirty=false;return;}
      const auto value=navigate_value();address_dirty=false;const auto target=window;auto& shell_ref=shell;
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
      item(popup_menu,MenuHistory,ui::tr(L"History\tCtrl+H"));item(popup_menu,MenuDownloads,ui::tr(L"Downloads\tCtrl+J"));item(popup_menu,MenuBookmarks,ui::tr(L"Bookmarks"));item(popup_menu,MenuPasswords,ui::tr(L"Passwords"));item(popup_menu,MenuManageExtensions,ui::tr(L"Extensions"));separator(popup_menu);
      item(popup_menu,MenuReopenTab,ui::tr(L"Reopen closed tab\tCtrl+Shift+T"),!shell.closed_tabs.empty());item(popup_menu,MenuCloseWorkspaceTabs,ui::tr(L"Close all tabs in this workspace…"),shell.workspace_tab_count(workspace())>0);separator(popup_menu);
      auto zoom=CreatePopupMenu();item(zoom,MenuZoomIn,ui::tr(L"Zoom in\tCtrl++"));item(zoom,MenuZoomOut,ui::tr(L"Zoom out\tCtrl+-"));item(zoom,MenuZoomReset,ui::tr(L"Reset zoom\tCtrl+0"));AppendMenuW(popup_menu,MF_POPUP,reinterpret_cast<UINT_PTR>(zoom),ui::tr(L"Zoom"));
      item(popup_menu,MenuFind,ui::tr(L"Find on page\tCtrl+F"));item(popup_menu,MenuBookmark,ui::tr(L"Bookmark this page\tCtrl+D"));item(popup_menu,MenuPrint,ui::tr(L"Print\tCtrl+P"));item(popup_menu,MenuPdf,ui::tr(L"Save as PDF…"));item(popup_menu,MenuPermissions,ui::tr(L"Site permissions"));separator(popup_menu);
      auto appearance=CreatePopupMenu();item(appearance,MenuThemeSystem,ui::tr(L"Theme: System"));item(appearance,MenuThemeLight,ui::tr(L"Theme: Light"));item(appearance,MenuThemeDark,ui::tr(L"Theme: Dark"));
      CheckMenuRadioItem(appearance,MenuThemeSystem,MenuThemeDark,ui::theme_mode==ui::ThemeMode::system?MenuThemeSystem:ui::theme_mode==ui::ThemeMode::light?MenuThemeLight:MenuThemeDark,MF_BYCOMMAND);
      AppendMenuW(appearance,MF_SEPARATOR,0,nullptr);item(appearance,MenuSidebarNarrower,ui::tr(L"Narrower sidebar"));item(appearance,MenuSidebarWider,ui::tr(L"Wider sidebar"));item(appearance,MenuSidebarReset,ui::tr(L"Reset sidebar width"));
      AppendMenuW(popup_menu,MF_POPUP,reinterpret_cast<UINT_PTR>(appearance),ui::tr(L"Appearance"));
      auto languages=CreatePopupMenu();AppendMenuW(languages,MF_STRING,MenuLanguageEnglish,L"English");AppendMenuW(languages,MF_STRING,MenuLanguageChinese,L"简体中文");
      CheckMenuRadioItem(languages,MenuLanguageEnglish,MenuLanguageChinese,ui::preferred_language==ui::Language::simplified_chinese?MenuLanguageChinese:MenuLanguageEnglish,MF_BYCOMMAND);
      AppendMenuW(popup_menu,MF_POPUP,reinterpret_cast<UINT_PTR>(languages),L"Language / 语言");
      auto help=CreatePopupMenu();item(help,MenuQuickTour,ui::tr(L"Quick tour"));item(help,MenuDocumentation,ui::tr(L"GitHub documentation"));item(help,MenuUpdates,ui::tr(L"Check for updates"));item(help,MenuAbout,ui::tr(L"About Xenon"));item(help,MenuNotices,ui::tr(L"Third-party notices"));
      AppendMenuW(popup_menu,MF_POPUP,reinterpret_cast<UINT_PTR>(help),ui::tr(L"Help"));separator(popup_menu);
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
      AppendMenuW(menu_handle,MF_STRING|(owner=="human"?MF_GRAYED|MF_CHECKED:0),OwnerTake,ui::tr(L"Take ownership"));
      if(owner=="human")for(const auto& worker:shell.workers)if(str(worker,"workspaceId")==workspace()&&worker.value("connected",false)&&str(worker,"state")=="connected"&&recipients.size()<32){
        recipients.push_back(str(worker,"agentSessionId"));AppendMenuW(menu_handle,MF_STRING,OwnerGive+static_cast<int>(recipients.size())-1,ui::format(ui::tr(L"Give to {0}"),{ui::wide(str(worker,"name"))}).c_str());}
      AppendMenuW(menu_handle,MF_SEPARATOR,0,nullptr);AppendMenuW(menu_handle,MF_STRING,OwnerControls,ui::tr(L"Xenon Controls…"));
      POINT at{chip.left,chip.bottom};ClientToScreen(window,&at);const auto chosen=TrackPopupMenu(menu_handle,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN,at.x,at.y+d(2),0,window,nullptr);DestroyMenu(menu_handle);
      if(chosen==OwnerTake){shell.broker.human_acquire(selected);set_status(ui::tr(L"Ownership requested. Current agent input finishes first."));}
      else if(chosen>=OwnerGive&&chosen<OwnerGive+32&&static_cast<size_t>(chosen-OwnerGive)<recipients.size()){shell.broker.human_release(selected,recipients[static_cast<size_t>(chosen-OwnerGive)]);set_status(ui::tr(L"Control offered to the selected agent."));}
      else if(chosen==OwnerControls)shell.native.show_section(NativeUi::Section::workspaces,workspace());
    }
    void tab_menu(const std::string& id,POINT at);
    void group_menu(const std::string& workspace_id,POINT at);
    void command(int id);
    static LRESULT CALLBACK edit_proc(HWND control,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR owner){auto self=reinterpret_cast<Frame*>(owner);
      if(message==WM_KEYDOWN&&wp==VK_RETURN){if(control==self->address)self->navigate();
        else {const auto text=ui::text(control);self->shell.engine.native_command(self->selected,(GetKeyState(VK_SHIFT)&0x8000)?"find-previous":text==self->last_find?"find-next":"find",text);self->last_find=text;}return 0;}
      if(message==WM_KEYDOWN&&wp==VK_ESCAPE){if(control==self->find_text)self->hide_find();else {self->address_dirty=false;auto found=self->shell.hosts.find(self->selected);self->set_address(found==self->shell.hosts.end()?std::string{}:str(found->second.metadata,"url"));self->shell.engine.native_command(self->selected,"focus");}return 0;}
      // The first click into the address selects all of it, as in Chromium.
      if(message==WM_LBUTTONDOWN&&control==self->address&&GetFocus()!=control){SetFocus(control);SendMessageW(control,EM_SETSEL,0,-1);return 0;}
      if(message==WM_NCDESTROY)RemoveWindowSubclass(control,edit_proc,control==self->address?1:2);
      return DefSubclassProc(control,message,wp,lp);
    }
    static LRESULT CALLBACK tree_proc(HWND control,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR owner){auto self=reinterpret_cast<Frame*>(owner);auto& shell=self->shell;
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
  size_t workspace_tab_count(const std::string& workspace) const {return static_cast<size_t>(std::count_if(states.begin(),states.end(),[&](const auto& entry){return str(entry.second,"workspaceId")==workspace;}));}
  void theme_all(){const auto colors=ui::palette();for(auto& frame:frames)if(frame->window)frame->theme();
    for(auto& panel:panels)if(panel->window){ui::icons(panel->window);if(panel->list){ListView_SetBkColor(panel->list,colors.canvas);ListView_SetTextBkColor(panel->list,colors.canvas);ListView_SetTextColor(panel->list,colors.ink);}InvalidateRect(panel->window,nullptr,TRUE);}
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
      auto& host=found->second;host.metadata=row;host.metadata["title"]=ui::tab_title(str(row,"title"),str(row,"url"));const auto state=states.contains(host.id)?states.at(host.id):Json::object();const bool agent=state.value("agentAvailable",false),paused=state.value("humanPaused",false);
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
    const bool slow=Clock::now()>=next_slow;if(slow)next_slow=Clock::now()+std::chrono::seconds(1);
    for(auto& frame:frames)if(frame->window){frame->rebuild_tree();frame->sync_toolbar();if(slow)frame->refresh_page_state();frame->update_cursor();frame->animate();
      // A closing window whose tabs survived (a page refused to close) returns.
      if(frame->closing&&GetTickCount64()-frame->closing_since>10000&&frame->has_hosts()){frame->closing=false;ShowWindow(frame->window,SW_SHOWNORMAL);frame->set_status(ui::tr(L"Some tabs did not close."));}}
    if(Clock::now()>=next_flush){next_flush=Clock::now()+std::chrono::seconds(3);if(!data.flush())if(auto frame=live_active())frame->set_status(ui::tr(L"Browser metadata could not be saved. Existing data is preserved."));}
    std::erase_if(panels,[](const auto& panel){return !panel->window;});
    if(active&&!active->window)active=nullptr;
    std::erase_if(frames,[&](const auto& frame){return !frame->window&&std::none_of(hosts.begin(),hosts.end(),[&](const auto& entry){return entry.second.frame==frame.get();});});
  }
  void panel(Frame& frame,const std::string& kind,const std::string& description={},std::function<void(bool)> answer={},const std::string& source_tab={});
  static LRESULT CALLBACK panel_proc(HWND,UINT,WPARAM,LPARAM);
  static LRESULT CALLBACK host_proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){if(msg==WM_NCHITTEST)return HTCLIENT;return DefWindowProcW(h,msg,wp,lp);}
  static LRESULT CALLBACK cursor_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){auto self=reinterpret_cast<Frame*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Frame*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(message==WM_NCHITTEST)return HTTRANSPARENT;if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_PAINT&&self){PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT rect{};GetClientRect(h,&rect);ui::fill(dc,rect,RGB(255,0,255));
      const int scale=GetDpiForWindow(self->window);auto d=[&](int v){return MulDiv(v,scale,96);};POINT points[]={{d(2),d(1)},{d(2),d(22)},{d(7),d(17)},{d(11),d(25)},{d(15),d(23)},{d(11),d(15)},{d(19),d(15)}};
      auto pen=CreatePen(PS_SOLID,d(1),RGB(1,1,1));auto brush=CreateSolidBrush(RGB(250,250,250));auto old_pen=SelectObject(dc,pen),old_brush=SelectObject(dc,brush);Polygon(dc,points,7);SelectObject(dc,old_brush);DeleteObject(brush);
      auto accent=CreateSolidBrush(self->color(self->selected));SelectObject(dc,accent);Ellipse(dc,d(17),d(21),d(24),d(28));SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(accent);DeleteObject(pen);EndPaint(h,&paint);return 0;}
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
  static LRESULT CALLBACK message_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(h,message,wp,lp);
    try{
      if(message==ShellRun){self->run_posted();return 0;}
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
    switch(message){case WM_CREATE:self->build();return 0;case WM_SIZE:self->layout();return 0;
      case WM_ACTIVATE:if(LOWORD(wp)!=WA_INACTIVE){shell.active=self;if(!self->selected.empty())shell.engine.select_native_tab(self->selected);}break;
      case WM_SETCURSOR:{POINT point{};GetCursorPos(&point);ScreenToClient(h,&point);if(LOWORD(lp)==HTCLIENT&&(self->resizing_sidebar||self->sidebar_hit(point))){SetCursor(LoadCursorW(nullptr,IDC_SIZEWE));return TRUE;}
        if(LOWORD(lp)==HTCLIENT&&PtInRect(&self->chip,point)){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}break;}
      case WM_LBUTTONDOWN:{const POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if(self->sidebar_hit(point)){self->drag_origin=point.x;self->drag_width=self->sidebar;self->resizing_sidebar=true;SetPropW(h,L"XenonSidebarDrag",reinterpret_cast<HANDLE>(1));SetCapture(h);return 0;}
        if(PtInRect(&self->chip,point)){self->owner_menu();return 0;}
        if(PtInRect(&self->omnibox,point)){self->focus_address();return 0;}break;}
      case WM_MOUSEMOVE:if(shell.drag.active&&shell.drag.source==h){POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(h,&point);shell.drag_move(point);return 0;}
        if(self->resizing_sidebar){self->resize_sidebar(self->drag_width+MulDiv(GET_X_LPARAM(lp)-self->drag_origin,96,GetDpiForWindow(h)));return 0;}
        {const bool hover=self->sidebar_hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});if(hover!=self->sidebar_hover){self->sidebar_hover=hover;self->invalidate_grip();}if(hover){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);}}break;
      case WM_MOUSELEAVE:if(self->sidebar_hover){self->sidebar_hover=false;self->invalidate_grip();}return 0;
      case WM_LBUTTONUP:if(shell.drag.active&&shell.drag.source==h){POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(h,&point);shell.drag_drop(point);return 0;}
        if(self->resizing_sidebar){self->finish_sidebar(true);return 0;}break;
      case WM_CAPTURECHANGED:if(shell.drag.active&&shell.drag.source==h&&!shell.drag.finishing){shell.drag_cancel();return 0;}self->finish_sidebar(true);return 0;
      case WM_CANCELMODE:if(shell.drag.active&&shell.drag.source==h)shell.drag_cancel();self->finish_sidebar(true);break;
      case WM_KEYDOWN:if(wp==VK_ESCAPE&&self->resizing_sidebar){self->finish_sidebar(true);return 0;}if(wp==VK_ESCAPE&&shell.drag.active){shell.drag_cancel();return 0;}break;
      case WM_LBUTTONDBLCLK:if(self->sidebar_hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)})){self->finish_sidebar(false);self->resize_sidebar(240);shell.save_sidebar(*self);return 0;}break;
      case WM_DPICHANGED:{const auto rect=reinterpret_cast<RECT*>(lp);SetWindowPos(h,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);DeleteObject(self->font);DeleteObject(self->bold);self->font=ui::font(h);self->bold=ui::font(h,13,FW_SEMIBOLD);
        for(const auto& [id,c]:self->controls)SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(self->font),TRUE);TreeView_SetItemHeight(self->tree,self->d(TabRow));self->layout();return 0;}
      case WM_SETTINGCHANGE:case WM_THEMECHANGED:shell.theme_all();return 0;
      case WM_GETMINMAXINFO:{auto value=reinterpret_cast<MINMAXINFO*>(lp);value->ptMinTrackSize={ui::dip(h,640),ui::dip(h,420)};return 0;}
      case WM_COMMAND:if(LOWORD(wp)==Address&&HIWORD(wp)==EN_CHANGE&&!self->setting_address)self->address_dirty=true;
        else if(LOWORD(wp)==Address&&(HIWORD(wp)==EN_SETFOCUS||HIWORD(wp)==EN_KILLFOCUS)){self->address_focus.set(HIWORD(wp)==EN_SETFOCUS,FocusDuration);InvalidateRect(h,&self->omnibox,FALSE);}else if(HIWORD(wp)==BN_CLICKED)self->command(LOWORD(wp));return 0;
      case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item&&item->CtlType==ODT_BUTTON){
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
        if(self==shell.primary()&&self->controls.contains(static_cast<int>(item->CtlID))){auto& counts=shell.fixture_paints[static_cast<int>(item->CtlID)];++counts.first;if(ui::hovered(*item))++counts.second;}
#endif
        if(item->CtlID==NewTab){self->draw_new_tab(*item);return TRUE;}
        ui::button(*item,self->font,self->button_icon(static_cast<int>(item->CtlID)));
        // Controls carries small dots: orange for a pending pairing request,
        // blue for an available update (below the orange one when both show).
        if(item->CtlID==Controls){const int radius=self->d(4);const LONG x=item->rcItem.right-self->d(7);
          if(shell.pairing_count)ui::dot(item->hDC,{x,item->rcItem.top+self->d(7)},radius,ui::palette().orange);
          if(!self->update_version.empty())ui::dot(item->hDC,{x,shell.pairing_count?item->rcItem.bottom-self->d(7):item->rcItem.top+self->d(7)},radius,ui::notice_blue());}
        return TRUE;}break;}
      case WM_NOTIFY:{auto notice=reinterpret_cast<NMHDR*>(lp);
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
      case WM_DESTROY:self->finish_sidebar(false);if(shell.drag.active&&(shell.drag.source==h||shell.drag.target==h))shell.drag_cancel();if(shell.active==self)shell.active=nullptr;return 0;
      case WM_NCDESTROY:if(self->font)DeleteObject(self->font);if(self->bold)DeleteObject(self->bold);self->font=self->bold=nullptr;self->window=nullptr;return DefWindowProcW(h,message,wp,lp);
    }return DefWindowProcW(h,message,wp,lp);
  }
  void request_exit(){if(shutting_down)return;shutting_down=true;data.flush();engine.shutdown();}
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
  item(menu_handle,TabTakeOwnership,ui::tr(L"Take ownership"),owner!="human");item(menu_handle,TabControls,ui::tr(L"Xenon Controls…"));AppendMenuW(menu_handle,MF_SEPARATOR,0,nullptr);
  item(menu_handle,TabClose,ui::tr(L"Close tab\tCtrl+W"));item(menu_handle,TabCloseOthers,ui::tr(L"Close other tabs in workspace"));item(menu_handle,TabCloseBelow,ui::tr(L"Close tabs below"));
  item(menu_handle,TabCloseWorkspace,ui::format(ui::tr(L"Close all tabs in {0}…"),{ui::wide(shell.workspace_names.contains(host.workspace)?shell.workspace_names.at(host.workspace):host.workspace)}));
  const auto chosen=TrackPopupMenu(menu_handle,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RIGHTBUTTON,at.x,at.y,0,window,nullptr);DestroyMenu(menu_handle);
  if(!chosen||!shell.hosts.contains(id))return;
  const auto workspace_id=host.workspace;
  const auto same_group=[&]{std::vector<std::string> tabs;for(const auto& tab:visual_tabs())if(shell.hosts.at(tab).workspace==workspace_id)tabs.push_back(tab);return tabs;};
  try{
    if(chosen==TabNewBelow){const auto tabs=same_group();auto next=std::find(tabs.begin(),tabs.end(),id);open_tab(workspace_id,"about:blank",next!=tabs.end()&&std::next(next)!=tabs.end()?*std::next(next):std::string{});}
    else if(chosen==TabReload)shell.engine.native_command(id,"reload");
    else if(chosen==TabDuplicate){const auto tabs=same_group();auto next=std::find(tabs.begin(),tabs.end(),id);open_tab(workspace_id,url,next!=tabs.end()&&std::next(next)!=tabs.end()?*std::next(next):std::string{});}
    else if(chosen==TabPin){host.pinned=!host.pinned;tree_signature=nullptr;rebuild_tree();}
    else if(chosen==TabMute)shell.engine.native_command(id,host.metadata.value("muted",false)?"unmute":"mute");
    else if(chosen==TabCopyLink)copy_text(window,url);
    else if(chosen==TabBookmark)set_status(shell.data.bookmark(workspace_id,host.metadata.value("private",false),url,str(host.metadata,"title"))?ui::tr(L"Bookmark saved."):ui::tr(L"Bookmark could not be saved."));
    else if(chosen==TabNewWindow){if(auto frame=shell.new_window(this))shell.move_host(host,*frame,{});}
    else if(chosen>=TabToWindow&&chosen<TabToWindow+32&&static_cast<size_t>(chosen-TabToWindow)<targets.size()){if(auto frame=shell.frame_of(targets[static_cast<size_t>(chosen-TabToWindow)]))shell.move_host(host,*frame,{});}
    else if(chosen>=TabToWorkspace&&chosen<TabToWorkspace+64&&static_cast<size_t>(chosen-TabToWorkspace)<destinations.size())shell.move_to_workspace(*this,id,destinations[static_cast<size_t>(chosen-TabToWorkspace)],{});
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
void BrowserShell::Impl::Frame::command(int id){try{
  auto& engine=shell.engine;auto& hosts=shell.hosts;
  if(id==Back||id==Forward||id==Reload)engine.native_command(selected,id==Back?"back":id==Forward?"forward":hosts.contains(selected)&&hosts.at(selected).metadata.value("loading",false)?"stop":"reload");
  else if(id==Go)navigate();else if(id==Controls)engine.show_controls();else if(id==Menu)menu();else if(id==Extensions)extensions_menu();
  else if(id==Passwords){if(hosts.contains(selected))shell.native.request_autofill(selected);}
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
  else if(id==MenuDocumentation||(id==MenuQuickTour&&ui::show_introduction(window,false)==ui::IntroductionResult::documentation))open_tab("native-default",ui::utf8(ui::documentation_url));
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
void BrowserShell::Impl::panel(Frame& frame,const std::string& kind,const std::string& description,std::function<void(bool)> answer,const std::string& source_tab){
  if(kind=="permission"&&std::count_if(panels.begin(),panels.end(),[](const auto& panel){return panel->kind=="permission"&&panel->window;})>=8){if(answer)answer(false);return;}
  auto panel=std::make_unique<Panel>();panel->owner=this;panel->parent=frame.window;panel->kind=kind;panel->workspace=frame.workspace();panel->private_mode=frame.is_private();panel->tab=source_tab.empty()?frame.selected:source_tab;panel->answer=std::move(answer);auto raw=panel.get();panels.push_back(std::move(panel));
  const auto title=ui::wide(kind=="permission"?ui::tr8("Website permission request"):kind=="bookmarks"?ui::tr8("Xenon Bookmarks"):kind=="history"?ui::tr8("Xenon History"):kind=="downloads"?ui::tr8("Xenon Downloads"):kind=="site"?ui::tr8("Xenon Site Permissions"):kind=="notices"?ui::tr8("Xenon Third-party Notices"):ui::tr8("About Xenon"));
  CreateWindowExW(0,L"XenonBrowserPanel",title.c_str(),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,780,480,frame.window,nullptr,branding_module(),raw);
  if(!description.empty())SetWindowTextW(raw->message,ui::wide(description).c_str());ShowWindow(raw->window,kind=="permission"?SW_SHOWNOACTIVATE:SW_SHOW);
}
LRESULT CALLBACK BrowserShell::Impl::panel_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){try{auto panel=reinterpret_cast<Panel*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(message==WM_NCCREATE){panel=static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);panel->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(panel));}if(!panel)return DefWindowProcW(h,message,wp,lp);auto owner=panel->owner;
  LRESULT result{};if(ui::list_draw(message,wp,lp,h,result)||ui::ctl_color(message,wp,lp,result))return result;
  if(message==WM_CREATE){panel->font=ui::font(h);ui::icons(h);auto make=[&](int id,const wchar_t* type,const wchar_t* text,DWORD style){auto control=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,10,10,h,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),branding_module(),nullptr);SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(panel->font),TRUE);ui::control_theme(control);return control;};
    panel->message=make(10,L"STATIC",L"",SS_LEFT);make(1,L"BUTTON",panel->kind=="permission"?ui::tr(L"Allow"):panel->kind=="site"?ui::tr(L"Reset site permissions"):ui::tr(L"Open selected"),BS_OWNERDRAW|WS_TABSTOP);
    make(2,L"BUTTON",panel->kind=="permission"?ui::tr(L"Deny"):panel->kind=="bookmarks"?ui::tr(L"Remove bookmark"):ui::tr(L"Close"),BS_OWNERDRAW|WS_TABSTOP);
    const bool listing=panel->kind=="bookmarks"||panel->kind=="history"||panel->kind=="downloads";
    if(listing){panel->list=make(11,WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|WS_TABSTOP|WS_BORDER);ListView_SetExtendedListViewStyle(panel->list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
      auto colors=ui::palette();ListView_SetBkColor(panel->list,colors.canvas);ListView_SetTextBkColor(panel->list,colors.canvas);ListView_SetTextColor(panel->list,colors.ink);
      LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=220;column.pszText=const_cast<LPWSTR>(panel->kind=="downloads"?ui::tr(L"Name"):ui::tr(L"Title"));ListView_InsertColumn(panel->list,0,&column);column.cx=460;column.pszText=const_cast<LPWSTR>(panel->kind=="downloads"?ui::tr(L"Status / bytes"):L"URL");ListView_InsertColumn(panel->list,1,&column);
      if(panel->kind=="downloads"){for(const auto& tab:owner->engine.native_tabs())if(str(tab,"workspaceId")==panel->workspace){owner->engine.execute("files.downloads",{{"workspaceId",panel->workspace}},[panel](Json value){if(value.value("ok",false))panel->rows=value["result"]["downloads"];});break;}ShowWindow(GetDlgItem(h,1),SW_HIDE);}
      else panel->rows=owner->data.list(panel->workspace,panel->private_mode)[panel->kind];
      int index=0;for(const auto& row:panel->rows){auto title=ui::wide(str(row,panel->kind=="downloads"?"name":"title"));LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=index;item.pszText=title.data();ListView_InsertItem(panel->list,&item);
        auto detail=panel->kind=="downloads"?std::wstring(row.value("complete",false)?ui::tr(L"Complete"):row.value("canceled",false)?ui::tr(L"Canceled"):ui::tr(L"Downloading"))+L" · "+std::to_wstring(row.value("receivedBytes",uint64_t{}))+ui::tr(L" bytes"):ui::wide(str(row,"url"));ListView_SetItemText(panel->list,index++,1,detail.data());}
      SetWindowTextW(panel->message,ui::wide(ui::tr8("Workspace: ")+(owner->workspace_names.contains(panel->workspace)?owner->workspace_names[panel->workspace]:panel->workspace)).c_str());
    }else if(panel->kind=="about"){SetWindowTextW(panel->message,ui::wide(std::string(ui::tr8("Xenon Browser\n"))+std::string(kVersion)+ui::tr8("\n\nA local browser for people and MCP agents.\nCEF supplies the sandboxed web engine.\n\nThird-party licenses and notices are available from the Menu.")).c_str());ShowWindow(GetDlgItem(h,1),SW_HIDE);}
    else if(panel->kind=="notices"){wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);const auto directory=std::filesystem::path(module).parent_path();std::string notice;for(const auto& name:{"THIRD_PARTY_NOTICES.md","CEF-LICENSE.txt","NOTICE"}){auto file=directory/name;if(!std::filesystem::exists(file))file=std::filesystem::path(name);if(std::filesystem::exists(file)&&std::filesystem::file_size(file)<1024*1024){std::ifstream input(file);notice.append(std::istreambuf_iterator<char>(input),{});notice+="\n\n";}}
      if(notice.empty())notice="Xenon: Apache License 2.0.\nCEF and Chromium: see CEF-LICENSE.txt and Chromium-CREDITS.html beside Xenon.exe.\nSQLite: public domain. nlohmann/json: MIT.\nMCP SDK: MIT.";
      DestroyWindow(panel->message);panel->message=make(10,L"EDIT",ui::wide(notice).c_str(),ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_TABSTOP);SetWindowTextW(GetDlgItem(h,1),ui::tr(L"Open engine credits"));EnableWindow(GetDlgItem(h,1),std::filesystem::is_regular_file(directory/L"Chromium-CREDITS.html"));}
    else if(panel->kind=="site"){auto found=owner->hosts.find(panel->tab);const auto url=found==owner->hosts.end()?"":str(found->second.metadata,"url");SetWindowTextW(panel->message,ui::wide(ui::tr8("Site: ")+url+ui::tr8("\n\nCamera, microphone, location and notification requests use native approval prompts.\nReset clears remembered permissions for this HTTPS origin.\nBrowser and workspace permissions are configured in Controls.")).c_str());}
    return 0;
  }
  if(message==WM_SIZE){RECT rect{};GetClientRect(h,&rect);auto d=[&](int value){return ui::dip(h,value);};MoveWindow(panel->message,d(16),d(14),rect.right-d(32),panel->list?d(35):rect.bottom-d(80),TRUE);if(panel->list)MoveWindow(panel->list,d(16),d(56),rect.right-d(32),rect.bottom-d(120),TRUE);MoveWindow(GetDlgItem(h,1),d(16),rect.bottom-d(48),d(200),d(30),TRUE);MoveWindow(GetDlgItem(h,2),rect.right-d(176),rect.bottom-d(48),d(160),d(30),TRUE);return 0;}
  if(message==WM_DPICHANGED){auto rect=reinterpret_cast<RECT*>(lp);auto old=panel->font;panel->font=ui::font(h);for(auto child=GetWindow(h,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(panel->font),TRUE);DeleteObject(old);SetWindowPos(h,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOACTIVATE|SWP_NOZORDER);return 0;}
  if(message==WM_GETMINMAXINFO){auto value=reinterpret_cast<MINMAXINFO*>(lp);value->ptMinTrackSize={ui::dip(h,540),ui::dip(h,360)};return 0;}
  if(message==WM_DRAWITEM){auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item&&item->CtlType==ODT_BUTTON){ui::button(*item,panel->font);return TRUE;}}
  if(message==WM_COMMAND&&HIWORD(wp)==BN_CLICKED){const int id=LOWORD(wp);
    if(panel->kind=="permission"){panel->answered=true;if(panel->answer)panel->answer(id==1);DestroyWindow(h);return 0;}
    if(id==2&&panel->kind!="bookmarks"){DestroyWindow(h);return 0;}
    if(panel->kind=="notices"&&id==1){wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);const auto credits=std::filesystem::path(module).parent_path()/L"Chromium-CREDITS.html";if(std::filesystem::is_regular_file(credits))ShellExecuteW(h,L"open",credits.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;}
    if(panel->kind=="site"&&id==1){owner->engine.native_command(panel->tab,"site-reset",{},[panel](Json value){if(panel->window)SetWindowTextW(panel->message,value.value("ok",false)?ui::tr(L"Remembered site permissions reset."):ui::tr(L"Site permissions could not be reset."));});return 0;}
    const int index=panel->list?ListView_GetNextItem(panel->list,-1,LVNI_SELECTED):-1;
    if(index>=0&&static_cast<size_t>(index)<panel->rows.size()){const auto row=panel->rows[index];if(id==1){if(auto frame=owner->frame_of(panel->parent))owner->engine.native_command(frame->selected,"navigate",str(row,"url"));DestroyWindow(h);}
      else if(panel->kind=="bookmarks"&&owner->data.remove_bookmark(panel->workspace,panel->private_mode,str(row,"url"))){panel->rows.erase(panel->rows.begin()+index);ListView_DeleteItem(panel->list,index);}}
    return 0;
  }
  if(message==WM_CLOSE){DestroyWindow(h);return 0;}if(message==WM_DESTROY){if(panel->answer&&!panel->answered){panel->answered=true;panel->answer(false);}DeleteObject(panel->font);return 0;}
  if(message==WM_NCDESTROY){panel->window=nullptr;return DefWindowProcW(h,message,wp,lp);}
  if(message==WM_PAINT){PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT rect{};GetClientRect(h,&rect);ui::fill(dc,rect,ui::palette().canvas);EndPaint(h,&paint);return 0;}
  return DefWindowProcW(h,message,wp,lp);
}catch(...){return message==WM_NCCREATE?FALSE:message==WM_CREATE?-1:0;}
}
BrowserShell::BrowserShell(Broker& broker,CefEngine& engine,NativeUi& native,ExtensionStore& extensions,const std::filesystem::path& root):impl_(std::make_unique<Impl>(broker,engine,native,extensions,root)){
  INITCOMMONCONTROLSEX common{sizeof(common),ICC_TREEVIEW_CLASSES|ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&common);
  for(const auto& [name,proc]:std::vector<std::pair<const wchar_t*,WNDPROC>>{{L"XenonBrowserShell",Impl::proc},{L"XenonTabHost",Impl::host_proc},{L"XenonAgentCursor",Impl::cursor_proc},{L"XenonBrowserPanel",Impl::panel_proc},
      {L"XenonFindBar",Impl::find_proc},{L"XenonTabDrag",Impl::ghost_proc},{L"XenonShellMessages",Impl::message_proc}}){
    WNDCLASSW type{};type.style=wcscmp(name,L"XenonBrowserShell")==0?CS_DBLCLKS:wcscmp(name,L"XenonTabDrag")==0?CS_DROPSHADOW:0;type.lpfnWndProc=proc;type.hInstance=branding_module();type.lpszClassName=name;type.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&type);}
  impl_->messages=CreateWindowExW(0,L"XenonShellMessages",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,branding_module(),impl_.get());
  if(!impl_->messages||!impl_->create_frame())throw std::runtime_error("Cannot create native browser shell");
  impl_->active=impl_->frames.front().get();
  SetTimer(impl_->messages,1,16,nullptr);SetTimer(impl_->messages,2,1000,nullptr);
}
BrowserShell::~BrowserShell(){impl_->data.flush();if(impl_->messages){KillTimer(impl_->messages,1);KillTimer(impl_->messages,2);}
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
  auto frame=impl_->frame_of(root);if(!frame)return false;
  if(message.message==WM_KEYDOWN||message.message==WM_SYSKEYDOWN){const bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0,alt=(GetKeyState(VK_MENU)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;const auto key=message.wParam;
    const auto run=[&](int id){frame->command(id);return true;};
    if(control&&shift&&key=='X')return run(Controls);
    if((control&&key=='L')||(alt&&key=='D')||key==VK_F6){frame->focus_address();return true;}
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
