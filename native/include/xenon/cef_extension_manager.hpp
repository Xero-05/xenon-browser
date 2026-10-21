#pragma once
#include "include/cef_client.h"
#include "include/cef_request_context.h"
#include "xenon/contracts.hpp"
#include <functional>
#include <memory>
#include <string>

namespace xenon {
// Chromium's extension WebUI and store prompts inside a Xenon content host.
// This inventory is separate from broker tabs. No method is reachable by MCP.
class CefExtensionManager {
 public:
  using Changed=std::function<void(const std::string& workspace)>;
  explicit CefExtensionManager(Changed changed);
  ~CefExtensionManager();
  void set_hosts(std::function<HWND(const std::string&,const std::string&)> create,
                 std::function<void(const std::string&,HWND)> created,
                 std::function<void(const std::string&)> closed);
  Json native_tabs() const;
  bool native_command(const std::string& id,const std::string& command,const std::string& value,Reply reply);
  bool open(const std::string& workspace,CefRefPtr<CefRequestContext> context,
            const std::string& url="chrome://extensions/",const std::string& label={});
  bool has_browsers(const std::string& workspace={}) const;
  bool handles(CefRefPtr<CefBrowser> browser) const;
  void close(const std::string& workspace={});
  CefRefPtr<CefClient> default_client();
  void quick_list(const std::string& workspace,CefRefPtr<CefRequestContext> context,const std::string& origin,Reply reply);
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
  // Synthetic fixture driver only. Never exposed by the production DLL.
  CefRefPtr<CefBrowser> fixture_browser(const std::string& workspace) const;
  bool fixture_account_navigation(const std::string& workspace);
  bool fixture_untracked_window(const std::string& workspace);
  Json fixture_surface(const std::string& workspace) const;
#endif
 private:
  struct Impl;std::shared_ptr<Impl> impl_;
};
}
