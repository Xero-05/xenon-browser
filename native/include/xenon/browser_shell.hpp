#pragma once
#include "xenon/contracts.hpp"
#include <filesystem>
#include <functional>
#include <string>
#include <memory>
#include <windows.h>

namespace xenon {
class Broker;class CefEngine;class NativeUi;class ExtensionStore;
// Native browser chrome only. No methods of this class are exposed through MCP.
class BrowserShell {
 public:
  BrowserShell(Broker&,CefEngine&,NativeUi&,ExtensionStore&,const std::filesystem::path& root);
  ~BrowserShell();
  void show();
  void request_exit();
  // Native tab hosts may live in any Xenon window. Popups join their opener's.
  HWND create_host(const std::string& workspace,const std::string& tab,bool human,const std::string& opener={});
  void tab_created(const std::string& tab,HWND browser);
  void tab_closed(const std::string& tab);
  void refresh();
  // A page context-menu link command chosen by the human.
  void open_link(const std::string& tab,const std::string& url,bool new_window);
  bool pretranslate(MSG&);
  void permission(const std::string& tab,const std::string& origin,const std::string& description,std::function<void(bool)> answer);
  // Engine notices for native chrome, on the UI thread. A page in the tab
  // entered or left fullscreen.
  void page_fullscreen(const std::string& tab,bool fullscreen);
  // A website link for another app (see CefEngine::set_external_link_callback).
  // Confirmed links open in the app Windows registered for the scheme.
  void external_link(const std::string& tab,const std::string& origin,const std::string& url,const std::string& scheme,bool permitted,std::function<void(bool)> done);
  // A page asks to share every display; `answer` receives the person's choice.
  void screen_share(const std::string& tab,const std::string& origin,bool audio,std::function<void(bool)> answer);
  // A web address Windows sent to Xenon as the default browser. It opens in a
  // new Personal tab owned by the person.
  void open_external_url(const std::string& url);
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
  void fixture_snapshot(const std::filesystem::path& destination);
  bool fixture_store_menu();
  Json fixture_extension_panel(const std::string& action,const std::filesystem::path& root);
#endif
 private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
}
