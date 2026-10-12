#include "xenon/uncertain_click_log.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace xenon;
void require_at(bool value,int line){if(!value)throw std::runtime_error("Uncertain click log regression at line "+std::to_string(line));}
#define require(value) require_at(value,__LINE__)
Json evidence(){return {{"visibilityStatus","uncertain"},{"role","button"},{"tag","BUTTON"},{"visibilityReason","clipping_or_occlusion_unproven"},{"hitMatch","native_ax_ancestry"},{"bounds",{{"x",30},{"y",30},{"width",180},{"height",36}}},{"viewport",{{"width",800},{"height",600},{"pageX",0},{"pageY",0}}},{"click",{{"x",120},{"y",48}}},{"name","HIDDEN_NAME"},{"url","https://private.invalid/?secret"},{"password","SECRET"},{"data","SCREENSHOT"},{"candidateId","PRIVATE_ID"}};}
#ifdef _WIN32
std::wstring dacl(const std::filesystem::path& path){PSECURITY_DESCRIPTOR descriptor{};require(GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,nullptr,nullptr,&descriptor)==ERROR_SUCCESS);LPWSTR encoded{};const bool converted=ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor,SDDL_REVISION_1,DACL_SECURITY_INFORMATION,&encoded,nullptr);LocalFree(descriptor);require(converted);std::wstring value(encoded);LocalFree(encoded);return value;}
void unprotect(const std::filesystem::path& path){PACL acl{};PSECURITY_DESCRIPTOR descriptor{};require(GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&acl,nullptr,&descriptor)==ERROR_SUCCESS);const auto result=SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|UNPROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,acl,nullptr);LocalFree(descriptor);require(result==ERROR_SUCCESS);}
#endif
int main() try {
  const auto safe=UncertainClickLog::sanitize(evidence());const auto encoded=safe.dump();require(!safe.empty());for(const auto* marker:{"HIDDEN","SECRET","SCREENSHOT","PRIVATE_ID","private.invalid"})require(encoded.find(marker)==std::string::npos);
  auto invalid=evidence();invalid["role"]="HOSTILE";require(UncertainClickLog::sanitize(invalid).empty());invalid=evidence();invalid["hitMatch"]="approximate_snapshot_position";require(UncertainClickLog::sanitize(invalid).empty());invalid=evidence();invalid["bounds"]["x"]=std::numeric_limits<double>::infinity();require(UncertainClickLog::sanitize(invalid).empty());invalid=evidence();invalid["role"]=Json::array();require(UncertainClickLog::sanitize(invalid).empty());
#ifdef _WIN32
  const auto root=std::filesystem::temp_directory_path()/("xenon-click-log-"+local_security::random_hex(8));std::filesystem::create_directory(root);
  const auto file=root/"visibility-diagnostics"/"uncertain-clicks.jsonl";
  {
    UncertainClickLog log(root,1800);require(!log.enabled());log.record(log.generation(),evidence());require(!std::filesystem::exists(file));
    log.enable(true);if(!log.enabled())std::cerr<<log.status().dump()<<'\n';require(log.enabled());const auto first=log.generation();log.record(first,evidence());require(log.status()["recordsThisSession"]==1);const auto size=std::filesystem::file_size(file);
    log.enable(false);log.record(first,evidence());require(std::filesystem::file_size(file)==size);
    log.enable(true);log.record(first,evidence());require(std::filesystem::file_size(file)==size);require(log.status()["recordsThisSession"]==1);
    for(int i=0;i<20;++i)log.record(log.generation(),evidence());require(!log.enabled());require(std::filesystem::file_size(file)<=1800);require(!log.status()["error"].get<std::string>().empty());
    log.clear();require(std::filesystem::file_size(file)==0);require(!log.enabled());log.enable(true);const auto before_clear=log.generation();log.clear();require(log.enabled());log.record(before_clear,evidence());require(std::filesystem::file_size(file)==0);log.record(log.generation(),evidence());require(log.status()["recordsThisSession"]==1);
  }
  {UncertainClickLog restarted(root);require(!restarted.enabled());}
  // A multiply-linked file must be rejected before clearing or appending.
  const auto alias=root/"sentinel";require(CreateHardLinkW(alias.c_str(),file.c_str(),nullptr));const auto original_size=std::filesystem::file_size(alias);
  unprotect(file);const auto original_acl=dacl(alias);
  {UncertainClickLog log(root);log.enable(true);require(!log.enabled());log.clear();require(std::filesystem::file_size(alias)==original_size);require(dacl(alias)==original_acl);}
  std::filesystem::remove(alias);std::filesystem::remove_all(root);
#endif
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
