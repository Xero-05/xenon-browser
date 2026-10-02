#include "xenon/workspace_storage.hpp"
#include "xenon/local_security.hpp"
#include <algorithm>
#include <cwchar>
#include <limits>
#include <set>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#endif

namespace xenon {
const char* workspace_cleanup_status_name(WorkspaceCleanupStatus status) noexcept {
  switch(status) {
    case WorkspaceCleanupStatus::removed:return "removed";
    case WorkspaceCleanupStatus::already_absent:return "already_absent";
    case WorkspaceCleanupStatus::deferred:return "deferred";
    case WorkspaceCleanupStatus::refused:return "refused";
    case WorkspaceCleanupStatus::limit_reached:return "limit_reached";
  }
  return "refused";
}
namespace {
bool valid_id(const std::string& id) {
  return !id.empty() && id.size() <= 160 && std::all_of(id.begin(),id.end(),[](unsigned char ch){
    return (ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_';
  });
}
void append(WorkspaceCleanupResult& result, WorkspaceCleanupEntry entry) {
  switch(entry.status) {
    case WorkspaceCleanupStatus::removed:++result.removed;break;
    case WorkspaceCleanupStatus::already_absent:++result.already_absent;break;
    case WorkspaceCleanupStatus::deferred:++result.deferred;break;
    case WorkspaceCleanupStatus::refused:++result.refused;break;
    case WorkspaceCleanupStatus::limit_reached:++result.limited;break;
  }
  result.files_removed+=entry.files_removed;result.directories_removed+=entry.directories_removed;
  result.entries.push_back(std::move(entry));
}
#ifdef _WIN32
struct Handle {
  HANDLE value=INVALID_HANDLE_VALUE;
  explicit Handle(HANDLE h=INVALID_HANDLE_VALUE):value(h){}
  ~Handle(){reset();}
  Handle(const Handle&)=delete;
  Handle& operator=(const Handle&)=delete;
  Handle(Handle&& other) noexcept:value(std::exchange(other.value,INVALID_HANDLE_VALUE)){}
  Handle& operator=(Handle&& other) noexcept {if(this!=&other){reset();value=std::exchange(other.value,INVALID_HANDLE_VALUE);}return *this;}
  void reset(){if(value!=INVALID_HANDLE_VALUE){CloseHandle(value);value=INVALID_HANDLE_VALUE;}}
};
struct FindHandle {
  HANDLE value=INVALID_HANDLE_VALUE;
  ~FindHandle(){if(value!=INVALID_HANDLE_VALUE)FindClose(value);}
};
struct CleanupFailure {WorkspaceCleanupStatus status;DWORD error;};
struct Node {Handle handle;std::filesystem::path path;bool directory;};
[[noreturn]] void failed(DWORD error){throw CleanupFailure{WorkspaceCleanupStatus::deferred,error};}
[[noreturn]] void refused(DWORD error=ERROR_INVALID_DATA){throw CleanupFailure{WorkspaceCleanupStatus::refused,error};}
bool same_path(const std::filesystem::path& left,const std::filesystem::path& right) {
  const auto a=left.lexically_normal().native(),b=right.lexically_normal().native();
  return CompareStringOrdinal(a.c_str(),static_cast<int>(a.size()),b.c_str(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;
}
std::filesystem::path final_path(HANDLE handle) {
  const DWORD required=GetFinalPathNameByHandleW(handle,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
  if(!required)failed(GetLastError());
  if(required>32768)refused(ERROR_FILENAME_EXCED_RANGE);
  std::wstring value(required,L'\0');
  const auto count=GetFinalPathNameByHandleW(handle,value.data(),required,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
  if(!count||count>=required)failed(GetLastError());
  value.resize(count);return std::filesystem::path(value).lexically_normal();
}
BY_HANDLE_FILE_INFORMATION information(HANDLE handle) {
  BY_HANDLE_FILE_INFORMATION info{};
  if(GetFileType(handle)!=FILE_TYPE_DISK)refused();
  if(!GetFileInformationByHandle(handle,&info))failed(GetLastError());
  if(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)refused(ERROR_REPARSE_TAG_INVALID);
  if(!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&info.nNumberOfLinks!=1)refused(ERROR_INVALID_DATA);
  return info;
}
void budget(const WorkspaceCleanupLimits& limits,std::chrono::steady_clock::time_point deadline,size_t count,size_t depth) {
  if(count>=limits.max_entries_per_workspace||depth>limits.max_depth||std::chrono::steady_clock::now()>=deadline)
    throw CleanupFailure{WorkspaceCleanupStatus::limit_reached,ERROR_NOT_ENOUGH_QUOTA};
}
void inspect(const std::filesystem::path& path,const std::filesystem::path& parent,size_t depth,
             const WorkspaceCleanupLimits& limits,std::chrono::steady_clock::time_point deadline,std::vector<Node>& nodes) {
  budget(limits,deadline,nodes.size(),depth);
  // OPEN_REPARSE_POINT inspects a link itself; retained handles deny rename/
  // replacement throughout preflight and deletion. No recursive API follows it.
  Handle handle(CreateFileW(path.c_str(),DELETE|FILE_READ_ATTRIBUTES|FILE_LIST_DIRECTORY,
      FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
  if(handle.value==INVALID_HANDLE_VALUE)failed(GetLastError());
  const auto info=information(handle.value);
  const bool directory=(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
  const auto actual=final_path(handle.value);
  if(!same_path(actual.parent_path(),parent)||!same_path(actual,path))refused();
  if(depth==0&&!directory)refused(ERROR_DIRECTORY);
  nodes.push_back({std::move(handle),actual,directory});
  if(!directory)return;
  WIN32_FIND_DATAW entry{};FindHandle search;
  search.value=FindFirstFileExW((actual/L"*").c_str(),FindExInfoBasic,&entry,FindExSearchNameMatch,nullptr,0);
  if(search.value==INVALID_HANDLE_VALUE){const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND)return;failed(error);}
  for(;;){
    if(std::wcscmp(entry.cFileName,L".")&&std::wcscmp(entry.cFileName,L"..")) {
      if(entry.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)refused(ERROR_REPARSE_TAG_INVALID);
      const std::filesystem::path name(entry.cFileName);
      if(name.empty()||name.has_parent_path()||name.native().find_first_of(L"/\\:")!=std::wstring::npos)refused();
      inspect(actual/name,actual,depth+1,limits,deadline,nodes);
    }
    if(!FindNextFileW(search.value,&entry)){const auto error=GetLastError();if(error!=ERROR_NO_MORE_FILES)failed(error);break;}
  }
}
bool ordinary_root(const std::filesystem::path& root) {
  const auto& text=root.native();
  return root.is_absolute()&&root.has_filename()&&root!=root.root_path()&&text.size()>=3&&
    ((text[0]>=L'A'&&text[0]<=L'Z')||(text[0]>=L'a'&&text[0]<=L'z'))&&text[1]==L':'&&
    text.find(L':',2)==std::wstring::npos;
}
#endif
}

WorkspaceCleanupResult cleanup_workspace_storage(const std::filesystem::path& supplied_root,
    const std::vector<std::string>& workspace_ids,WorkspaceCleanupLimits limits) {
  WorkspaceCleanupResult result;
  const auto deadline=std::chrono::steady_clock::now()+std::clamp(limits.max_elapsed,std::chrono::milliseconds(0),std::chrono::milliseconds(30000));
  limits.max_workspaces=std::min<size_t>(limits.max_workspaces,4096);
  limits.max_entries_per_workspace=std::min<size_t>(limits.max_entries_per_workspace,100000);
  limits.max_depth=std::min<size_t>(limits.max_depth,128);
  std::set<std::string> seen;
  for(size_t i=0;i<workspace_ids.size();++i) {
    if(result.entries.size()>=limits.max_workspaces){result.unprocessed=workspace_ids.size()-i;break;}
    const auto& id=workspace_ids[i];if(!seen.insert(id).second)continue;
    WorkspaceCleanupEntry entry;entry.workspace_id=id;
    if(!valid_id(id)){entry.status=WorkspaceCleanupStatus::refused;append(result,std::move(entry));continue;}
#ifdef _WIN32
    try {
      if(std::chrono::steady_clock::now()>=deadline)throw CleanupFailure{WorkspaceCleanupStatus::limit_reached,ERROR_NOT_ENOUGH_QUOTA};
      const auto root=supplied_root.lexically_normal();
      if(!ordinary_root(root))refused(ERROR_BAD_PATHNAME);
      Handle root_handle(CreateFileW(root.c_str(),FILE_READ_ATTRIBUTES|FILE_LIST_DIRECTORY,
          FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
          FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
      if(root_handle.value==INVALID_HANDLE_VALUE){const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND){entry.status=WorkspaceCleanupStatus::already_absent;append(result,std::move(entry));continue;}failed(error);}
      if(!(information(root_handle.value).dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))refused(ERROR_DIRECTORY);
      const auto actual_root=final_path(root_handle.value);
      const auto target=actual_root/("workspace-"+local_security::sha256(id));
      if(!same_path(target.parent_path(),actual_root)||same_path(target,actual_root))refused();
      const auto attributes=GetFileAttributesW(target.c_str());
      if(attributes==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND){entry.status=WorkspaceCleanupStatus::already_absent;append(result,std::move(entry));continue;}failed(error);}
      if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)refused(ERROR_REPARSE_TAG_INVALID);
      std::vector<Node> nodes;
      inspect(target,actual_root,0,limits,deadline,nodes);
      // Nothing is removed until the entire bounded tree has been inspected.
      // Reverse preorder closes children before disposing their parent handles.
      for(auto it=nodes.rbegin();it!=nodes.rend();++it) {
        budget(limits,deadline,0,0);
        information(it->handle.value);
        FILE_DISPOSITION_INFO remove{TRUE};
        if(!SetFileInformationByHandle(it->handle.value,FileDispositionInfo,&remove,sizeof(remove)))failed(GetLastError());
        it->handle.reset();
        if(it->directory)++entry.directories_removed;else ++entry.files_removed;
      }
      entry.status=WorkspaceCleanupStatus::removed;
    }catch(const CleanupFailure& failure){entry.status=failure.status;entry.system_error=failure.error;}
     catch(const std::exception&){entry.status=WorkspaceCleanupStatus::deferred;entry.system_error=ERROR_GEN_FAILURE;}
#else
    (void)supplied_root;entry.status=WorkspaceCleanupStatus::refused;
#endif
    append(result,std::move(entry));
  }
  return result;
}
}
