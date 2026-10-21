#include "xenon/web_store.hpp"
#include "xenon/local_security.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <set>
#include <stdexcept>

namespace xenon::web_store {
namespace {
[[noreturn]] void fail(const char* text) { throw std::runtime_error(text); }
bool valid_id(std::string_view id) { return id.size()==32&&std::all_of(id.begin(),id.end(),[](char c){return c>='a'&&c<='p';}); }
uint32_t number(std::string_view data,size_t at,size_t count=4) {
  if(at>data.size()||count>data.size()-at)fail("The extension package is truncated.");
  uint32_t value{};for(size_t n=0;n<count;++n)value|=uint32_t(static_cast<unsigned char>(data[at+n]))<<(n*8);return value;
}
void little(std::string& bytes,uint32_t n){for(int i=0;i<4;++i)bytes.push_back(static_cast<char>(n>>(i*8)));}
uint64_t varint(std::string_view& bytes) {
  uint64_t value{};
  for(unsigned shift=0;shift<64;shift+=7){if(bytes.empty())fail("The CRX header is invalid.");const auto c=static_cast<unsigned char>(bytes.front());bytes.remove_prefix(1);if(shift==63&&c>1)fail("The CRX header is invalid.");value|=uint64_t(c&127)<<shift;if(!(c&128))return value;}
  fail("The CRX header is invalid.");
}
struct Field {unsigned tag;std::string_view bytes;};
std::vector<Field> fields(std::string_view bytes) {
  std::vector<Field> result;
  while(!bytes.empty()){
    if(result.size()>=128)fail("The CRX header has too many fields.");
    const auto key=varint(bytes);if(!key||(key>>3)>0x1fffffff)fail("The CRX header is invalid.");
    const auto wire=key&7;std::string_view value;
    if(wire==2){const auto size=varint(bytes);if(size>bytes.size())fail("The CRX header is truncated.");value=bytes.substr(0,static_cast<size_t>(size));bytes.remove_prefix(static_cast<size_t>(size));}
    else if(wire==0){varint(bytes);}
    else if(wire==1||wire==5){size_t size=wire==1?8:4;if(bytes.size()<size)fail("The CRX header is truncated.");bytes.remove_prefix(size);}
    else fail("The CRX header uses an unsupported encoding.");
    result.push_back({static_cast<unsigned>(key>>3),value});
  }
  return result;
}
std::string_view unique(const std::vector<Field>& values,unsigned tag) {
  std::string_view value;bool seen=false;
  for(const auto& entry:values)if(entry.tag==tag){if(seen||entry.bytes.empty())fail("The CRX header has an invalid required field.");seen=true;value=entry.bytes;}
  if(!seen)fail("The CRX header is missing a required field.");return value;
}
std::string hex_bytes(const std::string& hex){std::string out;for(size_t i=0;i<hex.size();i+=2){auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};out.push_back(static_cast<char>((digit(hex[i])<<4)|digit(hex[i+1])));}return out;}
std::string base64(std::string_view bytes){DWORD length{};if(!CryptBinaryToStringA(reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&length))fail("The extension key could not be encoded.");std::string out(length,'\0');if(!CryptBinaryToStringA(reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,out.data(),&length))fail("The extension key could not be encoded.");while(!out.empty()&&out.back()=='\0')out.pop_back();return out;}
// CNG expects a fixed-width r||s pair, whereas CRX uses a DER ECDSA signature.
std::string ecdsa_raw(std::string_view der) {
  if(der.size()<8||der.size()>72||static_cast<unsigned char>(der[0])!=0x30||static_cast<unsigned char>(der[1])!=der.size()-2)return {};
  der.remove_prefix(2);std::string out;
  for(int i=0;i<2;++i){if(der.size()<2||der[0]!=2)return {};const auto len=static_cast<unsigned char>(der[1]);der.remove_prefix(2);if(!len||len>der.size())return {};auto part=der.substr(0,len);der.remove_prefix(len);
    if(static_cast<unsigned char>(part[0])&0x80)return {};
    if(part.size()>1&&part[0]==0){if(!(static_cast<unsigned char>(part[1])&0x80))return {};part.remove_prefix(1);}
    if(part.size()>32)return {};out.append(32-part.size(),'\0');out.append(part);
  }
  return der.empty()?out:std::string{};
}
struct Internet {HINTERNET value{};explicit Internet(HINTERNET h):value(h){}~Internet(){if(value)WinHttpCloseHandle(value);}Internet(const Internet&)=delete;};
std::wstring widen(std::string_view value){return {value.begin(),value.end()};}
void safe_name(std::string_view name) {
  if(name.empty()||name.size()>512||name.front()=='/'||name.find_first_of("\\:")!=name.npos)fail("The extension archive contains an unsafe path.");
  size_t depth{};
  while(!name.empty()){
    const auto slash=name.find('/');const auto part=name.substr(0,slash);
    if(++depth>ExtensionStore::max_depth||part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' '||std::any_of(part.begin(),part.end(),[](unsigned char c){return c<32||c>=127||c=='<'||c=='>'||c=='\"'||c=='|'||c=='?'||c=='*';}))fail("The extension archive contains an unsupported or unsafe path.");
    auto stem=std::string(part.substr(0,part.find('.')));while(!stem.empty()&&stem.back()==' ')stem.pop_back();for(auto& c:stem)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL"||stem=="CONIN$"||stem=="CONOUT$"||(stem.size()==4&&(stem.starts_with("COM")||stem.starts_with("LPT"))&&stem[3]>='0'&&stem[3]<='9'))fail("The extension archive contains a reserved Windows path.");
    if(slash==name.npos)break;name.remove_prefix(slash+1);
  }
}
}

