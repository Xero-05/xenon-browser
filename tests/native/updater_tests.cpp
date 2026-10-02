#include "xenon/updater.hpp"
#include "xenon/local_security.hpp"
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace xenon;
using namespace xenon::updates;
namespace fs=std::filesystem;
namespace {
void require(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
template<class F> void rejected(F action,const char* message){try{action();}catch(const std::exception&){return;}throw std::runtime_error(message);}
const std::string content="Synthetic Xenon update fixture; never executable.\r\n";
Release release(const std::string& version="0.1.0-alpha.10"){
  return {version,"https://github.com/Xero-05/xenon-browser/releases/download/v"+version+"/Xenon-"+version+"-windows-x64-setup-unsigned.exe",local_security::sha256(content),content.size()};
}
Json metadata(const std::string& version="0.1.0-alpha.10"){
  const auto value=release(version);
  return {{"draft",false},{"prerelease",version.find('-')!=std::string::npos},{"tag_name","v"+version},
    {"assets",Json::array({{{"name","Xenon-"+version+"-windows-x64-setup-unsigned.exe"},{"state","uploaded"},{"digest","sha256:"+value.sha256},{"size",value.bytes},{"browser_download_url",value.url}}})}};
}
struct Scratch {
  fs::path parent=fs::absolute(fs::current_path()/".cache").lexically_normal();
  fs::path root=parent/("updater-tests-"+local_security::random_hex(8));
  Scratch(){fs::create_directories(root);}
  ~Scratch(){if(root.is_absolute()&&root.parent_path()==parent&&root.filename().wstring().starts_with(L"updater-tests-"))try{fs::remove_all(root);}catch(...) {}}
};
void write(const fs::path& path,const std::string& bytes){std::ofstream stream(path,std::ios::binary);stream.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));require(stream.good(),"Cannot write updater fixture");}
void no_stages(const fs::path& root){require(!fs::exists(root/"updates")||fs::is_empty(root/"updates"),"Failed or canceled download left a partial or published stage");}
void versions(){
  auto selected=select_release(Json::array({metadata("0.1.0-alpha.8"),metadata("0.1.0-alpha.10"),metadata("0.1.0-alpha.9")}),"0.1.0-alpha.9");
  require(selected&&selected->version=="0.1.0-alpha.10","Numeric prerelease ordering is incorrect");
  selected=select_release(Json::array({metadata("0.1.0-alpha.999"),metadata("0.1.0")}),"0.1.0-alpha.9");
  require(selected&&selected->version=="0.1.0","Stable release must sort above same-core prereleases");
  require(!select_release(Json::array({metadata("1.1.0-alpha.1")}),"1.0.0"),"Stable installations must not enter prerelease channel");
  auto flagged=metadata("1.1.0");flagged["prerelease"]=true;
  require(!select_release(Json::array({flagged}),"1.0.0"),"GitHub prerelease flag must also exclude stable-channel updates");
  require(!select_release(Json::array({metadata("0.1.0-alpha.10"),metadata("0.1.0-alpha.9"),metadata("0.1.0-alpha.10+build")}),"0.1.0-alpha.10"),"Same precedence or older release offered");
  selected=select_release(Json::array({metadata("0.1.0-beta"),metadata("0.1.0-alpha.999999999999999999999")}),"0.1.0-alpha.10");
  require(selected&&selected->version=="0.1.0-beta","Identifier ordering must implement SemVer");
  for(const auto* bad:{"01.0.0","1.0","1.0.0-alpha.01","1.0.0-","1.0.0+","1.0.0+bad+metadata","1.0.0-alpha/evil","18446744073709551616.0.0"}){
    require(!select_release(Json::array({metadata(bad)}),"0.0.0-alpha.1"),"Malformed release version accepted");
    rejected([&]{select_release(Json::array(),bad);},"Malformed current version accepted");
  }
  auto duplicate=metadata();auto conflict=duplicate;conflict["assets"][0]["digest"]="sha256:"+std::string(64,'1');
  rejected([&]{select_release(Json::array({duplicate,conflict}),"0.1.0-alpha.9");},"Conflicting equal-precedence releases accepted");
}
void asset_boundaries(){
  const auto reject=[](const Json& candidate){require(!select_release(Json::array({candidate}),"0.1.0-alpha.9"),"Invalid installer metadata selected");};
  for(const auto* field:{"draft","prerelease","tag_name","assets"}){auto item=metadata();item.erase(field);reject(item);}
  auto item=metadata();item["draft"]=true;reject(item);
  item=metadata();item["tag_name"]="0.1.0-alpha.10";reject(item);
  item=metadata();item["assets"].push_back(item["assets"][0]);reject(item);
  for(const auto* field:{"state","digest","size","browser_download_url","name"}){item=metadata();item["assets"][0].erase(field);reject(item);}
  for(const auto& size:std::vector<Json>{0,-1,1.5,"50",1024ull*1024*1024+1,std::numeric_limits<uint64_t>::max()}){item=metadata();item["assets"][0]["size"]=size;reject(item);}
  for(const auto& digest:std::vector<std::string>{"","sha512:"+std::string(64,'a'),"sha256:"+std::string(63,'a'),"sha256:"+std::string(64,'g')}){item=metadata();item["assets"][0]["digest"]=digest;reject(item);}
  item=metadata();item["assets"][0]["state"]="new";reject(item);
  item=metadata();item["assets"][0]["name"]="Xenon-0.1.0-alpha.10-windows-x64.zip";reject(item);
  const auto good=release().url;
  for(const auto& bad:std::vector<std::string>{"http"+good.substr(5),"https://example.com/file.exe","https://github.com/Other/repo/releases/download/v0.1.0-alpha.10/file.exe",good+"?other=1",good+"#fragment",release("0.1.0-alpha.9").url}){item=metadata();item["assets"][0]["browser_download_url"]=bad;reject(item);}
  item=metadata();auto digest=release().sha256;std::transform(digest.begin(),digest.end(),digest.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});item["assets"][0]["digest"]="sha256:"+digest;
  const auto selected=select_release(Json::array({item}),"0.1.0-alpha.9");require(selected&&selected->sha256==release().sha256,"Valid SHA-256 not normalized");
  rejected([]{select_release(Json::object(),"0.1.0-alpha.9");},"Non-array release list accepted");
  rejected([]{select_release(Json(std::vector<Json>(101,metadata())),"0.1.0-alpha.9");},"Unbounded release list accepted");
}
void transport_urls(){
  require(detail::allowed_redirect_url(release().url,false),"Exact installer URL rejected");
  require(detail::allowed_redirect_url("https://release-assets.githubusercontent.com/github-production-release-asset/42?token=synthetic",false),"GitHub asset redirect rejected");
  const std::string api="https://api.github.com/repos/Xero-05/xenon-browser/releases?per_page=100";
  require(detail::allowed_redirect_url(api,true),"Fixed metadata URL rejected");
  require(!detail::allowed_redirect_url(api,false)&&!detail::allowed_redirect_url(release().url,true),"Metadata and installer origin scopes crossed");
  for(const auto* bad:{"http://release-assets.githubusercontent.com/file","https://release-assets.githubusercontent.com.evil.invalid/file","https://release-assets.githubusercontent.com@evil.invalid/file","https://evil.invalid@release-assets.githubusercontent.com/file","https://release-assets.githubusercontent.com:443/file","https://release-assets.githubusercontent.com/file#fragment","https://release-assets.githubusercontent.com/file\nInjected","https://release-assets.githubusercontent.com\\evil/file","https://api.github.com/other","https://github.com/Xero-05/xenon-browser/releases/download/v../file.exe","https://github.com/Xero-05/xenon-browser/releases/download/v%2e%2e/file.exe","https://github.com/Other/repo/releases/download/v1/file.exe"})require(!detail::allowed_redirect_url(bad,false),"Unsafe redirect URL accepted");
  require(!detail::allowed_redirect_url(api+"&extra=1",true),"Metadata redirect changed fixed API scope");
}
void bounded_json(){
  require(detail::select_release_json(Json::array({metadata()}).dump(),"0.1.0-alpha.9").has_value(),"Valid JSON metadata rejected");
  for(const auto& bad:std::vector<std::string>{"{ malformed",std::string(40,'[')+"0"+std::string(40,']'),std::string(4*1024*1024+1,' ')})rejected([&]{detail::select_release_json(bad,"0.1.0-alpha.9");},"Malformed or unbounded release metadata accepted");
}
void file_verification(){
  Scratch scratch;const auto path=scratch.root/"fixture.bin";write(path,content);auto value=release();detail::verify_installer_file(path,value);
  value.bytes++;rejected([&]{detail::verify_installer_file(path,value);},"Wrong installer size accepted");
  value=release();value.sha256=std::string(64,'0');rejected([&]{detail::verify_installer_file(path,value);},"Wrong installer hash accepted");
  value=release();auto changed=content;changed[0]='X';write(path,changed);rejected([&]{detail::verify_installer_file(path,value);},"Changed installer passed immediate re-verification");write(path,content);
  const auto link=scratch.root/"hardlink.bin";require(CreateHardLinkW(link.c_str(),path.c_str(),nullptr)!=FALSE,"Cannot create synthetic installer hardlink");
  rejected([&]{detail::verify_installer_file(path,value);},"Hardlinked installer accepted");fs::remove(link);
  rejected([&]{detail::verify_installer_file(scratch.root,value);},"Directory accepted as installer");
  HANDLE writer=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);require(writer!=INVALID_HANDLE_VALUE,"Cannot hold synthetic writer");
  try{rejected([&]{detail::verify_installer_file(path,value);},"Writable open file passed exclusive verification");}catch(...){CloseHandle(writer);throw;}CloseHandle(writer);
  detail::verify_installer_file(path,value);
}
void staged_downloads(){
  Scratch scratch;std::atomic_bool cancel=false;const auto value=release();std::vector<uint64_t> progress;
  const auto send=[](const detail::ByteSink& sink){sink(reinterpret_cast<const unsigned char*>(content.data()),content.size());};
  const auto path=detail::stage_installer(value,scratch.root/"complete",[&](uint64_t current,uint64_t total){require(total==content.size(),"Incorrect progress total");progress.push_back(current);},cancel,send);
  require(path.parent_path().parent_path()==scratch.root/"complete"/"updates"&&path.filename()=="Xenon-0.1.0-alpha.10-windows-x64-setup-unsigned.exe","Unexpected published update path");
  require(progress==std::vector<uint64_t>{0,content.size()},"Progress did not report bounded download");detail::verify_installer_file(path,value);require(!fs::exists(path.parent_path()/"installer.part"),"Successful publication retained partial file");
  for(const auto* scenario:{"truncated","oversized","corrupt","interrupted","cancel"}){
    cancel=false;const auto root=scratch.root/scenario;
    rejected([&]{detail::stage_installer(value,root,{},cancel,[&](const detail::ByteSink& sink){
      const std::string kind=scenario;
      if(kind=="corrupt"){auto bytes=content;bytes[0]='X';sink(reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size());return;}
      if(kind=="oversized"){const auto bytes=content+"extra";sink(reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size());return;}
      sink(reinterpret_cast<const unsigned char*>(content.data()),3);
      if(kind=="interrupted")throw std::runtime_error("Synthetic interrupted transport");
      if(kind=="cancel"){cancel=true;sink(reinterpret_cast<const unsigned char*>(content.data()+3),content.size()-3);}
    });},"Failed synthetic transfer unexpectedly published");
    no_stages(root);
  }
  cancel=true;bool called=false;rejected([&]{detail::stage_installer(value,scratch.root/"pre-canceled",{},cancel,[&](const detail::ByteSink&){called=true;});},"Pre-canceled update succeeded");require(!called&&!fs::exists(scratch.root/"pre-canceled"),"Pre-canceled update touched storage or source");
  // Production admission rejects cancellation/metadata before locating default
  // profile storage. These calls cannot write to an installed browser profile.
  rejected([&]{download_installer(value,{},cancel);},"Production download ignored cancellation");
  auto bad=value;bad.url="https://example.invalid/not-xenon.exe";cancel=false;rejected([&]{download_installer(bad,{},cancel);},"Production download accepted arbitrary URL");
  rejected([&]{detail::stage_installer(value,"relative",{},cancel,send);},"Relative staging root accepted");
}
}
int main(int argc,char** argv){
  try{
    if(argc>1&&std::string(argv[1])=="--check-live"){
      const auto selected=check_for_update(argc>2?argv[2]:"0.1.0-alpha.9");
      std::cout<<(selected?"Available release: "+selected->version:"No eligible newer installer release")<<"\n";return 0;
    }
    if(argc>1&&std::string(argv[1])=="--download-live"){
      Scratch scratch;const auto selected=check_for_update(argc>2?argv[2]:"0.1.0-alpha.9");require(selected.has_value(),"No eligible release for download smoke");std::atomic_bool cancel=false;
      const auto path=detail::download_installer_in(*selected,scratch.root/"download",{},cancel);detail::verify_installer_file(path,*selected);
      std::cout<<"Verified release download: "<<selected->version<<" ("<<selected->bytes<<" bytes); no installer launched\n";return 0;
    }
    versions();asset_boundaries();transport_urls();bounded_json();file_verification();staged_downloads();
    std::cout<<"Updater tests passed: SemVer/channel, metadata, redirect, bounded JSON, file integrity, staged transfer cleanup\n";return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
