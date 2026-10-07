#include "xenon/browser_shell.hpp"
#include "xenon/broker.hpp"
#include "xenon/cef_engine.hpp"
#include "xenon/browser_data.hpp"
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
#include <map>
#include <set>
#include <vector>
#include <utility>

namespace xenon {
namespace {
constexpr UINT ShellChanged=WM_APP+90;
enum ShellId {Back=2001,Forward,Reload,Address,Go,Controls,Menu,NewTab,Tree,FindText,FindNext,FindPrevious,FindClose,
  MenuFind=2100,MenuZoomIn,MenuZoomOut,MenuZoomReset,MenuBookmark,MenuBookmarks,MenuHistory,MenuDownloads,MenuPrint,MenuPdf,
  MenuPermissions,MenuThemeSystem,MenuThemeLight,MenuThemeDark,MenuAbout,MenuNotices,MenuExit,MenuPrivate,MenuCloseTab,
  MenuSidebarNarrower,MenuSidebarWider,MenuSidebarReset,MenuUpdates,MenuQuickTour,MenuDocumentation,MenuLanguageEnglish=2200,MenuLanguageChinese};
std::string str(const Json& value,const char* key){auto found=value.find(key);return found!=value.end()&&found->is_string()?found->get<std::string>():std::string{};}
std::wstring short_title(const std::string& value,size_t maximum=55){auto text=ui::wide(value);if(text.size()>maximum)text=text.substr(0,maximum-1)+L"…";return text;}
using Clock=std::chrono::steady_clock;
double fraction(Clock::time_point start,int duration){return std::clamp(std::chrono::duration<double,std::milli>(Clock::now()-start).count()/duration,0.0,1.0);}
uint32_t seed(){return static_cast<uint32_t>(std::stoul(local_security::random_hex(4),nullptr,16));}
// Native chrome motion timings in milliseconds. Website pixels and geometry
// never animate; these only change how the shell around the page is painted.
constexpr int GlideDuration=200,AccentDuration=240,FocusDuration=160,LoadingCycle=1300;
double elapsed(ULONGLONG start,int duration){return start&&ui::motion()?std::clamp(static_cast<double>(GetTickCount64()-start)/duration,0.0,1.0):1.0;}
}
struct BrowserShell::Impl {
  Broker& broker;CefEngine& engine;BrowserData data;std::filesystem::path root;
  HWND window{},tree{},address{},find_text{},cursor{},tooltips{};HFONT font{};std::map<int,HWND> controls;
  struct Host {std::string id,workspace;HWND window{},browser{};Json metadata=Json::object();bool human{},agent{},paused{},initialized{},animating{};
    PointerPoint display,from,to;Clock::time_point animation;int duration{120};PointerOverlayTransitions transitions;std::string recorded_url;};
  std::map<std::string,Host> hosts;std::vector<std::string> order;std::string selected;
  std::map<std::string,Json> states;std::map<std::string,std::string> names;std::map<std::string,std::string> workspace_names;
  std::vector<std::pair<std::string,bool>> tree_keys;Json tree_signature;bool rebuilding{},find_visible{};RECT page{},panel_bounds{},address_bounds{};
  std::wstring status;bool dark{};bool shutting_down{};size_t pairing_count{};Clock::time_point next_state{},next_flush{};
  int sidebar{240},drag_origin{},drag_width{};bool resizing_sidebar{},sidebar_hover{};
  bool address_dirty{},setting_address{};HTREEITEM hovered_row{};std::string pressed_close,opening_workspace,hovered_close;
  // Sampled while painting; animate() only invalidates what is still moving.
  ui::Fade address_focus;std::map<HTREEITEM,ui::Fade> row_fades;RECT glide_from{},pill{};ULONGLONG glide_start{},accent_start{};
  COLORREF accent_from{},accent_to{};bool accent_ready{},pill_valid{},tree_moving{},window_moving{},was_loading{};
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
  std::filesystem::path fixture_destination;std::map<int,std::pair<uint64_t,uint64_t>> fixture_paints;
  void fixture_snapshot(const std::filesystem::path& destination);
#endif
  struct Panel {Impl* owner{};HWND window{},list{},message{};HFONT font{};std::string kind,workspace,tab;Json rows=Json::array();bool private_mode{},answered{};std::function<void(bool)> answer;};
  std::vector<std::unique_ptr<Panel>> panels;
  Impl(Broker& b,CefEngine& e,std::filesystem::path path):broker(b),engine(e),data(path),root(std::move(path)){}
  HWND add(int id,const wchar_t* type,const wchar_t* caption,DWORD style=0){auto result=CreateWindowExW(0,type,caption,WS_CHILD|WS_VISIBLE|style,0,0,10,10,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),branding_module(),nullptr);
    SendMessageW(result,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);controls[id]=result;ui::control_theme(result);return result;}
  void build(){font=ui::font(window);ui::icons(window);dark=ui::palette().dark;
    tooltips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,window,nullptr,branding_module(),nullptr);
    SendMessageW(tooltips,TTM_SETDELAYTIME,TTDT_INITIAL,400);SendMessageW(tooltips,TTM_SETMAXTIPWIDTH,0,ui::dip(window,280));
    for(const auto& [id,title]:std::vector<std::pair<int,const wchar_t*>>{{Back,ui::tr(L"Back")},{Forward,ui::tr(L"Forward")},{Reload,ui::tr(L"Reload")},{Go,ui::tr(L"Go to address")},{Controls,ui::tr(L"Controls")},{Menu,ui::tr(L"Menu")},{NewTab,ui::tr(L"New tab")},{FindPrevious,ui::tr(L"Previous match")},{FindNext,ui::tr(L"Next match")},{FindClose,ui::tr(L"Close find")}}){auto control=add(id,L"BUTTON",title,BS_OWNERDRAW|WS_TABSTOP);
      TOOLINFOW tip{sizeof(tip)};tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tip.hwnd=window;tip.uId=reinterpret_cast<UINT_PTR>(control);tip.lpszText=const_cast<LPWSTR>(title);SendMessageW(tooltips,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tip));}
    SetPropW(controls.at(Go),L"XenonCircle",reinterpret_cast<HANDLE>(1));for(int id:{Go,FindPrevious,FindNext,FindClose})SetPropW(controls.at(id),L"XenonSurface",reinterpret_cast<HANDLE>(1));
    address=add(Address,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL);
    find_text=add(FindText,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL|WS_BORDER);
    tree=add(Tree,WC_TREEVIEWW,ui::tr(L"Workspace tabs"),WS_TABSTOP|TVS_HASBUTTONS|TVS_LINESATROOT|TVS_SHOWSELALWAYS|TVS_FULLROWSELECT|TVS_NOHSCROLL);
    SetWindowSubclass(tree,tree_proc,3,reinterpret_cast<DWORD_PTR>(this));TreeView_SetExtendedStyle(tree,TVS_EX_DOUBLEBUFFER,TVS_EX_DOUBLEBUFFER);TreeView_SetItemHeight(tree,ui::dip(window,38));theme();layout();SetTimer(window,1,16,nullptr);SetTimer(window,2,1000,nullptr);
  }
  void theme(){auto colors=ui::palette();dark=colors.dark;ui::frame(window);TreeView_SetBkColor(tree,colors.canvas);TreeView_SetTextColor(tree,colors.ink);TreeView_SetLineColor(tree,colors.border);
    SetWindowTheme(tooltips,L"",L"");SendMessageW(tooltips,TTM_SETTIPBKCOLOR,colors.surface,0);SendMessageW(tooltips,TTM_SETTIPTEXTCOLOR,colors.ink,0);
    for(const auto& [id,control]:controls){ui::control_theme(control);InvalidateRect(control,nullptr,TRUE);}InvalidateRect(window,nullptr,TRUE);
    for(auto& panel:panels)if(panel->window){ui::icons(panel->window);if(panel->list){ListView_SetBkColor(panel->list,colors.canvas);ListView_SetTextBkColor(panel->list,colors.canvas);ListView_SetTextColor(panel->list,colors.ink);}InvalidateRect(panel->window,nullptr,TRUE);}
    engine.native_command("","theme");
  }
  void place(int id,int x,int y,int width,int height){auto control=controls.at(id);MoveWindow(control,ui::dip(window,x),ui::dip(window,y),ui::dip(window,width),ui::dip(window,height),TRUE);}
  void layout(){RECT rect{};GetClientRect(window,&rect);const int scale=GetDpiForWindow(window);const int width=MulDiv(rect.right,96,scale),height=MulDiv(rect.bottom,96,scale);
    sidebar=std::clamp(ui::sidebar_width,180,std::max(180,std::min(480,width-400)));
    place(Back,20,16,36,34);place(Forward,64,16,36,34);place(Reload,108,16,36,34);
    place(Tree,12,96,sidebar-24,std::max<int>(60,height-162));place(NewTab,20,height-48,36,34);place(Controls,72,height-48,36,34);place(Menu,124,height-48,36,34);
    panel_bounds={ui::dip(window,sidebar),ui::dip(window,12),rect.right-ui::dip(window,12),rect.bottom-ui::dip(window,12)};
    address_bounds={ui::dip(window,sidebar+16),ui::dip(window,26),rect.right-ui::dip(window,68),ui::dip(window,60)};
    place(Address,sidebar+28,32,std::max<int>(120,width-sidebar-108),22);place(Go,width-56,27,32,32);
    const int content_top=find_visible?138:98;place(FindText,sidebar+16,98,std::max<int>(100,width-sidebar-156),29);place(FindPrevious,width-128,98,32,29);place(FindNext,width-88,98,32,29);place(FindClose,width-48,98,32,29);
    for(int id:{FindText,FindPrevious,FindNext,FindClose})ShowWindow(controls.at(id),find_visible?SW_SHOW:SW_HIDE);
    // The tree view repaints only newly exposed pixels on resize, but each
    // row's close button and pill are positioned from its full width.
    InvalidateRect(tree,nullptr,FALSE);
    // The page stays rectangular and entirely inside the frame's straight edges.
    // Reserve the curved footer instead of masking website pixels or hit targets.
    page={panel_bounds.left,ui::dip(window,content_top),panel_bounds.right,panel_bounds.bottom-ui::dip(window,10)};
    RECT inner=page;InflateRect(&inner,-ui::dip(window,2),-ui::dip(window,2));
    for(auto& [id,host]:hosts){SetWindowPos(host.window,nullptr,inner.left,inner.top,std::max<int>(1,inner.right-inner.left),std::max<int>(1,inner.bottom-inner.top),SWP_NOACTIVATE|SWP_NOZORDER);
      if(host.browser)SetWindowPos(host.browser,nullptr,0,0,std::max<int>(1,inner.right-inner.left),std::max<int>(1,inner.bottom-inner.top),SWP_NOACTIVATE|SWP_NOZORDER);}
    InvalidateRect(window,nullptr,TRUE);
  }
  bool sidebar_hit(POINT point) const {RECT bounds{};GetClientRect(window,&bounds);return point.x>=ui::dip(window,sidebar-10)&&point.x<ui::dip(window,sidebar)&&point.y>=ui::dip(window,12)&&point.y<bounds.bottom-ui::dip(window,12);}
  RECT sidebar_grip() const {RECT bounds{};GetClientRect(window,&bounds);return {ui::dip(window,sidebar-8),bounds.bottom-ui::dip(window,87),ui::dip(window,sidebar-3),bounds.bottom-ui::dip(window,53)};}
  void invalidate_grip(){auto rect=sidebar_grip();InvalidateRect(window,&rect,FALSE);}
  void set_address(const std::string& value){if(ui::text(address)==value)return;setting_address=true;SetWindowTextW(address,ui::wide(value).c_str());setting_address=false;}
  void save_sidebar(){if(!ui::save_settings())status=ui::tr(L"Sidebar resized for this run; saving failed.");InvalidateRect(window,nullptr,FALSE);}
  void finish_sidebar(bool save){if(!resizing_sidebar)return;resizing_sidebar=false;RemovePropW(window,L"XenonSidebarDrag");if(GetCapture()==window)ReleaseCapture();if(save)save_sidebar();}
  void resize_sidebar(int width){RECT bounds{};GetClientRect(window,&bounds);const auto maximum=std::max(180,std::min(480,MulDiv(bounds.right,96,GetDpiForWindow(window))-400));const auto next=std::clamp(width,180,maximum);
    if(ui::sidebar_width!=next){ui::sidebar_width=next;layout();}}
  std::string workspace() const {auto found=hosts.find(selected);return found==hosts.end()?"native-default":found->second.workspace;}
  bool is_private() const {auto found=hosts.find(selected);return found!=hosts.end()&&found->second.metadata.value("private",false);}
  bool empty_workspace(const std::string& id) const {return std::none_of(hosts.begin(),hosts.end(),[&](const auto& entry){return entry.second.workspace==id;});}
  void open_workspace(const std::string& id){if(!workspace_names.contains(id)||!empty_workspace(id)||!opening_workspace.empty())return;
    opening_workspace=id;broker.open_human_tab(id,"about:blank",[this](Json value){opening_workspace.clear();if(!value.value("ok",false)){status=ui::error_text(str(value["error"],"message"));InvalidateRect(window,nullptr,FALSE);}});}
  RECT close_rect(HTREEITEM item) const {RECT rect{};if(!TreeView_GetItemRect(tree,item,&rect,FALSE))return {};RECT bounds{};GetClientRect(tree,&bounds);rect.left=bounds.right-ui::dip(window,34);rect.right=bounds.right-ui::dip(window,6);rect.top+=ui::dip(window,5);rect.bottom-=ui::dip(window,5);return rect;}
  std::string close_at(POINT point) const {TVHITTESTINFO hit{};hit.pt=point;auto item=TreeView_HitTest(tree,&hit);if(!item)return {};TVITEMW value{};value.hItem=item;value.mask=TVIF_PARAM;TreeView_GetItem(tree,&value);
    if(value.lParam<=0||static_cast<size_t>(value.lParam)>tree_keys.size()||tree_keys[value.lParam-1].second)return {};auto rect=close_rect(item);return PtInRect(&rect,point)?tree_keys[value.lParam-1].first:std::string{};}
  void choose(const std::string& id,bool focus){auto found=hosts.find(id);if(found==hosts.end())return;
    if(selected!=id){RECT from{};const bool visible=pill_rect(from);glide_from=from;glide_start=visible&&ui::motion()?GetTickCount64():0;address_dirty=false;set_address(str(found->second.metadata,"url"));}selected=id;
    status.clear();track_accent();
    SetWindowPos(found->second.window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    engine.select_native_tab(id);if(focus&&found->second.browser)engine.native_command(id,"focus");sync_toolbar();InvalidateRect(window,nullptr,FALSE);InvalidateRect(tree,nullptr,FALSE);update_cursor();
  }
  COLORREF color(const std::string& id) const {const auto colors=ui::palette();auto found=states.find(id);if(found==states.end()||!found->second.value("agentAvailable",false))return colors.gray;return found->second.value("humanPaused",false)?colors.orange:colors.teal;}
  std::wstring state_text(const std::string& id) const {auto found=states.find(id);if(found==states.end())return ui::tr(L"Opening");const auto& state=found->second;
    auto owner=str(state,"ownerSessionId");std::wstring value=owner=="human"?ui::tr(L"You"):owner.empty()?ui::tr(L"No agent"):ui::wide(names.contains(owner)?names.at(owner):ui::tr8("Agent"));
    if(state.value("agentAvailable",false)&&state.value("humanPaused",false))value+=ui::tr(L" · Paused for you");else if(state.value("handoffPending",false))value+=ui::tr(L" · Handing off");
    else if(owner!="human"&&!owner.empty())value+=state.value("agentAvailable",false)?ui::tr(L" · Agent control"):ui::tr(L" · Unavailable");
    if(state.value("protected",false))value+=ui::tr(L" · Protected authentication");return value;
  }
  void sync_toolbar(){auto found=hosts.find(selected);const auto metadata=found==hosts.end()?Json::object():found->second.metadata;
    if(GetFocus()!=address&&!address_dirty)set_address(str(metadata,"url"));
    const auto enable=[&](int id,bool enabled){if((IsWindowEnabled(controls.at(id))!=FALSE)!=enabled)EnableWindow(controls.at(id),enabled);};
    enable(Back,metadata.value("canGoBack",false));enable(Forward,metadata.value("canGoForward",false));enable(Reload,found!=hosts.end());
    const auto reload_title=metadata.value("loading",false)?ui::tr(L"Stop"):ui::tr(L"Reload");if(ui::text(controls.at(Reload))!=ui::utf8(reload_title)){SetWindowTextW(controls.at(Reload),reload_title);TOOLINFOW tip{sizeof(tip)};tip.hwnd=window;tip.uId=reinterpret_cast<UINT_PTR>(controls.at(Reload));tip.lpszText=const_cast<LPWSTR>(reload_title);SendMessageW(tooltips,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&tip));}
    auto title=found==hosts.end()?ui::tr(L"Xenon Browser"):short_title(ui::tab_title(str(metadata,"title"),str(metadata,"url")),100)+L" — Xenon";if(ui::text(window)!=ui::utf8(title))SetWindowTextW(window,title.c_str());
    auto caption=ui::tr(L"Controls")+(pairing_count?L" ("+std::to_wstring(pairing_count)+L")":std::wstring{});if(ui::text(controls.at(Controls))!=ui::utf8(caption))SetWindowTextW(controls.at(Controls),caption.c_str());
  }
  ui::Icon button_icon(int id) const {switch(id){case Back:return ui::Icon::back;case Forward:return ui::Icon::forward;case Reload:return ui::text(controls.at(Reload))==ui::tr8("Stop")?ui::Icon::stop:ui::Icon::reload;case Go:return ui::Icon::go;case Controls:return ui::Icon::controls;case Menu:return ui::Icon::menu;case NewTab:return ui::Icon::plus;case FindPrevious:return ui::Icon::up;case FindNext:return ui::Icon::down;case FindClose:return ui::Icon::close;default:return ui::Icon::none;}}
  HTREEITEM item_of(const std::string& id) const {
    for(auto group=TreeView_GetRoot(tree);group;group=TreeView_GetNextSibling(tree,group))for(auto item=TreeView_GetChild(tree,group);item;item=TreeView_GetNextSibling(tree,item)){
      TVITEMW value{};value.mask=TVIF_PARAM;value.hItem=item;if(TreeView_GetItem(tree,&value)&&value.lParam>0&&static_cast<size_t>(value.lParam)<=tree_keys.size()&&!tree_keys[value.lParam-1].second&&tree_keys[value.lParam-1].first==id)return item;}
    return nullptr;
  }
  // The selected tab's pill in tree coordinates. After a selection change it
  // glides from the previous row; the page and its host window never move.
  bool pill_rect(RECT& rect) const {const auto item=item_of(selected);RECT row{},bounds{};if(!item||!TreeView_GetItemRect(tree,item,&row,FALSE))return false;
    GetClientRect(tree,&bounds);if(row.top<0||row.bottom>bounds.bottom)return false;
    rect={ui::dip(window,12),row.top+ui::dip(window,3),bounds.right+ui::dip(window,16),row.bottom-ui::dip(window,3)};
    if(const auto t=elapsed(glide_start,GlideDuration);t<1){const auto height=rect.bottom-rect.top;rect.top=glide_from.top+static_cast<LONG>(std::lround((rect.top-glide_from.top)*ui::ease(t)));rect.bottom=rect.top+height;}
    return true;
  }
  bool gliding() const {return elapsed(glide_start,GlideDuration)<1;}
  // The ownership color crossfades when control changes or another tab is chosen.
  COLORREF accent() const {return accent_ready?ui::mix(accent_from,accent_to,ui::ease(elapsed(accent_start,AccentDuration))):color(selected);}
  void track_accent(){const auto target=color(selected);if(!accent_ready){accent_from=accent_to=target;accent_ready=true;return;}
    if(target==accent_to)return;accent_from=accent();accent_to=target;accent_start=ui::motion()?GetTickCount64():0;}
  double row_hover(HTREEITEM item) const {auto found=row_fades.find(item);return found!=row_fades.end()?found->second.value(ui::hover_duration):item==hovered_row?1.0:0.0;}
  bool loading() const {auto found=hosts.find(selected);return found!=hosts.end()&&found->second.metadata.value("loading",false);}
  // The 2 DIP strip between the page's top edge and its host window.
  RECT loading_strip() const {return {page.left+ui::dip(window,2),page.top,page.right-ui::dip(window,2),page.top+ui::dip(window,2)};}
  void paint_loading(HDC dc,COLORREF tint) const {if(!loading())return;const auto strip=loading_strip();const LONG width=strip.right-strip.left;if(width<=0)return;
    if(!ui::motion()){ui::fill(dc,strip,tint);return;}
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
  }
  void draw_tree(NMTVCUSTOMDRAW& draw){const auto index=draw.nmcd.lItemlParam;if(index<=0||static_cast<size_t>(index)>tree_keys.size())return;const auto& [id,group]=tree_keys[index-1];auto colors=ui::palette();const auto dc=draw.nmcd.hdc;const auto item=reinterpret_cast<HTREEITEM>(draw.nmcd.dwItemSpec);
    RECT bounds{};GetClientRect(tree,&bounds);auto rect=draw.nmcd.rc;rect.left=0;rect.right=bounds.right;const auto row=rect;ui::fill(dc,row,colors.canvas);
    RECT label{};TreeView_GetItemRect(tree,item,&label,TRUE);label.right=bounds.right-ui::dip(window,12);
    const double hover=row_hover(item);const bool emphasized=hover>=0.5&&colors.contrast;const auto hover_fill=ui::mix(colors.canvas,ui::hover_background(),hover);
    // While the selection glides, each row paints its own slice of the moving pill.
    const auto glide=[&]{RECT overlap{};if(!gliding()||!pill_valid||!IntersectRect(&overlap,&pill,&row))return;const int saved=SaveDC(dc);IntersectClipRect(dc,row.left,row.top,row.right,row.bottom);
      ui::rounded(dc,pill,colors.surface,accent(),ui::dip(window,8),ui::dip(window,2));RestoreDC(dc,saved);};
    if(group){if(hover>0){auto background=rect;InflateRect(&background,-ui::dip(window,2),-ui::dip(window,3));ui::rounded(dc,background,hover_fill,hover_fill,ui::dip(window,6));}glide();
      TVITEMW value{};value.hItem=item;value.mask=TVIF_STATE;value.stateMask=TVIS_EXPANDED;TreeView_GetItem(tree,&value);RECT arrow=label;arrow.left=ui::dip(window,2);arrow.right=ui::dip(window,24);ui::icon(dc,arrow,value.state&TVIS_EXPANDED?ui::Icon::down:ui::Icon::chevron_right,emphasized?ui::selection_ink():colors.muted,window);
      if(empty_workspace(id)){label.right-=ui::dip(window,24);auto add=close_rect(item);ui::icon(dc,add,ui::Icon::plus,colors.muted,window);}
      ui::text(dc,label,ui::wide(workspace_names.contains(id)?workspace_names.at(id):ui::tr8("Workspace")),font,emphasized?ui::selection_ink():colors.muted);return;}
    const bool active=id==selected,settled=active&&!gliding();rect.left=ui::dip(window,12);InflateRect(&rect,0,-ui::dip(window,3));rect.right-=active?0:ui::dip(window,6);auto shape=rect;if(active)shape.right+=ui::dip(window,16);
    const auto state=states.find(id);const bool agent=!active&&state!=states.end()&&state->second.value("agentAvailable",false)&&!state->second.value("humanPaused",false);
    const auto background=settled?colors.surface:hover_fill;
    ui::rounded(dc,shape,background,settled?accent():agent?color(id):background,ui::dip(window,8),ui::dip(window,settled?2:1));glide();
    // Close stays visible on every tab, quieter until its row is hovered or selected.
    auto close=close_rect(item);POINT pointer{};GetCursorPos(&pointer);ScreenToClient(tree,&pointer);if(item==hovered_row&&PtInRect(&close,pointer))ui::rounded(dc,close,ui::hover_background(),ui::hover_background(),ui::dip(window,5));
    const double reveal=colors.contrast||active?1.0:0.45+0.55*hover;
    ui::icon(dc,close,ui::Icon::close,emphasized&&!active?ui::selection_ink():ui::mix(active?colors.surface:background,colors.muted,reveal),window);
    auto found=hosts.find(id);const auto title=found==hosts.end()?ui::tr8("New tab"):ui::tab_title(str(found->second.metadata,"title"),str(found->second.metadata,"url"));label.left=ui::dip(window,26);label.right=close.left-ui::dip(window,3);ui::text(dc,label,ui::wide(title),font,emphasized&&!active?ui::selection_ink():colors.ink);
    if((draw.nmcd.uItemState&CDIS_FOCUS)&&GetFocus()==tree&&!gliding()){auto focus=rect;InflateRect(&focus,-ui::dip(window,3),-ui::dip(window,3));ui::focus_mark(dc,focus,tree,5);}
  }
  void paint(HDC dc){RECT rect{};GetClientRect(window,&rect);const auto colors=ui::palette();const auto tint=accent();ui::fill(dc,rect,colors.canvas);
    ui::rounded(dc,panel_bounds,colors.surface,tint,ui::dip(window,10),ui::dip(window,2));ui::rounded(dc,address_bounds,colors.canvas,colors.canvas,ui::dip(window,17));
    if(const auto focus=address_focus.value(FocusDuration);focus>0)ui::outline(dc,address_bounds,ui::mix(colors.canvas,tint,focus),ui::dip(window,17),ui::dip(window,2));
    RECT brand{ui::dip(window,24),ui::dip(window,64),ui::dip(window,sidebar-16),ui::dip(window,88)};ui::text(dc,brand,ui::tr(L"Xenon / Workspaces"),font,colors.muted);
    // Ownership reads as a dot in the frame's color, aligned with the address text.
    RECT status_rect{ui::dip(window,sidebar+28),ui::dip(window,66),rect.right-ui::dip(window,26),ui::dip(window,90)};
    if(status.empty()&&hosts.contains(selected)){const int radius=ui::dip(window,3);ui::dot(dc,{status_rect.left+radius,(status_rect.top+status_rect.bottom)/2},radius,tint);status_rect.left+=radius*2+ui::dip(window,8);}
    ui::text(dc,status_rect,status.empty()?state_text(selected):status,font,colors.muted);paint_loading(dc,tint);
    RECT row{};if(pill_rect(row)){MapWindowPoints(tree,window,reinterpret_cast<POINT*>(&row),2);RECT rail{};GetWindowRect(tree,&rail);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rail),2);RECT bridge{rail.right,row.top,panel_bounds.left+ui::dip(window,2),row.bottom};ui::fill(dc,bridge,colors.surface);
      const auto thickness=ui::dip(window,2);ui::fill(dc,{bridge.left,bridge.top,bridge.right,bridge.top+thickness},tint);ui::fill(dc,{bridge.left,bridge.bottom-thickness,bridge.right,bridge.bottom},tint);
      for(const bool top:{true,false})ui::tab_junction(dc,panel_bounds.left,top?bridge.top:bridge.bottom,top,thickness,ui::dip(window,8),colors.canvas,colors.surface,tint);}
    if(sidebar_hover||resizing_sidebar){RECT grip{ui::dip(window,sidebar-7),rect.bottom-ui::dip(window,86),ui::dip(window,sidebar-4),rect.bottom-ui::dip(window,54)};ui::rounded(dc,grip,colors.muted,colors.muted,ui::dip(window,1));}
  }
  void rebuild_tree(){Json signature=Json::array();for(const auto& [id,name]:workspace_names)signature.push_back({id,name});for(const auto& id:order)if(auto found=hosts.find(id);found!=hosts.end())signature.push_back({id,found->second.workspace,str(found->second.metadata,"title")});
    if(signature==tree_signature)return;tree_signature=signature;std::set<std::string> expanded;
    for(auto item=TreeView_GetRoot(tree);item;item=TreeView_GetNextSibling(tree,item)){TVITEMW entry{};entry.hItem=item;entry.mask=TVIF_PARAM|TVIF_STATE;entry.stateMask=TVIS_EXPANDED;TreeView_GetItem(tree,&entry);
      if(entry.lParam>0&&static_cast<size_t>(entry.lParam)<=tree_keys.size()&&((entry.state&TVIS_EXPANDED)||!TreeView_GetChild(tree,item)))expanded.insert(tree_keys[entry.lParam-1].first);}
    std::set<std::string> known;for(const auto& [id,group]:tree_keys)if(group)known.insert(id);
    const bool first=tree_keys.empty();rebuilding=true;hovered_row=nullptr;hovered_close.clear();row_fades.clear();SendMessageW(tree,WM_SETREDRAW,FALSE,0);TreeView_DeleteAllItems(tree);tree_keys.clear();std::map<std::string,HTREEITEM> groups;HTREEITEM current=nullptr;
    auto add_group=[&](const std::string& id){if(groups.contains(id))return;auto label=ui::wide(workspace_names.contains(id)?workspace_names.at(id):ui::tr8("Workspace"));tree_keys.emplace_back(id,true);TVINSERTSTRUCTW entry{};entry.hParent=TVI_ROOT;entry.hInsertAfter=TVI_LAST;entry.item.mask=TVIF_TEXT|TVIF_PARAM;entry.item.pszText=label.data();entry.item.lParam=tree_keys.size();groups[id]=TreeView_InsertItem(tree,&entry);};
    if(workspace_names.contains("native-default"))add_group("native-default");for(const auto& [id,name]:workspace_names)add_group(id);
    for(const auto& id:order)if(auto found=hosts.find(id);found!=hosts.end()){
      auto& host=found->second;if(!groups.contains(host.workspace)){const auto label=ui::wide(workspace_names.contains(host.workspace)?workspace_names[host.workspace]:ui::tr8("Workspace"));
        tree_keys.emplace_back(host.workspace,true);TVINSERTSTRUCTW entry{};entry.hParent=TVI_ROOT;entry.hInsertAfter=TVI_LAST;entry.item.mask=TVIF_TEXT|TVIF_PARAM;entry.item.pszText=const_cast<LPWSTR>(label.c_str());entry.item.lParam=tree_keys.size();groups[host.workspace]=TreeView_InsertItem(tree,&entry);}
      auto label=short_title(ui::tab_title(str(host.metadata,"title"),str(host.metadata,"url")),37);tree_keys.emplace_back(id,false);
      TVINSERTSTRUCTW entry{};entry.hParent=groups[host.workspace];entry.hInsertAfter=TVI_LAST;entry.item.mask=TVIF_TEXT|TVIF_PARAM;entry.item.pszText=label.data();entry.item.lParam=tree_keys.size();auto item=TreeView_InsertItem(tree,&entry);if(id==selected)current=item;
    }
    for(const auto& [id,group]:groups)if(first||!known.contains(id)||expanded.contains(id))TreeView_Expand(tree,group,TVE_EXPAND);
    if(current&&(first||expanded.contains(hosts.at(selected).workspace)))TreeView_SelectItem(tree,current);SendMessageW(tree,WM_SETREDRAW,TRUE,0);InvalidateRect(tree,nullptr,TRUE);rebuilding=false;
  }
  PointerPoint parked(const Host& host) const {RECT rect{};GetClientRect(host.window,&rect);const double scale=GetDpiForWindow(host.window)/96.0*host.metadata.value("zoom",1.0);const auto random=seed();return {rect.right/scale*(.76+(random%100)/1000.0),rect.bottom/scale*(.76+((random/100)%100)/1000.0)};}
  void animate(Host& host,PointerPoint target,int duration){host.from=host.display;host.to=target;host.animation=Clock::now();host.duration=duration;host.animating=true;}
  void update_cursor(){auto found=hosts.find(selected);if(found==hosts.end()||!found->second.agent||!IsWindowVisible(window)||IsIconic(window)){ShowWindow(cursor,SW_HIDE);return;}auto& host=found->second;
    if(host.animating){const auto t=fraction(host.animation,host.duration),f=t*t*(3-2*t);host.display={host.from.x+(host.to.x-host.from.x)*f,host.from.y+(host.to.y-host.from.y)*f};if(t>=1)host.animating=false;}
    RECT rect{};GetClientRect(host.window,&rect);const double scale=GetDpiForWindow(host.window)/96.0*host.metadata.value("zoom",1.0);POINT point{static_cast<LONG>(std::clamp(host.display.x*scale,0.0,std::max(0.0,rect.right-25.0))),static_cast<LONG>(std::clamp(host.display.y*scale,0.0,std::max(0.0,rect.bottom-30.0)))};ClientToScreen(host.window,&point);
    SetWindowPos(cursor,nullptr,point.x,point.y,ui::dip(window,25),ui::dip(window,30),SWP_NOACTIVATE|SWP_NOZORDER|SWP_SHOWWINDOW);InvalidateRect(cursor,nullptr,FALSE);
  }
  void refresh(){
    for(const auto& workspace:broker.removed_workspaces())data.forget(workspace);
    if(Clock::now()>=next_state){next_state=Clock::now()+std::chrono::milliseconds(75);auto snapshot=broker.state();std::map<std::string,Json> next;names.clear();workspace_names.clear();
      for(const auto& worker:snapshot["workers"])names[str(worker,"agentSessionId")]=str(worker,"name");
      for(const auto& workspace:snapshot["workspaces"]){const auto id=str(workspace,"workspaceId");workspace_names[id]=ui::workspace_label(id,str(workspace,"displayName"),workspace.value("private",false));}
      for(auto tab:snapshot["tabs"]){const auto tab_id=str(tab,"tabId");next[tab_id]=std::move(tab);}
      if(next!=states){states=std::move(next);InvalidateRect(window,nullptr,FALSE);InvalidateRect(tree,nullptr,FALSE);}
      pairing_count=snapshot["pairings"].size();
    }
    for(const auto& row:engine.native_tabs())if(auto found=hosts.find(str(row,"tabId"));found!=hosts.end()){
      auto& host=found->second;host.metadata=row;host.metadata["title"]=ui::tab_title(str(row,"title"),str(row,"url"));const auto state=states.contains(host.id)?states.at(host.id):Json::object();const bool agent=state.value("agentAvailable",false),paused=state.value("humanPaused",false);
      const PointerPoint actual{row.value("pointerX",0.0),row.value("pointerY",0.0)};
      if(agent&&!host.initialized){host.display=parked(host);host.initialized=true;}
      const auto human_revision=row.value("humanPointerRevision",uint64_t{}),sync_revision=row.value("pointerSyncRevision",uint64_t{}),agent_revision=row.value("agentPointerRevision",uint64_t{});
      switch(host.transitions.update(agent,paused,human_revision,sync_revision,agent_revision)){
        case PointerVisual::human:animate(host,actual,100);break;
        case PointerVisual::park:animate(host,parked(host),120);break;
        case PointerVisual::synchronize:animate(host,actual,80);break;
        case PointerVisual::follow:host.animating=false;host.display=actual;break;
        default:break;
      }
      host.agent=agent;host.paused=paused;
      if(!row.value("loading",true)&&!row.value("protected",false)&&row.value("privacyReady",false)&&str(row,"url")!=host.recorded_url){host.recorded_url=str(row,"url");try{data.visit(host.workspace,row.value("private",false),false,host.recorded_url,str(row,"title"));}catch(...){status=ui::tr(L"Browser history could not be saved.");}}
    }
    rebuild_tree();sync_toolbar();update_cursor();animate();
    if(Clock::now()>=next_flush){next_flush=Clock::now()+std::chrono::seconds(3);if(!data.flush())status=ui::tr(L"Browser metadata could not be saved. Existing data is preserved.");}
    std::erase_if(panels,[](const auto& panel){return !panel->window;});
  }
  std::string navigate_value(){const auto input=ui::text(address);if(input=="about:blank"||input.rfind("http://",0)==0||input.rfind("https://",0)==0)return input;
    if(input.find(' ')==std::string::npos&&input.find('.')!=std::string::npos&&input.find(':')==std::string::npos)return "https://"+input;
    return "https://www.google.com/search?q="+CefURIEncode(input,true).ToString();
  }
  void navigate(){const auto value=navigate_value();address_dirty=false;engine.native_command(selected,"navigate",value,[this](Json result){if(!result.value("ok",false)){status=ui::tr(L"This address could not be opened.");InvalidateRect(window,nullptr,FALSE);}});engine.native_command(selected,"focus");}
  void show_find(){find_visible=true;layout();SetFocus(find_text);SendMessageW(find_text,EM_SETSEL,0,-1);}
  void menu(){auto popup=CreatePopupMenu();auto item=[&](int id,const wchar_t* caption){AppendMenuW(popup,MF_STRING,id,caption);};
    item(MenuFind,ui::tr(L"Find on page\tCtrl+F"));item(MenuZoomIn,ui::tr(L"Zoom in\tCtrl++"));item(MenuZoomOut,ui::tr(L"Zoom out\tCtrl+-"));item(MenuZoomReset,ui::tr(L"Reset zoom\tCtrl+0"));AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
    item(MenuBookmark,ui::tr(L"Bookmark this page\tCtrl+D"));item(MenuBookmarks,ui::tr(L"Bookmarks"));item(MenuHistory,ui::tr(L"History"));item(MenuDownloads,ui::tr(L"Downloads"));AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
    item(MenuPrint,ui::tr(L"Print\tCtrl+P"));item(MenuPdf,ui::tr(L"Save as PDF…"));item(MenuPermissions,ui::tr(L"Site permissions"));item(MenuPrivate,ui::tr(L"New private workspace"));item(MenuCloseTab,ui::tr(L"Close tab\tCtrl+W"));AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
    item(MenuThemeSystem,ui::tr(L"Theme: System"));item(MenuThemeLight,ui::tr(L"Theme: Light"));item(MenuThemeDark,ui::tr(L"Theme: Dark"));
    CheckMenuItem(popup,ui::theme_mode==ui::ThemeMode::system?MenuThemeSystem:ui::theme_mode==ui::ThemeMode::light?MenuThemeLight:MenuThemeDark,MF_BYCOMMAND|MF_CHECKED);
    auto languages=CreatePopupMenu();AppendMenuW(languages,MF_STRING,MenuLanguageEnglish,L"English");AppendMenuW(languages,MF_STRING,MenuLanguageChinese,L"简体中文");
    CheckMenuRadioItem(languages,MenuLanguageEnglish,MenuLanguageChinese,ui::preferred_language==ui::Language::simplified_chinese?MenuLanguageChinese:MenuLanguageEnglish,MF_BYCOMMAND);
    AppendMenuW(popup,MF_POPUP,reinterpret_cast<UINT_PTR>(languages),L"Language / 语言");
    auto sidebar_menu=CreatePopupMenu();AppendMenuW(sidebar_menu,MF_STRING,MenuSidebarNarrower,ui::tr(L"Narrower"));AppendMenuW(sidebar_menu,MF_STRING,MenuSidebarWider,ui::tr(L"Wider"));AppendMenuW(sidebar_menu,MF_STRING,MenuSidebarReset,ui::tr(L"Reset width"));AppendMenuW(popup,MF_POPUP,reinterpret_cast<UINT_PTR>(sidebar_menu),ui::tr(L"Sidebar width"));
    AppendMenuW(popup,MF_SEPARATOR,0,nullptr);item(MenuQuickTour,ui::tr(L"Quick tour"));item(MenuDocumentation,ui::tr(L"GitHub documentation"));item(MenuUpdates,ui::tr(L"Check for updates"));item(MenuAbout,ui::tr(L"About Xenon"));item(MenuNotices,ui::tr(L"Third-party notices"));item(MenuExit,ui::tr(L"Exit Xenon"));
    RECT rect{};GetWindowRect(controls.at(Menu),&rect);const auto id=TrackPopupMenu(popup,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_BOTTOMALIGN,rect.left,rect.top-ui::dip(window,6),0,window,nullptr);DestroyMenu(popup);if(id)command(id);
  }
  static LRESULT CALLBACK edit_proc(HWND control,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR owner){auto self=reinterpret_cast<Impl*>(owner);
    if(message==WM_KEYDOWN&&wp==VK_RETURN){if(control==self->address)self->navigate();else self->engine.native_command(self->selected,"find",ui::text(control));return 0;}
    if(message==WM_KEYDOWN&&wp==VK_ESCAPE){if(control==self->find_text){self->find_visible=false;self->engine.native_command(self->selected,"find-close");self->layout();}else {self->address_dirty=false;auto found=self->hosts.find(self->selected);self->set_address(found==self->hosts.end()?std::string{}:str(found->second.metadata,"url"));}self->engine.native_command(self->selected,"focus");return 0;}return DefSubclassProc(control,message,wp,lp);
  }
  static LRESULT CALLBACK tree_proc(HWND control,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR owner){auto self=reinterpret_cast<Impl*>(owner);
    if(message==WM_KEYDOWN&&(wp==VK_DELETE||wp==VK_RETURN)){TVITEMW item{};item.hItem=TreeView_GetSelection(control);item.mask=TVIF_PARAM;if(item.hItem&&TreeView_GetItem(control,&item)&&item.lParam>0&&static_cast<size_t>(item.lParam)<=self->tree_keys.size()){const auto [id,group]=self->tree_keys[item.lParam-1];if(wp==VK_DELETE&&!group){self->engine.native_command(id,"close");return 0;}if(wp==VK_RETURN&&group&&self->empty_workspace(id)){self->open_workspace(id);return 0;}}}
    if(message==WM_LBUTTONDOWN){self->pressed_close=self->close_at({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});if(!self->pressed_close.empty()){SetCapture(control);return 0;}}
    if(message==WM_LBUTTONUP&&!self->pressed_close.empty()){auto id=std::exchange(self->pressed_close,{});if(GetCapture()==control)ReleaseCapture();if(id==self->close_at({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}))self->engine.native_command(id,"close");return 0;}
    if(message==WM_CAPTURECHANGED||message==WM_CANCELMODE)self->pressed_close.clear();
    if(message==WM_MBUTTONUP){TVHITTESTINFO hit{};hit.pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};auto item=TreeView_HitTest(control,&hit);TVITEMW entry{};entry.hItem=item;entry.mask=TVIF_PARAM;if(item&&TreeView_GetItem(control,&entry)&&entry.lParam>0&&static_cast<size_t>(entry.lParam)<=self->tree_keys.size()&&!self->tree_keys[entry.lParam-1].second)self->engine.native_command(self->tree_keys[entry.lParam-1].first,"close");return 0;}
    if(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK){TVHITTESTINFO hit{};hit.pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};auto item=TreeView_HitTest(control,&hit);TVITEMW entry{};entry.hItem=item;entry.mask=TVIF_PARAM;if(item&&TreeView_GetItem(control,&entry)&&entry.lParam>0&&static_cast<size_t>(entry.lParam)<=self->tree_keys.size()){const auto& [id,group]=self->tree_keys[entry.lParam-1];if(group&&self->empty_workspace(id)){self->open_workspace(id);return 0;}}}
    auto result=DefSubclassProc(control,message,wp,lp);
    if(message==WM_MOUSEMOVE||message==WM_MOUSELEAVE){TVHITTESTINFO hit{};hit.pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};const auto next=message==WM_MOUSELEAVE?nullptr:TreeView_HitTest(control,&hit);const auto close=message==WM_MOUSELEAVE?std::string{}:self->close_at(hit.pt);if(next!=self->hovered_row||close!=self->hovered_close){if(next!=self->hovered_row){if(self->hovered_row)self->row_fades[self->hovered_row].set(false,ui::hover_duration);if(next)self->row_fades[next].set(true,ui::hover_duration);}self->hovered_row=next;self->hovered_close=close;InvalidateRect(control,nullptr,FALSE);}if(message==WM_MOUSEMOVE){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,control,0};TrackMouseEvent(&track);}}
    if(message==WM_VSCROLL||message==WM_MOUSEWHEEL||message==WM_KEYDOWN)InvalidateRect(self->window,nullptr,FALSE);if(message==WM_NCDESTROY)RemoveWindowSubclass(control,tree_proc,3);return result;}
  static LRESULT CALLBACK host_proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){if(msg==WM_NCHITTEST)return HTCLIENT;return DefWindowProcW(h,msg,wp,lp);}
  static LRESULT CALLBACK cursor_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(message==WM_NCHITTEST)return HTTRANSPARENT;if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_PAINT&&self){PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT rect{};GetClientRect(h,&rect);ui::fill(dc,rect,RGB(255,0,255));
      const int scale=GetDpiForWindow(self->window);auto d=[&](int v){return MulDiv(v,scale,96);};POINT points[]={{d(2),d(1)},{d(2),d(22)},{d(7),d(17)},{d(11),d(25)},{d(15),d(23)},{d(11),d(15)},{d(19),d(15)}};
      auto pen=CreatePen(PS_SOLID,d(1),RGB(1,1,1));auto brush=CreateSolidBrush(RGB(250,250,250));auto old_pen=SelectObject(dc,pen),old_brush=SelectObject(dc,brush);Polygon(dc,points,7);SelectObject(dc,old_brush);DeleteObject(brush);
      auto accent=CreateSolidBrush(self->color(self->selected));SelectObject(dc,accent);Ellipse(dc,d(17),d(21),d(24),d(28));SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(accent);DeleteObject(pen);EndPaint(h,&paint);return 0;}
    return DefWindowProcW(h,message,wp,lp);
  }
  void command(int id);
  void panel(const std::string& kind,const std::string& description={},std::function<void(bool)> answer={},const std::string& source_tab={});
  static LRESULT CALLBACK panel_proc(HWND,UINT,WPARAM,LPARAM);
  static LRESULT CALLBACK proc(HWND h,UINT message,WPARAM wp,LPARAM lp){try{return dispatch_proc(h,message,wp,lp);}catch(...){return message==WM_NCCREATE?FALSE:message==WM_CREATE?-1:0;}}
  static LRESULT CALLBACK dispatch_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(h,message,wp,lp);
    LRESULT result{};if(ui::ctl_color(message,wp,lp,result))return result;
    switch(message){case WM_CREATE:self->build();return 0;case WM_SIZE:self->layout();return 0;
      case WM_SETCURSOR:{POINT point{};GetCursorPos(&point);ScreenToClient(h,&point);if(LOWORD(lp)==HTCLIENT&&(self->resizing_sidebar||self->sidebar_hit(point))){SetCursor(LoadCursorW(nullptr,IDC_SIZEWE));return TRUE;}break;}
      case WM_LBUTTONDOWN:if(self->sidebar_hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)})){self->drag_origin=GET_X_LPARAM(lp);self->drag_width=self->sidebar;self->resizing_sidebar=true;SetPropW(h,L"XenonSidebarDrag",reinterpret_cast<HANDLE>(1));SetCapture(h);return 0;}break;
      case WM_MOUSEMOVE:if(self->resizing_sidebar){self->resize_sidebar(self->drag_width+MulDiv(GET_X_LPARAM(lp)-self->drag_origin,96,GetDpiForWindow(h)));return 0;}
        {const bool hover=self->sidebar_hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});if(hover!=self->sidebar_hover){self->sidebar_hover=hover;self->invalidate_grip();}if(hover){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);}}break;
      case WM_MOUSELEAVE:if(self->sidebar_hover){self->sidebar_hover=false;self->invalidate_grip();}return 0;
      case WM_LBUTTONUP:if(self->resizing_sidebar){self->finish_sidebar(true);return 0;}break;
      case WM_CAPTURECHANGED:self->finish_sidebar(true);return 0;
      case WM_CANCELMODE:self->finish_sidebar(true);break;
      case WM_KEYDOWN:if(wp==VK_ESCAPE&&self->resizing_sidebar){self->finish_sidebar(true);return 0;}break;
      case WM_LBUTTONDBLCLK:if(self->sidebar_hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)})){self->finish_sidebar(false);self->resize_sidebar(240);self->save_sidebar();return 0;}break;
      case WM_DPICHANGED:{const auto rect=reinterpret_cast<RECT*>(lp);SetWindowPos(h,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);DeleteObject(self->font);self->font=ui::font(h);for(const auto& [id,c]:self->controls)SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(self->font),TRUE);TreeView_SetItemHeight(self->tree,ui::dip(h,38));self->layout();return 0;}
      case WM_SETTINGCHANGE:case WM_THEMECHANGED:self->theme();return 0;
      case WM_GETMINMAXINFO:{auto value=reinterpret_cast<MINMAXINFO*>(lp);value->ptMinTrackSize={ui::dip(h,800),ui::dip(h,480)};return 0;}
      case WM_TIMER:case ShellChanged:self->refresh();
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
        if(message==WM_TIMER&&wp==2&&!self->fixture_destination.empty())self->fixture_snapshot(self->fixture_destination);
