#include "xenon/cef_extension_catalog.hpp"
#include "include/cef_client.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_load_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_focus_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_task.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace xenon {
namespace {
class CatalogTask final:public CefTask {
 public:explicit CatalogTask(std::function<void()> action):action_(std::move(action)){}void Execute()override{action_();}
 private:std::function<void()> action_;IMPLEMENT_REFCOUNTING(CatalogTask);
};
constexpr char catalog_url[]="chrome://extensions/";
void quiet_window(CefRefPtr<CefWindow> window){
  const auto handle=window->GetWindowHandle();
  SetWindowLongPtrW(handle,GWL_EXSTYLE,GetWindowLongPtrW(handle,GWL_EXSTYLE)|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW);
  EnableWindow(handle,FALSE);
}
}
struct CefExtensionCatalog::Impl:public std::enable_shared_from_this<Impl> {
  struct Job {
    std::weak_ptr<Impl> owner;uint64_t id{};std::string workspace,origin;Reply reply;Json outcome;bool closing=false,queried=false;
    CefRefPtr<CefRequestContext> context;CefRefPtr<CefBrowser> browser;CefRefPtr<CefBrowserView> view;CefRefPtr<CefWindow> window,parent;CefRefPtr<CefRegistration> observer;
  };
  std::map<uint64_t,std::shared_ptr<Job>> jobs;uint64_t next=0;bool stopping=false;std::function<void(const std::string&)> closed;
  explicit Impl(std::function<void(const std::string&)> callback):closed(std::move(callback)){}
  void retire(uint64_t id){
    auto it=jobs.find(id);if(it==jobs.end()||it->second->browser||it->second->window||it->second->parent)return;
    const auto job=it->second;const auto workspace=job->workspace;jobs.erase(it);
    auto reply=std::exchange(job->reply,{});if(reply)reply(job->outcome.is_null()?failure("extensions_unavailable","Reading enabled extensions was canceled."):std::move(job->outcome));
    if(closed)closed(workspace);
  }
  static void finish(const std::shared_ptr<Job>& job,Json value){
    if(job->closing)return;job->closing=true;job->observer=nullptr;
    job->outcome=std::move(value);
    if(job->browser)job->browser->GetHost()->CloseBrowser(true);
    else if(job->window)job->window->Close();
    else if(job->parent)job->parent->Close();
    else if(auto owner=job->owner.lock())owner->retire(job->id);
  }
  class Observer final:public CefDevToolsMessageObserver {
   public:explicit Observer(std::weak_ptr<Job> job):job_(std::move(job)){}
    void OnDevToolsMethodResult(CefRefPtr<CefBrowser>,int id,bool ok,const void* bytes,size_t size)override{
      auto job=job_.lock();if(!job||job->closing||id!=994001)return;
      try{
        if(!ok||size>8*1024*1024)throw std::runtime_error("Catalog failed");
        const auto envelope=Json::parse(static_cast<const char*>(bytes),static_cast<const char*>(bytes)+size);
        if(envelope.contains("exceptionDetails"))throw std::runtime_error("Catalog unavailable");
        const auto data=envelope.at("result").at("value");const auto& rows=data.at("extensions");
        if(!rows.is_array()||rows.size()>512)throw std::runtime_error("Catalog limit");
        Json result=Json::array();std::set<std::string> seen;
        for(const auto& row:rows){
          const auto id=row.value("id",std::string{}),name=row.value("name",std::string{}),access=row.value("access",std::string{});
          if(id.size()!=32||!std::all_of(id.begin(),id.end(),[](char c){return c>='a'&&c<='p';})||!seen.insert(id).second||name.empty()||name.size()>4096)throw std::runtime_error("Invalid extension metadata");
          if(access!="allowed"&&access!="on_click"&&access!="other"&&access!="blocked")throw std::runtime_error("Invalid site access");
          const auto icon=row.value("icon",std::string{}),options=row.value("options",std::string{});
          if(icon.size()>131072||(!icon.empty()&&!icon.starts_with("data:image/png;base64,"))||options.size()>8192||(!options.empty()&&!options.starts_with("chrome-extension://"+id+"/")))throw std::runtime_error("Invalid extension display metadata");
          result.push_back({{"id",id},{"name",name},{"access",access},{"icon",icon},{"options",options},
            {"pinned",row.value("pinned",false)},{"allSites",row.value("allSites",false)},
            {"canChangeSite",row.value("canChangeSite",false)},{"canRemove",row.value("canRemove",false)}});
        }
        finish(job,success({{"extensions",result},{"origin",job->origin},{"siteBlocked",data.value("siteBlocked",false)}}));
      }catch(...){finish(job,failure("extensions_unavailable","The enabled extensions could not be read. Try again."));}
    }
   private:std::weak_ptr<Job> job_;IMPLEMENT_REFCOUNTING(Observer);
  };
  class Client final:public CefClient,public CefLifeSpanHandler,public CefLoadHandler,public CefRequestHandler,public CefFocusHandler {
   public:explicit Client(std::weak_ptr<Job> job):job_(std::move(job)){}
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler()override{return this;}
    CefRefPtr<CefLoadHandler> GetLoadHandler()override{return this;}
    CefRefPtr<CefRequestHandler> GetRequestHandler()override{return this;}
    CefRefPtr<CefFocusHandler> GetFocusHandler()override{return this;}
    bool OnSetFocus(CefRefPtr<CefBrowser>,FocusSource)override{return true;}
    void OnAfterCreated(CefRefPtr<CefBrowser> browser)override{
      if(auto job=job_.lock()){job->browser=browser;if(job->closing||!job->context->IsSame(browser->GetHost()->GetRequestContext()))browser->GetHost()->CloseBrowser(true);}
      else browser->GetHost()->CloseBrowser(true);
    }
    bool OnBeforeBrowse(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame> frame,CefRefPtr<CefRequest> request,bool,bool)override{
      const auto job=job_.lock();return !job||job->closing||(frame&&frame->IsMain()&&request->GetURL().ToString()!=catalog_url);
    }
    bool OnOpenURLFromTab(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,const CefString&,cef_window_open_disposition_t,bool)override{return true;}
    bool OnBeforePopup(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,int,const CefString&,const CefString&,CefLifeSpanHandler::WindowOpenDisposition,bool,const CefPopupFeatures&,CefWindowInfo&,CefRefPtr<CefClient>&,CefBrowserSettings&,CefRefPtr<CefDictionaryValue>&,bool*)override{return true;}
    void OnLoadEnd(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,int)override{
      const auto job=job_.lock();if(!job||job->closing||job->queried||!frame||!frame->IsMain()||frame->GetURL().ToString()!=catalog_url)return;
      job->queried=true;job->observer=browser->GetHost()->AddDevToolsMessageObserver(new Observer(job));
      // Only Chromium's own extension metadata APIs in our private WebUI.
      // The sole argument is a native-derived HTTP(S) origin, encoded as JSON.
      const auto expression=std::string(R"JS((async(origin)=>{
        const api=chrome.developerPrivate;
        const [extensions,matching,settings]=await Promise.all([
          api.getExtensionsInfo({includeDisabled:false,includeTerminated:false}),
          origin?api.getMatchingExtensionsForSite(origin+'/*'):[],api.getUserSiteSettings()]);
        const restricted=origin&&settings.restrictedSites.includes(origin);
        const permitted=origin&&settings.permittedSites.includes(origin);
        const matches=new Map(matching.map(item=>[item.id,item.siteAccess]));
        return {siteBlocked:!!restricted,extensions:extensions.filter(e=>e.state==='ENABLED'&&e.type==='EXTENSION').map(e=>{
          const site=matches.get(e.id);return {id:e.id,name:e.name,access:restricted&&site?'blocked':
            site&&permitted?'allowed':site==='ON_CLICK'?'on_click':site==='ON_ALL_SITES'||site==='ON_SPECIFIC_SITES'?'allowed':'other',
            icon:e.iconUrl,options:e.optionsPage?.url||'',pinned:!!e.pinnedToToolbar,
            allSites:e.permissions.runtimeHostPermissions?.hostAccess==='ON_ALL_SITES'&&e.permissions.runtimeHostPermissions.hasAllHosts,
            canChangeSite:!!e.permissions.runtimeHostPermissions,canRemove:e.userMayModify&&!e.mustRemainInstalled};
        })};
      })()JS")+Json(job->origin).dump()+")";
      const auto request=Json{{"id",994001},{"method","Runtime.evaluate"},{"params",{{"expression",expression},{"returnByValue",true},{"awaitPromise",true}}}}.dump();
      if(!browser->GetHost()->SendDevToolsMessage(request.data(),request.size()))finish(job,failure("extensions_unavailable","The enabled extensions could not be read."));
    }
    void OnBeforeClose(CefRefPtr<CefBrowser>)override{
      if(auto job=job_.lock()){job->browser=nullptr;CefPostTask(TID_UI,new CatalogTask([job]{if(job->window)job->window->Close();else if(auto owner=job->owner.lock())owner->retire(job->id);}));}
    }
   private:std::weak_ptr<Job> job_;IMPLEMENT_REFCOUNTING(Client);
  };
  class View final:public CefBrowserViewDelegate {
   public:cef_runtime_style_t GetBrowserRuntimeStyle()override{return CEF_RUNTIME_STYLE_CHROME;}
   private:IMPLEMENT_REFCOUNTING(View);
  };
  class Parent final:public CefWindowDelegate {
   public:explicit Parent(std::weak_ptr<Job> job):job_(std::move(job)){}
    cef_runtime_style_t GetWindowRuntimeStyle()override{return CEF_RUNTIME_STYLE_CHROME;}
    bool IsFrameless(CefRefPtr<CefWindow>)override{return true;}
    void OnWindowCreated(CefRefPtr<CefWindow> window)override{quiet_window(window);if(auto job=job_.lock())job->parent=window;else window->Close();}
    void OnWindowDestroyed(CefRefPtr<CefWindow>)override{if(auto job=job_.lock()){job->parent=nullptr;CefPostTask(TID_UI,new CatalogTask([job]{if(auto owner=job->owner.lock())owner->retire(job->id);}));}}
   private:std::weak_ptr<Job> job_;IMPLEMENT_REFCOUNTING(Parent);
  };
  class Window final:public CefWindowDelegate {
   public:explicit Window(std::weak_ptr<Job> job):job_(std::move(job)){}
    cef_runtime_style_t GetWindowRuntimeStyle()override{return CEF_RUNTIME_STYLE_CHROME;}
    bool IsFrameless(CefRefPtr<CefWindow>)override{return true;}
    CefRefPtr<CefWindow> GetParentWindow(CefRefPtr<CefWindow>,bool* is_menu,bool* can_activate)override{*is_menu=true;*can_activate=false;const auto job=job_.lock();return job?job->parent:nullptr;}
    void OnWindowCreated(CefRefPtr<CefWindow> window)override{quiet_window(window);if(auto job=job_.lock()){job->window=window;window->AddChildView(job->view);}else window->Close();}
    bool CanClose(CefRefPtr<CefWindow>)override{if(auto job=job_.lock();job&&job->browser)return job->browser->GetHost()->TryCloseBrowser();return true;}
    void OnWindowDestroyed(CefRefPtr<CefWindow>)override{if(auto job=job_.lock()){job->window=nullptr;job->view=nullptr;CefPostTask(TID_UI,new CatalogTask([job]{if(job->parent)job->parent->Close();else if(auto owner=job->owner.lock())owner->retire(job->id);}));}}
   private:std::weak_ptr<Job> job_;IMPLEMENT_REFCOUNTING(Window);
  };
};
CefExtensionCatalog::CefExtensionCatalog(std::function<void(const std::string&)> closed):impl_(std::make_shared<Impl>(std::move(closed))){}
CefExtensionCatalog::~CefExtensionCatalog(){impl_->closed={};close();}
void CefExtensionCatalog::query(const std::string& workspace,CefRefPtr<CefRequestContext> context,const std::string& origin,Reply reply){
  if(impl_->stopping||impl_->jobs.size()>=8||!context||context->IsGlobal()||context->GetCachePath().empty()){reply(failure("extensions_unavailable","Extensions are unavailable in this workspace."));return;}
  const auto previous=context->GetPreference("session.restore_on_startup");auto fresh=CefValue::Create();fresh->SetInt(5);CefString error;
  if(!previous||previous->GetType()!=VTYPE_INT||!context->SetPreference("session.restore_on_startup",fresh,error)){reply(failure("extensions_unavailable","Extensions are unavailable in this workspace."));return;}
  const auto job=std::make_shared<Impl::Job>();job->owner=impl_;job->id=++impl_->next;job->workspace=workspace;job->context=context;job->origin=origin;job->reply=std::move(reply);impl_->jobs.emplace(job->id,job);
  const auto parent=CefWindow::CreateTopLevelWindow(new Impl::Parent(job));
  if(!parent){context->SetPreference("session.restore_on_startup",previous,error);Impl::finish(job,failure("extensions_unavailable","The enabled extensions could not be read."));return;}
  CefBrowserSettings settings;job->view=CefBrowserView::CreateBrowserView(new Impl::Client(job),catalog_url,settings,nullptr,context,new Impl::View);
  const auto window=CefWindow::CreateTopLevelWindow(new Impl::Window(job));
  const bool restored=context->SetPreference("session.restore_on_startup",previous,error);
  if(!window||!job->browser||!restored){Impl::finish(job,failure("extensions_unavailable","The enabled extensions could not be read."));return;}
  // The API-only view is never shown, activated or added to a tab inventory.
  CefPostDelayedTask(TID_UI,new CatalogTask([weak=std::weak_ptr<Impl::Job>(job)]{if(auto job=weak.lock();job&&!job->closing)Impl::finish(job,failure("extensions_unavailable","Reading enabled extensions timed out. Try again."));}),8000);
}
void CefExtensionCatalog::close(const std::string& workspace){if(workspace.empty())impl_->stopping=true;std::vector<std::shared_ptr<Impl::Job>> jobs;for(auto& [id,job]:impl_->jobs)if(workspace.empty()||job->workspace==workspace)jobs.push_back(job);for(const auto& job:jobs)Impl::finish(job,failure("workspace_unavailable","The extension list was canceled."));}
bool CefExtensionCatalog::pending(const std::string& workspace)const{for(const auto& [id,job]:impl_->jobs)if(workspace.empty()||job->workspace==workspace)return true;return false;}
}