std::string extension_id(std::string_view link) {
  if(link.size()>4096||std::any_of(link.begin(),link.end(),[](unsigned char c){return c<=32||c>=127||c=='\\';}))fail("Paste a Chrome Web Store extension link.");
  constexpr std::string_view prefix="https://chromewebstore.google.com/detail/";
  if(!link.starts_with(prefix))fail("Use an HTTPS extension link from chromewebstore.google.com.");
  link.remove_prefix(prefix.size());link=link.substr(0,link.find_first_of("?#"));
  if(!link.empty()&&link.back()=='/')link.remove_suffix(1);
  const auto slash=link.find('/');auto id=link;
  if(slash!=link.npos){if(!slash||link.find('/',slash+1)!=link.npos)fail("The Chrome Web Store link is invalid.");id=link.substr(slash+1);}
  if(!valid_id(id))fail("The Chrome Web Store link needs a valid extension ID.");return std::string(id);
}
namespace detail {
bool verify_signature(std::string_view key,std::string_view signature,const std::string& signed_bytes,bool rsa) {
  if(key.empty()||key.size()>4096||signature.empty()||signature.size()>1024)return false;
  CERT_PUBLIC_KEY_INFO* info{};DWORD size{};
  if(!CryptDecodeObjectEx(X509_ASN_ENCODING,X509_PUBLIC_KEY_INFO,reinterpret_cast<const BYTE*>(key.data()),static_cast<DWORD>(key.size()),CRYPT_DECODE_ALLOC_FLAG,nullptr,&info,&size))return false;
  const bool algorithm=info->Algorithm.pszObjId&&std::string_view(info->Algorithm.pszObjId)==(rsa?szOID_RSA_RSA:szOID_ECC_PUBLIC_KEY);
  BCRYPT_KEY_HANDLE handle{};const bool imported=algorithm&&CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING,info,0,nullptr,&handle);LocalFree(info);if(!imported)return false;
  auto hash=hex_bytes(local_security::sha256(signed_bytes));auto sig=rsa?std::string(signature):ecdsa_raw(signature);
  BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
  const auto status=sig.empty()?-1:BCryptVerifySignature(handle,rsa?&padding:nullptr,reinterpret_cast<PUCHAR>(hash.data()),static_cast<ULONG>(hash.size()),reinterpret_cast<PUCHAR>(sig.data()),static_cast<ULONG>(sig.size()),rsa?BCRYPT_PAD_PKCS1:0);
  BCryptDestroyKey(handle);return status>=0;
}
bool allowed_download_url(std::string_view url) {
  if(url.size()>8192||std::any_of(url.begin(),url.end(),[](unsigned char c){return c<=32||c>=127||c=='\\'||c=='#';}))return false;
  if(!url.starts_with("https://"))return false;url.remove_prefix(8);const auto slash=url.find('/');if(slash==url.npos)return false;
  const auto host=url.substr(0,slash),path=url.substr(slash);
  return (host=="clients2.google.com"&&path.starts_with("/service/update2/crx?"))||((host=="clients2.googleusercontent.com"||host=="clients2.google.com")&&path.starts_with("/crx/"));
}
}
VerifiedCrx verify_crx3(const std::string& bytes,const std::string& expected_id) {
  if(!valid_id(expected_id)||bytes.size()<12||bytes.size()>ExtensionStore::max_bytes||bytes.compare(0,4,"Cr24")||number(bytes,4)!=3)fail("The download is not a supported CRX3 extension package.");
  const auto length=number(bytes,8);if(length>1024*1024||length>bytes.size()-12)fail("The CRX header is too large or truncated.");
  const auto header=fields(std::string_view(bytes).substr(12,length));const auto signed_header=unique(header,10000);const auto id_bytes=unique(fields(signed_header),1);
  if(id_bytes.size()!=16)fail("The CRX extension ID is invalid.");
  std::string id;for(unsigned char c:id_bytes){id.push_back('a'+(c>>4));id.push_back('a'+(c&15));}
  if(id!=expected_id)fail("The downloaded extension does not match the Web Store link.");
  const auto archive=std::string_view(bytes).substr(12+length);if(archive.size()<4||archive.substr(0,4)!=std::string_view("PK\3\4",4))fail("The extension archive is missing.");
  std::string signed_bytes("CRX3 SignedData\0",16);little(signed_bytes,static_cast<uint32_t>(signed_header.size()));signed_bytes.append(signed_header);signed_bytes.append(archive);
  bool publisher=false;std::string developer;size_t proofs{};
  for(const auto& entry:header)if(entry.tag==2||entry.tag==3){
    if(++proofs>16)fail("The CRX package has too many signatures.");
    const auto proof=fields(entry.bytes);const auto key=unique(proof,1),signature=unique(proof,2);
    if(!detail::verify_signature(key,signature,signed_bytes,entry.tag==2))fail("The extension package signature is invalid.");
    const auto hash=local_security::sha256(std::string(key));
    // Chromium components/crx_file/crx_verifier.cc, ecdsa_2017_public SPKI hash.
    // The Chromium test publisher key is deliberately never accepted here.
    if(hash=="61f7f2a6bfcf74cd0bc1fe2497cc9b04254c658f79f2145392867ea8366367cf")publisher=true;
    if(ExtensionStore::generate_id(std::string(key))==id)developer=base64(key);
  }
  if(!publisher||developer.empty())fail("The package is missing a valid Chrome Web Store or developer signature.");
  VerifiedCrx result;result.id=id;result.public_key=std::move(developer);result.zip=std::string(archive);return result;
}

