#pragma once
#include <memory>
#include <windows.h>

namespace xenon {
class Broker;
class CefEngine;
class Vault;
class FilePolicy;
class NativeUi {
 public:
  NativeUi(Broker&,CefEngine&,Vault&,FilePolicy&);
  ~NativeUi();
  void show();
  bool pretranslate(MSG&);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
