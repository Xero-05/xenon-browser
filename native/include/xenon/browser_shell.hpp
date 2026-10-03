#pragma once
#include "xenon/contracts.hpp"
#include <filesystem>
#include <memory>
#include <windows.h>

namespace xenon {
class Broker;class CefEngine;
// Native browser chrome only. No methods of this class are exposed through MCP.
class BrowserShell {
 public:
  BrowserShell(Broker&,CefEngine&,const std::filesystem::path& root);
  ~BrowserShell();
  void show();
  void request_exit();
  HWND create_host(const std::string& workspace,const std::string& tab,bool human);
  void tab_created(const std::string& tab,HWND browser);
  void tab_closed(const std::string& tab);
  void refresh();
  bool pretranslate(MSG&);
  void permission(const std::string& tab,const std::string& origin,const std::string& description,std::function<void(bool)> answer);
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
  void fixture_snapshot(const std::filesystem::path& destination);
#endif
 private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
}
