#include "xenon/updater.hpp"
#include "xenon/local_security.hpp"
#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace xenon::updates {
namespace {
constexpr uint64_t max_installer = 1024ull * 1024 * 1024;
constexpr size_t max_metadata = 4 * 1024 * 1024;
constexpr auto api_url = "https://api.github.com/repos/Xero-05/xenon-browser/releases?per_page=100";
constexpr auto asset_prefix = "https://github.com/Xero-05/xenon-browser/releases/download/v";
using Clock = std::chrono::steady_clock;
[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
std::vector<std::string> split(std::string_view text, char delimiter) {
  std::vector<std::string> result;
  do { auto at=text.find(delimiter); result.emplace_back(text.substr(0,at)); if(at==text.npos)break; text.remove_prefix(at+1); } while(true);
  return result;
}
bool numeric(std::string_view value) { return !value.empty() && std::all_of(value.begin(),value.end(),[](char c){return c>='0'&&c<='9';}); }
struct Version { std::array<uint64_t,3> core{}; std::vector<std::string> pre; };
std::optional<Version> version(std::string_view text) {
  if(text.empty()||text.size()>100)return {};
  const auto plus=text.find('+');
  if(plus!=text.npos) {
    auto build=split(text.substr(plus+1),'.');
    for(const auto& part:build)if(part.empty()||!std::all_of(part.begin(),part.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-';}))return {};
    text=text.substr(0,plus);
  }
  Version result;const auto dash=text.find('-');
  if(dash!=text.npos){
    result.pre=split(text.substr(dash+1),'.');text=text.substr(0,dash);
    for(const auto& part:result.pre)if(part.empty()||(numeric(part)&&part.size()>1&&part[0]=='0')||!std::all_of(part.begin(),part.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-';}))return {};
  }
  auto core=split(text,'.');if(core.size()!=3)return {};
  for(size_t i=0;i<3;++i){if(!numeric(core[i])||(core[i].size()>1&&core[i][0]=='0'))return {};const auto parsed=std::from_chars(core[i].data(),core[i].data()+core[i].size(),result.core[i]);if(parsed.ec!=std::errc{}||parsed.ptr!=core[i].data()+core[i].size())return {};}
  return result;
}
int compare(const Version& a,const Version& b) {
  if(a.core!=b.core)return a.core<b.core?-1:1;
  if(a.pre.empty()||b.pre.empty())return a.pre.empty()==b.pre.empty()?0:a.pre.empty()?1:-1;
  for(size_t i=0;i<std::min(a.pre.size(),b.pre.size());++i){
    const auto& x=a.pre[i];const auto& y=b.pre[i];if(x==y)continue;
    const bool xn=numeric(x),yn=numeric(y);if(xn!=yn)return xn?-1:1;
    if(xn&&x.size()!=y.size())return x.size()<y.size()?-1:1;
    return x<y?-1:1;
  }
  return a.pre.size()==b.pre.size()?0:a.pre.size()<b.pre.size()?-1:1;
}
std::string lower(std::string text){for(auto& c:text)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return text;}
bool hash_valid(const std::string& hash){return hash.size()==64&&std::all_of(hash.begin(),hash.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');});}
std::string filename(const Release& release){return "Xenon-"+release.version+"-windows-x64-setup-unsigned.exe";}
bool release_valid(const Release& release){return version(release.version).has_value()&&release.bytes>0&&release.bytes<=max_installer&&hash_valid(release.sha256)&&release.url==std::string(asset_prefix)+release.version+"/"+filename(release);}
struct Url {std::string host,path;};
std::optional<Url> parse_url(const std::string& text){
  if(text.size()>8192||!text.starts_with("https://")||std::any_of(text.begin(),text.end(),[](unsigned char c){return c<=32||c>=127||c=='\\'||c=='#';}))return {};
  const auto slash=text.find('/',8);if(slash==std::string::npos)return {};
  auto host=lower(text.substr(8,slash-8));if(host!="api.github.com"&&host!="github.com"&&host!="release-assets.githubusercontent.com")return {};
  return Url{std::move(host),text.substr(slash)};
}
std::wstring widen(const std::string& text){return std::wstring(text.begin(),text.end());} // All permitted URL/asset bytes are ASCII.
struct Internet {
  HINTERNET value{};
  explicit Internet(HINTERNET h=nullptr):value(h){}
  ~Internet(){if(value)WinHttpCloseHandle(value);}
  Internet(const Internet&)=delete;Internet& operator=(const Internet&)=delete;
};
struct File {
  HANDLE value=INVALID_HANDLE_VALUE;
  explicit File(HANDLE h=INVALID_HANDLE_VALUE):value(h){}
  ~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
  File(File&& other)noexcept:value(std::exchange(other.value,INVALID_HANDLE_VALUE)){}
  File(const File&)=delete;File& operator=(const File&)=delete;
};
void check_budget(const std::atomic_bool* cancel,Clock::time_point deadline){
  if(cancel&&cancel->load(std::memory_order_relaxed))fail("Update download canceled.");
  if(Clock::now()>=deadline)fail("The update request timed out. Try again later.");
}
std::wstring header(HINTERNET request,DWORD name){
  DWORD bytes{};WinHttpQueryHeaders(request,name,WINHTTP_HEADER_NAME_BY_INDEX,nullptr,&bytes,WINHTTP_NO_HEADER_INDEX);
  if(GetLastError()==ERROR_WINHTTP_HEADER_NOT_FOUND)return {};
  if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER||bytes>32768||bytes%sizeof(wchar_t))fail("The update server returned invalid headers.");
  std::wstring value(bytes/sizeof(wchar_t),L'\0');
  if(!WinHttpQueryHeaders(request,name,WINHTTP_HEADER_NAME_BY_INDEX,value.data(),&bytes,WINHTTP_NO_HEADER_INDEX))fail("The update server returned invalid headers.");
  while(!value.empty()&&value.back()==L'\0')value.pop_back();return value;
}
void get(const std::string& initial,bool metadata,uint64_t limit,const std::atomic_bool* cancel,
         Clock::time_point deadline,const std::function<void(const unsigned char*,size_t)>& consume){
  Internet session(WinHttpOpen(L"Xenon-Native-Updater/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
  if(!session.value)fail("The native HTTPS updater is unavailable.");
  DWORD protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
  if(!WinHttpSetOption(session.value,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof(protocols)))fail("Secure update transport is unavailable.");
  std::string current=initial;
  for(unsigned redirects=0;redirects<=5;++redirects){
    check_budget(cancel,deadline);
    if(!detail::allowed_redirect_url(current,metadata))fail("The update redirect destination was rejected.");
    const auto parsed=*parse_url(current);const auto host=widen(parsed.host),path=widen(parsed.path);
    Internet connection(WinHttpConnect(session.value,host.c_str(),INTERNET_DEFAULT_HTTPS_PORT,0));
    if(!connection.value)fail("Cannot connect to the update service.");
    Internet request(WinHttpOpenRequest(connection.value,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    if(!request.value)fail("Cannot create the secure update request.");
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER,disabled=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION,autologon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH,header_limit=32768;
    if(!WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect))||
       !WinHttpSetOption(request.value,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled))||
       !WinHttpSetOption(request.value,WINHTTP_OPTION_AUTOLOGON_POLICY,&autologon,sizeof(autologon))||
       !WinHttpSetOption(request.value,WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE,&header_limit,sizeof(header_limit)))fail("Cannot apply update transport protections.");
    auto timeout=[&]{check_budget(cancel,deadline);const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count();const int ms=static_cast<int>(std::clamp<int64_t>(remaining,1,10000));if(!WinHttpSetTimeouts(request.value,ms,ms,ms,ms))fail("Cannot set update time limits.");};
    timeout();
    const auto headers=metadata?L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n":L"Accept: application/octet-stream\r\n";
    if(!WinHttpSendRequest(request.value,headers,static_cast<DWORD>(-1),WINHTTP_NO_REQUEST_DATA,0,0,0))fail("The secure update request failed. Check the network and try again.");
    timeout();if(!WinHttpReceiveResponse(request.value,nullptr))fail("The update service did not return a valid response.");
    check_budget(cancel,deadline);
    DWORD status{},bytes=sizeof(status);
    if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&bytes,WINHTTP_NO_HEADER_INDEX))fail("Cannot read the update response status.");
    if(status==301||status==302||status==303||status==307||status==308){
      if(redirects==5)fail("The update service redirected too many times.");
      const auto location=header(request.value,WINHTTP_QUERY_LOCATION);
      if(location.empty()||std::any_of(location.begin(),location.end(),[](wchar_t c){return c>126||c<33;}))fail("The update service returned an invalid redirect.");
      current.assign(location.begin(),location.end());continue;
    }
    if(status==403||status==429)fail("The update service is rate limited or unavailable. Try again later.");
    if(status!=200)fail("The update service returned an unsuccessful response.");
    auto length=header(request.value,WINHTTP_QUERY_CONTENT_LENGTH);
    if(!length.empty()){
      uint64_t advertised{};std::string number(length.begin(),length.end());const auto n=std::from_chars(number.data(),number.data()+number.size(),advertised);
      if(n.ec!=std::errc{}||n.ptr!=number.data()+number.size()||advertised>limit||(!metadata&&advertised!=limit))fail("The update response size does not match its metadata.");
    }
    auto encoding=header(request.value,WINHTTP_QUERY_CONTENT_ENCODING);
    if(!encoding.empty()&&encoding!=L"identity")fail("Compressed update responses are unsupported.");
    uint64_t received{};std::array<unsigned char,65536> buffer{};
    for(;;){timeout();DWORD read{};if(!WinHttpReadData(request.value,buffer.data(),static_cast<DWORD>(buffer.size()),&read)){check_budget(cancel,deadline);fail("The update download was interrupted. Try again later.");}check_budget(cancel,deadline);if(!read)break;if(read>limit||received>limit-read)fail("The update response exceeded its permitted size.");received+=read;consume(buffer.data(),read);}
    if(!metadata&&received!=limit)fail("The update download was incomplete.");return;
  }
  fail("The update request could not complete.");
}
std::filesystem::path local_root(){
  PWSTR value{};if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&value)))fail("Cannot locate private update storage.");
  std::filesystem::path root(value);CoTaskMemFree(value);
  if(!root.is_absolute()||root.native().starts_with(L"\\\\")||root.root_name().native().size()!=2)fail("Private update storage requires a local Windows path.");
  return root.lexically_normal();
}
bool same_path(const std::filesystem::path& a,const std::filesystem::path& b){return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_EQUAL;}
void ordinary(HANDLE handle,bool directory){
  BY_HANDLE_FILE_INFORMATION info{};
  if(GetFileType(handle)!=FILE_TYPE_DISK||!GetFileInformationByHandle(handle,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||!!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=directory||(!directory&&info.nNumberOfLinks!=1))fail("The update staging path is not an ordinary private file or directory.");
}
std::vector<File> directories(const std::filesystem::path& path,bool create,const std::filesystem::path& private_root){
  if(!path.is_absolute()||path.native().starts_with(L"\\\\")||path.root_name().native().size()!=2||path!=path.lexically_normal())fail("The update staging path is invalid.");
  std::vector<File> held;auto cursor=path.root_path();
  auto open=[&]{File handle(CreateFileW(cursor.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));if(handle.value==INVALID_HANDLE_VALUE)fail("Cannot protect the update directory.");ordinary(handle.value,true);held.push_back(std::move(handle));};
  open();bool protect=false;
  for(const auto& part:path.relative_path()){
    cursor/=part;if(same_path(cursor,private_root))protect=true;
    if(create&&protect){local_security::SecurityDescriptor policy(true);if(!CreateDirectoryW(cursor.c_str(),&policy.attributes)&&GetLastError()!=ERROR_ALREADY_EXISTS)fail("Cannot create private update storage.");}
    open();if(protect)local_security::restrict_path(cursor);
  }
  return held;
}
std::string hash_handle(HANDLE file){
  BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)fail("Update hashing is unavailable.");
  struct Cleanup{BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}}cleanup{algorithm,hash};
  if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)fail("Update hashing is unavailable.");
  LARGE_INTEGER zero{};if(!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN))fail("Cannot read the staged update.");
  std::array<unsigned char,65536> bytes{};DWORD read{};
  while(true){if(!ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr))fail("Cannot verify the staged update.");if(!read)break;if(BCryptHashData(hash,bytes.data(),read,0)<0)fail("Cannot hash the staged update.");}
  std::array<unsigned char,32> digest{};if(BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0)fail("Cannot finish update verification.");
  std::string result;constexpr auto alphabet="0123456789abcdef";for(auto b:digest){result+=alphabet[b>>4];result+=alphabet[b&15];}return result;
}
File verified_file(const std::filesystem::path& path,const Release& release){
  if(!release_valid(release))fail("Update release metadata is invalid.");
  File file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
  if(file.value==INVALID_HANDLE_VALUE)fail("Cannot open the staged update for verification.");ordinary(file.value,false);
  LARGE_INTEGER size{};if(!GetFileSizeEx(file.value,&size)||size.QuadPart<0||static_cast<uint64_t>(size.QuadPart)!=release.bytes)fail("The staged update size does not match its release.");
  if(hash_handle(file.value)!=lower(release.sha256))fail("The staged update failed SHA-256 verification.");return file;
}
}

