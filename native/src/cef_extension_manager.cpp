#include "xenon/cef_extension_manager.hpp"
#include "xenon/cef_extension_catalog.hpp"
#include "xenon/extension_manager_policy.hpp"
#include "xenon/extension_manager_ui.hpp"
#include "xenon/cef_branding.hpp"
#include "include/cef_command_handler.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_id_mappers.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_permission_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_task.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/views/cef_fill_layout.h"
#include <commctrl.h>
#include <cmath>
#include <map>
#include <set>
#include <vector>
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
#include "include/cef_image.h"
#include <filesystem>
#include <fstream>
#endif

namespace xenon {
namespace {
class ManagerTask final:public CefTask {
 public:explicit ManagerTask(std::function<void()> action):action_(std::move(action)){}
  void Execute()override{action_();}
 private:std::function<void()> action_;IMPLEMENT_REFCOUNTING(ManagerTask);
};
LRESULT CALLBACK RejectedChromeWindow(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR id,DWORD_PTR){
  // A Chromium-owned post-install browser must stay alive through its queued
  // completion callback, but must never expose a second browser shell.
  if(message==WM_WINDOWPOSCHANGING&&lparam){auto* position=reinterpret_cast<WINDOWPOS*>(lparam);position->flags=(position->flags&~SWP_SHOWWINDOW)|SWP_HIDEWINDOW;}
  if(message==WM_NCDESTROY)RemoveWindowSubclass(window,RejectedChromeWindow,id);
  return DefSubclassProc(window,message,wparam,lparam);
}
void reject_browser(CefRefPtr<CefBrowser> browser){
  const auto window=browser->GetHost()->GetWindowHandle();
  if(window&&GetAncestor(window,GA_ROOT)==window){SetWindowSubclass(window,RejectedChromeWindow,1,0);ShowWindow(window,SW_HIDE);}
  // Chromium's install watcher posts its callback after creating this browser.
  // CloseBrowser itself posts a task. This additional queue turn puts its
  // destructive close after that callback, without a timing-based delay.
  CefPostTask(TID_UI,new ManagerTask([browser]{if(browser->IsValid())browser->GetHost()->CloseBrowser(true);}));
}
struct StartupPreference {
  CefRefPtr<CefRequestContext> context;CefRefPtr<CefValue> previous;bool pending=false;
  explicit StartupPreference(CefRefPtr<CefRequestContext> value):context(value){
    previous=context->GetPreference("session.restore_on_startup");
    if(!previous||previous->GetType()!=VTYPE_INT)return;
    auto fresh=CefValue::Create();fresh->SetInt(5);CefString error;
    pending=context->SetPreference("session.restore_on_startup",fresh,error);
  }
  bool restore(){if(!pending)return false;pending=false;CefString error;return context->SetPreference("session.restore_on_startup",previous,error);}
  ~StartupPreference(){if(pending)restore();}
};
}
struct CefExtensionManager::Impl : public std::enable_shared_from_this<Impl> {
  struct Entry {CefRefPtr<CefRequestContext> context;std::map<int,CefRefPtr<CefBrowser>> browsers,auxiliary;std::wstring label;std::string navigation_error;extension_manager::NavigationPolicy navigation;bool closing=false;HWND parent{};CefRefPtr<CefWindow> window;CefRefPtr<CefBrowserView> view;};
  std::map<std::string,Entry> entries;
  std::function<HWND(const std::string&,const std::string&)> create;
  std::function<void(const std::string&,HWND)> created;
  std::function<void(const std::string&)> closed;
  static std::string tab_id(const std::string& workspace){return "xenon-extensions:"+workspace;}
  Changed changed;bool stopping=false;std::unique_ptr<CefExtensionCatalog> catalog;
  explicit Impl(Changed value):changed(std::move(value)),catalog(std::make_unique<CefExtensionCatalog>(changed)){}
  void retire(const std::string& name){
    const auto e=entries.find(name);if(e==entries.end()||!e->second.browsers.empty()||!e->second.auxiliary.empty())return;
    if(e->second.window&&!e->second.window->IsClosed()){e->second.window->Close();return;}
    entries.erase(e);if(closed)closed(tab_id(name));if(changed)changed(name);
  }
  void title(CefRefPtr<CefBrowser> browser){
    const auto entry=entries.find(workspace(browser));if(entry==entries.end())return;
    // The Xenon shell owns the top-level caption and navigation controls.
  }
  class ViewDelegate final:public CefBrowserViewDelegate {
   public:
    cef_runtime_style_t GetBrowserRuntimeStyle()override{return CEF_RUNTIME_STYLE_CHROME;}
    ChromeToolbarType GetChromeToolbarType(CefRefPtr<CefBrowserView>)override{return CEF_CTT_NONE;}
   private:IMPLEMENT_REFCOUNTING(ViewDelegate);
  };
  class WindowDelegate final:public CefWindowDelegate {
   public:
    WindowDelegate(std::weak_ptr<Impl> owner,std::string workspace):owner_(std::move(owner)),workspace_(std::move(workspace)){}
    cef_runtime_style_t GetWindowRuntimeStyle()override{return CEF_RUNTIME_STYLE_CHROME;}
    bool IsFrameless(CefRefPtr<CefWindow>)override{return true;}
    bool CanResize(CefRefPtr<CefWindow>)override{return false;}
    bool CanMaximize(CefRefPtr<CefWindow>)override{return false;}
    bool CanMinimize(CefRefPtr<CefWindow>)override{return false;}
    void OnWindowCreated(CefRefPtr<CefWindow> window)override{
      auto owner=owner_.lock();if(!owner)return;auto entry=owner->entries.find(workspace_);if(entry==owner->entries.end())return;
      auto& e=entry->second;e.window=window;const auto hwnd=window->GetWindowHandle();
      // Attach the empty, hidden Views container once, before its browser is
      // created. Existing pages and ownership handoffs are never reparented.
      SetWindowLongPtrW(hwnd,GWL_STYLE,WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS|WS_TABSTOP);
      SetWindowLongPtrW(hwnd,GWL_EXSTYLE,GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&~WS_EX_APPWINDOW);
      SetLastError(0);SetParent(hwnd,e.parent);
      if(GetParent(hwnd)!=e.parent){window->Close();return;}
      RECT bounds{};GetClientRect(e.parent,&bounds);
      SetWindowPos(hwnd,nullptr,0,0,std::max<LONG>(1,bounds.right),std::max<LONG>(1,bounds.bottom),SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED);
      // Showing just the HWND leaves Views' widget/compositor hidden. Notify
      // Views itself so the renderer becomes visible and starts painting.
      window->SetToFillLayout();window->AddChildView(e.view);window->Layout();window->Show();
    }
    bool CanClose(CefRefPtr<CefWindow>)override{
      if(auto owner=owner_.lock())if(auto entry=owner->entries.find(workspace_);entry!=owner->entries.end()&&entry->second.view){auto browser=entry->second.view->GetBrowser();return !browser||browser->GetHost()->TryCloseBrowser();}return true;
    }
    void OnWindowDestroyed(CefRefPtr<CefWindow>)override{
      if(auto owner=owner_.lock())if(auto entry=owner->entries.find(workspace_);entry!=owner->entries.end()){
        entry->second.window=nullptr;entry->second.view=nullptr;
        CefPostTask(TID_UI,new ManagerTask([weak=owner_,name=workspace_]{if(auto state=weak.lock())state->retire(name);}));
      }
    }
   private:std::weak_ptr<Impl> owner_;std::string workspace_;IMPLEMENT_REFCOUNTING(WindowDelegate);
  };
  std::string workspace(CefRefPtr<CefBrowser> browser) const {
    if(!browser||browser->GetHost()->GetRuntimeStyle()!=CEF_RUNTIME_STYLE_CHROME)return {};
    const auto context=browser->GetHost()->GetRequestContext();
    for(const auto& [name,entry]:entries)if(entry.context&&entry.context->IsSame(context))return name;
    return {};
  }
  bool permitted(CefRefPtr<CefBrowser> browser,const std::string& url) const {
    const auto name=workspace(browser);const auto entry=entries.find(name);
    return !stopping&&entry!=entries.end()&&!entry->second.closing&&entry->second.browsers.contains(browser->GetIdentifier())&&entry->second.navigation.permits(url);
  }
  static bool command(int id) {
    // Keep this a management surface, rather than an untracked general browser.
    // WebUI extension controls use Chromium's own privileged API, not commands.
    static const std::set<int> allowed=[] {
      std::set<int> result;
      for(const char* name:{"IDC_BACK","IDC_FORWARD","IDC_RELOAD","IDC_RELOAD_BYPASSING_CACHE","IDC_STOP","IDC_CLOSE_TAB","IDC_CLOSE_WINDOW","IDC_ZOOM_PLUS","IDC_ZOOM_MINUS","IDC_ZOOM_NORMAL","IDC_FIND","IDC_FIND_NEXT","IDC_FIND_PREVIOUS","IDC_FOCUS_LOCATION","IDC_COPY","IDC_CUT","IDC_PASTE","IDC_SELECT_ALL","IDC_UNDO","IDC_REDO","IDC_MANAGE_EXTENSIONS","IDC_SHOW_EXTENSIONS","IDC_EXTENSIONS_SUBMENU"}){
        const auto value=cef_id_for_command_id_name(name);if(value>=0)result.insert(value);
      }
      return result;
    }();
    return allowed.contains(id);
  }
  class Client final : public CefClient,public CefLifeSpanHandler,public CefRequestHandler,
                       public CefDisplayHandler,public CefCommandHandler,public CefContextMenuHandler,
                       public CefPermissionHandler,public CefLoadHandler {
   public:
    explicit Client(std::weak_ptr<Impl> owner,std::string expected={}):owner_(std::move(owner)),expected_(std::move(expected)){}
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler()override{return this;}
    CefRefPtr<CefRequestHandler> GetRequestHandler()override{return this;}
    CefRefPtr<CefDisplayHandler> GetDisplayHandler()override{return this;}
    CefRefPtr<CefCommandHandler> GetCommandHandler()override{return this;}
    CefRefPtr<CefContextMenuHandler> GetContextMenuHandler()override{return this;}
    CefRefPtr<CefPermissionHandler> GetPermissionHandler()override{return this;}
    CefRefPtr<CefLoadHandler> GetLoadHandler()override{return this;}
    void OnLoadStart(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,TransitionType)override{
      if(auto owner=owner_.lock();owner&&frame&&frame->IsMain())if(auto entry=owner->entries.find(owner->workspace(browser));entry!=owner->entries.end()&&entry->second.browsers.contains(browser->GetIdentifier()))entry->second.navigation_error.clear();
    }
    void OnLoadError(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,ErrorCode code,const CefString&,const CefString& failed)override{
      // Policy cancellation and superseded navigations report ERR_ABORTED.
      // Preserve the policy explanation instead of replacing it with an error.
      if(code==ERR_ABORTED)return;
      if(auto owner=owner_.lock();owner&&frame&&frame->IsMain()&&(extension_manager::store_url(failed.ToString())||extension_manager::account_url(failed.ToString())))
        if(auto entry=owner->entries.find(owner->workspace(browser));entry!=owner->entries.end()&&entry->second.browsers.contains(browser->GetIdentifier()))entry->second.navigation_error="Store failed (error "+std::to_string(static_cast<int>(code))+")";
    }
    void OnLoadEnd(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,int)override{
      const auto owner=owner_.lock();
      if(owner&&frame&&frame->IsMain()&&owner->permitted(browser,frame->GetURL().ToString())&&extension_manager::manager_url(frame->GetURL().ToString()))
        frame->ExecuteJavaScript(extension_manager::ui_script,"chrome://extensions/",1);
    }
    void OnAfterCreated(CefRefPtr<CefBrowser> browser)override{
      if(auto owner=owner_.lock()){
        const auto name=owner->workspace(browser);auto entry=owner->entries.find(name);
        if(!expected_.empty()&&name==expected_&&!owner->stopping&&entry!=owner->entries.end()&&!entry->second.closing&&entry->second.browsers.empty()){
          entry->second.browsers[browser->GetIdentifier()]=browser;
          if(owner->created&&entry->second.window)owner->created(tab_id(name),entry->second.window->GetWindowHandle());
          owner->title(browser);return;
        }
        if(entry!=owner->entries.end())entry->second.auxiliary.emplace(browser->GetIdentifier(),browser);
      }
      reject_browser(browser);
    }
    void OnBeforeClose(CefRefPtr<CefBrowser> browser)override{
      if(auto owner=owner_.lock()){
        const auto name=owner->workspace(browser);if(auto entry=owner->entries.find(name);entry!=owner->entries.end()){
          entry->second.browsers.erase(browser->GetIdentifier());
          entry->second.auxiliary.erase(browser->GetIdentifier());
          if(entry->second.browsers.empty()&&entry->second.auxiliary.empty())CefPostTask(TID_UI,new ManagerTask([weak=owner_,name]{if(auto state=weak.lock())state->retire(name);}));
        }
      }
    }
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,CefRefPtr<CefRequest> request,bool,bool)override{
      const auto owner=owner_.lock();if(!owner)return true;
      // Subframes are governed by Chromium WebUI/extension-origin policies.
      // Native code never exposes a page-to-host install bridge.
      if(!frame||!frame->IsMain())return owner->stopping;
      // Auxiliary Chrome windows never navigate or change the visible
      // manager's Store/account policy while waiting for deferred rejection.
      if(auto entry=owner->entries.find(owner->workspace(browser));entry==owner->entries.end()||!entry->second.browsers.contains(browser->GetIdentifier()))return true;
      const auto url=request->GetURL().ToString();const bool permitted=owner->permitted(browser,url);
      if(permitted)owner->entries.at(owner->workspace(browser)).navigation.admit(url);
      if(!permitted)if(auto entry=owner->entries.find(owner->workspace(browser));entry!=owner->entries.end()&&!entry->second.closing&&!owner->stopping)
        entry->second.navigation_error=extension_manager::page_url(url)?"Extensions context unavailable":"Blocked: "+extension_manager::blocked_destination(url);
      return !permitted;
    }
    bool OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame>,const CefString& target,cef_window_open_disposition_t,bool)override{
      const auto owner=owner_.lock();const auto url=target.ToString();
      if(owner&&owner->permitted(browser,url))CefPostTask(TID_UI,new ManagerTask([weak=owner_,browser,url]{if(auto state=weak.lock();state&&browser->IsValid()&&state->permitted(browser,url))browser->GetMainFrame()->LoadURL(url);}));
      return true;
    }
    bool OnBeforePopup(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame>,int,const CefString& target,const CefString&,CefLifeSpanHandler::WindowOpenDisposition,bool,const CefPopupFeatures&,CefWindowInfo&,CefRefPtr<CefClient>&,CefBrowserSettings&,CefRefPtr<CefDictionaryValue>&,bool*)override{
      OnOpenURLFromTab(browser,nullptr,target,CEF_WOD_CURRENT_TAB,false);return true;
    }
    void OnTitleChange(CefRefPtr<CefBrowser> browser,const CefString&)override{if(auto owner=owner_.lock())owner->title(browser);}
    bool OnConsoleMessage(CefRefPtr<CefBrowser>,cef_log_severity_t,const CefString&,const CefString&,int)override{return true;}
    bool OnChromeCommand(CefRefPtr<CefBrowser>,int id,cef_window_open_disposition_t)override{return !command(id);}
    bool IsChromeAppMenuItemVisible(CefRefPtr<CefBrowser>,int id)override{return command(id);}
    bool IsChromeAppMenuItemEnabled(CefRefPtr<CefBrowser>,int id)override{return command(id);}
    void OnBeforeContextMenu(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,CefRefPtr<CefContextMenuParams>,CefRefPtr<CefMenuModel> menu)override{menu->Clear();}
    bool OnRequestMediaAccessPermission(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,const CefString&,uint32_t,CefRefPtr<CefMediaAccessCallback> callback)override{callback->Continue(0);return true;}
    bool OnShowPermissionPrompt(CefRefPtr<CefBrowser>,uint64_t,const CefString&,uint32_t,CefRefPtr<CefPermissionPromptCallback> callback)override{callback->Continue(CEF_PERMISSION_RESULT_DENY);return true;}
   private:
    std::weak_ptr<Impl> owner_;std::string expected_;IMPLEMENT_REFCOUNTING(Client);
  };
};