VerifiedCrx download(const std::string& id,const std::atomic_bool& cancel) {
  if(!valid_id(id))fail("The extension ID is invalid.");
  Internet session(WinHttpOpen(L"Xenon-Extension-Installer/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
  if(!session.value)fail("The extension download service is unavailable.");
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
  const auto check=[&]{if(cancel.load())fail("Extension installation canceled.");if(std::chrono::steady_clock::now()>=deadline)fail("The extension download timed out.");};
  std::string url="https://clients2.google.com/service/update2/crx?response=redirect&prodversion=" XENON_CHROMIUM_VERSION "&acceptformat=crx3&x=id%3D"+id+"%26installsource%3Dondemand%26uc";
  for(unsigned redirects=0;redirects<=5;++redirects){
    check();if(!detail::allowed_download_url(url))fail("The extension download redirected outside the permitted Google service.");
    const auto slash=url.find('/',8);const auto host=widen(std::string_view(url).substr(8,slash-8)),path=widen(std::string_view(url).substr(slash));
    Internet connection(WinHttpConnect(session.value,host.c_str(),INTERNET_DEFAULT_HTTPS_PORT,0));if(!connection.value)fail("Cannot connect to the extension service.");
    Internet request(WinHttpOpenRequest(connection.value,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));if(!request.value)fail("Cannot create the extension download request.");
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER,disabled=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION,autologon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH,headers=32768;
    if(!WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect))||!WinHttpSetOption(request.value,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled))||!WinHttpSetOption(request.value,WINHTTP_OPTION_AUTOLOGON_POLICY,&autologon,sizeof(autologon))||!WinHttpSetOption(request.value,WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE,&headers,sizeof(headers)))fail("Cannot apply extension transport protections.");
    auto timeout=[&]{check();const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();int ms=static_cast<int>(std::clamp<int64_t>(remaining,1,10000));if(!WinHttpSetTimeouts(request.value,ms,ms,ms,ms))fail("Cannot set extension download time limits.");};
    timeout();if(!WinHttpSendRequest(request.value,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0))fail("The secure extension download failed.");
    timeout();if(!WinHttpReceiveResponse(request.value,nullptr))fail("The extension service did not respond.");check();
    DWORD status{},size=sizeof(status);if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr))fail("Cannot read the extension response.");
    if(status==301||status==302||status==303||status==307||status==308){std::array<wchar_t,8192> location{};size=static_cast<DWORD>(location.size()*sizeof(wchar_t));if(redirects==5||!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_LOCATION,nullptr,location.data(),&size,nullptr))fail("The extension download redirect is invalid.");std::wstring text(location.data(),size/sizeof(wchar_t));while(!text.empty()&&!text.back())text.pop_back();if(std::any_of(text.begin(),text.end(),[](wchar_t c){return c>126||c<33;}))fail("The extension download redirect is invalid.");url.assign(text.begin(),text.end());continue;}
    if(status!=200)fail("The Chrome Web Store did not provide this extension. It may be unavailable or incompatible.");
    std::string bytes;std::array<char,65536> buffer{};
    for(;;){timeout();DWORD read{};if(!WinHttpReadData(request.value,buffer.data(),static_cast<DWORD>(buffer.size()),&read))fail("The extension download was interrupted.");check();if(!read)break;if(read>ExtensionStore::max_bytes-bytes.size())fail("The extension package exceeds 128 MiB.");bytes.append(buffer.data(),read);}
    check();return verify_crx3(bytes,id);
  }
  fail("The extension download redirected too many times.");
}