bool detail::allowed_redirect_url(const std::string& url,bool metadata){
  const auto parsed=parse_url(url);if(!parsed)return false;
  if(metadata)return parsed->host=="api.github.com"&&parsed->path=="/repos/Xero-05/xenon-browser/releases?per_page=100";
  if(parsed->host=="release-assets.githubusercontent.com")return true;
  constexpr std::string_view prefix="/Xero-05/xenon-browser/releases/download/v";
  if(parsed->host!="github.com"||!parsed->path.starts_with(prefix))return false;
  const auto rest=parsed->path.substr(prefix.size());const auto slash=rest.find('/');
  if(slash==std::string::npos||!version(rest.substr(0,slash)))return false;
  return rest.substr(slash+1)=="Xenon-"+rest.substr(0,slash)+"-windows-x64-setup-unsigned.exe";
}
void detail::verify_installer_file(const std::filesystem::path& path,const Release& release){auto file=verified_file(path,release);}

std::optional<Release> select_release(const Json& releases,const std::string& current){
  const auto running=version(current);if(!running)fail("The running Xenon version is invalid.");
  if(!releases.is_array()||releases.size()>100)fail("The update release list is invalid.");
  std::optional<Release> selected;std::optional<Version> selected_version;
  for(const auto& item:releases){
    if(!item.is_object()||!item.contains("draft")||!item["draft"].is_boolean()||item["draft"].get<bool>()||!item.contains("prerelease")||!item["prerelease"].is_boolean()||!item.contains("tag_name")||!item["tag_name"].is_string())continue;
    const auto tag=item["tag_name"].get<std::string>();if(!tag.starts_with('v'))continue;const auto candidate=version(tag.substr(1));
    if(!candidate||compare(*candidate,*running)<=0||(running->pre.empty()&&(!candidate->pre.empty()||item["prerelease"].get<bool>())))continue;
    if(!item.contains("assets")||!item["assets"].is_array()||item["assets"].size()>100)continue;
    Release release;release.version=tag.substr(1);const Json* asset=nullptr;bool duplicate=false;
    for(const auto& value:item["assets"])if(value.is_object()&&value.contains("name")&&value["name"].is_string()&&value["name"].get<std::string>()==filename(release)){if(asset)duplicate=true;asset=&value;}
    if(!asset||duplicate||!asset->contains("state")||(*asset)["state"]!="uploaded"||!asset->contains("digest")||!(*asset)["digest"].is_string()||!asset->contains("size")||!(*asset)["size"].is_number_integer()||!asset->contains("browser_download_url")||!(*asset)["browser_download_url"].is_string())continue;
    const auto& size=(*asset)["size"];if((size.is_number_unsigned()&&size.get<uint64_t>()>max_installer)||(!size.is_number_unsigned()&&size.get<int64_t>()<=0))continue;
    release.bytes=size.get<uint64_t>();release.url=(*asset)["browser_download_url"].get<std::string>();const auto digest=(*asset)["digest"].get<std::string>();if(!digest.starts_with("sha256:"))continue;release.sha256=lower(digest.substr(7));if(!release_valid(release))continue;
    if(selected_version&&compare(*candidate,*selected_version)==0&&(selected->version!=release.version||selected->sha256!=release.sha256||selected->bytes!=release.bytes))fail("The update service returned ambiguous releases.");
    if(!selected_version||compare(*candidate,*selected_version)>0){selected=std::move(release);selected_version=candidate;}
  }
  return selected;
}
std::optional<Release> check_for_update(const std::string& current){
  if(!version(current))fail("The running Xenon version is invalid.");
  std::string body;body.reserve(65536);
  get(api_url,true,max_metadata,nullptr,Clock::now()+std::chrono::seconds(30),[&](const unsigned char* bytes,size_t size){body.append(reinterpret_cast<const char*>(bytes),size);});
  return detail::select_release_json(body,current);
}
std::optional<Release> detail::select_release_json(const std::string& body,const std::string& current){
  if(body.size()>max_metadata)fail("The update release metadata exceeded its permitted size.");
  Json parsed;
  try{
    parsed=Json::parse(body,[](int depth,Json::parse_event_t,Json&){if(depth>32)fail("The update release metadata is too deeply nested.");return true;},false);
  }catch(const std::exception&){fail("The update service returned invalid release metadata.");}
  if(parsed.is_discarded())fail("The update service returned invalid release metadata.");return select_release(parsed,current);
}
std::filesystem::path detail::stage_installer(const Release& release,const std::filesystem::path& base,Progress progress,const std::atomic_bool& cancel,const ByteSource& source){
  if(!release_valid(release))fail("Update release metadata is invalid.");
  const auto deadline=Clock::now()+std::chrono::minutes(10);check_budget(&cancel,deadline);
  if(base==base.root_path()||!base.is_absolute()||base!=base.lexically_normal())fail("The private update storage root is invalid.");
  const auto stage=base/L"updates"/widen("stage-"+local_security::random_hex(16));
  const auto partial=stage/L"installer.part",final=stage/widen(filename(release));
  struct Cleanup{std::filesystem::path stage,partial,final;bool keep=false;~Cleanup(){if(!keep){DeleteFileW(partial.c_str());DeleteFileW(final.c_str());RemoveDirectoryW(stage.c_str());}}}cleanup{stage,partial,final};
  auto held=directories(stage,true,base);
  {
    local_security::SecurityDescriptor policy;File file(CreateFileW(partial.c_str(),GENERIC_WRITE,0,&policy.attributes,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));if(file.value==INVALID_HANDLE_VALUE)fail("Cannot create the private update file.");
    uint64_t received{};if(progress)progress(0,release.bytes);
    source([&](const unsigned char* bytes,size_t count){
      check_budget(&cancel,deadline);
      if(count>release.bytes||received>release.bytes-count||count>MAXDWORD)fail("The update response exceeded its permitted size.");
      if(!count)return;
      DWORD written{};if(!WriteFile(file.value,bytes,static_cast<DWORD>(count),&written,nullptr)||written!=count)fail("Cannot write the update download.");received+=count;if(progress)progress(received,release.bytes);
    });
    check_budget(&cancel,deadline);if(received!=release.bytes)fail("The update download was incomplete.");
    if(!FlushFileBuffers(file.value))fail("Cannot finish writing the update download.");
  }
  check_budget(&cancel,deadline);
  {auto verified=verified_file(partial,release);}
  check_budget(&cancel,deadline);
  if(!MoveFileExW(partial.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH))fail("Cannot publish the verified update file.");
  cleanup.keep=true;return final;
}
std::filesystem::path detail::download_installer_in(const Release& release,const std::filesystem::path& base,Progress progress,const std::atomic_bool& cancel){
  const auto deadline=Clock::now()+std::chrono::minutes(10);
  return stage_installer(release,base,std::move(progress),cancel,[&](const ByteSink& sink){get(release.url,false,release.bytes,&cancel,deadline,sink);});
}
std::filesystem::path download_installer(const Release& release,Progress progress,const std::atomic_bool& cancel){
  if(!release_valid(release))fail("Update release metadata is invalid.");
  check_budget(&cancel,Clock::now()+std::chrono::minutes(10));
  return detail::download_installer_in(release,local_root()/L"Xenon Browser",std::move(progress),cancel);
}
void launch_installer(const std::filesystem::path& installer,const Release& release){
  if(!release_valid(release))fail("Update release metadata is invalid.");
  const auto base=local_root()/L"Xenon Browser",expected=base/L"updates";
  const auto stage=installer.parent_path();const auto name=stage.filename().wstring();
  if(!same_path(stage.parent_path(),expected)||name.size()!=38||!name.starts_with(L"stage-")||!std::all_of(name.begin()+6,name.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');})||installer.filename()!=widen(filename(release)))fail("Only a verified private staged installer can be launched.");
  auto held=directories(stage,false,base);auto verified=verified_file(installer,release);
  // Retain read-only file and non-delete-sharing ancestor handles until the
  // image is created, so the verified pathname cannot be replaced in between.
  auto command=L"\""+installer.native()+L"\"";STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
  if(!CreateProcessW(installer.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,stage.c_str(),&startup,&process))fail("The verified installer could not start. Windows may have blocked this unsigned file.");
  CloseHandle(process.hThread);CloseHandle(process.hProcess);
}
}
