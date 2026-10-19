#include "xenon/web_store.hpp"
#include "xenon/local_security.hpp"
#include "include/cef_api_hash.h"
#include <fstream>
#include <iostream>

using namespace xenon;
namespace fs=std::filesystem;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
  // Explicit opt-in integration check. This verifies a real store-signed CRX in
  // memory and writes only to a disposable test root; Chromium never loads it.
  // No network or profile reads are part of the normal native CTest suite.
  if(argc!=3||std::string(argv[1])!="--download")throw std::runtime_error("Usage: cef_web_store_tests --download extension-id");
  const auto api=cef_api_hash(CEF_API_VERSION,0);require(api&&std::string(api)==CEF_API_HASH_PLATFORM,"Pinned CEF API hash");
  std::atomic_bool cancel=false;auto crx=web_store::download(argv[2],cancel);const auto id=crx.extension_id();
  cancel=true;bool canceled=false;try{web_store::unpack(crx,cancel);}catch(const std::exception&){canceled=true;}require(canceled,"Canceled package was not discarded");cancel=false;
  auto package=web_store::unpack(std::move(crx),cancel);const auto review=package.permission_review();require(review.find(id)!=std::string::npos,"Review binds the verified ID");
  require(!review.starts_with("Extension: \"__MSG_"),"Review resolves the extension's signed localized name");
  const auto fixture_root=fs::absolute(fs::path(".cache")/"web-store-package-tests");
  const auto base=fixture_root/local_security::random_hex(8);fs::create_directories(base);
  try{
    ExtensionStore store(base);require(store.list().empty(),"A review alone does not install");
    const auto installed=store.install_web_store(package);require(installed.id==id&&!store.active(id),"Verified package keeps its ID and waits for restart");
    std::ifstream input(store.directory(installed)/"manifest.json");Json manifest=Json::parse(input);input.close();require(ExtensionStore::key_id(manifest.at("key").get<std::string>())==std::optional<std::string>(id),"Managed copy preserves developer key");
    bool duplicate=false;try{store.install_web_store(package);}catch(const std::exception&){duplicate=true;}require(duplicate&&store.list().size()==1,"Duplicate install changes nothing");
    {
      ExtensionStore restarted(base);require(restarted.startup_paths().size()==1&&restarted.active(id),"Restart selects the managed extension");require(restarted.set_enabled(id,false)&&restarted.remove(id),"Disable and remove store extension");require(fs::exists(store.directory(installed)),"Loaded files wait until restart");
    }
    ExtensionStore cleanup(base);require(cleanup.list().empty()&&!fs::exists(store.directory(installed)),"Restart completes deferred cleanup");
    require(fs::canonical(base).parent_path()==fixture_root,"Bounded cleanup");fs::remove_all(base);
  }catch(...){std::cerr<<"Synthetic integration state retained at "<<base<<'\n';throw;}
  std::cout<<"Store package integration passed: signed download, safe decompression/CRC, cancel, permission review, managed install, persistent ID, restart activation, duplicate refusal and removal. No extension code executed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
