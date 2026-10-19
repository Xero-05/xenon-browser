#pragma once
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
#include "xenon/cef_engine.hpp"
#include "xenon/local_security.hpp"
#include "include/cef_task.h"
#include <fstream>

namespace xenon {
// File-driven synthetic native fixture, compiled only into XenonAuthTest. It
// accepts a fixed action enum, never JavaScript, URLs, files or native handles.
class NativeExtensionManagerFixture : public std::enable_shared_from_this<NativeExtensionManagerFixture> {
 public:
  NativeExtensionManagerFixture(std::filesystem::path root,CefEngine& engine,std::function<bool()> store_menu={},std::function<Json(const std::string&)> panel_menu={}):root_(std::move(root)),engine_(engine),store_menu_(std::move(store_menu)),panel_menu_(std::move(panel_menu)){
    if(root_.parent_path().filename()!=L".cache"||!root_.filename().wstring().starts_with(L"extension-manager-integration-"))throw std::runtime_error("Extension fixture requires a disposable profile");
    std::ifstream marker(root_/"SYNTHETIC_TEST_PROFILE",std::ios::binary);std::string text{std::istreambuf_iterator<char>(marker),{}};
    if(text!="XENON_SYNTHETIC_EXTENSION_MANAGER_FIXTURE\n"||std::filesystem::exists(root_/"extension-manager-result.json"))throw std::runtime_error("Invalid extension fixture marker");
  }
  void start(){CefPostDelayedTask(TID_UI,new Tick(weak_from_this()),100);}
 private:
  class Tick final:public CefTask {
   public:explicit Tick(std::weak_ptr<NativeExtensionManagerFixture> owner):owner_(std::move(owner)){}
    void Execute()override{if(auto owner=owner_.lock())owner->poll();}
   private:std::weak_ptr<NativeExtensionManagerFixture> owner_;IMPLEMENT_REFCOUNTING(Tick);
  };
  void finish(uint64_t id,Json value){
    try{value["id"]=id;const auto temporary=root_/"extension-manager-result.tmp";
      {std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);stream<<value.dump();stream.close();if(!stream)return;}
      local_security::restrict_path(temporary);
      publishing_=!MoveFileExW(temporary.c_str(),(root_/"extension-manager-result.json").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
      pending_=publishing_;return;
    }catch(...){}pending_=false;
  }
  void poll(){
    // Node may briefly hold the previous result open. Retain this exact result
    // until atomic replacement succeeds; do not accept the next request yet.
    if(publishing_){
      if(MoveFileExW((root_/"extension-manager-result.tmp").c_str(),(root_/"extension-manager-result.json").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        publishing_=false;pending_=false;if(shutdown_after_publish_){engine_.shutdown();return;}
      }start();return;
    }
    if(!pending_)try{
      const auto path=root_/"extension-manager-request.json";
      if(std::filesystem::exists(path)){
        const auto attrs=GetFileAttributesW(path.c_str());if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||std::filesystem::file_size(path)>2048)throw std::runtime_error("Invalid fixture request");
        std::ifstream stream(path);const auto request=Json::parse(stream);const auto id=request.at("id").get<uint64_t>();
        if(id>last_){last_=id;const auto action=request.value("action",std::string{});
          if(action=="shutdown"){shutdown_after_publish_=true;finish(id,success());if(publishing_)start();else engine_.shutdown();return;}
          if(action!="manager"&&action!="store"&&action!="probe"&&action!="close"&&action!="inventory"&&action!="drawer"&&action!="sidebar-link"&&action!="empty-store-link"&&action!="native-sidebar-link"&&action!="close-pages"&&action!="account-redirect"&&action!="account-direct"&&action!="install-prompt"&&action!="untracked-window"&&action!="install-page"&&action!="install-click"&&action!="confirm-install"&&action!="installed"&&action!="install-mismatch"&&action!="install-rejected"&&action!="quick-list"&&action!="quick-site"&&action!="quick-stale"&&action!="quick-marker"&&action!="popup-state"&&action!="popup-marker"&&action!="popup-close"&&action!="action-popup"&&action!="action-click"&&action!="action-allow-site"&&action!="action-on-click-matching"&&action!="action-on-site-matching"&&action!="action-on-site-other"&&action!="panel-open"&&action!="panel-state"&&action!="panel-pin"&&action!="panel-activate"&&action!="panel-outside"&&action!="panel-pinned-action"&&action!="panel-site-toggle"&&action!="action-side"&&action!="side-button"&&action!="side-close"&&action!="side-protect"&&action!="side-unprotect"&&action!="panel-dock-state"&&action!="panel-dock-new-tab"&&action!="panel-dock-return"&&action!="panel-dock-private"&&action!="panel-dock-resize"&&action!="panel-foreground")throw std::runtime_error("Invalid fixture action");
          pending_=true;const auto weak=weak_from_this();auto reply=[weak,id](Json value){if(auto self=weak.lock())self->finish(id,std::move(value));};
          if(action.starts_with("panel-")&&panel_menu_)reply(panel_menu_(action.substr(6)));
          else if(action=="store"&&store_menu_)reply(store_menu_()?success():failure("NOT_READY","Synthetic shell is initializing"));
          else if(action=="store")engine_.open_extensions_manager("native-default","https://chromewebstore.google.com/category/extensions",std::move(reply));
          else if(action=="manager")engine_.open_extensions_manager("native-default","chrome://extensions/",std::move(reply));
          else engine_.fixture_extension_manager("native-default",action,std::move(reply));
        }
      }
    }catch(...){finish(last_,failure("FIXTURE_FAILED","Synthetic extension-manager request failed"));}
    start();
  }
  std::filesystem::path root_;CefEngine& engine_;uint64_t last_{};bool pending_=false,publishing_=false,shutdown_after_publish_=false;
  std::function<bool()> store_menu_;
  std::function<Json(const std::string&)> panel_menu_;
};
}
#endif
