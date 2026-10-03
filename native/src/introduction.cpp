#include "xenon/introduction.hpp"
#include "xenon/ui_theme.hpp"
#include <array>
#include <stdexcept>

namespace xenon::ui {
bool needs_introduction(const std::filesystem::path& root) {
  std::error_code error;
  const bool exists=std::filesystem::exists(root,error);
  if(error)return false;
  if(!exists)return true;
  // A failed atomic preference save may leave its temporary file. It is not
  // an existing browser profile and must not suppress a retry on the next run.
  std::filesystem::directory_iterator entries(root,error),end;
  if(error)return false;
  if(entries==end)return true;
  if(entries->path().filename()!=L"ui-settings.json.tmp")return false;
  entries.increment(error);return !error&&entries==end;
}
namespace {
enum Id {Next=1,Skip=2,Previous=3,Locale=4,Documentation=5,Title=10,Summary,Progress,Error,LanguageLabel,LanguageHint,RowTitle=20,RowBody=30};
struct Copy {const wchar_t* english;const wchar_t* chinese;const wchar_t* in(Language locale)const{return locale==Language::simplified_chinese?chinese:english;}};
struct Row {Icon icon;Copy title,body;};
struct Page {Copy title,summary;std::array<Row,3> rows;};
constexpr Page pages[]{
  {{L"Welcome to Xenon",L"欢迎使用 Xenon"},
   {L"A browser for you and your agents. Let's get you started.",L"供您和代理使用的浏览器。现在开始了解 Xenon。"},{}},
  {{L"Your browser, at a glance",L"快速了解浏览器"},
   {L"The essentials are always close to your page.",L"常用功能就在网页旁边。"},{{
    {Icon::go,{L"Address and search",L"地址和搜索"},{L"Type a website or search above the page. Ctrl+L jumps to the address bar.",L"在网页上方输入网址或搜索内容。按 Ctrl+L 可跳转到地址栏。"}},
    {Icon::plus,{L"Tabs in the sidebar",L"侧边栏标签页"},{L"Choose a tab on the left. Use the + button or Ctrl+T for a new tab; X closes it.",L"在左侧选择标签页。使用 + 按钮或 Ctrl+T 新建标签页；使用 X 关闭。"}},
    {Icon::menu,{L"Your everyday tools",L"常用工具"},{L"The bottom-left Menu has bookmarks, history, downloads, themes and language settings.",L"左下角菜单提供书签、历史记录、下载、主题和语言设置。"}}
   }}},
  {{L"Keep work organized and stay in control",L"组织工作，掌握控制权"},
   {L"Open Controls with the sliders button at the bottom left, or Ctrl+Shift+X.",L"点击左下角的滑块按钮或按 Ctrl+Shift+X 打开控制中心。"},{{
    {Icon::plus,{L"Workspaces separate your sessions",L"工作区隔离浏览会话"},{L"Start in Personal. Create more workspaces in Controls for separate website logins and tasks.",L"从个人工作区开始。可在控制中心创建更多工作区，将网站登录和任务分开。"}},
    {Icon::controls,{L"Connect an agent when you're ready",L"准备好后连接代理"},{L"Approve an MCP client in Controls, then share a workspace and choose its permissions. Browsing needs no model account.",L"在控制中心批准 MCP 客户端，然后共享工作区并选择权限。浏览网页无需模型账号。"}},
    {Icon::stop,{L"You can step in at any time",L"随时介入"},{L"Typing or clicking on a page pauses its agent. Use Take ownership in Controls to keep control of the tab.",L"在网页上输入或点击会暂停该代理。使用控制中心的接管控制权功能可持续控制标签页。"}}
   }}},
  {{L"You're ready to browse",L"开始浏览吧"},
   {L"Start with a blank tab, or open the getting-started guide on GitHub.",L"从空白标签页开始，或打开 GitHub 上的入门指南。"},{{
    {Icon::go,{L"Installation, pairing and first steps",L"安装、配对和入门"},{L"The GitHub documentation walks through setup and links to the full user guide and MCP reference.",L"GitHub 文档介绍设置步骤，并提供完整用户指南和 MCP 参考文档的链接。"}},
    {Icon::menu,{L"Come back whenever you need",L"随时重新查看"},{L"Open Menu → Quick tour to see this introduction again. Change the browser language from Menu → Language / 语言.",L"打开菜单 → 快速导览可再次查看介绍。可通过菜单 → Language / 语言更改浏览器语言。"}},
    {Icon::none,{L"",L""},{L"",L""}}
   }}}
};
struct Introduction {
  HWND window{},owner{};HFONT face{},heading{},bold{};Language selected{preferred_language};
  int page{};bool first_run{},done{};IntroductionResult result{IntroductionResult::cancelled};
  const wchar_t* copy(Copy value)const{return value.in(selected);}
  HWND control(int id)const{return GetDlgItem(window,id);}
  void set(int id,const wchar_t* value){SetWindowTextW(control(id),value);}
  HWND add(int id,const wchar_t* type,const wchar_t* value,DWORD style) {
    auto child=CreateWindowExW(0,type,value,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),branding_module(),nullptr);
    if(!child)throw std::runtime_error("Cannot create introduction control");
    control_theme(child);return child;
  }
  void fonts() {
    const HFONT previous[]{face,heading,bold};face=font(window,14,FW_NORMAL,selected);heading=font(window,25,FW_SEMIBOLD,selected);bold=font(window,15,FW_SEMIBOLD,selected);
    for(auto child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(face),TRUE);
    SendMessageW(control(Title),WM_SETFONT,reinterpret_cast<WPARAM>(heading),TRUE);
    for(int index=0;index<3;++index)SendMessageW(control(RowTitle+index),WM_SETFONT,reinterpret_cast<WPARAM>(bold),TRUE);
    for(auto prior:previous)if(prior)DeleteObject(prior);
  }
  void layout() {
    RECT bounds{};GetClientRect(window,&bounds);auto d=[&](int value){return dip(window,value);};
    const int width=MulDiv(bounds.right,96,GetDpiForWindow(window)),height=MulDiv(bounds.bottom,96,GetDpiForWindow(window));
    auto place=[&](int id,int x,int y,int w,int h){MoveWindow(control(id),d(x),d(y),d(w),d(h),TRUE);};
    place(Progress,32,22,width-64,22);place(Title,32,54,width-64,42);place(Summary,32,104,width-64,48);
    place(LanguageLabel,56,185,width-112,26);place(Locale,56,220,width-112,160);place(LanguageHint,56,273,width-112,65);
    for(int index=0;index<3;++index){const int y=175+index*88;place(RowTitle+index,92,y,width-148,26);place(RowBody+index,92,y+29,width-148,55);}
    place(Documentation,92,367,width-148,32);place(Error,32,height-104,width-64,42);
    place(Skip,32,height-52,136,34);place(Previous,width-284,height-52,110,34);place(Next,width-160,height-52,128,34);
    InvalidateRect(window,nullptr,TRUE);
  }
  void update() {
    SetWindowTextW(window,first_run?L"Welcome to Xenon / 欢迎使用 Xenon":tr(L"Quick tour"));
    const auto& current=pages[page];set(Title,copy(current.title));set(Summary,copy(current.summary));
    const auto progress=std::wstring(copy({L"GET STARTED",L"开始使用"}))+L"   "+std::to_wstring(first_run?page+1:page)+L" / "+(first_run?L"4":L"3");set(Progress,progress.c_str());
    set(LanguageLabel,L"Choose your language / 选择语言");
    set(LanguageHint,copy({L"Your choice applies as soon as Xenon opens. You can change it later from the Menu.",L"Xenon 打开后即应用所选语言。稍后可在菜单中更改。"}));
    for(int id:{Locale,LanguageLabel,LanguageHint})ShowWindow(control(id),page==0?SW_SHOW:SW_HIDE);
    for(int index=0;index<3;++index){set(RowTitle+index,copy(current.rows[index].title));set(RowBody+index,copy(current.rows[index].body));
      for(int id:{RowTitle+index,RowBody+index})ShowWindow(control(id),page>0&&*copy(current.rows[index].title)?SW_SHOW:SW_HIDE);}
    set(Documentation,copy({L"GitHub documentation ↗",L"GitHub 使用文档 ↗"}));ShowWindow(control(Documentation),page==3?SW_SHOW:SW_HIDE);
    set(Next,copy(page==3?Copy{L"Start browsing",L"开始浏览"}:Copy{L"Next",L"下一步"}));
    set(Skip,copy(first_run?Copy{L"Skip tour",L"跳过导览"}:Copy{L"Close",L"关闭"}));set(Previous,copy({L"Previous",L"上一步"}));
    EnableWindow(control(Previous),page>(first_run?0:1));set(Error,L"");fonts();layout();
  }
  void build() {
    icons(window);
    for(int id:std::array<int,12>{Progress,Title,Summary,Error,LanguageLabel,LanguageHint,RowTitle,RowTitle+1,RowTitle+2,RowBody,RowBody+1,RowBody+2})add(id,L"STATIC",L"",SS_LEFT|SS_NOPREFIX);
    add(Locale,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
    SendMessageW(control(Locale),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"English"));SendMessageW(control(Locale),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"简体中文"));
    SendMessageW(control(Locale),CB_SETCURSEL,selected==Language::simplified_chinese?1:0,0);
    for(int id:{Documentation,Skip,Previous,Next})add(id,L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP);
    update();
  }
  void finish(IntroductionResult choice) {
    if(first_run&&!complete_introduction(selected)){
      set(Error,copy({L"Your preferences could not be saved. Try again, or close this window to leave setup for next time.",L"无法保存设置。请重试，或关闭此窗口，下次继续设置。"}));return;
    }
    result=choice;DestroyWindow(window);
  }
  void paint(HDC dc) {
    RECT bounds{};GetClientRect(window,&bounds);const auto colors=palette();fill(dc,bounds,colors.canvas);
    const auto d=[&](int v){return dip(window,v);};
    RECT card{d(32),d(160),bounds.right-d(32),d(447)};rounded(dc,card,colors.canvas,colors.border,d(12));
    if(page>0)for(int index=0;index<3;++index){const auto kind=pages[page].rows[index].icon;if(kind==Icon::none)continue;
      RECT mark{d(48),d(181+index*88),d(78),d(211+index*88)};icon(dc,mark,kind,colors.teal,window);}
  }
  static LRESULT CALLBACK proc(HWND h,UINT message,WPARAM wp,LPARAM lp) {
    try{return dispatch(h,message,wp,lp);}catch(...){return message==WM_NCCREATE?FALSE:message==WM_CREATE?-1:0;}
  }
  static LRESULT dispatch(HWND h,UINT message,WPARAM wp,LPARAM lp) {
    auto self=reinterpret_cast<Introduction*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Introduction*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(h,message,wp,lp);
    LRESULT color{};if(ctl_color(message,wp,lp,color))return color;
    switch(message){
      case WM_CREATE:self->build();return 0;
      case WM_SIZE:self->layout();return 0;
      case WM_COMMAND:
        if(LOWORD(wp)==Locale&&HIWORD(wp)==CBN_SELCHANGE){self->selected=SendMessageW(self->control(Locale),CB_GETCURSEL,0,0)==1?Language::simplified_chinese:Language::english;self->update();return 0;}
        if(HIWORD(wp)!=BN_CLICKED)return 0;
        if(LOWORD(wp)==Next){if(self->page==3)self->finish(IntroductionResult::start);else{++self->page;self->update();SetFocus(self->control(Next));}}
        else if(LOWORD(wp)==Previous&&self->page>(self->first_run?0:1)){--self->page;self->update();SetFocus(self->page==0?self->control(Locale):self->control(Next));}
        else if(LOWORD(wp)==Skip)self->finish(IntroductionResult::start);
        else if(LOWORD(wp)==Documentation&&self->page==3)self->finish(IntroductionResult::documentation);
        return 0;
      case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item&&item->CtlType==ODT_BUTTON){button(*item,self->face,Icon::none,item->CtlID==Next);return TRUE;}break;}
      case WM_DPICHANGED:{auto rect=reinterpret_cast<RECT*>(lp);self->fonts();SetWindowPos(h,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOACTIVATE|SWP_NOZORDER);self->layout();return 0;}
      case WM_GETMINMAXINFO:{auto size=reinterpret_cast<MINMAXINFO*>(lp);size->ptMinTrackSize={dip(h,720),dip(h,600)};return 0;}
      case WM_THEMECHANGED:case WM_SETTINGCHANGE:icons(h);for(auto child=GetWindow(h,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))control_theme(child);InvalidateRect(h,nullptr,TRUE);return 0;
      case WM_ERASEBKGND:return 1;
      case WM_PAINT:{PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);self->paint(dc);EndPaint(h,&paint);return 0;}
      case WM_CLOSE:DestroyWindow(h);return 0;
      case WM_DESTROY:for(auto face:{self->face,self->heading,self->bold})if(face)DeleteObject(face);self->face=self->heading=self->bold=nullptr;return 0;
      case WM_NCDESTROY:self->done=true;self->window=nullptr;return DefWindowProcW(h,message,wp,lp);
    }
    return DefWindowProcW(h,message,wp,lp);
  }
};
}
IntroductionResult show_introduction(HWND owner,bool first_run) {
  INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
  WNDCLASSW type{};type.lpfnWndProc=Introduction::proc;type.hInstance=branding_module();type.lpszClassName=L"XenonIntroduction";type.hCursor=LoadCursorW(nullptr,IDC_ARROW);
  if(!RegisterClassW(&type)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot register introduction window");
  Introduction state;state.owner=owner;state.first_run=first_run;state.page=first_run?0:1;state.selected=first_run?preferred_language:language;
  const auto dpi=owner?GetDpiForWindow(owner):GetDpiForSystem();auto d=[&](int v){return MulDiv(v,dpi,96);};
  RECT work{};MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTOPRIMARY),&monitor);work=monitor.rcWork;
  const int width=d(760),height=d(600),x=work.left+std::max<LONG>(0,(work.right-work.left-width)/2),y=work.top+std::max<LONG>(0,(work.bottom-work.top-height)/2);
  auto window=CreateWindowExW(WS_EX_CONTROLPARENT,L"XenonIntroduction",L"",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,x,y,width,height,owner,nullptr,branding_module(),&state);
  if(!window||state.done)throw std::runtime_error("Cannot create introduction window");
  const bool enabled=owner&&IsWindowEnabled(owner);if(enabled)EnableWindow(owner,FALSE);
  ShowWindow(window,SW_SHOWNORMAL);SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);
  SetForegroundWindow(window);SetFocus(state.control(first_run?Locale:Next));
  MSG message{};bool quit=false;int quit_code{};
  while(!state.done){const auto received=GetMessageW(&message,nullptr,0,0);if(received<=0){quit=received==0;quit_code=static_cast<int>(message.wParam);break;}
    if(message.message==WM_KEYDOWN&&GetAncestor(message.hwnd,GA_ROOT)==window){
      if(message.wParam==VK_ESCAPE){DestroyWindow(window);continue;}
      if(message.wParam==VK_RETURN&&message.hwnd!=state.control(Locale)){SendMessageW(window,WM_COMMAND,GetFocus()==state.control(Previous)?Previous:GetFocus()==state.control(Documentation)?Documentation:GetFocus()==state.control(Skip)?Skip:Next,0);continue;}
    }
    if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
  }
  if(!state.done)DestroyWindow(window);
  if(enabled&&IsWindow(owner)){EnableWindow(owner,TRUE);SetForegroundWindow(owner);}
  if(quit)PostQuitMessage(quit_code);
  return state.result;
}
}