std::vector<ZipEntry> inspect_zip(std::string_view zip) {
  if(zip.size()<22||zip.size()>ExtensionStore::max_bytes)fail("The extension ZIP size is invalid.");
  size_t end=zip.size()-22;const auto lower=end>65535?end-65535:0;
  while(number(zip,end)!=0x06054b50||end+22+number(zip,end+20,2)!=zip.size()){if(end==static_cast<size_t>(lower))fail("The extension ZIP directory is invalid.");--end;}
  const auto count=number(zip,end+10,2),offset=number(zip,end+16),length=number(zip,end+12);
  if(number(zip,end+4,2)||number(zip,end+6,2)||count!=number(zip,end+8,2)||!count||count>ExtensionStore::max_files||offset>end||length!=end-offset)fail("Split, ZIP64 or oversized extension archives are unsupported.");
  std::vector<ZipEntry> result;std::set<std::string> names,tree;uint64_t total{};size_t at=offset;
  std::vector<std::pair<size_t,size_t>> spans;
  for(uint32_t i=0;i<count;++i){
    if(at> end||end-at<46||number(zip,at)!=0x02014b50)fail("The extension ZIP directory is truncated.");
    const auto flags=number(zip,at+8,2),method=number(zip,at+10,2),size=number(zip,at+24),compressed=number(zip,at+20),crc=number(zip,at+16);
    const auto name_size=number(zip,at+28,2),extra=number(zip,at+30,2),comment=number(zip,at+32,2),local=number(zip,at+42);
    const auto record=46ull+name_size+extra+comment;if(record>end-at)fail("The extension ZIP directory is truncated.");
    std::string name(zip.substr(at+46,name_size));safe_name(name);const bool directory=name.back()=='/';
    auto canonical=name;if(directory)canonical.pop_back();for(auto& c:canonical)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if(!names.insert(canonical).second)fail("The extension ZIP contains duplicate Windows paths.");
    tree.insert(canonical);for(size_t slash=canonical.find('/');slash!=canonical.npos;slash=canonical.find('/',slash+1))tree.insert(canonical.substr(0,slash));
    if(tree.size()>ExtensionStore::max_files)fail("The extension ZIP contains too many files and directories.");
    const auto mode=(number(zip,at+38)>>16)&0170000;
    if((mode&&mode!=(directory?0040000:0100000))||(number(zip,at+38)&FILE_ATTRIBUTE_REPARSE_POINT))fail("The extension ZIP contains links or unsupported file modes.");
    if(number(zip,at+34,2)||(flags&~0x080e)||(method!=0&&method!=8)||size==0xffffffff||compressed==0xffffffff)fail("The extension ZIP uses unsupported compression, encryption or ZIP64.");
    // A deflated empty directory can have a nonzero compressed length.
    if(directory&&(size||crc))fail("An extension ZIP directory contains file data.");
    total+=size;if(total>ExtensionStore::max_bytes)fail("The expanded extension exceeds 128 MiB.");
    if(local>offset||offset-local<30||number(zip,local)!=0x04034b50||number(zip,local+6,2)!=flags||number(zip,local+8,2)!=method||number(zip,local+26,2)!=name_size)fail("The extension ZIP local header is invalid.");
    const auto start=uint64_t(local)+30+name_size+number(zip,local+28,2);if(start>offset||compressed>offset-start||zip.substr(local+30,name_size)!=name)fail("The extension ZIP entry is outside its archive.");
    if(!(flags&8)&&(number(zip,local+14)!=crc||number(zip,local+18)!=compressed||number(zip,local+22)!=size))fail("The extension ZIP sizes do not match.");
    spans.emplace_back(local,static_cast<size_t>(start+compressed));result.push_back({std::move(name),size,compressed,crc,directory});at+=static_cast<size_t>(record);
  }
  if(at!=end)fail("The extension ZIP directory size is invalid.");
  std::sort(spans.begin(),spans.end());for(size_t i=1;i<spans.size();++i)if(spans[i].first<spans[i-1].second)fail("The extension ZIP entries overlap.");
  for(const auto& entry:result)if(!entry.directory){auto lower_name=entry.name;for(auto& c:lower_name)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));for(const auto& other:names)if(other.starts_with(lower_name+"/"))fail("An extension ZIP file conflicts with a directory.");}
  return result;
}

