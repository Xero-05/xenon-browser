#pragma once
#include "include/cef_request_context.h"
#include "xenon/contracts.hpp"
#include <functional>
#include <memory>

namespace xenon {
// Read-only Chromium WebUI API query for native human chrome. No broker/MCP
// inventory, website script, arbitrary expression or input endpoint.
class CefExtensionCatalog {
 public:
  explicit CefExtensionCatalog(std::function<void(const std::string&)> closed);
  ~CefExtensionCatalog();
  void query(const std::string& workspace,CefRefPtr<CefRequestContext> context,const std::string& origin,Reply reply);
  void close(const std::string& workspace={});
  bool pending(const std::string& workspace={})const;
 private:struct Impl;std::shared_ptr<Impl> impl_;
};
}
