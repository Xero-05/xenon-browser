#pragma once
#include <memory>
#include <functional>
#include <windows.h>

namespace xenon {
namespace updates {class InstallerLaunch;}
class Broker;
class CefEngine;
class Vault;
class FilePolicy;
class NativeUi {
 public:
  NativeUi(Broker&,CefEngine&,Vault&,FilePolicy&);
  ~NativeUi();
  void show();
  void show_updates();
  void set_update_install_callback(std::function<void(std::shared_ptr<updates::InstallerLaunch>)>);
  bool pretranslate(MSG&);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
