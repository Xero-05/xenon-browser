#include "xenon/native_ui.hpp"
#include "xenon/broker.hpp"
#include "xenon/cef_engine.hpp"
#include "xenon/vault.hpp"
#include "xenon/file_policy.hpp"
#include "xenon/dialog_notices.hpp"
#include "xenon/branding.hpp"
#include "xenon/updater.hpp"
#include "xenon/version.hpp"
#include "xenon/ui_theme.hpp"
#include <commctrl.h>
#include <windows.h>
#include <commdlg.h>
#include <wtsapi32.h>
#include <shobjidl.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace xenon {
namespace {
std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);std::wstring v(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),v.data(),n);return v;}
std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string v(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),v.data(),n,nullptr,nullptr);return v;}
std::string str(const Json& j,const char* key){auto i=j.find(key);return i!=j.end()&&i->is_string()?i->get<std::string>():"";}
std::string compact(const std::string& text,size_t limit){if(text.size()<=limit)return text;while(limit&&(static_cast<unsigned char>(text[limit])&0xc0)==0x80)--limit;return text.substr(0,limit)+"…";}
enum Id { Pairings=101,Approve,Deny,Clients,Workspaces,Share,Tabs,Take,Workers,Give,Stop,ResumeAuth,
          Origin,Username,Password,Label,Save,Import,Accounts,DeleteAccount,Grant,NewWorkspace,UploadGrant,Status,Revoke,PrivateWorkspace,RestoreSession,DialogAccept,DialogDismiss,DialogText,RemoveWorkspace,FillSavedAccount,CheckUpdates,PageClients,PageWorkspaces,PagePasswords,ConfigureClient,ConfigureWorkspace,ClientFiles,ClientAccountGrant,AccountRevoke,DialogMessage,SectionTitle,Brand };
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
  struct PromptLayout {SIZE original{};HFONT face{};std::vector<std::pair<HWND,RECT>> children;};
  std::map<HWND,PromptLayout> prompt_layouts;
  void remember_prompt(HWND target){PromptLayout layout;RECT parent{};GetClientRect(target,&parent);layout.original={parent.right,parent.bottom};layout.face=ui::font(target);
    for(auto child=GetWindow(target,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){RECT rect{};GetWindowRect(child,&rect);MapWindowPoints(nullptr,target,reinterpret_cast<POINT*>(&rect),2);layout.children.emplace_back(child,rect);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(layout.face),TRUE);ui::control_theme(child);}prompt_layouts[target]=std::move(layout);}
  void resize_prompt(HWND target){auto found=prompt_layouts.find(target);if(found==prompt_layouts.end())return;RECT parent{};GetClientRect(target,&parent);const auto& layout=found->second;
    for(const auto& [child,r]:layout.children)MoveWindow(child,MulDiv(r.left,parent.right,layout.original.cx),MulDiv(r.top,parent.bottom,layout.original.cy),MulDiv(r.right-r.left,parent.right,layout.original.cx),MulDiv(r.bottom-r.top,parent.bottom,layout.original.cy),TRUE);}
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
  enum class UpdatePhase { idle,checking,current,available,downloading,ready,launching,launched,canceled,failed };
  struct UpdateState {
    std::mutex mutex;std::atomic_bool cancel=false;bool alive=true;
    UpdatePhase phase=UpdatePhase::idle;std::optional<updates::Release> release;
    std::filesystem::path installer;std::string message=ui::tr8("Check for a newer Xenon release when you are ready.");
    std::shared_ptr<updates::InstallerLaunch> prepared;
    uint64_t downloaded{},total{},revision{};
  };
  std::shared_ptr<UpdateState> update_state=std::make_shared<UpdateState>();
  std::function<void(std::shared_ptr<updates::InstallerLaunch>)> update_install_callback;
  int page{},building_page{-1};
  std::optional<ui::Palette> applied_palette;
  struct Placement {HWND control{};RECT bounds{};int page{-1};};
  std::vector<Placement> placements;
  struct Configuration {Impl* owner{};HWND window{},resources{},clients{};HFONT face{};bool workspace{},creating{};std::string id,client;Json client_rows=Json::array(),resource_rows=Json::array();};
  std::vector<std::unique_ptr<Configuration>> configurations;
  static HWND config_control(Configuration& config,int id){return GetDlgItem(config.window,id);}
  static bool checked(Configuration& config,int id){return SendMessageW(config_control(config,id),BM_GETCHECK,0,0)==BST_CHECKED;}
  static void check(Configuration& config,int id,bool value){SendMessageW(config_control(config,id),BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);}
  void config_resources(Configuration& config){
    config.resource_rows=Json::array();ListView_DeleteAllItems(config.resources);
    const auto scope=config.workspace?config.id:client_file_scope(config.client);
    std::vector<std::string> scopes{scope};if(config.workspace&&!config.client.empty())scopes.push_back(client_file_scope(config.client));
    std::set<std::string> added;
    for(const auto& resource_scope:scopes)for(const auto& [command,key,id]:std::vector<std::tuple<bool,const char*,const char*>>{{true,"folders","folderId"},{false,"files","fileId"}}){
      auto result=command?files.list_folders(resource_scope):files.list_files(resource_scope);
      if(result.value("ok",false))for(auto item:result["result"][key])if(added.insert(str(item,id)).second){item["resourceId"]=str(item,id);item["kind"]=command?"Folder":"File";item["scope"]=resource_scope;config.resource_rows.push_back(std::move(item));}
    }
    const auto state=broker.state();const auto accounts=vault.list_accounts();
    if(accounts.value("ok",false))for(auto account:accounts["result"]["accounts"]){bool granted=false;
      for(const auto& grant:state["accountGrants"])if(str(grant,"clientId")==config.client&&str(grant,"accountId")==str(account,"accountId")&&(!config.workspace||str(grant,"workspaceId").empty()||str(grant,"workspaceId")==config.id))granted=true;
      if(config.workspace&&!granted)continue;account["resourceId"]=str(account,"accountId");account["name"]=str(account,"label")+" · "+str(account,"origin");account["kind"]="Account";account["granted"]=granted;config.resource_rows.push_back(std::move(account));
    }
    WorkspaceAccess access;for(const auto& workspace:state["workspaces"])if(str(workspace,"workspaceId")==config.id)if(workspace["access"].contains(config.client))access=WorkspaceAccess::read(workspace["access"][config.client]);
    int index=0;for(const auto& item:config.resource_rows){auto title=wide(str(item,"name"));LVITEMW row{};row.mask=LVIF_TEXT;row.iItem=index;row.pszText=title.data();ListView_InsertItem(config.resources,&row);auto kind=wide(ui::tr8(str(item,"kind").c_str()));ListView_SetItemText(config.resources,index,1,kind.data());
      const auto id=str(item,"resourceId");const bool account=str(item,"kind")=="Account";
      ListView_SetCheckState(config.resources,index,config.workspace?(account?(!access.restrict_accounts||access.account_ids.contains(id)):(!access.restrict_files||access.file_ids.contains(id))):item.value("granted",false));++index;
    }
  }
  void load_configuration(Configuration& config){
    const auto state=broker.state();Json policy=Json::object();bool allowed=false;
    for(int id=20;id<=28;++id)EnableWindow(config_control(config,id),!config.workspace||!config.client.empty());
    if(config.workspace){for(const auto& workspace:state["workspaces"])if(str(workspace,"workspaceId")==config.id){
      if(GetFocus()!=config_control(config,10))SetWindowTextW(config_control(config,10),wide(str(workspace,"displayName")).c_str());
      if(workspace["access"].contains(config.client)){policy=workspace["access"][config.client];allowed=true;}
    }}else for(const auto& client:state["clients"])if(str(client,"clientId")==config.client)policy=client["policy"];
    check(config,20,policy.value("interaction",false));check(config,21,policy.value("uploads",false));check(config,22,policy.value("downloads",false));check(config,23,policy.value("savedAccounts",false));
    check(config,24,config.workspace?allowed:policy.value("automaticWorkspaces",false));check(config,25,policy.value("inheritFiles",false));check(config,26,policy.value("inheritAccounts",false));
    check(config,27,policy.value("restrictFiles",false));check(config,28,policy.value("restrictAccounts",false));
    SetWindowTextW(config_control(config,30),std::to_wstring(policy.value("maxWorkers",4)).c_str());SetWindowTextW(config_control(config,31),std::to_wstring(policy.value("maxAutomaticWorkspaces",4)).c_str());config_resources(config);
  }
  void save_configuration(Configuration& config){
    try{
      const auto name=ui::text(config_control(config,10));
      if(config.creating){if(!valid_workspace_name(name))throw std::runtime_error("Enter a workspace name with at most 80 characters and no control characters.");
        broker.open_human_workspace("about:blank",[this](Json value){result(value);refresh();},false,name);DestroyWindow(config.window);return;}
      Capabilities capabilities{checked(config,20),checked(config,21),checked(config,22),checked(config,23)};
      bool saved=false;
      if(config.workspace){saved=broker.rename_workspace(config.id,name);if(!config.client.empty()){
          if(!checked(config,24))saved=broker.configure_workspace_client(config.id,config.client,std::nullopt)&&saved;
          else {WorkspaceAccess access;access.capabilities=capabilities;access.inherit_files=checked(config,25);access.inherit_accounts=checked(config,26);access.restrict_files=checked(config,27);access.restrict_accounts=checked(config,28);
            for(size_t n=0;n<config.resource_rows.size();++n)if(ListView_GetCheckState(config.resources,n)){auto item=config.resource_rows[n];(str(item,"kind")=="Account"?access.account_ids:access.file_ids).insert(str(item,"resourceId"));}
            saved=broker.configure_workspace_client(config.id,config.client,access)&&saved;}
        }}else {const auto workers=ui::text(config_control(config,30)),workspaces=ui::text(config_control(config,31));
          if(workers.empty()||workspaces.empty()||!std::all_of(workers.begin(),workers.end(),::isdigit)||!std::all_of(workspaces.begin(),workspaces.end(),::isdigit))throw std::runtime_error("Enter numeric quotas.");
          ClientPolicy policy{capabilities,checked(config,24),std::stoul(workers),std::stoul(workspaces)};saved=broker.configure_client(config.client,policy);
          for(size_t n=0;n<config.resource_rows.size();++n){const auto& item=config.resource_rows[n];if(str(item,"kind")!="Account")continue;const auto account=str(item,"accountId");
            if(ListView_GetCheckState(config.resources,n))saved=broker.grant_client_account(config.client,account,str(item,"origin"))&&saved;
            else saved=broker.revoke_account(config.client,"",account)&&saved;
          }
        }
      SetWindowTextW(config_control(config,40),saved?ui::tr(L"Saved. Effective permissions also depend on the client's overall policy."):ui::tr(L"Could not save all changes. Review permissions and retry."));refresh();
    }catch(const std::exception& error){SetWindowTextW(config_control(config,40),ui::error_text(error.what()).c_str());}
  }
  static LRESULT CALLBACK config_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    try { return dispatch_configuration(window,message,wp,lp); }
    catch(...) { return message==WM_NCCREATE?FALSE:message==WM_CREATE?-1:0; }
  }
  static LRESULT dispatch_configuration(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto config=reinterpret_cast<Configuration*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){config=static_cast<Configuration*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);config->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(config));}
    if(!config)return DefWindowProcW(window,message,wp,lp);auto owner=config->owner;LRESULT themed{};if(owner->theme_message(window,message,wp,lp,themed))return themed;
    if(message==WM_COMMAND){const int id=LOWORD(wp);
      if(id==1)owner->save_configuration(*config);
      else if(id==2)DestroyWindow(window);
      else if(id==11&&HIWORD(wp)==LBN_SELCHANGE){const auto index=SendMessageW(config->clients,LB_GETCURSEL,0,0);config->client=index>=0&&static_cast<size_t>(index)<config->client_rows.size()?str(config->client_rows[index],"clientId"):"";owner->load_configuration(*config);}
      else if(id==3||id==4){try{auto path=id==3?owner->pick(false):owner->pick_folder();if(!path.empty()){auto scope=config->workspace?config->id:client_file_scope(config->client);owner->result(id==3?owner->files.grant_upload(scope,path):owner->files.grant_folder(scope,path));owner->broker.resource_changed(config->workspace?"":config->client);owner->config_resources(*config);}}catch(...){SetWindowTextW(config_control(*config,40),ui::tr(L"This resource could not be granted."));}}
      else if(id==5){const auto index=ListView_GetNextItem(config->resources,-1,LVNI_SELECTED);if(index>=0&&static_cast<size_t>(index)<config->resource_rows.size()){const auto item=config->resource_rows[index];if(str(item,"kind")=="Account")owner->broker.revoke_account(config->client,config->workspace?config->id:"",str(item,"accountId"));else owner->files.revoke_grant(str(item,"scope"),str(item,"resourceId"));owner->broker.resource_changed(config->workspace?"":config->client);owner->config_resources(*config);}}
      return 0;
    }
    if(message==WM_SIZE){RECT rect{};GetClientRect(window,&rect);const int d=GetDpiForWindow(window),width=MulDiv(rect.right,96,d),height=MulDiv(rect.bottom,96,d);auto move=[&](int id,int x,int y,int w,int h){MoveWindow(config_control(*config,id),ui::dip(window,x),ui::dip(window,y),ui::dip(window,w),ui::dip(window,h),TRUE);};
      move(10,20,44,width-40,30);if(config->creating){move(40,20,100,width-40,height-170);move(1,20,height-52,160,32);move(2,width-140,height-52,120,32);return 0;}
      move(11,20,112,220,height-190);move(12,20,86,220,22);
      const int x=config->workspace?260:20,w=width-x-20;
      if(!config->workspace){ShowWindow(config_control(*config,11),SW_HIDE);ShowWindow(config_control(*config,12),SW_HIDE);}
      for(int n=0;n<4;++n)move(20+n,x,98+n*30,w,26);
      move(24,x,226,w,26);move(25,x,260,w/2,26);move(26,x+w/2,260,w/2,26);move(27,x,292,w/2,26);move(28,x+w/2,292,w/2,26);
      move(32,x,265,145,24);move(30,x+150,261,70,28);move(33,x+240,265,180,24);move(31,x+425,261,70,28);
      move(34,x,328,w,25);move(13,x,357,w,std::max(80,height-505));move(3,x,height-140,130,30);move(4,x+138,height-140,130,30);move(5,x+276,height-140,150,30);
      move(40,x,height-100,w,40);move(1,x,height-52,160,32);move(2,width-140,height-52,120,32);return 0;
    }
    if(message==WM_GETMINMAXINFO){auto value=reinterpret_cast<MINMAXINFO*>(lp);value->ptMinTrackSize={ui::dip(window,config->creating?440:840),ui::dip(window,config->creating?250:660)};return 0;}
    if(message==WM_DPICHANGED){auto rect=reinterpret_cast<RECT*>(lp);auto prior=config->face;config->face=ui::font(window);
      for(auto child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(config->face),TRUE);ui::control_theme(child);}
      DeleteObject(prior);SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}if(message==WM_NCDESTROY){config->window=nullptr;DeleteObject(config->face);config->face=nullptr;return DefWindowProcW(window,message,wp,lp);}
    return DefWindowProcW(window,message,wp,lp);
  }
  void show_configuration(bool workspace,bool creating=false){
    const auto chosen=selected(workspace?Workspaces:Clients);const auto id=str(chosen,workspace?"workspaceId":"clientId");if(!creating&&id.empty()){status(ui::tr(L"Select an item to configure."));return;}
    for(auto& existing:configurations)if(existing->window&&existing->id==id&&existing->workspace==workspace&&existing->creating==creating){ShowWindow(existing->window,SW_SHOWNORMAL);SetForegroundWindow(existing->window);return;}
    auto config=std::make_unique<Configuration>();config->owner=this;config->id=id;config->workspace=workspace;config->creating=creating;config->client=workspace?str(selected(Clients),"clientId"):id;
    WNDCLASSW wc{};wc.lpfnWndProc=config_proc;wc.hInstance=branding_module();wc.lpszClassName=L"XenonConfiguration";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    auto handle=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,creating?ui::tr(L"Create workspace"):workspace?ui::tr(L"Configure workspace"):ui::tr(L"Configure client"),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,ui::dip(window,creating?600:900),ui::dip(window,creating?290:740),nullptr,nullptr,wc.hInstance,config.get());if(!handle)return;config->face=ui::font(handle);ui::icons(handle);
    auto make=[&](int id,const wchar_t* kind,const wchar_t* name,DWORD style=0){auto c=CreateWindowExW(0,kind,name,WS_CHILD|WS_VISIBLE|ui::styles(kind,style),0,0,10,10,handle,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),wc.hInstance,nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(config->face),TRUE);ui::control_theme(c);return c;};
    make(9,L"STATIC",creating||workspace?ui::tr(L"Workspace name"):ui::tr(L"Client"),0);MoveWindow(config_control(*config,9),ui::dip(handle,20),ui::dip(handle,16),ui::dip(handle,760),ui::dip(handle,24),TRUE);
    make(10,L"EDIT",creating?ui::tr(L"Workspace"):wide(str(chosen,workspace?"displayName":"name")).c_str(),WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL|(workspace?0:ES_READONLY));
    make(40,L"STATIC",creating?ui::tr(L"A blank tab will open with human ownership."):ui::tr(L"Changes take effect when you choose Save. Unchecked capabilities are read-only."));make(1,L"BUTTON",creating?ui::tr(L"Create workspace"):ui::tr(L"Save"),BS_OWNERDRAW|WS_TABSTOP);make(2,L"BUTTON",ui::tr(L"Close"),BS_OWNERDRAW|WS_TABSTOP);
    if(!creating){config->clients=make(11,L"LISTBOX",L"",WS_BORDER|WS_VSCROLL|WS_TABSTOP|LBS_NOTIFY);make(12,L"STATIC",ui::tr(L"Paired clients"));
      const wchar_t* caps[]{ui::tr(L"Interact with pages and create tabs"),ui::tr(L"Upload approved files"),ui::tr(L"Download files"),ui::tr(L"Use permitted saved accounts")};for(int n=0;n<4;++n)make(20+n,L"BUTTON",caps[n],BS_AUTOCHECKBOX|WS_TABSTOP);
      make(24,L"BUTTON",workspace?ui::tr(L"Allow this client in this workspace"):ui::tr(L"Allow automatic workspace creation"),BS_AUTOCHECKBOX|WS_TABSTOP);
      make(25,L"BUTTON",ui::tr(L"Inherit client files"),BS_AUTOCHECKBOX|WS_TABSTOP);make(26,L"BUTTON",ui::tr(L"Inherit client accounts"),BS_AUTOCHECKBOX|WS_TABSTOP);make(27,L"BUTTON",ui::tr(L"Restrict files to checked items"),BS_AUTOCHECKBOX|WS_TABSTOP);make(28,L"BUTTON",ui::tr(L"Restrict accounts to checked items"),BS_AUTOCHECKBOX|WS_TABSTOP);
      make(32,L"STATIC",ui::tr(L"Concurrent workers"));make(30,L"EDIT",L"4",WS_BORDER|WS_TABSTOP|ES_NUMBER);make(33,L"STATIC",ui::tr(L"Automatic workspaces"));make(31,L"EDIT",L"4",WS_BORDER|WS_TABSTOP|ES_NUMBER);
      for(int n:{25,26,27,28})ShowWindow(config_control(*config,n),workspace?SW_SHOW:SW_HIDE);for(int n:{30,31,32,33})ShowWindow(config_control(*config,n),workspace?SW_HIDE:SW_SHOW);
      make(34,L"STATIC",workspace?ui::tr(L"Available resources · checked items form restrictions when enabled"):ui::tr(L"Resources · check saved accounts to grant them to this client"));
      config->resources=make(13,WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|WS_TABSTOP|WS_BORDER);ListView_SetExtendedListViewStyle(config->resources,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=ui::dip(handle,430);column.pszText=const_cast<LPWSTR>(ui::tr(L"Name / HTTPS origin"));ListView_InsertColumn(config->resources,0,&column);column.cx=ui::dip(handle,90);column.pszText=const_cast<LPWSTR>(ui::tr(L"Type"));ListView_InsertColumn(config->resources,1,&column);
      make(3,L"BUTTON",ui::tr(L"Grant file"),BS_OWNERDRAW|WS_TABSTOP);make(4,L"BUTTON",ui::tr(L"Grant folder"),BS_OWNERDRAW|WS_TABSTOP);make(5,L"BUTTON",ui::tr(L"Revoke selected"),BS_OWNERDRAW|WS_TABSTOP);
      config->client_rows=broker.state()["clients"];int match=0,index=0;for(const auto& client:config->client_rows){auto name=wide(str(client,"name"));SendMessageW(config->clients,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(str(client,"clientId")==config->client)match=index;++index;}SendMessageW(config->clients,LB_SETCURSEL,match,0);if(config->client.empty()&&!config->client_rows.empty())config->client=str(config->client_rows[match],"clientId");load_configuration(*config);
    }
    SendMessageW(handle,WM_SIZE,0,0);ShowWindow(handle,SW_SHOWNORMAL);SetForegroundWindow(handle);configurations.push_back(std::move(config));
  }
  HWND update_window{},update_latest{},update_message{},update_progress{};
  HWND update_check{},update_download{},update_install{},update_cancel{};
  uint64_t update_shown_revision=~uint64_t{};int update_percent=-1;
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
    ui::frame(target);
    SendMessageW(target,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(branding_icon(GetSystemMetrics(SM_CXSMICON))));
    SendMessageW(target,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(branding_icon(GetSystemMetrics(SM_CXICON))));
  }
  RECT content_bounds() const {RECT bounds{};GetClientRect(window,&bounds);return {ui::dip(window,208),ui::dip(window,20),bounds.right-ui::dip(window,20),bounds.bottom-ui::dip(window,20)};}
  void paint_surface(HWND target,HDC dc){RECT bounds{};GetClientRect(target,&bounds);const auto colors=ui::palette();ui::fill(dc,bounds,colors.canvas);
    if(target==window&&font){auto panel=content_bounds();ui::rounded(dc,panel,colors.canvas,colors.gray,ui::dip(window,10));RECT nav{};GetWindowRect(controls.at(PageClients+page),&nav);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&nav),2);
      RECT bridge{nav.right,nav.top+1,panel.left+ui::dip(window,1),nav.bottom-1};ui::fill(dc,bridge,GetPropW(controls.at(PageClients+page),L"XenonHover")?ui::hover_background():colors.canvas);ui::fill(dc,{bridge.left,bridge.top,bridge.right,bridge.top+ui::dip(window,1)},colors.gray);ui::fill(dc,{bridge.left,bridge.bottom-ui::dip(window,1),bridge.right,bridge.bottom},colors.gray);}
    if(target==update_window&&update_percent>=0){RECT track{26,236,bounds.right-26,244};ui::fill(dc,track,ui::palette().border);track.right=track.left+(track.right-track.left)*std::clamp(update_percent,0,100)/100;ui::fill(dc,track,ui::palette().ink);}}
  void draw_button(const DRAWITEMSTRUCT& item){auto face=reinterpret_cast<HFONT>(SendMessageW(item.hwndItem,WM_GETFONT,0,0));const bool active=GetParent(item.hwndItem)==window&&static_cast<int>(item.CtlID)==PageClients+page;
    if(!active){ui::button(item,face?face:font);return;}auto rect=item.rcItem;const auto colors=ui::palette();ui::fill(item.hDC,rect,colors.canvas);InflateRect(&rect,-1,-1);auto shape=rect;shape.right+=ui::dip(window,16);const bool hover=ui::hovered(item);ui::rounded(item.hDC,shape,hover?ui::hover_background():colors.canvas,colors.gray,ui::dip(window,8));wchar_t caption[128]{};GetWindowTextW(item.hwndItem,caption,128);rect.left+=ui::dip(window,16);ui::text(item.hDC,rect,caption,heading_font,hover&&colors.contrast?ui::selection_ink():colors.ink);if(item.itemState&ODS_FOCUS){InflateRect(&rect,-3,-3);ui::focus_mark(item.hDC,rect,item.hwndItem);}}
  bool theme_message(HWND target,UINT message,WPARAM wp,LPARAM lp,LRESULT& result){
    if(message==WM_NCDESTROY){if(auto found=prompt_layouts.find(target);found!=prompt_layouts.end()){DeleteObject(found->second.face);prompt_layouts.erase(found);}return false;}
    if(target!=window&&message==WM_SIZE&&prompt_layouts.contains(target)){resize_prompt(target);result=0;return true;}
    if(target!=window&&message==WM_DPICHANGED&&prompt_layouts.contains(target)){auto rect=reinterpret_cast<RECT*>(lp);SetWindowPos(target,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);auto& layout=prompt_layouts.at(target);auto prior=layout.face;layout.face=ui::font(target);for(const auto& [child,bounds]:layout.children)SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(layout.face),TRUE);DeleteObject(prior);resize_prompt(target);result=0;return true;}
    if(target!=window&&message==WM_GETMINMAXINFO){auto found=prompt_layouts.find(target);if(found!=prompt_layouts.end()){auto value=reinterpret_cast<MINMAXINFO*>(lp);value->ptMinTrackSize={found->second.original.cx+16,found->second.original.cy+40};result=0;return true;}}
    if(ui::list_draw(message,wp,lp,target,result))return true;
    if(message==WM_ERASEBKGND){result=1;return true;}
    if(message==WM_PAINT){PAINTSTRUCT paint{};auto dc=BeginPaint(target,&paint);paint_surface(target,dc);EndPaint(target,&paint);result=0;return true;}
    if(message==WM_DRAWITEM){const auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item&&item->CtlType==ODT_BUTTON){draw_button(*item);result=TRUE;return true;}}
    return ui::ctl_color(message,wp,lp,result);
  }
  static bool update_busy(UpdatePhase phase){return phase==UpdatePhase::checking||phase==UpdatePhase::downloading||phase==UpdatePhase::launching;}
  static void pin_update_module(){
    // Background transfers may outlive the native window. Keep their code
    // loaded until process exit without joining network work on the UI thread.
    HMODULE module{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
      reinterpret_cast<LPCWSTR>(&pin_update_module),&module))throw std::runtime_error("Updates could not start.");
  }
  void update_worker(std::function<void(const std::shared_ptr<UpdateState>&)> work){
    const auto state=update_state;
    try{
      pin_update_module();
      std::thread([state,work=std::move(work)]{
        try{work(state);}
        catch(const std::exception& error){
          std::lock_guard lock(state->mutex);if(!state->alive)return;
          state->phase=state->cancel?UpdatePhase::canceled:UpdatePhase::failed;
          state->message=state->cancel?ui::tr8("Canceled. No update was installed."):ui::utf8(ui::error_text(compact(error.what(),300)));++state->revision;
        }catch(...){
          std::lock_guard lock(state->mutex);if(!state->alive)return;
          state->phase=UpdatePhase::failed;state->message=ui::tr8("The update operation could not be completed.");++state->revision;
        }
      }).detach();
    }catch(...){
      std::lock_guard lock(state->mutex);state->phase=UpdatePhase::failed;state->message=ui::tr8("Updates could not start. Try again later.");++state->revision;
    }
  }
  void begin_update_check(){
    {
      std::lock_guard lock(update_state->mutex);if(update_busy(update_state->phase))return;
      update_state->cancel=false;update_state->phase=UpdatePhase::checking;update_state->release.reset();update_state->installer.clear();
      update_state->downloaded=0;update_state->total=0;update_state->message=ui::tr8("Checking for a newer release…");++update_state->revision;
    }
    update_worker([current=std::string(kVersion)](const auto& state){
      auto release=updates::check_for_update(current);
      std::lock_guard lock(state->mutex);if(!state->alive)return;
      if(state->cancel){state->phase=UpdatePhase::canceled;state->message=ui::tr8("Update check canceled.");}
      else if(release){state->release=std::move(release);state->phase=UpdatePhase::available;state->message=ui::tr8("A newer Xenon release is available.");}
      else{state->phase=UpdatePhase::current;state->message=ui::tr8("No newer release is available for this version.");}
      ++state->revision;
    });
    poll_updates();
  }
  void begin_update_download(){
    std::optional<updates::Release> release;
    {
      std::lock_guard lock(update_state->mutex);
      if(update_busy(update_state->phase)||!update_state->release)return;
      release=update_state->release;update_state->cancel=false;update_state->phase=UpdatePhase::downloading;
      update_state->downloaded=0;update_state->total=release->bytes;update_state->message=ui::tr8("Downloading the update installer…");++update_state->revision;
    }
    update_worker([release=std::move(*release)](const auto& state){
      auto path=updates::download_installer(release,[state](uint64_t bytes,uint64_t total){
        std::lock_guard lock(state->mutex);if(!state->alive||state->cancel)return;
        state->downloaded=bytes;state->total=total;++state->revision;
      },state->cancel);
      std::lock_guard lock(state->mutex);if(!state->alive)return;
      if(state->cancel){state->phase=UpdatePhase::canceled;state->message=ui::tr8("Download canceled. No update was installed.");}
      else{state->installer=std::move(path);state->downloaded=release.bytes;state->total=release.bytes;state->phase=UpdatePhase::ready;state->message=ui::tr8("Installer downloaded and verified. Choose Install update when ready.");}
      ++state->revision;
    });
    poll_updates();
  }
  void begin_update_install(){
    std::optional<updates::Release> release;std::filesystem::path installer;
    {
      std::lock_guard lock(update_state->mutex);if(update_state->phase!=UpdatePhase::ready||!update_state->release)return;
      release=update_state->release;installer=update_state->installer;
    }
    if(!update_install_callback){status(ui::tr(L"Update installation is unavailable in this window."));return;}
    if(MessageBoxW(update_window,ui::tr(L"Install the update and exit Xenon?\n\nSave unfinished website work first. Xenon will close its tabs normally and disconnect agents, then open the verified unsigned setup program. Other Xenon instances must also be closed before installation.\n\nSetup upgrades your installed copy in its existing folder. Your saved workspaces and accounts are retained. When setup finishes, open Xenon from the Start menu."),
      ui::tr(L"Install Xenon update"),MB_YESNO|MB_ICONINFORMATION|MB_DEFBUTTON2)!=IDYES)return;
    {
      std::lock_guard lock(update_state->mutex);if(update_state->phase!=UpdatePhase::ready)return;
      update_state->cancel=false;update_state->phase=UpdatePhase::launching;update_state->message=ui::tr8("Rechecking the installer before exiting Xenon…");++update_state->revision;
    }
    update_worker([release=std::move(*release),installer=std::move(installer)](const auto& state){
      if(state->cancel)return;
      auto prepared=updates::prepare_installer(installer,release);
      std::lock_guard lock(state->mutex);if(!state->alive)return;
      state->prepared=std::move(prepared);state->message=ui::tr8("Installer verified. Closing Xenon before starting setup…");++state->revision;
    });
    poll_updates();
  }
  void cancel_update(){
    std::lock_guard lock(update_state->mutex);
    if(update_state->phase==UpdatePhase::checking||update_state->phase==UpdatePhase::downloading){
      update_state->cancel=true;update_state->message=ui::tr8("Canceling the current update operation…");++update_state->revision;
    }
  }
  void close_updates(){
    cancel_update();auto target=std::exchange(update_window,nullptr);
    update_latest=nullptr;update_message=nullptr;update_progress=nullptr;update_check=nullptr;update_download=nullptr;update_install=nullptr;update_cancel=nullptr;
    update_shown_revision=~uint64_t{};update_percent=-1;if(target&&IsWindow(target))DestroyWindow(target);
  }
  void poll_updates(){
    std::shared_ptr<updates::InstallerLaunch> prepared;
    {std::lock_guard lock(update_state->mutex);prepared=std::move(update_state->prepared);}
    if(prepared&&update_install_callback){update_install_callback(std::move(prepared));return;}
    if(!update_window)return;
    UpdatePhase phase;std::optional<updates::Release> release;std::string message;uint64_t downloaded{},total{};bool canceling{};
    {
      std::lock_guard lock(update_state->mutex);if(update_shown_revision==update_state->revision)return;
      update_shown_revision=update_state->revision;phase=update_state->phase;release=update_state->release;message=update_state->message;
      downloaded=update_state->downloaded;total=update_state->total;canceling=update_state->cancel;
    }
    const auto latest=release?wide(ui::tr8("Available version: ")+release->version):ui::tr(L"Available version: —");
    SetWindowTextW(update_latest,latest.c_str());SetWindowTextW(update_message,wide(message).c_str());
    std::wstring progress;
    update_percent=-1;
    if(phase==UpdatePhase::downloading||phase==UpdatePhase::ready||phase==UpdatePhase::launching||phase==UpdatePhase::launched){
      const auto decimal_mib=[](uint64_t bytes){return std::to_wstring(bytes/(1024*1024))+L"."+std::to_wstring((bytes%(1024*1024))*10/(1024*1024));};
      progress=ui::tr(L"Downloaded ")+decimal_mib(downloaded)+L" MiB";
      if(total){update_percent=static_cast<int>(std::min(100.0,100.0*static_cast<double>(downloaded)/static_cast<double>(total)));progress+=ui::tr(L" of ")+decimal_mib(total)+L" MiB ("+std::to_wstring(update_percent)+L"%)";}
    }
    SetWindowTextW(update_progress,progress.c_str());
    const bool busy=update_busy(phase);
    EnableWindow(update_check,!busy);EnableWindow(update_download,!busy&&release.has_value()&&phase!=UpdatePhase::ready&&phase!=UpdatePhase::launched);
    EnableWindow(update_install,phase==UpdatePhase::ready);EnableWindow(update_cancel,phase!=UpdatePhase::launching&&(!busy||!canceling));
    SetWindowTextW(update_cancel,busy?ui::tr(L"Cancel"):ui::tr(L"Close"));
    EnableMenuItem(GetSystemMenu(update_window,FALSE),SC_CLOSE,MF_BYCOMMAND|(phase==UpdatePhase::launching?MF_GRAYED:MF_ENABLED));
    RECT bar{26,236,614,244};InvalidateRect(update_window,&bar,TRUE);
  }
  void update_command(int id){
    if(id==1)begin_update_check();else if(id==2)begin_update_download();else if(id==3)begin_update_install();
    else if(id==4){bool busy;{std::lock_guard lock(update_state->mutex);busy=update_busy(update_state->phase);}if(busy){cancel_update();poll_updates();}else close_updates();}
  }
  static LRESULT CALLBACK update_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){
    auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(h,message,wp,lp);
    LRESULT painted{};if(self->theme_message(h,message,wp,lp,painted))return painted;
    if(message==WM_COMMAND&&HIWORD(wp)==BN_CLICKED){self->update_command(LOWORD(wp));return 0;}
    if(message==WM_CLOSE||(message==WM_KEYDOWN&&wp==VK_ESCAPE)){
      bool launching;{std::lock_guard lock(self->update_state->mutex);launching=self->update_state->phase==UpdatePhase::launching;}
      if(!launching)self->close_updates();return 0;
    }
    if(message==WM_NCDESTROY){SetWindowLongPtrW(h,GWLP_USERDATA,0);return DefWindowProcW(h,message,wp,lp);}
    return DefWindowProcW(h,message,wp,lp);
  }
  void show_updates(){
    if(!update_window){
      WNDCLASSW wc{};wc.lpfnWndProc=update_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"XenonUpdates";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
      constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MAXIMIZEBOX|WS_CLIPCHILDREN;
      RECT bounds{0,0,640,450};AdjustWindowRectEx(&bounds,style,FALSE,0);
      RECT owner{};GetWindowRect(window,&owner);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
      const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
      const int x=std::clamp(owner.left+110,monitor.rcWork.left,std::max(monitor.rcWork.left,monitor.rcWork.right-width));
      const int y=std::clamp(owner.top+70,monitor.rcWork.top,std::max(monitor.rcWork.top,monitor.rcWork.bottom-height));
      update_window=CreateWindowExW(0,wc.lpszClassName,ui::tr(L"Xenon Updates"),style,x,y,width,height,window,nullptr,wc.hInstance,this);
      if(!update_window){status(ui::tr(L"The updates window could not open."));return;}
      window_icon(update_window);
      auto add_update=[&](int id,const wchar_t* type,const wchar_t* caption,int x,int y,int width,int height,DWORD extra=0){
        auto control=CreateWindowExW(0,type,caption,WS_CHILD|WS_VISIBLE|ui::styles(type,extra),x,y,width,height,update_window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),wc.hInstance,nullptr);
        SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);if(std::wstring(type)==L"BUTTON")button_style(control,id==3?ButtonTone::primary:ButtonTone::normal);return control;
      };
      text_style(add_update(0,L"STATIC",ui::tr(L"Xenon  /  Updates"),60,16,252,27),TextTone::brand,heading_font);
      text_style(add_update(0,L"STATIC",ui::tr(L"Automatic checks are off"),327,21,286,20,SS_RIGHT),TextTone::brand_muted,small_font);
      text_style(add_update(0,L"STATIC",wide(ui::tr8("Current version: ")+std::string(kVersion)).c_str(),26,84,588,24),TextTone::heading,heading_font);
      update_latest=add_update(0,L"STATIC",ui::tr(L"Available version: —"),26,117,588,22);
      update_message=add_update(0,L"STATIC",L"",26,155,588,46);
      update_progress=add_update(0,L"STATIC",L"",26,211,588,22);text_style(update_progress,TextTone::muted,small_font);
      text_style(add_update(0,L"STATIC",ui::tr(L"Unsigned alpha software. Downloading does not install it. Install and exit asks for confirmation, then closes Xenon before opening setup."),26,267,588,42),TextTone::muted,small_font);
      text_style(add_update(0,L"STATIC",ui::tr(L"Setup upgrades or installs Xenon for your Windows account. Portable users should launch the installed copy from the Start menu afterward."),26,319,588,42),TextTone::muted,small_font);
      update_check=add_update(1,L"BUTTON",ui::tr(L"Check again"),26,395,124,34,WS_TABSTOP);
      update_download=add_update(2,L"BUTTON",ui::tr(L"Download update"),162,395,152,34,WS_TABSTOP);
      update_install=add_update(3,L"BUTTON",ui::tr(L"Install and exit"),326,395,144,34,WS_TABSTOP);
      update_cancel=add_update(4,L"BUTTON",ui::tr(L"Close"),482,395,132,34,WS_TABSTOP);
      update_shown_revision=~uint64_t{};
    }
    poll_updates();ShowWindow(update_window,SW_SHOWNORMAL);SetForegroundWindow(update_window);
    bool first;{std::lock_guard lock(update_state->mutex);first=update_state->phase==UpdatePhase::idle;}
    if(first)begin_update_check();
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
    if(tab.empty()){status(ui::tr(L"Select the login tab before filling a saved account."));return;}
    if(vault.locked()){status(ui::tr(L"Unlock Windows before using saved accounts."));return;}
    if(manual_autofill_tabs.contains(tab)){status(ui::tr(L"The selected login tab is still being checked…"));return;}
    if(autofill_window&&autofill_pending){status(ui::tr(L"A saved-account fill is already in progress."));return;}
    if(autofill_window&&str(autofill_offer,"tabId")==tab&&engine.autofill_offer_valid(str(autofill_offer,"offerId"))){
      remember_prompt(autofill_window);
    SetWindowPos(autofill_window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);return;
    }
    if(autofill_window)close_autofill(true);
    manual_autofill_tabs.insert(tab);
    status(ui::tr(L"Checking the selected tab for a supported HTTPS login form…"));
    engine.request_autofill(tab,autofill_reply({},tab,true));
  }
  void autofill_command(int id){
    if(autofill_pending)return;
    if(id==2){close_autofill(true);PostMessageW(window,AutofillNotice,0,0);return;}
    if(id!=1||autofill_offer.empty())return;
    const auto offer=str(autofill_offer,"offerId"),tab=str(autofill_offer,"tabId");
    if(vault.locked()||!engine.autofill_offer_valid(offer)){close_autofill(true);status(ui::tr(L"That login page changed. Open saved accounts again on the current login form."));return;}
    const auto index=SendMessageW(autofill_list,LB_GETCURSEL,0,0);
    const auto& accounts=autofill_offer["accounts"];
    if(index<0||static_cast<size_t>(index)>=accounts.size()){SetWindowTextW(autofill_status,ui::tr(L"Choose a saved account first."));return;}
    const auto account=str(accounts[static_cast<size_t>(index)],"accountId");
    if(account.empty())return;
    autofill_pending=true;EnableWindow(autofill_accept,FALSE);EnableWindow(GetDlgItem(autofill_window,2),FALSE);
    EnableMenuItem(GetSystemMenu(autofill_window,FALSE),SC_CLOSE,MF_BYCOMMAND|MF_GRAYED);
    SetWindowTextW(autofill_status,ui::tr(L"Filling the selected account…"));
    try{engine.fill_saved_account(offer,account,autofill_reply(offer,tab,false));}
    catch(...){close_autofill(true);status(ui::tr(L"Filling could not be confirmed. Check the login page before trying again."));}
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
    autofill_window=CreateWindowExW(extended,wc.lpszClassName,ui::tr(L"Fill saved account"),style,x,y,width,height,owner,nullptr,wc.hInstance,this);
    if(!autofill_window){engine.dismiss_autofill(str(offer,"offerId"));autofill_offer=Json::object();return;}
    window_icon(autofill_window);
    auto add=[&](int id,const wchar_t* type,const wchar_t* title,int x,int y,int w,int h,DWORD extra=0){
      auto control=CreateWindowExW(0,type,title,WS_CHILD|WS_VISIBLE|ui::styles(type,extra),x,y,w,h,autofill_window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),wc.hInstance,nullptr);SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);ui::control_theme(control);if(std::wstring(type)==L"BUTTON")button_style(control,id==1?ButtonTone::primary:ButtonTone::normal);return control;
    };
    text_style(add(0,L"STATIC",ui::tr(L"Xenon  /  Saved accounts"),60,16,394,27),TextTone::brand,heading_font);
    text_style(add(0,L"STATIC",ui::tr(L"Choose an account for this login website"),26,80,428,24),TextTone::heading,heading_font);
    const auto origin=wide(str(offer,"origin"));add(10,L"EDIT",origin.c_str(),26,112,428,50,WS_BORDER|ES_READONLY|ES_MULTILINE|WS_TABSTOP);
    autofill_list=add(11,L"LISTBOX",L"",26,174,428,80,WS_BORDER|WS_VSCROLL|LBS_NOTIFY|WS_TABSTOP);
    for(const auto& account:offer["accounts"]){
      auto label=str(account,"label");if(label.empty())label=ui::tr8("Saved account");
      const auto row=wide(compact(label,100)+"  ·  "+str(account,"accountId").substr(0,8));
      SendMessageW(autofill_list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str()));
    }
    if(!offer["accounts"].empty())SendMessageW(autofill_list,LB_SETCURSEL,0,0);
    text_style(add(0,L"STATIC",ui::tr(L"Fills this form without submitting. You finish signing in. Agent observations stay protected during login."),26,268,428,38),TextTone::muted,small_font);
    autofill_status=add(0,L"STATIC",L"",26,331,190,48);text_style(autofill_status,TextTone::status,small_font);
    autofill_accept=add(1,L"BUTTON",ui::tr(L"Fill"),230,335,104,34,WS_TABSTOP|BS_PUSHBUTTON);
    add(2,L"BUTTON",ui::tr(L"Not now"),346,335,108,34,WS_TABSTOP|BS_PUSHBUTTON);
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
          if(!ok){manual_autofill_tabs.erase(outcome.tab_id);status(ui::tr(L"Saved-account fill is unavailable for this tab. Use an HTTPS login form with an account saved for this exact website."));}
        }else{
          if(outcome.offer_id==str(autofill_offer,"offerId"))close_autofill(false);
          const auto phase=str(outcome.value.value("result",Json::object()),"phase");
          if(!ok)status(ui::tr(L"Filling could not be confirmed. Check the login page before trying again."));
          else if(phase=="username")status(ui::tr(L"Saved username filled. Continue on the website, then use Fill saved account again at the password step."));
          else if(phase=="password")status(ui::tr(L"Saved password filled without submitting. Return to the login tab to continue signing in."));
          else status(ui::tr(L"Saved account filled without submitting. Return to the login tab to continue signing in."));
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
    }catch(...){clear_autofill();status(ui::tr(L"Saved-account filling is temporarily unavailable."));}
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
      const auto state=broker.state();std::string workspace;for(const auto& tab:state["tabs"])if(str(tab,"tabId")==notice->tab_id)workspace=str(tab,"workspaceId");for(size_t n=0;n<rows[Workspaces].size();++n)if(str(rows[Workspaces][n],"workspaceId")==workspace)SendMessageW(controls.at(Workspaces),LB_SETCURSEL,n,0);
      page=1;layout_main();refresh();
      auto& tabs=rows[Tabs];
      for(size_t i=0;i<tabs.size();++i)if(str(tabs[i],"tabId")==notice->tab_id){
        ListView_SetItemState(controls.at(Tabs),i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_EnsureVisible(controls.at(Tabs),i,FALSE);
        SetWindowTextW(controls.at(DialogText),L"");
        const auto origin=notice->origin.empty()?ui::tr8("unknown origin"):notice->origin;
        status(wide((notice->protected_auth?ui::tr8("Protected authentication. "):"")+std::string(ui::tr8("Website "))+ui::tr8(notice->type.c_str())+ui::tr8(" from ")+origin+ui::tr8(" in the selected tab. Review its message above, then Accept dialog or Dismiss. Prompt response goes to the website.")));
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
        close_login_prompt(false);status(updated?ui::tr(L"Saved password updated in Xenon."):ui::tr(L"Password saved in Xenon."));refresh();PostMessageW(window,LoginNotice,0,0);
      }else{
        EnableWindow(login_accept,FALSE);
        SetWindowTextW(login_status,ui::tr(L"This login could not be saved. Close this prompt and try the login again."));
      }
    }catch(...){if(login_status)SetWindowTextW(login_status,ui::tr(L"The login could not be saved. No password details were displayed or logged."));}
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
    constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MAXIMIZEBOX|WS_CLIPCHILDREN;
    constexpr DWORD extended=WS_EX_TOOLWINDOW;
    RECT bounds{0,0,480,362};AdjustWindowRectEx(&bounds,style,FALSE,extended);
    const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    RECT source{};GetWindowRect(owner,&source);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
    const int x=std::clamp(source.right-width-18,monitor.rcWork.left,std::max(monitor.rcWork.left,monitor.rcWork.right-width));
    const int y=std::clamp(source.top+80,monitor.rcWork.top,std::max(monitor.rcWork.top,monitor.rcWork.bottom-height));
    login_candidate=candidate.candidate_id;
    login_window=CreateWindowExW(extended,wc.lpszClassName,candidate.update?ui::tr(L"Update saved password?"):ui::tr(L"Save password in Xenon?"),style,x,y,width,height,owner,nullptr,wc.hInstance,this);
    if(!login_window){vault.dismiss_login(login_candidate);login_candidate.clear();return;}
    window_icon(login_window);
    auto add=[&](int id,const wchar_t* type,const wchar_t* title,int x,int y,int w,int h,DWORD extra=0){
      auto c=CreateWindowExW(0,type,title,WS_CHILD|WS_VISIBLE|ui::styles(type,extra),x,y,w,h,login_window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),wc.hInstance,nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);ui::control_theme(c);if(std::wstring(type)==L"BUTTON")button_style(c,id==1?ButtonTone::primary:ButtonTone::normal);return c;
    };
    text_style(add(0,L"STATIC",ui::tr(L"Xenon  /  Saved accounts"),60,16,394,27),TextTone::brand,heading_font);
    text_style(add(0,L"STATIC",candidate.update?ui::tr(L"Update with the password you just submitted?"):ui::tr(L"Save the password you just submitted?"),26,80,428,42),TextTone::heading,heading_font);
    text_style(add(0,L"STATIC",ui::tr(L"Login website — this account will be saved for this origin"),26,122,428,21),TextTone::muted,small_font);
    auto origin=wide(candidate.origin);add(10,L"EDIT",origin.c_str(),26,149,428,55,WS_BORDER|WS_VSCROLL|ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_TABSTOP);
    auto account=wide(ui::tr8("Account: ")+candidate.username_label);add(0,L"STATIC",account.c_str(),26,214,428,22);
    text_style(add(0,L"STATIC",ui::tr(L"Sign-in may still require verification. Save only if this password is correct. Saving does not grant agent access."),26,243,428,36),TextTone::muted,small_font);
    login_accept=add(1,L"BUTTON",candidate.update?ui::tr(L"Update"):ui::tr(L"Save"),230,308,104,34,WS_TABSTOP|BS_PUSHBUTTON);
    add(2,L"BUTTON",ui::tr(L"Not now"),346,308,108,34,WS_TABSTOP|BS_PUSHBUTTON);
    login_status=add(0,L"STATIC",L"",26,300,190,54);text_style(login_status,TextTone::status,small_font);
    // Appearance changes neither browser focus nor agent ownership. Clicking a
    // native button later is an explicit human action.
    remember_prompt(login_window);
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
    HWND c=CreateWindowExW(0,type,title,WS_CHILD|WS_VISIBLE|ui::styles(type,style),x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);controls[id]=c;placements.push_back({c,{x,y,x+w,y+h},building_page});ui::control_theme(c);return c;
  }
  HWND label(const wchar_t* text,int x,int y,int w,TextTone tone=TextTone::normal){auto c=add(0,L"STATIC",text,x,y,w,20);text_style(c,tone,tone==TextTone::heading?heading_font:tone==TextTone::muted?small_font:font);return c;}
  void button(int id,const wchar_t* text,int x,int y,int w=125,ButtonTone tone=ButtonTone::normal){button_style(add(id,L"BUTTON",text,x,y,w,28,WS_TABSTOP|BS_PUSHBUTTON),tone);}
  std::string text(int id){auto c=controls.at(id);int n=GetWindowTextLengthW(c);std::wstring s(n+1,L'\0');GetWindowTextW(c,s.data(),n+1);s.resize(n);auto value=utf8(s);SecureZeroMemory(s.data(),s.size()*sizeof(wchar_t));return value;}
  Json selected(int id){int n=id==Tabs?ListView_GetNextItem(controls.at(id),-1,LVNI_SELECTED):static_cast<int>(SendMessageW(controls.at(id),LB_GETCURSEL,0,0));if(n<0||static_cast<size_t>(n)>=rows[id].size())return Json::object();return rows[id][n];}
  void status(const std::wstring& s){SetWindowTextW(controls.at(Status),s.c_str());}
  void result(const Json& r){if(r.value("ok",false))status(ui::tr(L"Done."));else status(ui::error_text(str(r.value("error",Json::object()),"message")));}
  void list(int id,const Json& values,const char* key,std::function<std::string(const Json&)> render){
    auto old=selected(id);auto old_id=str(old,key);auto c=controls.at(id);
    std::vector<Json> next;for(const auto& value:values)next.push_back(value);
    if(rows[id]==next)return;
    if(id==Tabs){ListView_DeleteAllItems(c);rows[id]=std::move(next);int match=0;for(size_t n=0;n<rows[id].size();++n){const auto& row=rows[id][n];auto title=wide(str(row,"title"));LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=static_cast<int>(n);item.pszText=title.data();ListView_InsertItem(c,&item);auto agent=wide(str(row,"ownerDisplay"));ListView_SetItemText(c,n,1,agent.data());auto state=wide(row.value("agentAvailable",false)&&row.value("humanPaused",false)?ui::tr8("Paused for human"):row.value("protected",false)?ui::tr8("Protected authentication"):row.contains("dialog")?ui::tr8("Website dialog"):str(row,"ownerSessionId")=="human"?ui::tr8("Human control"):str(row,"ownerSessionId").empty()?ui::tr8("No agent"):row.value("agentAvailable",false)?ui::tr8("Agent control"):ui::tr8("Agent unavailable"));ListView_SetItemText(c,n,2,state.data());auto tab=wide(str(row,"tabId"));ListView_SetItemText(c,n,3,tab.data());if(str(row,key)==old_id)match=static_cast<int>(n);}ListView_SetItemState(c,match,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);return;}
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
        const auto id=str(workspace,"workspaceId");workspace_names[id]=ui::workspace_label(id,str(workspace,"displayName"),workspace.value("private",false));
      }
      for(auto& tab:s["tabs"]){auto owner=str(tab,"ownerSessionId");tab["ownerDisplay"]=owner=="human"?ui::tr8("You"):owner.empty()?ui::tr8("No owner"):worker_names.contains(owner)?worker_names[owner]:ui::tr8("Agent ")+owner.substr(0,8);if(tab.value("humanPaused",false))tab["ownerDisplay"]=tab["ownerDisplay"].get<std::string>()+ui::tr8(" (paused for you)");tab["workspaceDisplay"]=workspace_names[str(tab,"workspaceId")];}
      for(const auto& cached:engine.native_tabs())for(auto& tab:s["tabs"])if(str(tab,"tabId")==str(cached,"tabId"))tab["title"]=ui::tab_title(str(cached,"title"),str(cached,"url"));
      list(Pairings,s["pairings"],"requestId",[](auto& j){return str(j,"name");});
      auto clients=s["clients"];
      for(auto pending:s.value("pendingRevocations",Json::array())){pending["revocationPending"]=true;clients.push_back(std::move(pending));}
      list(Clients,clients,"clientId",[](auto& j){return (j.value("revocationPending",false)?ui::tr8("NOT SAVED — retry revocation: "):"")+str(j,"name")+(j.value("connected",false)?ui::tr8(" · Connected"):ui::tr8(" · Offline"))+" · "+(j.value("policy",Json::object()).value("interaction",false)?ui::tr8("Interactive"):ui::tr8("Read-only"));});
      list(Workspaces,s["workspaces"],"workspaceId",[](auto& j){return ui::workspace_label(str(j,"workspaceId"),str(j,"displayName"),j.value("private",false));});
      for(const auto& dialog:engine.native_dialogs())for(auto& tab:s["tabs"])if(str(tab,"tabId")==str(dialog,"tabId"))tab["dialog"]=dialog;
      Json filtered=Json::array();const auto selected_workspace=str(selected(Workspaces),"workspaceId");for(const auto& tab:s["tabs"])if(str(tab,"workspaceId")==selected_workspace)filtered.push_back(tab);
      list(Tabs,filtered,"tabId",[](auto&){return std::string{};});
      Json filtered_workers=Json::array();for(auto worker:s["workers"])if(str(worker,"workspaceId")==selected_workspace){
        bool interactive=false;for(const auto& workspace:s["workspaces"])if(str(workspace,"workspaceId")==selected_workspace){const auto access=workspace["access"].value(str(worker,"clientId"),Json::object());interactive=access.value("effectivePermissions",Json::object()).value("interaction",false);}
        worker["canControl"]=interactive&&worker.value("connected",false)&&str(worker,"state")!="retiring";filtered_workers.push_back(std::move(worker));}
      list(Workers,filtered_workers,"agentSessionId",[](auto& j){return str(j,"name")+"  ·  "+str(j,"agentSessionId").substr(0,10)+(str(j,"state")=="retiring"?ui::tr8(" (retiring)"):j.value("connected",false)?"":ui::tr8(" (offline)"));});
      auto accounts=vault.list_accounts();if(accounts.value("ok",false))list(Accounts,accounts["result"]["accounts"],"accountId",[](auto& j){return str(j,"label")+"  |  "+str(j,"origin");});
      const bool pairing=!str(selected(Pairings),"requestId").empty(),client=!str(selected(Clients),"clientId").empty(),workspace=!str(selected(Workspaces),"workspaceId").empty();
      for(int id:{Approve,Deny})EnableWindow(controls.at(id),pairing);
      for(int id:{ConfigureClient,ClientFiles,Revoke})EnableWindow(controls.at(id),client);
      for(int id:{ConfigureWorkspace,UploadGrant})EnableWindow(controls.at(id),workspace);
      EnableWindow(controls.at(Share),client&&workspace);EnableWindow(controls.at(RemoveWorkspace),workspace&&str(selected(Workspaces),"workspaceId")!="native-default");
      const auto tab=selected(Tabs);const bool has_tab=!str(tab,"tabId").empty(),dialog=tab.contains("dialog");
      EnableWindow(controls.at(Take),has_tab);EnableWindow(controls.at(Give),has_tab&&str(tab,"ownerSessionId")=="human"&&selected(Workers).value("canControl",false));
      const auto dialog_info=tab.value("dialog",Json::object());const auto dialog_message=dialog?ui::tr8(str(dialog_info,"type").c_str())+ui::tr8(" from ")+str(dialog_info,"origin")+"\r\n"+str(dialog_info,"message"):std::string(ui::tr8("No website dialog waiting."));
      if(text(DialogMessage)!=dialog_message)SetWindowTextW(controls.at(DialogMessage),wide(dialog_message).c_str());
      EnableWindow(controls.at(ResumeAuth),has_tab&&tab.value("protected",false));for(int id:{DialogAccept,DialogDismiss,DialogText})EnableWindow(controls.at(id),dialog);
      const bool account=!str(selected(Accounts),"accountId").empty();EnableWindow(controls.at(DeleteAccount),account);EnableWindow(controls.at(FillSavedAccount),has_tab);
      EnableWindow(controls.at(ClientAccountGrant),account&&client);EnableWindow(controls.at(AccountRevoke),account&&client);EnableWindow(controls.at(Grant),account&&client&&workspace);
    }catch(const std::exception&){status(ui::tr(L"Some controls are unavailable while the vault is locked."));}
  }
  std::filesystem::path pick(bool csv){
    wchar_t path[32768]{};OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=window;ofn.lpstrFile=path;ofn.nMaxFile=32768;
    ofn.lpstrFilter=ui::file_filter(csv);
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
    for(const auto& entry:file_rows){auto name=wide(ui::tr8(str(entry,"kind").c_str())+": "+str(entry,"name")+"  ["+str(entry,entry.contains("folderId")?"folderId":"fileId")+"]");SendMessageW(file_list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}
    if(!file_rows.empty())SendMessageW(file_list,LB_SETCURSEL,0,0);
  }
  void file_command(int id){
    try{
      if(id==1){auto path=pick(false);if(!path.empty())result(files.grant_upload(file_scope,path));}
      else if(id==2){auto path=pick_folder();if(!path.empty())result(files.grant_folder(file_scope,path));}
      else if(id==3){int n=static_cast<int>(SendMessageW(file_list,LB_GETCURSEL,0,0));if(n>=0&&static_cast<size_t>(n)<file_rows.size()){const auto& entry=file_rows[n];files.revoke_grant(file_scope,str(entry,entry.contains("folderId")?"folderId":"fileId"));status(ui::tr(L"Selected file or folder grant revoked."));}}
      else if(id==4){ShowWindow(file_window,SW_HIDE);return;}
      broker.resource_changed(file_scope.starts_with("client:")?file_scope.substr(7):"");refresh_files();
    }catch(const std::exception&){status(ui::tr(L"The file permission could not be changed."));}
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
  void show_files(const std::string& client=""){
    auto workspace=selected(Workspaces);if(client.empty()&&workspace.value("private",false)){status(ui::tr(L"Private workspaces are human-only and do not accept agent file grants."));return;}
    file_scope=client.empty()?str(workspace,"workspaceId"):client_file_scope(client);if(file_scope.empty()){status(ui::tr(L"Select a workspace first."));return;}
    if(!file_window){
      WNDCLASSW wc{};wc.lpfnWndProc=file_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"XenonFilePermissions";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
      constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MAXIMIZEBOX|WS_CLIPCHILDREN;
      RECT bounds{0,0,860,420};AdjustWindowRectEx(&bounds,style,FALSE,0);
      file_window=CreateWindowExW(0,wc.lpszClassName,ui::tr(L"Xenon File Permissions"),style,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,window,nullptr,wc.hInstance,this);
      window_icon(file_window);
      auto make=[&](int id,const wchar_t* type,const wchar_t* name,int x,int y,int w,int h,DWORD style){auto child=CreateWindowExW(0,type,name,WS_CHILD|WS_VISIBLE|ui::styles(type,style),x,y,w,h,file_window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),wc.hInstance,nullptr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);ui::control_theme(child);if(std::wstring(type)==L"BUTTON")button_style(child,id==1?ButtonTone::primary:id==3?ButtonTone::caution:ButtonTone::normal);return child;};
      text_style(make(0,L"STATIC",ui::tr(L"Xenon  /  File permissions"),60,16,770,27,0),TextTone::brand,heading_font);
      text_style(make(0,L"STATIC",ui::tr(L"Approved resources remain subject to client and workspace permissions."),26,82,808,24,0),TextTone::muted);
      file_list=make(10,L"LISTBOX",L"",26,114,808,230,WS_BORDER|WS_VSCROLL|WS_HSCROLL|WS_TABSTOP);
      make(1,L"BUTTON",ui::tr(L"Grant file"),26,376,150,30,WS_TABSTOP);make(2,L"BUTTON",ui::tr(L"Grant folder"),188,376,150,30,WS_TABSTOP);
      make(3,L"BUTTON",ui::tr(L"Revoke selected"),350,376,170,30,WS_TABSTOP);make(4,L"BUTTON",ui::tr(L"Close"),684,376,150,30,WS_TABSTOP);
    }
    if(!prompt_layouts.contains(file_window))remember_prompt(file_window);
    SetWindowTextW(file_window,wide(ui::tr8("Xenon File Permissions — ")+file_scope).c_str());refresh_files();ShowWindow(file_window,SW_SHOWNORMAL);SetForegroundWindow(file_window);
  }
  void poll_removal_results(){
    std::deque<Json> results;{std::lock_guard lock(removal_results->mutex);results.swap(removal_results->values);}
    for(const auto& value:results){
      if(value.value("ok",false)){
        const auto detail=value.value("result",Json::object());
        status(detail.value("profileCleanup",std::string{})=="memory_only"?
          ui::tr(L"Private workspace removed. Saved passwords and downloaded files were kept."):
          ui::tr(L"Workspace removed. Its site data will be deleted on the next launch; locked files are retried later. Saved passwords and downloaded files were kept."));
      }else result(value);
    }
    if(!results.empty())refresh();
  }
  void remove_selected_workspace(){
    const auto workspace=selected(Workspaces);const auto id=str(workspace,"workspaceId");
    if(id.empty()){status(ui::tr(L"Select the workspace to remove."));return;}
    if(id=="native-default"){status(ui::tr(L"Personal is the default workspace and cannot be removed. Other workspaces can be removed here."));return;}
    if(workspace.value("removing",false)&&str(workspace,"removalStatus")!="retry"){
      status(ui::tr(L"Workspace removal is already in progress. Finish any held input or answer its pending website dialog."));return;
    }
    const auto name=str(workspace,"displayName");
    const auto message=wide(ui::tr8("Remove ")+name+ui::tr8("?\n\nThis closes all its tabs, stops its agents and downloads, and removes its agent and file permissions. Unsaved work in those tabs will be lost.\n\n")+
      std::string(workspace.value("private",false)?ui::tr8("Its private browsing session will be discarded."):ui::tr8("Its cookies, sign-in sessions, history and other profile data will be deleted on the next launch. Files that are still locked will be retried on later launches."))+
      ui::tr8("\n\nSaved passwords in the shared vault, downloaded files and files from approved folders will be kept. Tabs and stored data in other workspaces are kept."));
    if(MessageBoxW(window,message.c_str(),ui::tr(L"Remove workspace"),MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
    if(file_window&&file_scope==id){DestroyWindow(file_window);file_window=nullptr;file_list=nullptr;file_rows.clear();file_scope.clear();}
    const auto notices=removal_results;const auto target=window;
    status(ui::tr(L"Removing workspace. Waiting for active input to finish before closing its tabs…"));
    broker.remove_workspace(id,[notices,target](Json value){
      {std::lock_guard lock(notices->mutex);notices->values.push_back(std::move(value));}
      PostMessageW(target,RemovalNotice,0,0);
    });
  }
  void command(int id){
    try {
      if(id==PageClients||id==PageWorkspaces||id==PagePasswords){page=id-PageClients;layout_main();}
      else if(id==ConfigureClient)show_configuration(false);
      else if(id==ConfigureWorkspace)show_configuration(true);
      else if(id==ClientFiles)show_files(str(selected(Clients),"clientId"));
      else if(id==Approve){if(broker.approve_pairing(str(selected(Pairings),"requestId")))status(ui::tr(L"Client paired with read-only permissions. Configure it and share a workspace to allow access."));}
      else if(id==Deny)broker.deny_pairing(str(selected(Pairings),"requestId"));
      else if(id==Revoke){
        const auto revoked=broker.revoke_client(str(selected(Clients),"clientId"));
        if(revoked==Broker::RevocationStatus::durable){files.revoke_scope(client_file_scope(str(selected(Clients),"clientId")));status(ui::tr(L"Client access revoked and saved. Its queued actions were canceled."));}
        else if(revoked==Broker::RevocationStatus::pending)status(ui::tr(L"Client blocked for this run, but revocation could not be saved. Restarting can restore access. Select its NOT SAVED row and click Revoke / retry."));
        else status(ui::tr(L"Select a paired client or an unsaved revocation to retry."));
      }
      else if(id==Share){status(broker.share_workspace(str(selected(Workspaces),"workspaceId"),str(selected(Clients),"clientId"))?ui::tr(L"Workspace shared with the selected client."):ui::tr(L"Select a client and workspace."));}
      else if(id==Take){broker.human_acquire(str(selected(Tabs),"tabId"));status(ui::tr(L"Ownership requested for you until explicitly given to an agent. Current input will finish."));}
      else if(id==Give){broker.human_release(str(selected(Tabs),"tabId"),str(selected(Workers),"agentSessionId"));status(ui::tr(L"Control offered to the selected connected agent in this workspace."));}
      else if(id==Stop){broker.stop_all();status(ui::tr(L"All agent control stopped. Restart Xenon to accept clients again."));}
      else if(id==ResumeAuth){
        if(MessageBoxW(window,ui::tr(L"Resume agent observations for this tab? Confirm that login is finished and no password, code, recovery key, or other secret is visible."),ui::tr(L"Resume observations"),MB_YESNO|MB_ICONQUESTION)==IDYES){engine.release_protection(str(selected(Tabs),"tabId"));status(ui::tr(L"Protected authentication ended for the selected tab."));}
      }
      else if(id==DialogAccept||id==DialogDismiss){engine.answer_native_dialog(str(selected(Tabs),"tabId"),id==DialogAccept,text(DialogText));SetWindowTextW(controls[DialogText],L"");status(ui::tr(L"Selected tab's script dialog answered."));}
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
          std::wstring details=ui::tr(L"Review this local password import. Usernames and passwords are masked.\n\nValid rows: ")+std::to_wstring(data.at("validRows").get<size_t>())+
            ui::tr(L"\nInvalid rows to skip: ")+std::to_wstring(data.at("invalidRows").get<size_t>())+
            ui::tr(L"\nDuplicate/existing rows: ")+std::to_wstring(data.at("duplicates").get<size_t>())+
            ui::tr(L"\nConflicting rows: ")+std::to_wstring(data.at("conflicts").get<size_t>())+L"\n\n";
          size_t shown=0;
          for(const auto& item:data.at("preview")){
            if(shown++==12){details+=ui::tr(L"Additional accounts are included in the counts above.\n");break;}
            details+=wide(str(item,"origin"))+L" | "+wide(str(item,"usernameLabel"))+L" | "+wide(ui::tr8(str(item,"status").c_str()))+L"\n";
          }
          if(data.at("validRows").get<size_t>()==0){status(ui::tr(L"The CSV contains no supported HTTPS logins. Nothing was imported."));return;}
          details+=ui::tr(L"\nImport these logins? The source CSV remains unencrypted on disk until you remove it.");
          if(MessageBoxW(window,details.c_str(),ui::tr(L"Preview password import"),MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES){status(ui::tr(L"Import canceled. Nothing was imported."));return;}
          bool replace=false;
          if(data.at("conflicts").get<size_t>()>0){
            const auto answer=MessageBoxW(window,ui::tr(L"Some accounts have different passwords in the CSV.\n\nYes: replace conflicting saved passwords with the CSV values.\nNo: keep existing passwords and skip conflicting rows.\nCancel: import nothing."),ui::tr(L"Resolve password conflicts"),MB_YESNOCANCEL|MB_ICONQUESTION|MB_DEFBUTTON2);
            if(answer==IDCANCEL){status(ui::tr(L"Import canceled. Nothing was imported."));return;}
            replace=answer==IDYES;
          }
          auto imported=vault.import_csv(path,replace,str(data,"fileDigest"));result(imported);
          if(imported.value("ok",false)){
            const auto& counts=imported.at("result");
            status(ui::tr(L"Imported ")+std::to_wstring(counts.at("created").get<size_t>())+ui::tr(L", updated ")+std::to_wstring(counts.at("updated").get<size_t>())+
              ui::tr(L", skipped conflicts ")+std::to_wstring(counts.at("conflicts").get<size_t>())+ui::tr(L". Remove the source CSV when finished."));
          }
        }
      }
      else if(id==ClientAccountGrant){auto a=selected(Accounts);status(broker.grant_client_account(str(selected(Clients),"clientId"),str(a,"accountId"),str(a,"origin"))?ui::tr(L"Account granted to this client for its exact HTTPS origin."):ui::tr(L"Select a client and saved account."));}
      else if(id==AccountRevoke){auto a=selected(Accounts);status(broker.revoke_account(str(selected(Clients),"clientId"),"",str(a,"accountId"))?ui::tr(L"Client account permission revoked."):ui::tr(L"Account access blocked; the change could not be saved."));}
      else if(id==DeleteAccount){auto account=selected(Accounts);if(!account.empty()&&MessageBoxW(window,ui::tr(L"Remove this saved account from Xenon?"),ui::tr(L"Remove account"),MB_YESNO|MB_ICONQUESTION)==IDYES){result(vault.remove(str(account,"accountId")));broker.resource_changed("");}}
      else if(id==Grant){auto a=selected(Accounts);status(broker.grant_account(str(selected(Clients),"clientId"),str(selected(Workspaces),"workspaceId"),str(a,"accountId"),str(a,"origin"))?ui::tr(L"Selected account allowed for this client, workspace, and exact origin."):ui::tr(L"Select an account, a paired client, and a shared workspace."));}
      else if(id==NewWorkspace){show_configuration(true,true);}
      else if(id==RemoveWorkspace)remove_selected_workspace();
      else if(id==PrivateWorkspace){broker.open_human_workspace("about:blank",[this](Json r){result(r);},true);}
      else if(id==RestoreSession){
        auto state=broker.state();std::vector<std::string> allowed;for(const auto& workspace:state["workspaces"])if(!workspace.value("private",false))allowed.push_back(str(workspace,"workspaceId"));
        engine.restore_session(allowed,[this](Json r){if(r.value("ok",false))status(ui::tr(L"Restored ")+std::to_wstring(r["result"].value("restored",0))+ui::tr(L" tabs with human control. Closed, private and protected tabs are excluded."));else result(r);});
      }
      else if(id==UploadGrant)show_files();
      else if(id==CheckUpdates)show_updates();
      refresh();
    }catch(const std::exception&){status(ui::tr(L"The operation failed. No secret details were logged."));}
  }
  void layout_main(){if(!font)return;const auto panel=content_bounds();auto d=[&](int value){return ui::dip(window,value);};const int left=panel.left+d(24),body_top=d(100),width=panel.right-left-d(24),height=panel.bottom-d(16)-body_top;
    for(const auto& entry:placements){const auto r=entry.bounds;const int id=GetDlgCtrlID(entry.control);if(id==Brand)MoveWindow(entry.control,d(24),d(30),d(168),d(34),TRUE);
      else if(id==SectionTitle)MoveWindow(entry.control,left,d(36),std::max(100,width-d(220)),d(36),TRUE);
      else if(id==CheckUpdates)MoveWindow(entry.control,panel.right-d(190),d(38),d(166),d(30),TRUE);
      else if(id>=PageClients&&id<=PagePasswords)MoveWindow(entry.control,d(20),d(102+(id-PageClients)*52),d(176),d(40),TRUE);
      else MoveWindow(entry.control,left+MulDiv(r.left-24,width,912),body_top+MulDiv(r.top-116,height,553),MulDiv(r.right-r.left,width,912),MulDiv(r.bottom-r.top,height,553),TRUE);
      ShowWindow(entry.control,entry.page<0||entry.page==page?SW_SHOW:SW_HIDE);if(id>=PageClients&&id<=PagePasswords)InvalidateRect(entry.control,nullptr,TRUE);}
    SetWindowTextW(controls.at(SectionTitle),page==0?ui::tr(L"Clients"):page==1?ui::tr(L"Workspaces"):ui::tr(L"Passwords"));InvalidateRect(window,nullptr,TRUE);}
  void apply_theme(){ui::icons(window);for(const auto& entry:placements)ui::control_theme(entry.control);const auto colors=ui::palette();applied_palette=colors;auto table=controls.at(Tabs);ListView_SetBkColor(table,colors.canvas);ListView_SetTextBkColor(table,colors.canvas);ListView_SetTextColor(table,colors.ink);InvalidateRect(window,nullptr,TRUE);
    for(const auto& [prompt,layout]:prompt_layouts){ui::icons(prompt);for(const auto& [child,bounds]:layout.children)ui::control_theme(child);InvalidateRect(prompt,nullptr,TRUE);}
    for(auto& config:configurations)if(config->window){ui::icons(config->window);ListView_SetBkColor(config->resources,colors.canvas);ListView_SetTextBkColor(config->resources,colors.canvas);ListView_SetTextColor(config->resources,colors.ink);InvalidateRect(config->window,nullptr,TRUE);}}
  void build(){
    font=ui::font(window);heading_font=ui::font(window,16,FW_SEMIBOLD);brand_font=ui::font(window,24,FW_SEMIBOLD);small_font=ui::font(window,13);window_icon(window);building_page=-1;
    text_style(add(Brand,L"STATIC",L"Xenon",24,17,168,34),TextTone::normal,brand_font);text_style(add(SectionTitle,L"STATIC",ui::tr(L"Clients"),232,36,500,36),TextTone::normal,brand_font);button(CheckUpdates,ui::tr(L"Check for updates"),770,20,166);
    button(PageClients,ui::tr(L"Clients"),24,68,150);button(PageWorkspaces,ui::tr(L"Workspaces"),186,68,150);button(PagePasswords,ui::tr(L"Passwords"),348,68,150);
    for(int id:{PageClients,PageWorkspaces,PagePasswords})SetPropW(controls.at(id),L"XenonConnected",reinterpret_cast<HANDLE>(1));
    building_page=0;
    label(ui::tr(L"Pairing requests"),24,118,500,TextTone::heading);label(ui::tr(L"Approve only clients you recognize."),24,146,870,TextTone::muted);
    add(Pairings,L"LISTBOX",L"",24,177,912,110,WS_BORDER|WS_VSCROLL|LBS_NOTIFY|WS_TABSTOP);button(Approve,ui::tr(L"Approve"),24,301);button(Deny,ui::tr(L"Deny"),161,301);
    label(ui::tr(L"Paired clients · connection and access"),24,350,900,TextTone::heading);add(Clients,L"LISTBOX",L"",24,381,912,160,WS_BORDER|WS_VSCROLL|LBS_NOTIFY|WS_TABSTOP);
    button(ConfigureClient,ui::tr(L"Configure"),24,557,130);button(ClientFiles,ui::tr(L"File resources"),166,557,150);button(Revoke,ui::tr(L"Revoke / retry"),328,557,150);button(Stop,ui::tr(L"Stop all agents"),786,557,150);
    building_page=1;
    label(ui::tr(L"Workspaces"),24,116,425,TextTone::heading);label(ui::tr(L"Connected agents in selected workspace"),490,116,440,TextTone::heading);
    add(Workspaces,L"LISTBOX",L"",24,145,445,110,WS_BORDER|WS_VSCROLL|LBS_NOTIFY|WS_TABSTOP);add(Workers,L"LISTBOX",L"",490,145,446,110,WS_BORDER|WS_VSCROLL|LBS_NOTIFY|WS_TABSTOP);
    button(NewWorkspace,ui::tr(L"Create workspace"),24,267,155);button(ConfigureWorkspace,ui::tr(L"Configure"),187,267,110);button(RemoveWorkspace,ui::tr(L"Remove"),305,267,85);button(PrivateWorkspace,ui::tr(L"Private workspace"),398,267,142);button(RestoreSession,ui::tr(L"Restore last session"),548,267,164);button(UploadGrant,ui::tr(L"Files"),720,267,70);button(Share,ui::tr(L"Share read-only"),798,267,138);
    label(ui::tr(L"Tabs in selected workspace"),24,317,880,TextTone::heading);add(Tabs,WC_LISTVIEWW,L"",24,346,912,110,WS_BORDER|LVS_REPORT|LVS_SINGLESEL|WS_TABSTOP);auto table=controls.at(Tabs);ListView_SetExtendedListViewStyle(table,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);int column=0;for(const auto& [title,width]:std::vector<std::pair<const wchar_t*,int>>{{ui::tr(L"Title"),330},{ui::tr(L"Agent"),165},{ui::tr(L"Status"),175},{ui::tr(L"Tab ID"),225}}){LVCOLUMNW value{};value.mask=LVCF_TEXT|LVCF_WIDTH;value.cx=ui::dip(window,width);value.pszText=const_cast<LPWSTR>(title);ListView_InsertColumn(table,column++,&value);}
    add(DialogMessage,L"EDIT",ui::tr(L"No website dialog waiting."),24,466,912,67,WS_BORDER|ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_TABSTOP);
    button(Take,ui::tr(L"Take ownership"),24,545,145);button(Give,ui::tr(L"Give to agent"),181,545,140);button(ResumeAuth,ui::tr(L"Resume after login"),333,545,175);button(DialogAccept,ui::tr(L"Accept dialog"),520,545,135);button(DialogDismiss,ui::tr(L"Dismiss"),667,545,100);add(DialogText,L"EDIT",L"",779,545,157,28,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);
    building_page=2;
    label(ui::tr(L"Saved accounts · shared encrypted vault"),24,116,870,TextTone::heading);add(Accounts,L"LISTBOX",L"",24,149,912,148,WS_BORDER|WS_VSCROLL|LBS_NOTIFY|WS_TABSTOP);
    button(Import,ui::tr(L"Import CSV"),24,309,135);button(DeleteAccount,ui::tr(L"Remove account"),171,309,160);button(FillSavedAccount,ui::tr(L"Fill selected login tab"),343,309,210);
    label(ui::tr(L"Selected client/workspace come from the other sections. Grants use exact HTTPS origins."),24,356,912,TextTone::muted);button(ClientAccountGrant,ui::tr(L"Grant to client"),24,388,190);button(AccountRevoke,ui::tr(L"Revoke client grant"),226,388,190);button(Grant,ui::tr(L"Grant in workspace"),428,388,195);
    label(ui::tr(L"HTTPS origin"),24,439,430,TextTone::muted);label(ui::tr(L"Account label"),490,439,446,TextTone::muted);add(Origin,L"EDIT",L"",24,464,445,28,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);add(Label,L"EDIT",L"",490,464,446,28,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);
    label(ui::tr(L"Username"),24,507,445,TextTone::muted);label(ui::tr(L"Password"),490,507,446,TextTone::muted);add(Username,L"EDIT",L"",24,532,445,28,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);add(Password,L"EDIT",L"",490,532,446,28,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL|ES_PASSWORD);button(Save,ui::tr(L"Save account"),24,582,165);
    building_page=-1;text_style(add(Status,L"STATIC",ui::tr(L"Choose a section to manage clients, workspaces or saved passwords."),24,635,912,34),TextTone::status,small_font);SetTimer(window,1,750,nullptr);WTSRegisterSessionNotification(window,NOTIFY_FOR_THIS_SESSION);refresh();apply_theme();layout_main();
  }
  static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){try{return dispatch_proc(h,msg,wp,lp);}catch(...){return msg==WM_NCCREATE?FALSE:msg==WM_CREATE?-1:0;}}
  static LRESULT CALLBACK dispatch_proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(msg==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(h,msg,wp,lp);
    LRESULT painted{};if(self->theme_message(h,msg,wp,lp,painted))return painted;
    switch(msg){
      case WM_CREATE:self->build();return 0;
      case WM_COMMAND:if(HIWORD(wp)==LBN_DBLCLK&&LOWORD(wp)==Workspaces)self->show_configuration(true);else if(HIWORD(wp)==LBN_SELCHANGE)self->refresh();else if(HIWORD(wp)==BN_CLICKED)self->command(LOWORD(wp));return 0;
      case WM_SIZE:self->layout_main();return 0;
      case WM_SETTINGCHANGE:case WM_THEMECHANGED:self->apply_theme();return 0;
      case WM_DPICHANGED:{auto rect=reinterpret_cast<RECT*>(lp);const HFONT previous[]{self->font,self->heading_font,self->brand_font,self->small_font};
        self->font=ui::font(h);self->heading_font=ui::font(h,16,FW_SEMIBOLD);self->brand_font=ui::font(h,24,FW_SEMIBOLD);self->small_font=ui::font(h,13);
        const HFONT current[]{self->font,self->heading_font,self->brand_font,self->small_font};
        for(const auto& placement:self->placements){const auto face=reinterpret_cast<HFONT>(SendMessageW(placement.control,WM_GETFONT,0,0));for(int n=0;n<4;++n)if(face==previous[n]){SendMessageW(placement.control,WM_SETFONT,reinterpret_cast<WPARAM>(current[n]),TRUE);break;}}
        for(auto face:previous)DeleteObject(face);for(const auto& placement:self->placements)ui::control_theme(placement.control);SetWindowPos(h,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOACTIVATE|SWP_NOZORDER);self->layout_main();return 0;}
      case WM_GETMINMAXINFO:{auto value=reinterpret_cast<MINMAXINFO*>(lp);value->ptMinTrackSize={ui::dip(h,1160),ui::dip(h,750)};return 0;}
      case WM_TIMER:if(!self->applied_palette||*self->applied_palette!=ui::palette())self->apply_theme();self->poll_login_prompts();self->poll_autofill();self->poll_dialog_notices();self->poll_removal_results();self->poll_updates();self->refresh();return 0;
      case LoginNotice:self->poll_login_prompts();return 0;
      case DialogNotice:self->poll_dialog_notices();return 0;
      case RemovalNotice:self->poll_removal_results();return 0;
      case AutofillNotice:self->poll_autofill();return 0;
      case WM_CLOSE:ShowWindow(h,SW_HIDE);return 0;
      case WM_WTSSESSION_CHANGE:if(wp==WTS_SESSION_LOCK){self->clear_login_prompts();self->clear_autofill();self->vault.set_locked(true);self->engine.cancel_login_prompts();self->broker.stop_all();self->status(ui::tr(L"Windows session locked. Agent control stopped."));}else if(wp==WTS_SESSION_UNLOCK)self->vault.set_locked(false);return 0;
      case WM_DESTROY:KillTimer(h,1);WTSUnRegisterSessionNotification(h);return 0;
      default:return DefWindowProcW(h,msg,wp,lp);
    }
  }
};
NativeUi::NativeUi(Broker& b,CefEngine& e,Vault& v,FilePolicy& f):impl_(std::make_unique<Impl>(b,e,v,f)){
  INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&common);
  WNDCLASSW wc{};wc.lpfnWndProc=Impl::proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"XenonControlCenter";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
  CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,ui::tr(L"Xenon Controls"),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,ui::dip(GetDesktopWindow(),1220),ui::dip(GetDesktopWindow(),810),nullptr,nullptr,wc.hInstance,impl_.get());
  e.set_save_prompt_callback([self=impl_.get()](const std::string& candidate,CefWindowHandle source){self->notify_login(candidate,source);});
  e.set_autofill_prompt_callback([self=impl_.get()](const Json& offer,CefWindowHandle source){self->notify_autofill(offer,source);});
  e.set_dialog_callback([self=impl_.get()](const std::string& tab_id){self->notify_dialog(tab_id);});
}
NativeUi::~NativeUi(){
  impl_->engine.set_save_prompt_callback({});impl_->engine.set_autofill_prompt_callback({});impl_->engine.set_dialog_callback({});
  {std::lock_guard lock(impl_->autofill_results->mutex);impl_->autofill_results->alive=false;impl_->autofill_results->values.clear();}
  {std::lock_guard lock(impl_->update_state->mutex);impl_->update_state->alive=false;impl_->update_state->cancel=true;}
  for(auto& config:impl_->configurations){if(config->window)DestroyWindow(config->window);if(config->face)DeleteObject(config->face);}
  impl_->close_updates();
  impl_->clear_login_prompts();impl_->clear_autofill();if(impl_->file_window)DestroyWindow(impl_->file_window);if(impl_->window)DestroyWindow(impl_->window);
  for(auto face:{impl_->font,impl_->heading_font,impl_->brand_font,impl_->small_font})if(face)DeleteObject(face);
  for(auto brush:{impl_->canvas_brush,impl_->surface_brush,impl_->navy_brush})if(brush)DeleteObject(brush);
}
void NativeUi::show(){impl_->apply_theme();impl_->refresh();ShowWindow(impl_->window,SW_SHOWNORMAL);SetForegroundWindow(impl_->window);}
void NativeUi::show_updates(){impl_->show_updates();}
void NativeUi::set_update_install_callback(std::function<void(std::shared_ptr<updates::InstallerLaunch>)> callback){impl_->update_install_callback=std::move(callback);}
bool NativeUi::pretranslate(MSG& message){
  if(message.message<WM_KEYFIRST||message.message>WM_KEYLAST)return false;
  const auto root=GetAncestor(message.hwnd,GA_ROOT);
  bool belongs=root==impl_->window||root==impl_->file_window||root==impl_->login_window||root==impl_->autofill_window||root==impl_->update_window;
  for(const auto& config:impl_->configurations)belongs=belongs||root==config->window;
  return belongs&&IsDialogMessageW(root,&message)!=FALSE;
}
}
