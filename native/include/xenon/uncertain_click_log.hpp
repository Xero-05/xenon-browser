#pragma once
#include "xenon/contracts.hpp"
#include "xenon/local_security.hpp"
#include <chrono>
#include <cmath>
#include <mutex>
#include <set>

namespace xenon {
// Native-human opt-in only. Fixed schema, bounded local file, no website strings.
class UncertainClickLog {
 public:
  explicit UncertainClickLog(std::filesystem::path root,size_t limit=8*1024*1024):root_(std::move(root)),limit_(limit){}
  ~UncertainClickLog(){close();}
  bool enabled() const {std::lock_guard lock(mutex_);return enabled_;}
  uint64_t generation() const {std::lock_guard lock(mutex_);return generation_;}
  Json status() const {std::lock_guard lock(mutex_);return {{"enabled",enabled_},{"generation",generation_},{"recordsThisSession",records_},{"bytes",bytes_},{"limitBytes",limit_},{"error",error_},{"directory",(root_/"visibility-diagnostics").string()}};}
  void enable(bool value){std::lock_guard lock(mutex_);++generation_;enabled_=false;close();error_.clear();if(!value)return;try{open(false);enabled_=true;}catch(const std::exception& error){close();error_=std::string("Cannot open click log: ")+error.what();}catch(...){close();error_="Cannot safely open the local click log.";}}
  void clear(){std::lock_guard lock(mutex_);const bool resume=enabled_;++generation_;enabled_=false;close();error_.clear();try{open(true);records_=0;enabled_=resume;if(!resume)close();}catch(...){close();error_="Cannot safely clear the local click log.";}}
  void record(uint64_t generation,const Json& evidence){
    const auto row=sanitize(evidence);if(row.empty())return;
    std::lock_guard lock(mutex_);if(!enabled_||generation!=generation_)return;
    auto entry=row;entry["schemaVersion"]=1;entry["productVersion"]=XENON_VERSION;entry["source"]="agent_coordinate_click";entry["outcome"]="mouse_down_and_up_dispatched";entry["websiteSuccessVerified"]=false;
    entry["capturedAtUnixMs"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto line=entry.dump()+"\n";
    if(bytes_+line.size()>limit_){enabled_=false;++generation_;error_="Click log size limit reached. Clear the log to collect more.";close();return;}
#ifdef _WIN32
    DWORD count{};if(!WriteFile(file_,line.data(),static_cast<DWORD>(line.size()),&count,nullptr)||count!=line.size()||!FlushFileBuffers(file_)){enabled_=false;++generation_;error_="Click log write failed.";close();return;}
#endif
    bytes_+=line.size();++records_;
  }
  static Json sanitize(const Json& evidence) try {
    if(!evidence.is_object()||evidence.value("visibilityStatus",std::string{})!="uncertain"||evidence.value("hitMatch",std::string{})!="native_ax_ancestry")return Json::object();
    const auto role=evidence.value("role",std::string{}),reason=evidence.value("visibilityReason",std::string{});
    if(!std::set<std::string>{"button","link","textbox","searchbox","combobox","checkbox","radio","switch","menuitem","tab","option","slider","spinbutton","listbox","treeitem"}.contains(role)||
       !std::set<std::string>{"clipping_or_occlusion_unproven","transform_visibility_unproven","paint_effect_visibility_unproven","visibility_unproven"}.contains(reason))return Json::object();
    Json row{{"visibilityStatus","uncertain"},{"role",role},{"visibilityReason",reason},{"hitMatch","native_ax_ancestry"}};
    for(const auto* key:{"bounds","viewport","click"}){
      const auto source=evidence.value(key,Json::object());Json numbers=Json::object();
      for(const auto* field:{"x","y","width","height","pageX","pageY"})if(source.contains(field)&&source[field].is_number()){
        const double value=source[field].get<double>();if(!std::isfinite(value)||std::abs(value)>1e8)return Json::object();numbers[field]=value;
      }
      if(numbers.empty())return Json::object();row[key]=std::move(numbers);
    }
    const auto tag=evidence.value("tag",std::string{});if(std::set<std::string>{"INPUT","SELECT","TEXTAREA","BUTTON","A","CANVAS","IMG","IFRAME","LABEL"}.contains(tag))row["tag"]=tag;
    const auto frame=evidence.value("frameScope",std::string{});if(frame=="root_frame"||frame=="child_frame")row["frameScope"]=frame;
    row["boundsCoordinateSpace"]="frame_document_css";row["clickCoordinateSpace"]="viewport_css";
    const auto diagnostics=evidence.value("filterDiagnostics",Json::object());Json flags=Json::object();
    for(const auto* key:{"unsupportedTransform","unsupportedPaint","clipPresent","nonrectangularClip","basicStyleProven","boundsInsideViewport","ancestorGeometryProven","centerOcclusionProvenClear","occluderUnbounded","occluderTextShadow","occluderBoxShadow","occluderFilter"})if(diagnostics.contains(key)&&diagnostics[key].is_boolean())flags[key]=diagnostics[key];
    if(diagnostics.contains("effectiveOpacity")&&diagnostics["effectiveOpacity"].is_number()){const double opacity=diagnostics["effectiveOpacity"].get<double>();if(std::isfinite(opacity)&&opacity>=0&&opacity<=1)flags["effectiveOpacity"]=opacity;}
    if(diagnostics.contains("ancestorDepth")&&diagnostics["ancestorDepth"].is_number_integer()){const auto depth=diagnostics["ancestorDepth"].get<int>();if(depth>=0&&depth<=128)flags["ancestorDepth"]=depth;}
    if(!flags.empty())row["filterDiagnostics"]=std::move(flags);
    return row;
  }catch(...){return Json::object();}
 private:
  std::filesystem::path root_;size_t limit_,bytes_=0,records_=0;bool enabled_=false;uint64_t generation_=0;std::string error_;mutable std::mutex mutex_;
#ifdef _WIN32
  HANDLE root_handle_=INVALID_HANDLE_VALUE,directory_=INVALID_HANDLE_VALUE,file_=INVALID_HANDLE_VALUE;
  static void validate(HANDLE handle,bool directory){BY_HANDLE_FILE_INFORMATION info{};if(handle==INVALID_HANDLE_VALUE||GetFileType(handle)!=FILE_TYPE_DISK||!GetFileInformationByHandle(handle,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||static_cast<bool>(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=directory||(!directory&&info.nNumberOfLinks!=1))throw std::runtime_error("Unsafe diagnostic path");}
#endif
  void close(){
#ifdef _WIN32
    for(auto* handle:{&file_,&directory_,&root_handle_})if(*handle!=INVALID_HANDLE_VALUE){CloseHandle(*handle);*handle=INVALID_HANDLE_VALUE;}
#endif
  }
  void open(bool truncate){
#ifdef _WIN32
    // Pin both directories without delete sharing and refuse links before any write.
    root_handle_=CreateFileW(root_.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);validate(root_handle_,true);
    const auto folder=root_/"visibility-diagnostics";local_security::SecurityDescriptor directory_policy(true);
    if(!CreateDirectoryW(folder.c_str(),&directory_policy.attributes)&&GetLastError()!=ERROR_ALREADY_EXISTS)throw std::runtime_error("Diagnostic directory unavailable");
    directory_=CreateFileW(folder.c_str(),GENERIC_READ|WRITE_DAC,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);validate(directory_,true);
    // Inspect an existing folder's policy rather than propagating a replacement
    // ACL to unvalidated children (which could themselves be hard links).
    PACL expected_acl{},actual_acl{};BOOL present{},defaulted{};PSECURITY_DESCRIPTOR actual_policy{};
    if(!GetSecurityDescriptorDacl(directory_policy.descriptor,&present,&expected_acl,&defaulted)||GetSecurityInfo(directory_,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&actual_acl,nullptr,&actual_policy)!=ERROR_SUCCESS)throw std::runtime_error("Diagnostic directory ACL unavailable");
    SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision{};void* expected_ace{};void* actual_ace{};
    const bool policy_read=GetSecurityDescriptorControl(actual_policy,&control,&revision)&&actual_acl&&actual_acl->AceCount==1&&GetAce(expected_acl,0,&expected_ace)&&GetAce(actual_acl,0,&actual_ace);
    bool owner_only=false;if(policy_read){const auto* expected=static_cast<ACCESS_ALLOWED_ACE*>(expected_ace);const auto* actual=static_cast<ACCESS_ALLOWED_ACE*>(actual_ace);owner_only=(control&SE_DACL_PROTECTED)&&actual->Header.AceType==ACCESS_ALLOWED_ACE_TYPE&&EqualSid(const_cast<DWORD*>(&expected->SidStart),const_cast<DWORD*>(&actual->SidStart))&&((actual->Mask&FILE_ALL_ACCESS)==FILE_ALL_ACCESS||(actual->Mask&GENERIC_ALL));}
    LocalFree(actual_policy);if(!owner_only)throw std::runtime_error("Diagnostic directory is not owner-only");
    local_security::SecurityDescriptor policy;file_=CreateFileW((folder/"uncertain-clicks.jsonl").c_str(),GENERIC_WRITE|FILE_READ_ATTRIBUTES|WRITE_DAC,FILE_SHARE_READ,&policy.attributes,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);validate(file_,false);
    PACL acl{};if(!GetSecurityDescriptorDacl(policy.descriptor,&present,&acl,&defaulted)||SetSecurityInfo(file_,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,acl,nullptr)!=ERROR_SUCCESS)throw std::runtime_error("Diagnostic ACL unavailable");
    LARGE_INTEGER offset{};if(truncate){if(!SetFilePointerEx(file_,offset,nullptr,FILE_BEGIN)||!SetEndOfFile(file_)||!FlushFileBuffers(file_))throw std::runtime_error("Diagnostic clear failed");}
    LARGE_INTEGER size{};if(!GetFileSizeEx(file_,&size)||size.QuadPart<0||static_cast<uint64_t>(size.QuadPart)>limit_)throw std::runtime_error("Diagnostic limit exceeded");bytes_=static_cast<size_t>(size.QuadPart);
    if(!SetFilePointerEx(file_,offset,nullptr,FILE_END))throw std::runtime_error("Diagnostic append failed");
#else
    (void)truncate;throw std::runtime_error("Windows is required");
#endif
  }
};
}
