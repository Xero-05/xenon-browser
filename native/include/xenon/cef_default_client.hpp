#pragma once
#include "include/cef_client.h"
#include <functional>
#include <utility>

namespace xenon {
// CEF asks for a default client without identifying the new browser. Select
// its real handler once the browser/context exists, preserving normal page,
// popup and print clients while keeping native extension windows out of MCP.
class CefDefaultClient final:public CefClient,public CefLifeSpanHandler {
 public:
  using Resolve=std::function<CefRefPtr<CefClient>(CefRefPtr<CefBrowser>)>;
  explicit CefDefaultClient(Resolve resolve):resolve_(std::move(resolve)){}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler()override{return this;}
#define XENON_FORWARD_HANDLER(name) decltype(std::declval<CefClient>().name()) name()override{return delegate_?delegate_->name():nullptr;}
  XENON_FORWARD_HANDLER(GetAudioHandler)
  XENON_FORWARD_HANDLER(GetCommandHandler)
  XENON_FORWARD_HANDLER(GetContextMenuHandler)
  XENON_FORWARD_HANDLER(GetDialogHandler)
  XENON_FORWARD_HANDLER(GetDisplayHandler)
  XENON_FORWARD_HANDLER(GetDownloadHandler)
  XENON_FORWARD_HANDLER(GetDragHandler)
  XENON_FORWARD_HANDLER(GetFindHandler)
  XENON_FORWARD_HANDLER(GetFocusHandler)
  XENON_FORWARD_HANDLER(GetFrameHandler)
  XENON_FORWARD_HANDLER(GetPermissionHandler)
  XENON_FORWARD_HANDLER(GetJSDialogHandler)
  XENON_FORWARD_HANDLER(GetKeyboardHandler)
  XENON_FORWARD_HANDLER(GetLoadHandler)
  XENON_FORWARD_HANDLER(GetPrintHandler)
  XENON_FORWARD_HANDLER(GetRenderHandler)
  XENON_FORWARD_HANDLER(GetRequestHandler)
#undef XENON_FORWARD_HANDLER
  void OnAfterCreated(CefRefPtr<CefBrowser> browser)override{
    delegate_=resolve_(browser);resolve_={};
    if(const auto handler=life())handler->OnAfterCreated(browser);else browser->GetHost()->CloseBrowser(true);
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,int id,const CefString& url,const CefString& name,WindowOpenDisposition disposition,bool gesture,const CefPopupFeatures& features,CefWindowInfo& window,CefRefPtr<CefClient>& client,CefBrowserSettings& settings,CefRefPtr<CefDictionaryValue>& extra,bool* no_access)override{
    const auto handler=life();return !handler||handler->OnBeforePopup(browser,frame,id,url,name,disposition,gesture,features,window,client,settings,extra,no_access);
  }
  void OnBeforePopupAborted(CefRefPtr<CefBrowser> browser,int id)override{if(const auto handler=life())handler->OnBeforePopupAborted(browser,id);}
  bool DoClose(CefRefPtr<CefBrowser> browser)override{const auto handler=life();return handler&&handler->DoClose(browser);}
  void OnBeforeClose(CefRefPtr<CefBrowser> browser)override{if(const auto handler=life())handler->OnBeforeClose(browser);delegate_=nullptr;}
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,CefProcessId process,CefRefPtr<CefProcessMessage> message)override{return delegate_&&delegate_->OnProcessMessageReceived(browser,frame,process,message);}
 private:
  CefRefPtr<CefLifeSpanHandler> life()const{return delegate_?delegate_->GetLifeSpanHandler():nullptr;}
  Resolve resolve_;CefRefPtr<CefClient> delegate_;IMPLEMENT_REFCOUNTING(CefDefaultClient);
};
}
