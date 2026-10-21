#pragma once
#include "xenon/contracts.hpp"
#include <windows.h>
#include <functional>
#include <memory>
#include <string>

namespace xenon {
class ExtensionPanel {
 public:
  using Action=std::function<void(const std::string& id,const std::string& command)>;
  ExtensionPanel(HWND owner,RECT anchor,const Json& data,Action action);
  ~ExtensionPanel();
  void close();
  HWND window()const;
  static void draw_icon(HDC dc,RECT bounds,const std::string& data);
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
  Json fixture_state()const;
  bool fixture_action(const std::string& command);
#endif
 private:struct Impl;std::shared_ptr<Impl> impl_;
};
}