#endif
        return 0;
      case WM_COMMAND:if(LOWORD(wp)==Address&&HIWORD(wp)==EN_CHANGE&&!self->setting_address)self->address_dirty=true;
        else if(LOWORD(wp)==Address&&(HIWORD(wp)==EN_SETFOCUS||HIWORD(wp)==EN_KILLFOCUS)){self->address_focus.set(HIWORD(wp)==EN_SETFOCUS,FocusDuration);InvalidateRect(h,&self->address_bounds,FALSE);}else if(HIWORD(wp)==BN_CLICKED)self->command(LOWORD(wp));return 0;
      case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item&&item->CtlType==ODT_BUTTON){
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
        if(self->controls.contains(static_cast<int>(item->CtlID))){auto& counts=self->fixture_paints[static_cast<int>(item->CtlID)];++counts.first;if(ui::hovered(*item))++counts.second;}
#endif
        ui::button(*item,self->font,self->button_icon(static_cast<int>(item->CtlID)));return TRUE;}break;}
      case WM_NOTIFY:{auto notice=reinterpret_cast<NMHDR*>(lp);
        if(notice->hwndFrom!=self->tree)break;
        if(notice->code==TVN_SELCHANGEDW&&!self->rebuilding){auto item=reinterpret_cast<NMTREEVIEWW*>(lp);const auto index=item->itemNew.lParam;if(index>0&&static_cast<size_t>(index)<=self->tree_keys.size()&&!self->tree_keys[index-1].second)self->choose(self->tree_keys[index-1].first,true);return 0;}
        if(notice->code==TVN_ITEMEXPANDEDW){InvalidateRect(h,nullptr,FALSE);return 0;}
        if(notice->code==NM_CUSTOMDRAW){auto draw=reinterpret_cast<NMTVCUSTOMDRAW*>(lp);if(draw->nmcd.dwDrawStage==CDDS_PREPAINT){self->pill_valid=self->pill_rect(self->pill);return CDRF_NOTIFYITEMDRAW;}
          if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){self->draw_tree(*draw);return CDRF_SKIPDEFAULT;}}
        break;}
      case WM_ERASEBKGND:return 1;
      case WM_PAINT:{PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT bounds{};GetClientRect(h,&bounds);auto memory=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,std::max<LONG>(1,bounds.right),std::max<LONG>(1,bounds.bottom));if(memory&&bitmap){auto old=SelectObject(memory,bitmap);self->paint(memory);BitBlt(dc,paint.rcPaint.left,paint.rcPaint.top,paint.rcPaint.right-paint.rcPaint.left,paint.rcPaint.bottom-paint.rcPaint.top,memory,paint.rcPaint.left,paint.rcPaint.top,SRCCOPY);SelectObject(memory,old);}else self->paint(dc);if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);EndPaint(h,&paint);return 0;}
      case WM_CLOSE:if(!self->shutting_down){self->shutting_down=true;self->data.flush();self->engine.shutdown();}return 0;
      case WM_DESTROY:self->finish_sidebar(false);KillTimer(h,1);KillTimer(h,2);return 0;
    }return DefWindowProcW(h,message,wp,lp);
  }
};
void BrowserShell::Impl::command(int id){try{
  if(id==Back||id==Forward||id==Reload)engine.native_command(selected,id==Back?"back":id==Forward?"forward":hosts.contains(selected)&&hosts.at(selected).metadata.value("loading",false)?"stop":"reload");
  else if(id==Go)navigate();else if(id==Controls)engine.show_controls();else if(id==Menu)menu();
  else if(id==NewTab)broker.open_human_tab(workspace(),"about:blank",[this](Json value){if(!value.value("ok",false))status=ui::error_text(str(value["error"],"message"));});
  else if(id==MenuPrivate)broker.open_human_workspace("about:blank",[](Json){},true);
  else if(id==MenuCloseTab)engine.native_command(selected,"close");
  else if(id==MenuUpdates)engine.show_updates();
  else if(id==MenuDocumentation||(id==MenuQuickTour&&ui::show_introduction(window,false)==ui::IntroductionResult::documentation)){
    broker.open_human_tab("native-default",ui::utf8(ui::documentation_url),[this](Json value){if(!value.value("ok",false)){status=ui::tr(L"This address could not be opened.");InvalidateRect(window,nullptr,FALSE);}});
  }
  else if(id==MenuFind)show_find();else if(id==FindClose){find_visible=false;engine.native_command(selected,"find-close");layout();engine.native_command(selected,"focus");}
  else if(id==FindNext||id==FindPrevious)engine.native_command(selected,id==FindNext?"find-next":"find-previous",ui::text(find_text));
  else if(id==MenuZoomIn||id==MenuZoomOut||id==MenuZoomReset){double zoom=hosts.contains(selected)?hosts.at(selected).metadata.value("zoom",1.0):1;zoom=id==MenuZoomReset?1:id==MenuZoomIn?zoom*1.2:zoom/1.2;engine.native_command(selected,"zoom",std::to_string(zoom));}
  else if(id==MenuBookmark){auto found=hosts.find(selected);if(found!=hosts.end()&&!found->second.metadata.value("protected",false))status=data.bookmark(workspace(),is_private(),str(found->second.metadata,"url"),str(found->second.metadata,"title"))?ui::tr(L"Bookmark saved."):ui::tr(L"Bookmark could not be saved.");}
  else if(id==MenuBookmarks||id==MenuHistory||id==MenuDownloads||id==MenuPermissions||id==MenuAbout||id==MenuNotices)panel(id==MenuBookmarks?"bookmarks":id==MenuHistory?"history":id==MenuDownloads?"downloads":id==MenuPermissions?"site":id==MenuNotices?"notices":"about");
  else if(id==MenuPrint)engine.native_command(selected,"print");
  else if(id==MenuPdf){wchar_t path[32768]=L"Xenon page.pdf";OPENFILENAMEW picker{};picker.lStructSize=sizeof(picker);picker.hwndOwner=window;picker.lpstrFile=path;picker.nMaxFile=32768;picker.lpstrFilter=ui::pdf_filter();picker.lpstrDefExt=L"pdf";picker.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetSaveFileNameW(&picker))engine.native_command(selected,"pdf",ui::utf8(path),[this](Json value){status=value.value("ok",false)?ui::tr(L"PDF saved."):ui::tr(L"PDF could not be saved.");InvalidateRect(window,nullptr,FALSE);});}
  else if(id==MenuThemeSystem||id==MenuThemeLight||id==MenuThemeDark){if(!ui::save_theme(id==MenuThemeDark?ui::ThemeMode::dark:id==MenuThemeLight?ui::ThemeMode::light:ui::ThemeMode::system))status=ui::tr(L"Theme changed for this run; saving failed.");
    else status=ui::theme_mode==ui::web_theme_mode?std::wstring{}:ui::tr(L"Theme saved. Web pages use it after Xenon restarts.");theme();}
  else if(id==MenuSidebarNarrower||id==MenuSidebarWider||id==MenuSidebarReset){resize_sidebar(id==MenuSidebarReset?240:sidebar+(id==MenuSidebarWider?32:-32));save_sidebar();}
  else if(id==MenuLanguageEnglish||id==MenuLanguageChinese){
    if(!ui::save_language(id==MenuLanguageChinese?ui::Language::simplified_chinese:ui::Language::english))status=ui::tr(L"The language preference could not be saved. Try again.");
    else {status=ui::preferred_language==ui::language?ui::tr(L"Language preference saved."):ui::tr(L"Restart Xenon to apply the selected language. Save unfinished website work before exiting.");
      if(ui::preferred_language!=ui::language)MessageBoxW(window,status.c_str(),L"Language / 语言",MB_OK|MB_ICONINFORMATION);}
  }
  else if(id==MenuExit)SendMessageW(window,WM_CLOSE,0,0);
  InvalidateRect(window,nullptr,FALSE);
}catch(...){status=ui::tr(L"The browser action could not be completed. Existing data is preserved.");InvalidateRect(window,nullptr,FALSE);}}
void BrowserShell::Impl::panel(const std::string& kind,const std::string& description,std::function<void(bool)> answer,const std::string& source_tab){
  if(kind=="permission"&&std::count_if(panels.begin(),panels.end(),[](const auto& panel){return panel->kind=="permission"&&panel->window;})>=8){if(answer)answer(false);return;}
  auto panel=std::make_unique<Panel>();panel->owner=this;panel->kind=kind;panel->workspace=workspace();panel->private_mode=is_private();panel->tab=source_tab.empty()?selected:source_tab;panel->answer=std::move(answer);auto raw=panel.get();panels.push_back(std::move(panel));
  const auto title=ui::wide(kind=="permission"?ui::tr8("Website permission request"):kind=="bookmarks"?ui::tr8("Xenon Bookmarks"):kind=="history"?ui::tr8("Xenon History"):kind=="downloads"?ui::tr8("Xenon Downloads"):kind=="site"?ui::tr8("Xenon Site Permissions"):kind=="notices"?ui::tr8("Xenon Third-party Notices"):ui::tr8("About Xenon"));
  CreateWindowExW(0,L"XenonBrowserPanel",title.c_str(),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,780,480,window,nullptr,branding_module(),raw);
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
    if(index>=0&&static_cast<size_t>(index)<panel->rows.size()){const auto row=panel->rows[index];if(id==1){owner->engine.native_command(owner->selected,"navigate",str(row,"url"));DestroyWindow(h);}
      else if(panel->kind=="bookmarks"&&owner->data.remove_bookmark(panel->workspace,panel->private_mode,str(row,"url"))){panel->rows.erase(panel->rows.begin()+index);ListView_DeleteItem(panel->list,index);}}
    return 0;
  }
  if(message==WM_CLOSE){DestroyWindow(h);return 0;}if(message==WM_DESTROY){if(panel->answer&&!panel->answered){panel->answered=true;panel->answer(false);}DeleteObject(panel->font);return 0;}
  if(message==WM_NCDESTROY){panel->window=nullptr;return DefWindowProcW(h,message,wp,lp);}
  if(message==WM_PAINT){PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT rect{};GetClientRect(h,&rect);ui::fill(dc,rect,ui::palette().canvas);EndPaint(h,&paint);return 0;}
  return DefWindowProcW(h,message,wp,lp);
}catch(...){return message==WM_NCCREATE?FALSE:message==WM_CREATE?-1:0;}
}
BrowserShell::BrowserShell(Broker& broker,CefEngine& engine,const std::filesystem::path& root):impl_(std::make_unique<Impl>(broker,engine,root)){
  INITCOMMONCONTROLSEX common{sizeof(common),ICC_TREEVIEW_CLASSES|ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&common);
  for(const auto& [name,proc]:std::vector<std::pair<const wchar_t*,WNDPROC>>{{L"XenonBrowserShell",Impl::proc},{L"XenonTabHost",Impl::host_proc},{L"XenonAgentCursor",Impl::cursor_proc},{L"XenonBrowserPanel",Impl::panel_proc}}){WNDCLASSW type{};type.style=wcscmp(name,L"XenonBrowserShell")==0?CS_DBLCLKS:0;type.lpfnWndProc=proc;type.hInstance=branding_module();type.lpszClassName=name;type.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&type);}
  CreateWindowExW(0,L"XenonBrowserShell",ui::tr(L"Xenon Browser"),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1200,800,nullptr,nullptr,branding_module(),impl_.get());
  if(!impl_->window||!IsWindow(impl_->window))throw std::runtime_error("Cannot create native browser shell");
  impl_->cursor=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"XenonAgentCursor",ui::tr(L"Agent pointer"),WS_POPUP,0,0,25,30,impl_->window,nullptr,branding_module(),impl_.get());SetLayeredWindowAttributes(impl_->cursor,RGB(255,0,255),255,LWA_COLORKEY);
  SetWindowSubclass(impl_->address,Impl::edit_proc,1,reinterpret_cast<DWORD_PTR>(impl_.get()));SetWindowSubclass(impl_->find_text,Impl::edit_proc,2,reinterpret_cast<DWORD_PTR>(impl_.get()));
}
BrowserShell::~BrowserShell(){impl_->data.flush();for(auto& panel:impl_->panels)if(panel->window)DestroyWindow(panel->window);if(impl_->cursor)DestroyWindow(impl_->cursor);if(impl_->window)DestroyWindow(impl_->window);if(impl_->font)DeleteObject(impl_->font);}
void BrowserShell::show(){ShowWindow(impl_->window,SW_SHOWNORMAL);SetWindowPos(impl_->window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);SetForegroundWindow(impl_->window);}
void BrowserShell::request_exit(){PostMessageW(impl_->window,WM_CLOSE,0,0);}
HWND BrowserShell::create_host(const std::string& workspace,const std::string& id,bool human){if(auto found=impl_->hosts.find(id);found!=impl_->hosts.end())return found->second.window;
  Impl::Host host;host.id=id;host.workspace=workspace;host.human=human;host.window=CreateWindowExW(0,L"XenonTabHost",ui::tr(L"Web page"),WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,100,100,impl_->window,nullptr,branding_module(),nullptr);
  if(!host.window)return nullptr;impl_->hosts.emplace(id,std::move(host));impl_->order.push_back(id);impl_->layout();
  SetWindowPos(impl_->hosts.at(id).window,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
  if(human||impl_->selected.empty())impl_->choose(id,false);return impl_->hosts.at(id).window;
}
void BrowserShell::tab_created(const std::string& id,HWND browser){if(auto found=impl_->hosts.find(id);found!=impl_->hosts.end()){found->second.browser=browser;impl_->layout();if(found->second.human&&impl_->selected==id)impl_->choose(id,true);}impl_->refresh();}
void BrowserShell::tab_closed(const std::string& id){if(auto found=impl_->hosts.find(id);found!=impl_->hosts.end()){DestroyWindow(found->second.window);impl_->hosts.erase(found);std::erase(impl_->order,id);}
  if(impl_->selected==id){impl_->selected.clear();if(!impl_->order.empty())impl_->choose(impl_->order.back(),false);}PostMessageW(impl_->window,ShellChanged,0,0);
}
void BrowserShell::refresh(){PostMessageW(impl_->window,ShellChanged,0,0);}
bool BrowserShell::pretranslate(MSG& message){
  if(message.message<WM_KEYFIRST||message.message>WM_KEYLAST)return false;const auto root=GetAncestor(message.hwnd,GA_ROOT);
  for(const auto& panel:impl_->panels)if(panel->window==root)return IsDialogMessageW(root,&message)!=FALSE;
  if(root!=impl_->window)return false;
  if(message.message==WM_KEYDOWN){const bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0,alt=(GetKeyState(VK_MENU)&0x8000)!=0;
    if(control&&(GetKeyState(VK_SHIFT)&0x8000)&&message.wParam=='X'){impl_->command(Controls);return true;}
    if(control&&message.wParam=='L'){SetFocus(impl_->address);SendMessageW(impl_->address,EM_SETSEL,0,-1);return true;}
    if(control&&message.wParam=='F'){impl_->command(MenuFind);return true;}
    if(control&&message.wParam=='T'){impl_->command(NewTab);return true;}
    if(control&&message.wParam=='W'){impl_->command(MenuCloseTab);return true;}
    if(control&&message.wParam=='D'){impl_->command(MenuBookmark);return true;}
    if(control&&message.wParam=='P'){impl_->command(MenuPrint);return true;}
    if(control&&message.wParam=='H'){impl_->command(MenuHistory);return true;}
    if(control&&message.wParam=='J'){impl_->command(MenuDownloads);return true;}
    if(control&&message.wParam==VK_OEM_PLUS){impl_->command(MenuZoomIn);return true;}
    if(control&&message.wParam==VK_OEM_MINUS){impl_->command(MenuZoomOut);return true;}
    if(control&&message.wParam=='0'){impl_->command(MenuZoomReset);return true;}
    if(message.wParam==VK_F5){impl_->command(Reload);return true;}
    if(alt&&message.wParam==VK_LEFT){impl_->command(Back);return true;}
    if(alt&&message.wParam==VK_RIGHT){impl_->command(Forward);return true;}
  }
  if(ui::edit_command(message,impl_->address)||ui::edit_command(message,impl_->find_text))return true;
  for(const auto& [id,control]:impl_->controls)if(message.hwnd==control)return IsDialogMessageW(impl_->window,&message)!=FALSE;
  return false;
}
void BrowserShell::permission(const std::string& tab,const std::string& origin,const std::string& description,std::function<void(bool)> answer){impl_->panel("permission",ui::tr8("Website: ")+origin+ui::tr8("\n\nRequested access: ")+description+ui::tr8("\n\nAllow only if you intended to give this website access."),std::move(answer),tab);}
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
void BrowserShell::fixture_snapshot(const std::filesystem::path& destination){impl_->fixture_snapshot(destination);}
void BrowserShell::Impl::fixture_snapshot(const std::filesystem::path& destination){
  if(destination.parent_path().filename()!=L".cache"||!destination.filename().wstring().starts_with(L"shell-ui-"))return;
  std::ifstream marker(destination/"SYNTHETIC_TEST_PROFILE");std::string contents;std::getline(marker,contents);if(contents!="XENON_SYNTHETIC_UI_FIXTURE")return;
  fixture_destination=destination;
  RECT rect{};GetWindowRect(window,&rect);wchar_t title[256]{},type[128]{},desktop[128]{};GetWindowTextW(window,title,256);GetClassNameW(window,type,128);DWORD size{};GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof(desktop),&size);
  Json buttons=Json::array();for(const auto& [id,control]:controls)if((GetWindowLongPtrW(control,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW){const auto counts=fixture_paints[id];buttons.push_back({{"id",id},{"hover",GetPropW(control,L"XenonHover")!=nullptr},{"paints",counts.first},{"hoverPaints",counts.second}});}
  int footer_clearance{};if(auto found=hosts.find(selected);found!=hosts.end()){RECT host{};GetWindowRect(found->second.window,&host);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&host),2);footer_clearance=panel_bounds.bottom-host.bottom;}
  const auto colors=ui::palette();auto rgb=[](COLORREF value){return Json::array({GetRValue(value),GetGValue(value),GetBValue(value)});};
  Json value={{"visible",IsWindowVisible(window)!=FALSE},{"class",ui::utf8(type)},{"title",ui::utf8(title)},{"desktop",ui::utf8(desktop)},
    {"width",rect.right-rect.left},{"height",rect.bottom-rect.top},{"hosts",hosts.size()},{"selected",selected},{"style",GetWindowLongPtrW(window,GWL_STYLE)},
    {"dpi",GetDpiForWindow(window)},{"dpiAwareness",GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(window))},
    {"perMonitorV2",AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(window),DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)!=FALSE},{"antialiasedVectors",ui::vector_rendering()},
    {"sidebarWidth",sidebar},{"preferredSidebarWidth",ui::sidebar_width},{"sidebarDragging",resizing_sidebar},{"footerClearance",footer_clearance},{"buttons",buttons},
    {"palette",{{"canvas",rgb(colors.canvas)},{"surface",rgb(colors.surface)},{"hover",rgb(ui::hover_background())},{"selection",rgb(ui::selection())}}}};
  const auto target=destination/"native-shell-state.json",temporary=destination/"native-shell-state.json.tmp";
  {std::ofstream output(temporary);output<<value.dump(2);}local_security::restrict_path(temporary);MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
#endif
}