CefExtensionManager::CefExtensionManager(Changed changed):impl_(std::make_shared<Impl>(std::move(changed))){}
CefExtensionManager::~CefExtensionManager(){impl_->changed={};close();}
bool CefExtensionManager::open(const std::string& workspace,CefRefPtr<CefRequestContext> context,const std::string& url,const std::string& label){
  if(impl_->stopping||workspace.empty()||!context||context->IsGlobal()||context->GetCachePath().empty()||!extension_manager::page_url(url))return false;
  if(auto found=impl_->entries.find(workspace);found!=impl_->entries.end()){
    if(found->second.closing||!found->second.context->IsSame(context))return false;
    if(!found->second.browsers.empty()){
      const auto browser=found->second.browsers.begin()->second;if(!browser->IsValid())return false;
      found->second.navigation_error.clear();browser->GetMainFrame()->LoadURL(url);
      if(impl_->created&&found->second.window)impl_->created(Impl::tab_id(workspace),found->second.window->GetWindowHandle());return true;
    }
  }
  impl_->entries[workspace].context=context;
  const auto text=label.empty()?std::string(workspace=="native-default"?"Personal":"Workspace"):label;
  const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
  auto& caption=impl_->entries[workspace].label;caption.resize(size);
  if(size)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),caption.data(),size);
  else caption=L"Workspace";
  StartupPreference startup(context);
  if(!startup.pending){impl_->entries.erase(workspace);return false;}
  auto& entry=impl_->entries.at(workspace);
  entry.parent=impl_->create?impl_->create(workspace,Impl::tab_id(workspace)):nullptr;
  if(!entry.parent){impl_->entries.erase(workspace);return false;}
  CefBrowserSettings settings;
  entry.view=CefBrowserView::CreateBrowserView(new Impl::Client(impl_,workspace),url,settings,nullptr,context,new Impl::ViewDelegate);
  auto window=CefWindow::CreateTopLevelWindow(new Impl::WindowDelegate(impl_,workspace));
  auto browser=entry.view?entry.view->GetBrowser():nullptr;
  if(!startup.restore()){
    if(browser)browser->GetHost()->CloseBrowser(true);else impl_->entries.erase(workspace);return false;
  }
  if(!browser){if(window)window->Close();impl_->entries.erase(workspace);if(impl_->closed)impl_->closed(Impl::tab_id(workspace));return false;}return true;
}
void CefExtensionManager::set_hosts(std::function<HWND(const std::string&,const std::string&)> create,std::function<void(const std::string&,HWND)> created,std::function<void(const std::string&)> closed){impl_->create=std::move(create);impl_->created=std::move(created);impl_->closed=std::move(closed);}
Json CefExtensionManager::native_tabs()const{
  auto rows=Json::array();for(const auto& [workspace,e]:impl_->entries)for(const auto& [id,b]:e.browsers)if(b->IsValid()){
    const auto url=b->GetMainFrame()->GetURL().ToString();rows.push_back({{"tabId",Impl::tab_id(workspace)},{"workspaceId",workspace},{"title",extension_manager::account_url(url)?"Google account":extension_manager::store_url(url)?"Chrome Web Store":"Extensions"},{"url",url},{"loading",b->IsLoading()},{"navigationError",e.navigation_error},{"canGoBack",b->CanGoBack()},{"canGoForward",b->CanGoForward()},{"internal",true},{"protected",false},{"private",false},{"zoom",std::pow(1.2,b->GetHost()->GetZoomLevel())}});
  }return rows;
}
bool CefExtensionManager::native_command(const std::string& id,const std::string& command,const std::string& value,Reply reply){
  if(!id.starts_with("xenon-extensions:"))return false;
  auto done=[&](bool ok){if(reply)reply(ok?success():failure("extension_manager_unavailable","This action is unavailable in Extensions."));};
  const auto entry=impl_->entries.find(id.substr(17));
  if(entry==impl_->entries.end()||entry->second.closing||entry->second.browsers.empty()){done(false);return true;}
  const auto b=entry->second.browsers.begin()->second;auto h=b->GetHost();
  if(command=="close")h->CloseBrowser(false);
  else if(command=="focus")h->SetFocus(true);
  else if(command=="back"){if(b->CanGoBack())b->GoBack();}
  else if(command=="forward"){if(b->CanGoForward())b->GoForward();}
  else if(command=="reload")b->Reload();
  else if(command=="reload-hard")b->ReloadIgnoreCache();
  else if(command=="stop")b->StopLoad();
  else if(command=="navigate"&&extension_manager::page_url(value))b->GetMainFrame()->LoadURL(value);
  else if(command=="find-close")h->StopFinding(true);
  else if(command=="find"||command=="find-next"||command=="find-previous")h->Find(value,command!="find-previous",false,command!="find");
  else if(command=="zoom"){try{const auto zoom=std::stod(value);if(!std::isfinite(zoom)||zoom<0.25||zoom>5){done(false);return true;}h->SetZoomLevel(std::log(zoom)/std::log(1.2));}catch(...){done(false);return true;}}
  else {done(false);return true;}done(true);return true;
}
bool CefExtensionManager::has_browsers(const std::string& workspace)const{
  for(const auto& [name,entry]:impl_->entries)if(workspace.empty()||name==workspace)return true;return impl_->catalog->pending(workspace);
}
bool CefExtensionManager::handles(CefRefPtr<CefBrowser> browser)const{return !impl_->workspace(browser).empty();}
void CefExtensionManager::close(const std::string& workspace){
  if(workspace.empty())impl_->stopping=true;
  impl_->catalog->close(workspace);
  std::vector<CefRefPtr<CefBrowser>> browsers;
  for(auto& [name,entry]:impl_->entries)if(workspace.empty()||name==workspace){entry.closing=true;for(auto& [id,browser]:entry.browsers)browsers.push_back(browser);for(auto& [id,browser]:entry.auxiliary)if(browser->IsValid())reject_browser(browser);}
  for(const auto& browser:browsers)if(browser->IsValid())browser->GetHost()->CloseBrowser(true);
}
CefRefPtr<CefClient> CefExtensionManager::default_client(){return new Impl::Client(impl_);}
void CefExtensionManager::quick_list(const std::string& workspace,CefRefPtr<CefRequestContext> context,const std::string& origin,Reply reply){impl_->catalog->query(workspace,context,origin,std::move(reply));}
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
CefRefPtr<CefBrowser> CefExtensionManager::fixture_browser(const std::string& workspace)const{
  if(auto entry=impl_->entries.find(workspace);entry!=impl_->entries.end()&&!entry->second.browsers.empty())return entry->second.browsers.begin()->second;return nullptr;
}
bool CefExtensionManager::fixture_account_navigation(const std::string& workspace){
  const auto browser=fixture_browser(workspace);
  if(!browser)return false;
  const auto profile=std::filesystem::path(browser->GetHost()->GetRequestContext()->GetCachePath().ToWString()).parent_path();
  if(profile.parent_path().filename()!=L".cache"||!profile.filename().wstring().starts_with(L"extension-manager-integration-"))return false;
  std::ifstream marker(profile/"SYNTHETIC_TEST_PROFILE",std::ios::binary);const std::string text{std::istreambuf_iterator<char>(marker),{}};
  if(text!="XENON_SYNTHETIC_EXTENSION_MANAGER_FIXTURE\n")return false;
  browser->GetMainFrame()->LoadURL("https://accounts.google.com/ServiceLogin?continue=https%3A%2F%2Fchromewebstore.google.com%2F");return true;
}
bool CefExtensionManager::fixture_untracked_window(const std::string& workspace){
  const auto browser=fixture_browser(workspace);if(!browser)return false;
  const auto context=browser->GetHost()->GetRequestContext();
  const auto profile=std::filesystem::path(context->GetCachePath().ToWString()).parent_path();
  if(profile.parent_path().filename()!=L".cache"||!profile.filename().wstring().starts_with(L"extension-manager-integration-"))return false;
  CefWindowInfo window;window.runtime_style=CEF_RUNTIME_STYLE_CHROME;window.SetAsPopup(nullptr,"Synthetic untracked Chrome window");CefBrowserSettings settings;
  return CefBrowserHost::CreateBrowser(window,default_client(),extension_manager::home,settings,nullptr,context);
}
Json CefExtensionManager::fixture_surface(const std::string& workspace)const{
  const auto found=impl_->entries.find(workspace);if(found==impl_->entries.end())return Json::object();const auto& e=found->second;
  if(!e.window||!e.view)return Json::object();const auto hwnd=e.window->GetWindowHandle();
  wchar_t root_class[100]{},parent_class[100]{};GetClassNameW(GetAncestor(hwnd,GA_ROOT),root_class,100);GetClassNameW(GetParent(hwnd),parent_class,100);
  RECT parent{},child{};GetClientRect(e.parent,&parent);GetClientRect(hwnd,&child);
  const auto content=e.view->GetBounds();const auto scale=GetDpiForWindow(hwnd)/96.0;
  // Capture only this marked fixture's management HWND. DOM/API readiness did
  // not catch a hidden Views compositor; require actual varied painted pixels.
  bool painted=false;const auto width=child.right,height=child.bottom;
  const auto profile=std::filesystem::path(e.context->GetCachePath().ToWString()).parent_path();
  if(profile.parent_path().filename()==L".cache"&&profile.filename().wstring().starts_with(L"extension-manager-integration-")&&width>0&&height>0&&width<=4096&&height<=4096){
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels{};const auto bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);const auto dc=CreateCompatibleDC(nullptr);
    if(bitmap&&dc){const auto old=SelectObject(dc,bitmap);
      if(PrintWindow(hwnd,dc,PW_CLIENTONLY|2)){
        GdiFlush();std::set<uint32_t> colors;const auto data=static_cast<const uint32_t*>(pixels);
        for(int y=0;y<height;y+=4)for(int x=0;x<width;x+=4)colors.insert(data[y*width+x]&0xffffff);
        painted=colors.size()>16;
        if(painted){auto image=CefImage::CreateImage();if(image->AddBitmap(1.0,width,height,CEF_COLOR_TYPE_BGRA_8888,CEF_ALPHA_TYPE_OPAQUE,pixels,static_cast<size_t>(width)*height*4)){
          int w{},h{};if(auto png=image->GetAsPNG(1.0,false,w,h)){std::vector<char> bytes(png->GetSize());png->GetData(bytes.data(),bytes.size(),0);std::ofstream output(profile/L"extension-manager-render.png",std::ios::binary);output.write(bytes.data(),bytes.size());}
        }}
      }SelectObject(dc,old);
    }if(dc)DeleteDC(dc);if(bitmap)DeleteObject(bitmap);
  }
  return {{"insideXenon",std::wstring(root_class)==L"XenonBrowserShell"&&std::wstring(parent_class)==L"XenonTabHost"&&GetParent(hwnd)==e.parent},
    {"childWindow",(GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_CHILD)!=0},{"noBrowserFrame",(GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_CAPTION)==0&&!e.view->GetChromeToolbar()},
    {"fitsHost",parent.right==child.right&&parent.bottom==child.bottom&&std::abs(content.width*scale-child.right)<=2&&std::abs(content.height*scale-child.bottom)<=2},
    {"viewsVisible",e.window->IsVisible()&&e.view->IsDrawn()},{"paintedContent",painted},{"accountBlocked",e.navigation_error=="Blocked: accounts.google.com"},{"auxiliaryBrowsers",e.auxiliary.size()}};
}
#endif
}
