#include "xenon/workspace_storage.hpp"
#include "xenon/local_security.hpp"
#include <windows.h>
#include <winioctl.h>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace xenon;
namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void write(const fs::path& path){fs::create_directories(path.parent_path());std::ofstream file(path);file<<"synthetic storage only";require(file.good(),"Cannot create synthetic file");}
fs::path workspace(const fs::path& root,const std::string& id){return root/("workspace-"+local_security::sha256(id));}
void remove_fixture(const fs::path& path){
  const DWORD attributes=GetFileAttributesW(path.c_str());
  if(attributes==INVALID_FILE_ATTRIBUTES)return;
  if(attributes&FILE_ATTRIBUTE_REPARSE_POINT){
    if(attributes&FILE_ATTRIBUTE_DIRECTORY)RemoveDirectoryW(path.c_str());else DeleteFileW(path.c_str());
    return;
  }
  if(attributes&FILE_ATTRIBUTE_DIRECTORY){
    for(const auto& child:fs::directory_iterator(path))remove_fixture(child.path());
    RemoveDirectoryW(path.c_str());
  }else{SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_NORMAL);DeleteFileW(path.c_str());}
}
struct Scratch {
  fs::path parent=fs::absolute(fs::current_path()/".cache").lexically_normal();
  fs::path root=parent/("workspace-storage-"+local_security::random_hex(8));
  Scratch(){fs::create_directories(root);}
  ~Scratch(){if(root.is_absolute()&&root.parent_path()==parent&&root.filename().wstring().starts_with(L"workspace-storage-"))try{remove_fixture(root);}catch(...) {}}
};
void junction(const fs::path& path,const fs::path& target){
  struct MountPoint{ULONG tag;USHORT length,reserved,substitute_offset,substitute_length,print_offset,print_length;wchar_t paths[1];};
  const auto substitute=L"\\??\\"+target.native(),print=target.native();
  const size_t bytes=(substitute.size()+print.size()+2)*sizeof(wchar_t);
  std::vector<unsigned char> buffer(offsetof(MountPoint,paths)+bytes);
  auto* data=reinterpret_cast<MountPoint*>(buffer.data());
  data->tag=IO_REPARSE_TAG_MOUNT_POINT;data->length=static_cast<USHORT>(8+bytes);
  data->substitute_length=static_cast<USHORT>(substitute.size()*sizeof(wchar_t));
  data->print_offset=static_cast<USHORT>((substitute.size()+1)*sizeof(wchar_t));
  data->print_length=static_cast<USHORT>(print.size()*sizeof(wchar_t));
  std::memcpy(data->paths,substitute.c_str(),(substitute.size()+1)*sizeof(wchar_t));
  std::memcpy(reinterpret_cast<unsigned char*>(data->paths)+data->print_offset,print.c_str(),(print.size()+1)*sizeof(wchar_t));
  fs::create_directory(path);
  HANDLE handle=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
  require(handle!=INVALID_HANDLE_VALUE,"Cannot open synthetic junction");
  DWORD returned{};const bool okay=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,data,static_cast<DWORD>(buffer.size()),nullptr,0,&returned,nullptr)!=FALSE;
  CloseHandle(handle);require(okay,"Cannot create synthetic junction");
}
void status(const WorkspaceCleanupResult& result,WorkspaceCleanupStatus expected){
  require(result.entries.size()==1,"Unexpected cleanup entry count");
  if(result.entries[0].status!=expected)throw std::runtime_error(std::string("Unexpected cleanup status: ")+workspace_cleanup_status_name(result.entries[0].status)+", Windows error "+std::to_string(result.entries[0].system_error));
}
void selected_scope_only(){
  Scratch scratch;const auto root=scratch.root/"profile",selected=workspace(root,"selected"),other=workspace(root,"other");
  write(selected/"Network"/"Cookies");write(selected/"Cache"/"data");write(selected/"Preferences");
  write(other/"Preferences");write(root/"vault.sqlite3");write(root/"downloads"/"keep.txt");write(root/"import.csv");write(root/"broker-state.json");write(root/"Default"/"Preferences");
  auto result=cleanup_workspace_storage(root,{"selected"});status(result,WorkspaceCleanupStatus::removed);
  require(result.removed==1&&result.files_removed==3&&result.directories_removed==3,"Removal counts incorrect");
  require(!fs::exists(selected),"Selected profile remains");
  for(const auto& path:{other/"Preferences",root/"vault.sqlite3",root/"downloads"/"keep.txt",root/"import.csv",root/"broker-state.json",root/"Default"/"Preferences"})require(fs::exists(path),"Unrelated storage was changed");
  status(cleanup_workspace_storage(root,{"selected","selected"}),WorkspaceCleanupStatus::already_absent);
  status(cleanup_workspace_storage(scratch.root/"missing",{"selected"}),WorkspaceCleanupStatus::already_absent);
}
void refused_inputs(){
  Scratch scratch;const auto root=scratch.root/"profile";write(workspace(root,"valid")/"keep");
  for(const auto& id:std::vector<std::string>{"","../valid","..\\valid","C:\\root","valid:stream",std::string(161,'a')})status(cleanup_workspace_storage(root,{id}),WorkspaceCleanupStatus::refused);
  status(cleanup_workspace_storage("relative",{"valid"}),WorkspaceCleanupStatus::refused);
  status(cleanup_workspace_storage(root.root_path(),{"valid"}),WorkspaceCleanupStatus::refused);
  write(workspace(root,"file"));status(cleanup_workspace_storage(root,{"file"}),WorkspaceCleanupStatus::refused);
  require(fs::exists(workspace(root,"valid")/"keep")&&fs::exists(workspace(root,"file")),"Invalid input removed storage");
}
void refuse_links_before_removal(){
  Scratch scratch;const auto root=scratch.root/"profile",outside=scratch.root/"outside";write(outside/"sentinel");fs::create_directory(root);
  const auto root_link=scratch.root/"root-link";junction(root_link,root);
  status(cleanup_workspace_storage(root_link,{"selected"}),WorkspaceCleanupStatus::refused);
  require(RemoveDirectoryW(root_link.c_str()),"Cannot remove synthetic root junction");
  auto selected=workspace(root,"selected");junction(selected,outside);
  status(cleanup_workspace_storage(root,{"selected"}),WorkspaceCleanupStatus::refused);
  require(RemoveDirectoryW(selected.c_str()),"Cannot remove synthetic target junction");
  write(selected/"ordinary");junction(selected/"linked",outside);
  auto result=cleanup_workspace_storage(root,{"selected"});status(result,WorkspaceCleanupStatus::refused);
  require(result.files_removed==0&&result.directories_removed==0&&fs::exists(selected/"ordinary")&&fs::exists(outside/"sentinel"),"Reparse preflight changed files");
  require(RemoveDirectoryW((selected/"linked").c_str()),"Cannot remove synthetic nested junction");
  require(CreateHardLinkW((selected/"hardlink").c_str(),(outside/"sentinel").c_str(),nullptr),"Cannot create synthetic hardlink");
  result=cleanup_workspace_storage(root,{"selected"});status(result,WorkspaceCleanupStatus::refused);
  require(result.files_removed==0&&fs::exists(selected/"ordinary")&&fs::exists(outside/"sentinel"),"Hardlink preflight changed files");
  require(DeleteFileW((selected/"hardlink").c_str()),"Cannot remove synthetic hardlink");
  status(cleanup_workspace_storage(root,{"selected"}),WorkspaceCleanupStatus::removed);
  require(fs::exists(outside/"sentinel"),"Outside sentinel removed");
}
void locked_then_retry(){
  Scratch scratch;const auto root=scratch.root/"profile",selected=workspace(root,"locked");write(selected/"a-normal");write(selected/"z-locked");
  HANDLE lock=CreateFileW((selected/"z-locked").c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
  require(lock!=INVALID_HANDLE_VALUE,"Cannot lock synthetic file");
  const auto result=cleanup_workspace_storage(root,{"locked"});CloseHandle(lock);
  status(result,WorkspaceCleanupStatus::deferred);
  require(result.files_removed==0&&result.directories_removed==0&&fs::exists(selected/"a-normal")&&fs::exists(selected/"z-locked"),"Locked preflight partially deleted profile");
  status(cleanup_workspace_storage(root,{"locked"}),WorkspaceCleanupStatus::removed);
}
void bounded_and_retry(){
  Scratch scratch;const auto root=scratch.root/"profile",selected=workspace(root,"bounded");write(selected/"deep"/"deeper"/"leaf");write(selected/"normal");
  WorkspaceCleanupLimits limits;limits.max_entries_per_workspace=2;
  auto result=cleanup_workspace_storage(root,{"bounded"},limits);status(result,WorkspaceCleanupStatus::limit_reached);
  require(result.files_removed==0&&fs::exists(selected/"normal"),"Entry-bound preflight deleted data");
  limits={};limits.max_depth=1;result=cleanup_workspace_storage(root,{"bounded"},limits);status(result,WorkspaceCleanupStatus::limit_reached);
  require(result.files_removed==0&&fs::exists(selected/"deep"/"deeper"/"leaf"),"Depth-bound preflight deleted data");
  limits={};limits.max_elapsed=std::chrono::milliseconds(0);status(cleanup_workspace_storage(root,{"bounded"},limits),WorkspaceCleanupStatus::limit_reached);
  limits={};limits.max_workspaces=1;write(workspace(root,"later")/"keep");result=cleanup_workspace_storage(root,{"absent","bounded","later"},limits);
  require(result.entries.size()==1&&result.unprocessed==2&&fs::exists(selected/"normal")&&fs::exists(workspace(root,"later")/"keep"),"Workspace bound was not enforced");
  status(cleanup_workspace_storage(root,{"bounded"}),WorkspaceCleanupStatus::removed);
  require(fs::exists(workspace(root,"later")/"keep"),"Retry removed unselected workspace");
}
void readonly_failure_is_honest(){
  Scratch scratch;const auto root=scratch.root/"profile",selected=workspace(root,"readonly");write(selected/"readonly-file");
  require(SetFileAttributesW((selected/"readonly-file").c_str(),FILE_ATTRIBUTE_READONLY),"Cannot mark synthetic file readonly");
  status(cleanup_workspace_storage(root,{"readonly"}),WorkspaceCleanupStatus::deferred);
  require(fs::exists(selected/"readonly-file"),"Readonly file unexpectedly removed");
  require(SetFileAttributesW((selected/"readonly-file").c_str(),FILE_ATTRIBUTE_NORMAL),"Cannot restore synthetic attributes");
  status(cleanup_workspace_storage(root,{"readonly"}),WorkspaceCleanupStatus::removed);
}
}
int wmain(int argc,wchar_t** argv){try{
  if(argc==3&&std::wstring(argv[1])==L"--synthetic-cleanup"){
    const auto root=fs::absolute(argv[2]).lexically_normal();
    require(root.parent_path().filename()==L".cache"&&root.filename().wstring().starts_with(L"auth-integration-removal-"),"Probe requires a generated removal fixture");
    std::ifstream marker(root/"SYNTHETIC_TEST_PROFILE",std::ios::binary);
    const std::string value{std::istreambuf_iterator<char>(marker),{}};
    require(value=="XENON_SYNTHETIC_AUTH_FIXTURE\n","Probe requires the synthetic fixture marker");
    const auto result=cleanup_workspace_storage(root,{"auth_fixture_shared"});
    require(result.entries.size()==1,"Probe result missing");const auto& entry=result.entries[0];
    std::cout<<"{\"status\":\""<<workspace_cleanup_status_name(entry.status)<<"\",\"filesRemoved\":"<<entry.files_removed
      <<",\"directoriesRemoved\":"<<entry.directories_removed<<",\"systemError\":"<<entry.system_error<<"}\n";return 0;
  }
  require(argc==1,"Unexpected test arguments");
  selected_scope_only();refused_inputs();refuse_links_before_removal();locked_then_retry();bounded_and_retry();readonly_failure_is_honest();
  std::cout<<"Workspace storage cleanup tests passed (6 scenarios).\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
