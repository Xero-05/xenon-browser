#pragma once
#include <memory>
#include <functional>
#include <string>
#include <windows.h>

namespace xenon {
namespace updates {class InstallerLaunch;}
class Broker;
class CefEngine;
class Vault;
class FilePolicy;
class ExtensionStore;
class NativeUi {
 public:
  enum class Section { clients, workspaces, passwords, extensions };
  NativeUi(Broker&,CefEngine&,Vault&,FilePolicy&,ExtensionStore&);
  ~NativeUi();
  void show();
  void show_updates();
  // Opens Controls on a section, selecting a workspace ID or account origin.
  void show_section(Section section,const std::string& select={});
  // The browser toolbar's saved-password key: offer accounts for this tab.
  void request_autofill(const std::string& tab_id);
  void set_update_install_callback(std::function<void(std::shared_ptr<updates::InstallerLaunch>)>);
  bool pretranslate(MSG&);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
