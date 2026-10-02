#pragma once
// Compiled only into the separately named, never-packaged test browser.
#if defined(XENON_TEST_FIXTURE_CERT_SHA256)
#include "xenon/broker.hpp"
#include "xenon/local_security.hpp"
#include "include/cef_task.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>

namespace xenon {
// Calls the native API with real CEF, without a production/MCP deletion tool.
class NativeRemovalFixture : public std::enable_shared_from_this<NativeRemovalFixture> {
 public:
  NativeRemovalFixture(std::filesystem::path root,Broker& broker):root_(std::move(root)),broker_(broker){
    if(root_.parent_path().filename()!=L".cache"||!root_.filename().wstring().starts_with(L"auth-integration-removal-"))
      throw std::runtime_error("Native fixture requires a disposable removal profile");
    std::ifstream marker(root_/"SYNTHETIC_TEST_PROFILE",std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>(marker),{}};
    if(contents!="XENON_SYNTHETIC_AUTH_FIXTURE\n"||std::filesystem::exists(root_/"native-removal-result.json"))
      throw std::runtime_error("Native fixture marker is missing or already used");
  }
  void start(){CefPostDelayedTask(TID_UI,new Tick(weak_from_this()),100);}
 private:
  class Tick final:public CefTask {
   public:explicit Tick(std::weak_ptr<NativeRemovalFixture> owner):owner_(std::move(owner)){}
    void Execute()override{if(auto owner=owner_.lock())owner->poll();}
   private:std::weak_ptr<NativeRemovalFixture> owner_;IMPLEMENT_REFCOUNTING(Tick);
  };
  void write(Json value){
    try{
      const auto path=root_/"native-removal-result.json";
      {std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<value.dump();}
      local_security::restrict_path(path);
    }catch(const std::exception&){}
  }
  void poll(){
    try{
      const auto path=root_/"native-removal-request.json";
      if(!std::filesystem::exists(path)){start();return;}
      const auto attributes=GetFileAttributesW(path.c_str());
      if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||std::filesystem::file_size(path)>4096)
        throw std::runtime_error("Invalid fixture request");
      std::ifstream file(path,std::ios::binary);const auto request=Json::parse(file);
      const auto id=request.at("workspaceId").get<std::string>();
      const auto weak=weak_from_this();
      broker_.remove_workspace(id,[weak](Json result){if(auto self=weak.lock())self->write(std::move(result));});
    }catch(const std::exception&){write(failure("FIXTURE_FAILED","Native fixture request failed"));}
  }
  std::filesystem::path root_;Broker& broker_;
};
}
#endif
