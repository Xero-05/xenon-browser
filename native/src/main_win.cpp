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
#include "xenon/browser_shell.hpp"
#include "xenon/ui_theme.hpp"
#include "xenon/introduction.hpp"
#include "xenon/native_input_policy.hpp"
#include "xenon/local_security.hpp"
#include "xenon/workspace_storage.hpp"
#include "xenon/removal_fixture.hpp"
#include "xenon/autofill_fixture.hpp"
#include "xenon/updater.hpp"
#include "xenon/extension_store.hpp"
#include "xenon/launch_urls.hpp"
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
// Web addresses in a launch command line (Xenon as the default browser).
// A launch with links while Xenon already runs for the same data folder hands
// them to that browser through its message-only window, named for the folder.
// Any program the person runs could launch Xenon with a web link anyway, so the
// receiver accepts only the ordinary web links launch_urls admits.
constexpr ULONG_PTR launch_message=0x58454E4C;
std::wstring instance_name(const std::filesystem::path& root){
  auto identity=std::filesystem::weakly_canonical(root).wstring();CharLowerBuffW(identity.data(),static_cast<DWORD>(identity.size()));
  const auto digest=local_security::sha256(ui::utf8(identity));return L"XenonLaunch-"+std::wstring(digest.begin(),digest.end());
}
bool hand_off(const std::filesystem::path& root,const std::vector<std::string>& urls){
  const auto window=FindWindowExW(HWND_MESSAGE,nullptr,L"XenonLaunch",instance_name(root).c_str());if(!window)return false;
  // The running browser may bring its window forward for the person's link.
  DWORD process{};GetWindowThreadProcessId(window,&process);if(process)AllowSetForegroundWindow(process);
  const auto payload=Json(urls).dump();COPYDATASTRUCT data{launch_message,static_cast<DWORD>(payload.size()),const_cast<char*>(payload.data())};DWORD_PTR result{};
  return SendMessageTimeoutW(window,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&data),SMTO_ABORTIFHUNG|SMTO_BLOCK,10000,&result)&&result==TRUE;
}
// After --single-argument the rest of the line is one link, however it would
// otherwise split into arguments.
std::vector<std::string> command_urls(CefRefPtr<CefCommandLine> command){
  if(const auto single=single_argument(command->GetCommandLineString().ToWString());!single.empty())return launch_urls({ui::utf8(single)});
  std::vector<std::string> candidates;std::vector<CefString> arguments;command->GetArguments(arguments);for(const auto& argument:arguments)candidates.push_back(argument.ToString());
  return launch_urls(candidates);
}
class App final : public CefApp,public CefBrowserProcessHandler {
 public:
  App(std::filesystem::path root,Broker::Limits limits,std::wstring pipe,std::unique_ptr<ExtensionStore> extensions,bool test_removal=false,bool test_autofill=false,bool open_documentation=false,std::vector<std::string> launch={}):root_(std::move(root)),limits_(limits),pipe_name_(std::move(pipe)),extensions_(std::move(extensions)),test_removal_enabled_(test_removal),test_autofill_enabled_(test_autofill),open_documentation_(open_documentation),launch_urls_(std::move(launch)){
    // Read once, before Chromium starts. Later changes apply after restart.
    try{extension_paths_=extensions_->startup_paths();}catch(const std::exception&){extension_paths_.clear();}
  }
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler()override{return this;}
  void OnBeforeCommandLineProcessing(const CefString& process_type,CefRefPtr<CefCommandLine> command)override{
    if(process_type.empty()){
      // Chrome selects and opens its initial profile before OnContextInitialized,
      // and CEF uses that profile for the global request context. Never let a
      // previously active workspace (including a tombstone) become that context.
      command->RemoveSwitch("profile-directory");
      command->AppendSwitchWithValue("profile-directory","Default");
      // SetChromeColorScheme does not reach pages in native Alloy hosts. Force
      // Light/Dark for web content here; System leaves Windows in control.
      command->RemoveSwitch("force-light-mode");command->RemoveSwitch("force-dark-mode");
      ui::web_theme_mode=ui::theme_mode;
      if(const auto scheme=ui::web_color_scheme_switch(ui::web_theme_mode))command->AppendSwitch(scheme);
      // Human-installed unpacked extensions from Xenon's managed copies. They
      // load into every persistent workspace profile; MCP cannot change them.
      command->RemoveSwitch("load-extension");
      if(!extension_paths_.empty()){
        std::wstring folders;for(const auto& path:extension_paths_){if(!folders.empty())folders+=L",";folders+=path.wstring();}
        command->AppendSwitchWithValue("load-extension",folders);
        auto features=command->GetSwitchValue("disable-features").ToWString();
        command->RemoveSwitch("disable-features");command->AppendSwitchWithValue("disable-features",(features.empty()?std::wstring{}:features+L",")+L"DisableLoadExtensionCommandLineSwitch");
      }
    }
    // Concurrent agent windows must keep painting while covered by another
    // window. This is a fixed startup policy, never an ownership/handoff change.
    // It trades additional background CPU/GPU use for reliable page input.
    // Idle covered tabs still paint, at a reduced rate (TabRenderPacing).
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
      // The vault outlives the broker (see stop()), and its lookups never call the broker.
      broker_->set_account_directory([vault=vault_.get()](const std::string& account){return vault->account_metadata(account);});
      const auto removed=broker_->removed_workspaces();
      engine_->exclude_workspaces(removed);
      // OnContextInitialized runs only after CEF acquired this data root's
      // single-instance ownership, and before Xenon opens workspace contexts.
      cleanup_removed_profiles(root_,removed);
      engine_->set_extension_ids(extensions_->active_ids());
      native_=std::make_unique<NativeUi>(*broker_,*engine_,*vault_,*files_,*extensions_);
      shell_=std::make_unique<BrowserShell>(*broker_,*engine_,*native_,*extensions_,root_);
      engine_->set_open_link_callback([this](const std::string& tab,const std::string& url,bool window){if(shell_)shell_->open_link(tab,url,window);});
      engine_->set_host_callbacks([this](const std::string& workspace,const std::string& tab,bool human,const std::string& opener){return shell_->create_host(workspace,tab,human,opener);},
        [this](const std::string& tab,HWND browser){shell_->tab_created(tab,browser);},[this](const std::string& tab){shell_->tab_closed(tab);});
      engine_->set_download_callback([this](const std::string& tab){return broker_->allow_download(tab);});
      engine_->set_passkey_control_callback([this](const std::string& tab){return broker_->human_controls(tab);});
      engine_->set_permission_callback([this](const std::string& tab,const std::string& origin,const std::string& description,std::function<void(bool)> answer){shell_->permission(tab,origin,description,std::move(answer));});
      engine_->set_fullscreen_callback([this](const std::string& tab,bool fullscreen){if(shell_)shell_->page_fullscreen(tab,fullscreen);});
      engine_->set_external_link_callback([this](const std::string& tab,const std::string& origin,const std::string& url,const std::string& scheme,bool permitted,std::function<void(bool)> done){
        if(shell_)shell_->external_link(tab,origin,url,scheme,permitted,std::move(done));else done(false);});
      engine_->set_screen_share_callback([this](const std::string& tab,const std::string& origin,bool audio,std::function<void(bool)> answer){
        if(shell_)shell_->screen_share(tab,origin,audio,std::move(answer));else answer(false);});
      broker_->set_ui_state_callback([this]{if(shell_)shell_->refresh();});
      engine_->set_controls_callback([this]{native_->show();});
      engine_->set_updates_callback([this]{native_->show_updates();});
      native_->set_update_install_callback([this](std::shared_ptr<updates::InstallerLaunch> installer){
        if(pending_update_)return;pending_update_=std::move(installer);shell_->request_exit();
      });
      engine_->set_private_workspace_callback([this]{broker_->open_human_workspace("about:blank",[](Json){},true);});
      server_=std::make_unique<PipeServer>(*broker_,pipe_name_);server_->start();
      active_=this;
      engine_->set_native_key_callback([this](HWND page,UINT message,WPARAM key){native_key(page,message,key);});
      POINT pointer{};if(GetCursorPos(&pointer))input_policy_.seed_pointer(pointer.x,pointer.y);
      hook_=SetWindowsHookExW(WH_GETMESSAGE,InputHook,nullptr,GetCurrentThreadId());
      // A launch from Windows with a link opens it in the first Personal tab.
      const bool first_link=!open_documentation_&&!launch_urls_.empty();
      broker_->open_initial_human_workspace(open_documentation_?ui::utf8(ui::documentation_url):first_link?launch_urls_.front():"about:blank",[](Json){});
      shell_->show();
      for(size_t n=first_link?1:0;n<launch_urls_.size();++n)shell_->open_external_url(launch_urls_[n]);
      WNDCLASSW launch{};launch.lpfnWndProc=LaunchProc;launch.hInstance=branding_module();launch.lpszClassName=L"XenonLaunch";RegisterClassW(&launch);
      launch_window_=CreateWindowExW(0,L"XenonLaunch",instance_name(root_).c_str(),0,0,0,0,0,HWND_MESSAGE,nullptr,branding_module(),nullptr);
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
      shell_->fixture_snapshot(root_);
      if(test_removal_enabled_){
        removal_fixture_=std::make_shared<NativeRemovalFixture>(root_,*broker_);removal_fixture_->start();
      }
      if(test_autofill_enabled_){autofill_fixture_=std::make_shared<NativeAutofillFixture>(root_,*broker_,*engine_,*vault_);autofill_fixture_->start();}
#endif
    }catch(const std::exception&){MessageBoxW(nullptr,ui::tr(L"Xenon could not initialize its protected local state."),ui::tr(L"Xenon Browser"),MB_OK|MB_ICONERROR);CefQuitMessageLoop();}
  }
  CefRefPtr<CefClient> GetDefaultClient()override{return engine_?engine_->default_client():nullptr;}
  CefRefPtr<CefRequestContextHandler> GetDefaultRequestContextHandler()override{return engine_?engine_->default_context_handler():nullptr;}
  // With command-line arguments disabled, CEF forwards only its own switches
  // here, never a launch's links; those arrive through hand_off instead.
  bool OnAlreadyRunningAppRelaunch(CefRefPtr<CefCommandLine>,const CefString&)override{if(shell_)shell_->show();return true;}
  std::shared_ptr<updates::InstallerLaunch> pending_update()const{return pending_update_;}
  void stop(){
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
    removal_fixture_.reset();
    autofill_fixture_.reset();
#endif
    if(launch_window_){DestroyWindow(launch_window_);launch_window_=nullptr;}
    active_=nullptr;if(hook_){UnhookWindowsHookEx(hook_);hook_=nullptr;}
    if(input_timer_){KillTimer(nullptr,input_timer_);input_timer_=0;}
    if(engine_)engine_->set_native_key_callback({});
    if(engine_){engine_->set_controls_callback({});engine_->set_updates_callback({});engine_->set_private_workspace_callback({});engine_->set_open_link_callback({});}
    if(broker_)broker_->set_ui_state_callback({});
    if(engine_){engine_->set_host_callbacks({},{},{});engine_->set_download_callback({});engine_->set_passkey_control_callback({});engine_->set_permission_callback({});
      engine_->set_fullscreen_callback({});engine_->set_external_link_callback({});engine_->set_screen_share_callback({});}
    if(server_)server_->stop();server_.reset();native_.reset();shell_.reset();broker_.reset();engine_.reset();files_.reset();vault_.reset();
  }
 private:
  // Links from another launch open after the sending process's call returns.
  static LRESULT CALLBACK LaunchProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_COPYDATA){const auto data=reinterpret_cast<const COPYDATASTRUCT*>(lp);
      if(!active_||!data||data->dwData!=launch_message||!data->lpData||data->cbData>65536)return FALSE;
      const auto value=Json::parse(std::string(static_cast<const char*>(data->lpData),data->cbData),nullptr,false);if(!value.is_array())return FALSE;
      std::vector<std::string> candidates;for(const auto& item:value)if(item.is_string()&&candidates.size()<32)candidates.push_back(item.get<std::string>());
      const auto urls=launch_urls(candidates);if(urls.empty())return FALSE;
      for(const auto& url:urls)if(active_->handed_urls_.size()<32)active_->handed_urls_.push_back(url);PostMessageW(window,WM_APP,0,0);return TRUE;}
    if(message==WM_APP&&active_){auto urls=std::exchange(active_->handed_urls_,{});if(active_->shell_){if(urls.empty())active_->shell_->show();for(const auto& url:urls)active_->shell_->open_external_url(url);}return 0;}
    return DefWindowProcW(window,message,wp,lp);
  }
  static LRESULT CALLBACK InputHook(int code,WPARAM wp,LPARAM lp){
    if(code>=0&&wp==PM_REMOVE&&active_&&active_->engine_){
      auto& message=*reinterpret_cast<MSG*>(lp);
      if((active_->native_&&active_->native_->pretranslate(message))||(active_->shell_&&active_->shell_->pretranslate(message))){message.message=WM_NULL;return CallNextHookEx(nullptr,code,wp,lp);}
      active_->native_message(message);
    }
    return CallNextHookEx(nullptr,code,wp,lp);
  }
  static bool chrome_window(HWND window){
    wchar_t name[128]{};GetClassNameW(GetAncestor(window,GA_ROOT),name,128);
    return wcscmp(name,L"XenonBrowserShell")==0;
  }
  static HWND tab_host(HWND window){for(unsigned depth=0;window&&depth<20;++depth,window=GetParent(window)){wchar_t name[128]{};GetClassNameW(window,name,128);if(wcscmp(name,L"XenonTabHost")==0)return window;}return nullptr;}
  static HWND legacy_page(HWND window){
    // Pinned Chromium154 LegacyRenderWidgetHostHWND forwards original queued
    // mouse/keyboard messages to its Aura parent. RenderWidgetHostViewAura keeps
    // its native rectangle equal to the WebContents bounds and hides/reparents
    // it when the renderer is hidden. No page script or accessibility read is
    // needed to distinguish it from the omnibox and native browser controls.
    return tab_host(window);
  }
  static HWND mouse_page(const MSG& message){
    const auto root=GetAncestor(message.hwnd,GA_ROOT);
    if(!chrome_window(root)||GetPropW(root,L"XenonSidebarDrag"))return nullptr;
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
    if(activity&&engine_)engine_->native_input(reinterpret_cast<HWND>(activity->window),activity->busy,activity->credential_input,activity->substantive,activity->primary_click);
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
    page=tab_host(page);
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
      if(auto page=mouse_page(message))engine_->native_pointer(page,message.pt,input_policy_.tracking());
      return;
    }
    const auto page_for_mouse=[&]{
      const auto page=mouse_page(message);
      if(page)engine_->native_pointer(page,message.pt,true);
      return reinterpret_cast<NativeInputPolicy::Window>(page);
    };
    const auto press=[&](unsigned button){
      const auto page=page_for_mouse();
      if(!page)keyboard_pages_.erase(root);
      auto activity=input_policy_.press(page,button,true,button==VK_LBUTTON||button==VK_RBUTTON,held_modifiers());
      if(activity)activity->primary_click=button==VK_LBUTTON;
      emit_input(activity);
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
          const auto entry=keyboard_pages_.find(tab_host(GetFocus()));
          if(entry!=keyboard_pages_.end()&&GetAncestor(GetFocus(),GA_ROOT)==root)page=entry->second.page;
        }
        const auto target=reinterpret_cast<NativeInputPolicy::Window>(page);
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
  std::filesystem::path root_;Broker::Limits limits_;std::wstring pipe_name_;std::unique_ptr<ExtensionStore> extensions_;std::vector<std::filesystem::path> extension_paths_;bool test_removal_enabled_{},test_autofill_enabled_{},open_documentation_{};std::vector<std::string> launch_urls_,handed_urls_;HWND launch_window_{};HHOOK hook_{};
  NativeInputPolicy input_policy_;
  struct KeyboardPage {HWND page{};ULONGLONG observed_at{};};
  std::map<HWND,KeyboardPage> keyboard_pages_;
  UINT_PTR input_timer_{};
  std::unique_ptr<CefEngine> engine_;std::unique_ptr<Vault> vault_;std::unique_ptr<FilePolicy> files_;
  std::unique_ptr<Broker> broker_;std::unique_ptr<NativeUi> native_;std::unique_ptr<PipeServer> server_;
  std::unique_ptr<BrowserShell> shell_;
  std::shared_ptr<updates::InstallerLaunch> pending_update_;
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
  std::shared_ptr<NativeRemovalFixture> removal_fixture_;
  std::shared_ptr<NativeAutofillFixture> autofill_fixture_;
