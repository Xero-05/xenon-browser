#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_sandbox_win.h"
#include "include/cef_preference.h"
#include "xenon/cef_engine.hpp"
#include "xenon/broker.hpp"
#include "xenon/pipe_server.hpp"
#include "xenon/vault.hpp"
#include "xenon/file_policy.hpp"
#include "xenon/native_ui.hpp"
#include "xenon/native_input_policy.hpp"
#include "xenon/local_security.hpp"
#include "xenon/workspace_storage.hpp"
#include "xenon/removal_fixture.hpp"
#include "xenon/autofill_fixture.hpp"
#include <windows.h>
#include <shlobj.h>
#include <charconv>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <map>

namespace xenon {
namespace {
void cleanup_removed_profiles(const std::filesystem::path& root,std::vector<std::string> workspaces){
  if(workspaces.empty())return;
  // Round-robin across permanent tombstones so a locked/refused profile never
  // prevents later workspaces from receiving bounded cleanup attempts.
  const auto cursor_path=root/"workspace-cleanup.cursor";
  try{
    if(std::filesystem::exists(cursor_path)&&std::filesystem::file_size(cursor_path)<=160){
      std::ifstream file(cursor_path,std::ios::binary);std::string cursor;std::getline(file,cursor);
      const auto prior=std::find(workspaces.begin(),workspaces.end(),cursor);
      if(prior!=workspaces.end())std::rotate(workspaces.begin(),std::next(prior),workspaces.end());
    }
    const auto result=cleanup_workspace_storage(root,workspaces);
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
    // Test-build diagnostics only; never inspect or write real profile data.
    if(root.parent_path().filename()==L".cache"&&root.filename().wstring().starts_with(L"auth-integration-removal-")){
      std::ifstream marker(root/"SYNTHETIC_TEST_PROFILE",std::ios::binary);
      const std::string contents{std::istreambuf_iterator<char>(marker),{}};
      if(contents=="XENON_SYNTHETIC_AUTH_FIXTURE\n"){
        Json entries=Json::array();
        for(const auto& entry:result.entries)entries.push_back({{"workspaceId",entry.workspace_id},{"status",workspace_cleanup_status_name(entry.status)},
          {"systemError",entry.system_error},{"filesRemoved",entry.files_removed},{"directoriesRemoved",entry.directories_removed}});
        const auto diagnostic=root/"native-cleanup-result.json";
        {std::ofstream file(diagnostic,std::ios::binary|std::ios::trunc);file<<Json{{"entries",entries},{"unprocessed",result.unprocessed}}.dump();}
        local_security::restrict_path(diagnostic);
      }
    }
#endif
    if(!result.entries.empty()){
      const auto temporary=root/"workspace-cleanup.cursor.tmp";
      {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);file<<result.entries.back().workspace_id;file.flush();if(!file)return;}
      local_security::restrict_path(temporary);
      MoveFileExW(temporary.c_str(),cursor_path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }
  }catch(const std::exception&){/* Tombstones remain; retry cleanup next launch. */}
}
class App final : public CefApp,public CefBrowserProcessHandler {
 public:
  App(std::filesystem::path root,Broker::Limits limits,std::wstring pipe,bool test_removal=false,bool test_autofill=false):root_(std::move(root)),limits_(limits),pipe_name_(std::move(pipe)),test_removal_enabled_(test_removal),test_autofill_enabled_(test_autofill){}
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler()override{return this;}
  void OnBeforeCommandLineProcessing(const CefString& process_type,CefRefPtr<CefCommandLine> command)override{
    if(process_type.empty()){
      // Chrome selects and opens its initial profile before OnContextInitialized,
      // and CEF uses that profile for the global request context. Never let a
      // previously active workspace (including a tombstone) become that context.
      command->RemoveSwitch("profile-directory");
      command->AppendSwitchWithValue("profile-directory","Default");
    }
    // Concurrent agent windows must keep painting while covered by another
    // window. This is a fixed startup policy, never an ownership/handoff change.
    // It trades additional background CPU/GPU use for reliable page input.
    command->AppendSwitch("disable-backgrounding-occluded-windows");
    command->AppendSwitch("disable-background-timer-throttling");
    // Chrome initializes its logger separately from CEF's log-severity setting.
    command->AppendSwitch("disable-logging");
    command->RemoveSwitch("restore-last-session");
  }
  void OnContextInitialized()override{
    try{
      // Chromium's self-restart flag overrides per-profile startup settings.
      // Xenon exposes recovery only through the explicit native restore action.
      auto preferences=CefPreferenceManager::GetGlobalPreferenceManager();
      if(preferences&&preferences->HasPreference("was.restarted")){
        auto value=CefValue::Create();value->SetBool(false);CefString error;
        if(!preferences->SetPreference("was.restarted",value,error))throw std::runtime_error("Blank startup policy unavailable");
      }
      engine_=std::make_unique<CefEngine>(root_);
      vault_=std::make_unique<Vault>(root_/"vault.sqlite3");
      files_=std::make_unique<FilePolicy>(root_/"downloads",std::vector<std::filesystem::path>{root_});
      engine_->set_vault(vault_.get());
      engine_->set_file_policy(files_.get());
      broker_=std::make_unique<Broker>(*engine_,root_,limits_);
      const auto removed=broker_->removed_workspaces();
      engine_->exclude_workspaces(removed);
      // OnContextInitialized runs only after CEF acquired this data root's
      // single-instance ownership, and before Xenon opens workspace contexts.
      cleanup_removed_profiles(root_,removed);
      native_=std::make_unique<NativeUi>(*broker_,*engine_,*vault_,*files_);
      engine_->set_controls_callback([this]{native_->show();});
      engine_->set_private_workspace_callback([this]{broker_->open_human_workspace("about:blank",[](Json){},true);});
      server_=std::make_unique<PipeServer>(*broker_,pipe_name_);server_->start();
      active_=this;
      engine_->set_native_key_callback([this](HWND page,UINT message,WPARAM key){native_key(page,message,key);});
      POINT pointer{};if(GetCursorPos(&pointer))input_policy_.seed_pointer(pointer.x,pointer.y);
      hook_=SetWindowsHookExW(WH_GETMESSAGE,InputHook,nullptr,GetCurrentThreadId());
      broker_->open_initial_human_workspace("about:blank",[](Json){});
      native_->show();
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
      if(test_removal_enabled_){
        removal_fixture_=std::make_shared<NativeRemovalFixture>(root_,*broker_);removal_fixture_->start();
      }
      if(test_autofill_enabled_){autofill_fixture_=std::make_shared<NativeAutofillFixture>(root_,*broker_,*engine_,*vault_);autofill_fixture_->start();}
#endif
    }catch(const std::exception&){MessageBoxW(nullptr,L"Xenon could not initialize its protected local state.",L"Xenon Browser",MB_OK|MB_ICONERROR);CefQuitMessageLoop();}
  }
  CefRefPtr<CefClient> GetDefaultClient()override{return engine_?engine_->default_client():nullptr;}
  CefRefPtr<CefRequestContextHandler> GetDefaultRequestContextHandler()override{return engine_?engine_->default_context_handler():nullptr;}
  bool OnAlreadyRunningAppRelaunch(CefRefPtr<CefCommandLine>,const CefString&)override{if(native_)native_->show();return true;}
  void stop(){
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
    removal_fixture_.reset();
    autofill_fixture_.reset();
#endif
    active_=nullptr;if(hook_){UnhookWindowsHookEx(hook_);hook_=nullptr;}
    if(input_timer_){KillTimer(nullptr,input_timer_);input_timer_=0;}
    if(engine_)engine_->set_native_key_callback({});
    if(server_)server_->stop();server_.reset();native_.reset();broker_.reset();engine_.reset();files_.reset();vault_.reset();
  }
 private:
  static LRESULT CALLBACK InputHook(int code,WPARAM wp,LPARAM lp){
    if(code>=0&&wp==PM_REMOVE&&active_&&active_->engine_){
      active_->native_message(*reinterpret_cast<MSG*>(lp));
    }
    return CallNextHookEx(nullptr,code,wp,lp);
  }
  static bool chrome_window(HWND window){
    wchar_t name[128]{};GetClassNameW(GetAncestor(window,GA_ROOT),name,128);
    return wcsncmp(name,L"Chrome_WidgetWin_",17)==0;
  }
  static HWND legacy_page(HWND window){
    // Pinned Chromium154 LegacyRenderWidgetHostHWND forwards original queued
    // mouse/keyboard messages to its Aura parent. RenderWidgetHostViewAura keeps
    // its native rectangle equal to the WebContents bounds and hides/reparents
    // it when the renderer is hidden. No page script or accessibility read is
    // needed to distinguish it from the omnibox and native browser controls.
    const auto root=GetAncestor(window,GA_ROOT);
    if(!chrome_window(root))return nullptr;
    for(unsigned depth=0;window&&window!=root&&depth<16;++depth,window=GetParent(window)){
      wchar_t name[128]{};GetClassNameW(window,name,128);
      if(wcscmp(name,L"Chrome_RenderWidgetHostHWND")==0&&IsWindowVisible(window))return window;
    }
    return nullptr;
  }
  static HWND mouse_page(const MSG& message){
    const auto root=GetAncestor(message.hwnd,GA_ROOT);
    if(!chrome_window(root))return nullptr;
    if(auto page=legacy_page(message.hwnd)){
      RECT bounds{};
      if(GetWindowRect(page,&bounds)&&PtInRect(&bounds,message.pt))return page;
      return nullptr;
    }
    // Aura capture and Windows wheel routing can address the root window.
    // Only a visible page child under this queued screen position qualifies.
    // Never substitute a live cursor position or classify the whole Chrome root.
    HWND window=message.hwnd;
    for(unsigned depth=0;window&&depth<16;++depth){
      POINT point=message.pt;if(!ScreenToClient(window,&point))return nullptr;
      const auto child=ChildWindowFromPointEx(window,point,CWP_SKIPINVISIBLE|CWP_SKIPDISABLED);
      if(!child||child==window)return nullptr;
      if(auto page=legacy_page(child))return page;
      window=child;
    }
    return nullptr;
  }
  void emit_input(const std::optional<NativeInputPolicy::Activity>& activity){
    if(activity&&engine_)engine_->native_input(reinterpret_cast<HWND>(activity->window),activity->busy,activity->credential_input,activity->substantive);
  }
  void emit_input(const std::vector<NativeInputPolicy::Activity>& activities){
    for(const auto& activity:activities)emit_input(std::optional{activity});
  }
  void update_input_timer(){
    if(input_policy_.tracking()&&!input_timer_)input_timer_=SetTimer(nullptr,0,50,InputTimer);
    else if(!input_policy_.tracking()&&input_timer_){KillTimer(nullptr,input_timer_);input_timer_=0;}
  }
  static void CALLBACK InputTimer(HWND,UINT,UINT_PTR id,DWORD){
    if(!active_||active_->input_timer_!=id)return;
    active_->emit_input(active_->input_policy_.reconcile(
      [](unsigned key){return (GetAsyncKeyState(static_cast<int>(key))&0x8000)!=0;},
      [](NativeInputPolicy::Window page){return IsWindow(reinterpret_cast<HWND>(page))!=FALSE;}));
    active_->update_input_timer();
  }
  static std::vector<unsigned> held_modifiers(){
    std::vector<unsigned> result;
    for(const auto key:{VK_SHIFT,VK_CONTROL,VK_MENU})if(GetKeyState(key)&0x8000)result.push_back(static_cast<unsigned>(key));
    return result;
  }
  void native_key(HWND page,UINT message,WPARAM key){
    // CEF invokes this only for a native OS key event headed to the renderer.
    // Aura's top-level keyboard HWND alone cannot distinguish page and omnibox.
    page=GetAncestor(page,GA_ROOT);
    if(!page)return;
    const auto target=reinterpret_cast<NativeInputPolicy::Window>(page);
    if(message==WM_KEYDOWN||message==WM_SYSKEYDOWN){
      const bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0;
      const bool alt=(GetKeyState(VK_MENU)&0x8000)!=0;
      // CEF's pre-key callback runs before Chrome's accelerator handler. These
      // known native-focus shortcuts must remain passive even when that callback
      // first sees them. Page navigation keys (F5, Alt+Left) still count as input.
      if(native_modifier_key(static_cast<unsigned>(key)))return;
      if(native_browser_focus_shortcut(static_cast<unsigned>(key),control,alt)||
         (key=='X'&&control&&(GetKeyState(VK_SHIFT)&0x8000))){keyboard_pages_.erase(page);return;}
      prune_keyboard_pages();
      keyboard_pages_[page]={page,GetTickCount64()};
      emit_input(input_policy_.press(target,static_cast<unsigned>(key),false,true,held_modifiers()));
    }else if(message==WM_KEYUP||message==WM_SYSKEYUP){
      emit_input(input_policy_.release(static_cast<unsigned>(key)));
    }
    update_input_timer();
  }
  void native_message(const MSG& message){
    prune_keyboard_pages();
    const auto root=GetAncestor(message.hwnd,GA_ROOT);
    const bool move=message.message==WM_MOUSEMOVE||message.message==WM_NCMOUSEMOVE;
    if(move){
      // Hover stays passive on every surface. Only movement during a gesture
      // that began on a page extends that page's existing pause.
      emit_input(input_policy_.move(message.pt.x,message.pt.y));
      return;
    }
    const auto page_for_mouse=[&]{
      const auto page=mouse_page(message);
      return reinterpret_cast<NativeInputPolicy::Window>(page?GetAncestor(page,GA_ROOT):nullptr);
    };
    const auto press=[&](unsigned button){
      const auto page=page_for_mouse();
      if(!page)keyboard_pages_.erase(root);
      emit_input(input_policy_.press(page,button,true,button==VK_LBUTTON||button==VK_RBUTTON,held_modifiers()));
    };
    switch(message.message){
      case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK:press(VK_LBUTTON);break;
      case WM_RBUTTONDOWN:case WM_RBUTTONDBLCLK:press(VK_RBUTTON);break;
      case WM_MBUTTONDOWN:case WM_MBUTTONDBLCLK:press(VK_MBUTTON);break;
      case WM_XBUTTONDOWN:case WM_XBUTTONDBLCLK:press(HIWORD(message.wParam)==XBUTTON1?VK_XBUTTON1:VK_XBUTTON2);break;
      case WM_LBUTTONUP:emit_input(input_policy_.release(VK_LBUTTON));break;
      case WM_RBUTTONUP:emit_input(input_policy_.release(VK_RBUTTON));break;
      case WM_MBUTTONUP:emit_input(input_policy_.release(VK_MBUTTON));break;
      case WM_XBUTTONUP:emit_input(input_policy_.release(HIWORD(message.wParam)==XBUTTON1?VK_XBUTTON1:VK_XBUTTON2));break;
      case WM_NCLBUTTONDOWN:case WM_NCRBUTTONDOWN:case WM_NCMBUTTONDOWN:case WM_NCXBUTTONDOWN:keyboard_pages_.erase(root);break;
      case WM_MOUSEWHEEL:case WM_MOUSEHWHEEL:emit_input(input_policy_.pulse(page_for_mouse()));break;
      case WM_KEYUP:case WM_SYSKEYUP:emit_input(input_policy_.release(static_cast<unsigned>(message.wParam)));break;
      case WM_KEYDOWN:case WM_SYSKEYDOWN:{
        const auto key=message.wParam;
        const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        const bool alt=(GetKeyState(VK_MENU)&0x8000)!=0;
        if(native_browser_focus_shortcut(static_cast<unsigned>(key),ctrl,alt))keyboard_pages_.erase(root);
        if(chrome_window(root)&&key=='X'&&ctrl&&(GetKeyState(VK_SHIFT)&0x8000))native_->show();
        break;
      }
      case WM_IME_STARTCOMPOSITION:case WM_IME_COMPOSITION:{
        auto page=legacy_page(message.hwnd);
        if(!page){
          const auto entry=keyboard_pages_.find(root);
          if(entry!=keyboard_pages_.end()&&GetAncestor(GetFocus(),GA_ROOT)==root)page=entry->second.page;
        }
        const auto target=reinterpret_cast<NativeInputPolicy::Window>(page?GetAncestor(page,GA_ROOT):nullptr);
        emit_input(message.message==WM_IME_STARTCOMPOSITION?input_policy_.composition_start(target):input_policy_.composition_update(target));
        break;
      }
      case WM_IME_ENDCOMPOSITION:emit_input(input_policy_.composition_end());break;
      default:break;
    }
    update_input_timer();
  }
  void prune_keyboard_pages(){
    const auto now=GetTickCount64();
    for(auto entry=keyboard_pages_.begin();entry!=keyboard_pages_.end();){
      // This is only evidence for starting a new IME composition. Once started,
      // the policy keeps composition held independently until its end message.
      if(!IsWindow(entry->first)||now-entry->second.observed_at>5000)entry=keyboard_pages_.erase(entry);
      else ++entry;
    }
  }
  static inline App* active_=nullptr;
  std::filesystem::path root_;Broker::Limits limits_;std::wstring pipe_name_;bool test_removal_enabled_{},test_autofill_enabled_{};HHOOK hook_{};
  NativeInputPolicy input_policy_;
  struct KeyboardPage {HWND page{};ULONGLONG observed_at{};};
  std::map<HWND,KeyboardPage> keyboard_pages_;
  UINT_PTR input_timer_{};
  std::unique_ptr<CefEngine> engine_;std::unique_ptr<Vault> vault_;std::unique_ptr<FilePolicy> files_;
  std::unique_ptr<Broker> broker_;std::unique_ptr<NativeUi> native_;std::unique_ptr<PipeServer> server_;
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
  std::shared_ptr<NativeRemovalFixture> removal_fixture_;
  std::shared_ptr<NativeAutofillFixture> autofill_fixture_;
#endif
  IMPLEMENT_REFCOUNTING(App);
};
int run(HINSTANCE instance,void* sandbox_info){
  if(!sandbox_info){MessageBoxW(nullptr,L"Xenon requires the matching sandbox bootstrap executable.",L"Xenon Browser",MB_OK|MB_ICONERROR);return 1;}
  CefMainArgs args(instance);int child=CefExecuteProcess(args,nullptr,sandbox_info);if(child>=0)return child;
  SetCurrentProcessExplicitAppUserModelID(L"Xenon.Browser");
  auto command=CefCommandLine::CreateCommandLine();command->InitFromString(GetCommandLineW());
  Broker::Limits limits;
  std::wstring pipe_name=L"xenon-browser";
  if(command->HasSwitch("broker-pipe")){
    const auto value=command->GetSwitchValue("broker-pipe").ToString();
    if(value.size()>100||!value.starts_with("xenon-")||!std::all_of(value.begin(),value.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_';})){
      MessageBoxW(nullptr,L"--broker-pipe requires a name beginning xenon- with at most 100 letters, digits, hyphens or underscores.",L"Xenon Browser",MB_OK|MB_ICONERROR);return 1;
    }
    pipe_name.assign(value.begin(),value.end());
  }
  if(command->HasSwitch("max-concurrent-workers")){
    const auto value=command->GetSwitchValue("max-concurrent-workers").ToString();
    size_t count{};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),count);
    if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||count<1||count>256){
      MessageBoxW(nullptr,L"--max-concurrent-workers requires an integer from 1 to 256.",L"Xenon Browser",MB_OK|MB_ICONERROR);return 1;
    }
    limits.max_connected_workers=count;
  }
  std::filesystem::path root;
  if(command->HasSwitch("user-data-dir"))root=std::filesystem::absolute(command->GetSwitchValue("user-data-dir").ToWString());
  else{PWSTR local{};if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))return 1;root=std::filesystem::path(local)/"Xenon Browser";CoTaskMemFree(local);}
  std::filesystem::create_directories(root);local_security::restrict_path(root);
  CefSettings settings;settings.no_sandbox=false;settings.command_line_args_disabled=true;
  settings.persist_session_cookies=true;
  CefString(&settings.root_cache_path)=root.wstring();CefString(&settings.cache_path)=(root/"Default").wstring();
  settings.log_severity=LOGSEVERITY_DISABLE;
  bool test_removal=false,test_autofill=false;
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
  test_removal=command->HasSwitch("test-native-removal");
  test_autofill=command->HasSwitch("test-native-autofill");
#endif
  CefRefPtr<App> app=new App(root,limits,pipe_name,test_removal,test_autofill);
  if(!CefInitialize(args,settings,app,sandbox_info))return CefGetExitCode();
  CefRunMessageLoop();app->stop();app=nullptr;CefShutdown();return 0;
}
}
}
CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance,LPWSTR,int,void* sandbox_info,cef_version_info_t*) {
  try{return xenon::run(instance,sandbox_info);}catch(const std::exception&){MessageBoxW(nullptr,L"Xenon could not start. Verify the installation and local data directory.",L"Xenon Browser",MB_OK|MB_ICONERROR);return 1;}
}
