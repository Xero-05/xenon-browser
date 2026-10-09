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
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
  void fixture_snapshot(const std::filesystem::path& destination);
#endif
 private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
}