#endif
  IMPLEMENT_REFCOUNTING(App);
};
int run(HINSTANCE instance,void* sandbox_info){
  if(!sandbox_info){MessageBoxW(nullptr,ui::tr(L"Xenon requires the matching sandbox bootstrap executable."),ui::tr(L"Xenon Browser"),MB_OK|MB_ICONERROR);return 1;}
  CefMainArgs args(instance);int child=CefExecuteProcess(args,nullptr,sandbox_info);if(child>=0)return child;
  // Start native vector drawing only in the browser process. Shut it down
  // after the UI closes, outside DLL loader callbacks.
  ui::VectorRenderer vector_renderer;
  // Installation waits for normal browser shutdown. Creating this before
  // checking SetupMutex closes the start/upgrade race without killing a tab.
  struct RunningMarker {
    HANDLE handle{};
    RunningMarker(){
      local_security::SecurityDescriptor policy;
      handle=CreateMutexW(&policy.attributes,FALSE,L"Local\\XenonBrowserRunning");
      if(!handle)throw std::runtime_error("Cannot protect the running installation");
    }
    void release(){if(handle){CloseHandle(handle);handle=nullptr;}}
    ~RunningMarker(){release();}
  } running_marker;
  const auto setup=OpenMutexW(SYNCHRONIZE,FALSE,L"Local\\XenonBrowserSetup");
  const auto setup_error=GetLastError();
  if(setup||setup_error!=ERROR_FILE_NOT_FOUND){
    if(setup)CloseHandle(setup);
    MessageBoxW(nullptr,ui::tr(L"Xenon setup is open. Finish or cancel setup before starting the browser."),ui::tr(L"Xenon Browser"),MB_OK|MB_ICONINFORMATION);
    return 1;
  }
  SetCurrentProcessExplicitAppUserModelID(L"Xenon.Browser");
  auto command=CefCommandLine::CreateCommandLine();command->InitFromString(GetCommandLineW());
  Broker::Limits limits;
  std::wstring pipe_name=L"xenon-browser";
  if(command->HasSwitch("broker-pipe")){
    const auto value=command->GetSwitchValue("broker-pipe").ToString();
    if(value.size()>100||!value.starts_with("xenon-")||!std::all_of(value.begin(),value.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_';})){
      MessageBoxW(nullptr,ui::tr(L"--broker-pipe requires a name beginning xenon- with at most 100 letters, digits, hyphens or underscores."),ui::tr(L"Xenon Browser"),MB_OK|MB_ICONERROR);return 1;
    }
    pipe_name.assign(value.begin(),value.end());
  }
  if(command->HasSwitch("max-concurrent-workers")){
    const auto value=command->GetSwitchValue("max-concurrent-workers").ToString();
    size_t count{};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),count);
    if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||count<1||count>256){
      MessageBoxW(nullptr,ui::tr(L"--max-concurrent-workers requires an integer from 1 to 256."),ui::tr(L"Xenon Browser"),MB_OK|MB_ICONERROR);return 1;
    }
    limits.max_connected_workers=count;
  }
  std::filesystem::path root;
  if(command->HasSwitch("user-data-dir"))root=std::filesystem::absolute(command->GetSwitchValue("user-data-dir").ToWString());
  else{PWSTR local{};if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))return 1;root=std::filesystem::path(local)/"Xenon Browser";CoTaskMemFree(local);}
  // Links for a Xenon already running on this data folder open there.
  const auto launch=command_urls(command);
  if(!launch.empty()&&hand_off(root,launch))return 0;
  // Serialize startup through the introduction and CEF's profile acquisition.
  // A second launch cannot open CEF against a half-finished first-run choice.
  struct StartupGate {
    HANDLE handle{};bool acquired{};
    explicit StartupGate(const std::filesystem::path& path){
      auto identity=std::filesystem::weakly_canonical(path).wstring();CharLowerBuffW(identity.data(),static_cast<DWORD>(identity.size()));
      const auto digest=local_security::sha256(ui::utf8(identity));const auto name=L"Local\\XenonStartup-"+std::wstring(digest.begin(),digest.end());
      local_security::SecurityDescriptor policy;handle=CreateMutexW(&policy.attributes,FALSE,name.c_str());
      if(!handle)throw std::runtime_error("Cannot protect browser startup");
      const auto wait=WaitForSingleObject(handle,0);acquired=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;
      if(wait==WAIT_FAILED){CloseHandle(handle);handle=nullptr;throw std::runtime_error("Cannot acquire browser startup");}
    }
    bool wait(DWORD milliseconds){if(!handle||acquired)return acquired;const auto result=WaitForSingleObject(handle,milliseconds);acquired=result==WAIT_OBJECT_0||result==WAIT_ABANDONED;return acquired;}
    void release(){if(handle){if(acquired)ReleaseMutex(handle);CloseHandle(handle);handle=nullptr;}}
    ~StartupGate(){release();}
  } startup_gate(root);
  if(!startup_gate.acquired){
    // Another start of this data folder is under way. A launch with links
    // waits for it to finish, then hands them over.
    if(!launch.empty()&&startup_gate.wait(60000)){startup_gate.release();for(int attempt=0;attempt<100;++attempt){if(hand_off(root,launch))return 0;Sleep(100);}}
    return 0;
  }
  const bool first_run=ui::needs_introduction(root);
  std::filesystem::create_directories(root);local_security::restrict_path(root);
  ui::load_theme(root);
  bool open_documentation=false;
  if(first_run){
    const auto introduction=ui::show_introduction();
    if(introduction==ui::IntroductionResult::cancelled)return 0;
    // Apply the successfully persisted choice through the same loading path as
    // subsequent launches, before either native chrome or CEF reads its locale.
    ui::load_theme(root);
    open_documentation=introduction==ui::IntroductionResult::documentation;
  }
  CefSettings settings;settings.no_sandbox=false;settings.command_line_args_disabled=true;
  CefString(&settings.locale)=ui::language_tag(ui::language);
  settings.persist_session_cookies=true;
  CefString(&settings.root_cache_path)=root.wstring();CefString(&settings.cache_path)=(root/"Default").wstring();
  settings.log_severity=LOGSEVERITY_DISABLE;
  bool test_removal=false,test_autofill=false;
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
  test_removal=command->HasSwitch("test-native-removal");
  test_autofill=command->HasSwitch("test-native-autofill");
#endif
  CefRefPtr<App> app=new App(root,limits,pipe_name,std::make_unique<ExtensionStore>(root),test_removal,test_autofill,open_documentation,launch);
  if(!CefInitialize(args,settings,app,sandbox_info))return CefGetExitCode();
  startup_gate.release();
  CefRunMessageLoop();auto installer=app->pending_update();app->stop();app=nullptr;CefShutdown();
  running_marker.release();
  if(installer){try{installer->start();}catch(const std::exception&){MessageBoxW(nullptr,ui::tr(L"Xenon closed, but setup could not start. Open Xenon and try the update again."),ui::tr(L"Xenon update"),MB_OK|MB_ICONERROR);return 1;}}
  return 0;
}
}
}
CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance,LPWSTR,int,void* sandbox_info,cef_version_info_t*) {
  try{return xenon::run(instance,sandbox_info);}catch(const std::exception&){MessageBoxW(nullptr,xenon::ui::tr(L"Xenon could not start. Verify the installation and local data directory."),xenon::ui::tr(L"Xenon Browser"),MB_OK|MB_ICONERROR);return 1;}
}