void validate_file_name(std::string_view name) {
  safe_name(name);if(name.back()=='/')fail("An extension file cannot be a directory.");
}

std::string Package::permission_review() const {
  return detail::permission_review(manifest,name,version,id);
}
std::string detail::permission_review(const Json& manifest,const std::string& name,const std::string& version,const std::string& id) {
  Json scopes=Json::object();for(const char* key:{"permissions","host_permissions","optional_permissions","optional_host_permissions","content_scripts","externally_connectable","web_accessible_resources","oauth2","chrome_url_overrides","devtools_page","incognito"})if(manifest.contains(key))scopes[key]=manifest[key];
  return "Extension: "+Json(name).dump(-1,' ',true)+"\r\nVersion: "+Json(version).dump(-1,' ',true)+"\r\nID: "+id+"\r\n\r\nVerified developer and Chrome Web Store signatures.\r\n\r\nRuns in all non-private workspaces after restart. Within its permissions it can read or change websites and see typed passwords, including agent sign-ins. Xenon workspace and account grants do not restrict extensions.\r\n\r\nDeclared permissions and site access (optional permissions may be requested later):\r\n"+scopes.dump(2,' ',true)+"\r\n\r\nNo automatic extension updates. To update, remove it and install its link again. Compatibility with Chrome tab APIs and toolbar popups is limited.\r\n\r\nFull manifest:\r\n"+manifest.dump(2,' ',true);
}
}
