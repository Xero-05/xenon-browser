#include "xenon/web_store.hpp"
#include "include/cef_stream.h"
#include "include/cef_zip_reader.h"
#include <array>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace xenon::web_store {
namespace {
[[noreturn]] void fail(const char* message){throw std::runtime_error(message);}
uint32_t crc32(std::string_view bytes){
  static const auto table=[] {std::array<uint32_t,256> values{};for(uint32_t n=0;n<256;++n){auto c=n;for(int i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);values[n]=c;}return values;}();
  uint32_t value=0xffffffff;for(unsigned char c:bytes)value=table[(value^c)&255]^(value>>8);return value^0xffffffff;
}
struct Reader {CefRefPtr<CefZipReader> value;~Reader(){if(value)value->Close();}};
}
Package unpack(VerifiedCrx crx,const std::atomic_bool& cancel){
  const auto entries=inspect_zip(crx.zip);
  Reader reader{CefZipReader::Create(CefStreamReader::CreateForData(crx.zip.data(),crx.zip.size()))};
  if(!reader.value||!reader.value->MoveToFirstFile())fail("The extension ZIP could not be opened.");
  Package package;package.id=std::move(crx.id);package.public_key=std::move(crx.public_key);
  for(size_t index=0;index<entries.size();++index){
    if(cancel.load())fail("Extension installation canceled.");
    const auto& entry=entries[index];
    if(reader.value->GetFileName().ToString()!=entry.name||reader.value->GetFileSize()!=entry.size)fail("The extension ZIP directory is ambiguous.");
    if(!entry.directory){
      if(!reader.value->OpenFile(""))fail("An extension ZIP entry could not be opened.");
      std::string bytes;bytes.reserve(entry.size);std::array<char,65536> buffer{};
      for(;;){if(cancel.load())fail("Extension installation canceled.");const int n=reader.value->ReadFile(buffer.data(),buffer.size());if(n<0)fail("An extension ZIP entry could not be decompressed.");if(!n)break;if(static_cast<size_t>(n)>entry.size-bytes.size())fail("An extension ZIP entry exceeds its declared size.");bytes.append(buffer.data(),n);}
      if(!reader.value->CloseFile()||bytes.size()!=entry.size||crc32(bytes)!=entry.crc)fail("The extension ZIP integrity check failed.");
      if(entry.name=="manifest.json"){
        if(bytes.size()>1024*1024)fail("The extension manifest is too large.");
        package.manifest=Json::parse(bytes,nullptr,false,true);
      }
      package.files.push_back({entry.name,std::move(bytes)});
    }
    const bool next=reader.value->MoveToNextFile();if(next!=(index+1<entries.size()))fail("The extension ZIP file count is invalid.");
  }
  if(!package.manifest.is_object()||package.manifest.value("manifest_version",0)!=3)fail("Only Manifest V3 extensions are supported.");
  const auto& manifest=package.manifest;
  if(!manifest.contains("name")||!manifest["name"].is_string()||!manifest.contains("version")||!manifest["version"].is_string())fail("The extension manifest needs a name and version.");
  package.name=manifest["name"].get<std::string>();package.version=manifest["version"].get<std::string>();
  if(package.name.empty()||package.version.empty()||package.name.size()>4096||package.version.size()>32)fail("The extension name or version is invalid.");
  // Resolve the signed default-locale name from the same immutable file tree
  // that will be installed. Missing/invalid messages leave the manifest token.
  if(package.name.starts_with("__MSG_")&&package.name.ends_with("__")&&package.name.size()>8&&manifest.contains("default_locale")&&manifest["default_locale"].is_string()){
    const auto locale=manifest["default_locale"].get<std::string>();
    if(!locale.empty()&&locale.size()<=32&&std::all_of(locale.begin(),locale.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';})){
      const auto path="_locales/"+locale+"/messages.json";
      for(const auto& file:package.files)if(file.name==path&&file.bytes.size()<=1024*1024){
        const auto messages=Json::parse(file.bytes,nullptr,false,true);if(!messages.is_object())break;
        auto wanted=package.name.substr(6,package.name.size()-8);for(auto& c:wanted)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for(const auto& [key,value]:messages.items()){
          auto lowered=key;for(auto& c:lowered)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          if(lowered==wanted&&value.is_object()&&value.contains("message")&&value["message"].is_string()){
            const auto text=value["message"].get<std::string>();if(!text.empty()&&text.size()<=4096)package.name=text;break;
          }
        }
        break;
      }
    }
  }
  if(manifest.contains("key")&&(!manifest["key"].is_string()||ExtensionStore::key_id(manifest["key"].get<std::string>())!=std::optional<std::string>(package.id)))fail("The manifest key does not match the signed extension.");
  if(ExtensionStore::key_id(package.public_key)!=std::optional<std::string>(package.id))fail("The extension key does not match its ID.");
  return package;
}
}
