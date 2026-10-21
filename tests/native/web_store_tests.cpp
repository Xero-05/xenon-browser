#include "xenon/web_store.hpp"
#include "xenon/local_security.hpp"
#include <wincrypt.h>
#include <iostream>
#include <fstream>
#include <stdexcept>

using namespace xenon;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void refused(F work){bool rejected=false;try{work();}catch(const std::exception&){rejected=true;}require(rejected,"Unsafe input was accepted");}
void little(std::string& out,uint32_t n,int size=4){for(int i=0;i<size;++i)out.push_back(static_cast<char>(n>>(8*i)));}
void varint(std::string& out,uint64_t n){while(n>=128){out.push_back(static_cast<char>((n&127)|128));n>>=7;}out.push_back(static_cast<char>(n));}
std::string field(unsigned tag,const std::string& bytes){std::string out;varint(out,(uint64_t(tag)<<3)|2);varint(out,bytes.size());return out+bytes;}
std::string hash(const std::string& text){const auto hex=local_security::sha256(text);std::string bytes;auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};for(size_t i=0;i<hex.size();i+=2)bytes.push_back(static_cast<char>((digit(hex[i])<<4)|digit(hex[i+1])));return bytes;}
struct Key {
  BCRYPT_ALG_HANDLE algorithm{};BCRYPT_KEY_HANDLE value{};std::string spki;
  explicit Key(bool rsa){
    require(BCryptOpenAlgorithmProvider(&algorithm,rsa?BCRYPT_RSA_ALGORITHM:BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)>=0,"Open signature provider");
    require(BCryptGenerateKeyPair(algorithm,&value,rsa?2048:256,0)>=0&&BCryptFinalizeKeyPair(value,0)>=0,"Generate synthetic signing key");
    DWORD size{};require(CryptExportPublicKeyInfoFromBCryptKeyHandle(value,X509_ASN_ENCODING,nullptr,0,nullptr,nullptr,&size),"Public key info size");
    std::vector<unsigned char> storage(size);auto info=reinterpret_cast<CERT_PUBLIC_KEY_INFO*>(storage.data());require(CryptExportPublicKeyInfoFromBCryptKeyHandle(value,X509_ASN_ENCODING,nullptr,0,nullptr,info,&size),"Export public key info");
    require(CryptEncodeObjectEx(X509_ASN_ENCODING,X509_PUBLIC_KEY_INFO,info,0,nullptr,nullptr,&size),"SPKI size");spki.resize(size);require(CryptEncodeObjectEx(X509_ASN_ENCODING,X509_PUBLIC_KEY_INFO,info,0,nullptr,spki.data(),&size),"Encode SPKI");spki.resize(size);
  }
  ~Key(){if(value)BCryptDestroyKey(value);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
  std::string sign(const std::string& text,bool rsa){auto digest=hash(text);BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};ULONG size{};require(BCryptSignHash(value,rsa?&padding:nullptr,reinterpret_cast<PUCHAR>(digest.data()),32,nullptr,0,&size,rsa?BCRYPT_PAD_PKCS1:0)>=0,"Signature size");std::string signature(size,'\0');require(BCryptSignHash(value,rsa?&padding:nullptr,reinterpret_cast<PUCHAR>(digest.data()),32,reinterpret_cast<PUCHAR>(signature.data()),size,&size,rsa?BCRYPT_PAD_PKCS1:0)>=0,"Sign synthetic bytes");signature.resize(size);
    if(rsa)return signature;
    auto integer=[](std::string bytes){while(bytes.size()>1&&!bytes[0])bytes.erase(0,1);if(static_cast<unsigned char>(bytes[0])&128)bytes.insert(0,1,'\0');return std::string("\2",1)+static_cast<char>(bytes.size())+bytes;};
    auto values=integer(signature.substr(0,32))+integer(signature.substr(32));return std::string("\x30",1)+static_cast<char>(values.size())+values;
  }
};
std::string zip(const std::vector<std::string>& names,uint32_t mode=0100644u<<16,bool deflated_directory=false){
  std::string out,directory;uint32_t offset{};
  for(const auto& name:names){offset=static_cast<uint32_t>(out.size());little(out,0x04034b50);little(out,20,2);little(out,0,2);little(out,deflated_directory?8:0,2);little(out,0);little(out,0);little(out,deflated_directory?2:0);little(out,0);little(out,static_cast<uint32_t>(name.size()),2);little(out,0,2);out+=name;if(deflated_directory)out.append("\3\0",2);
    little(directory,0x02014b50);little(directory,0x314,2);little(directory,20,2);little(directory,0,2);little(directory,deflated_directory?8:0,2);little(directory,0);little(directory,0);little(directory,deflated_directory?2:0);little(directory,0);little(directory,static_cast<uint32_t>(name.size()),2);little(directory,0,2);little(directory,0,2);little(directory,0,2);little(directory,0,2);little(directory,mode);little(directory,offset);directory+=name;
  }
  const auto start=static_cast<uint32_t>(out.size());out+=directory;little(out,0x06054b50);little(out,0,2);little(out,0,2);little(out,static_cast<uint32_t>(names.size()),2);little(out,static_cast<uint32_t>(names.size()),2);little(out,static_cast<uint32_t>(directory.size()));little(out,start);little(out,0,2);return out;
}
}
int main(int argc,char** argv){try{
  if(argc==3&&std::string(argv[1])=="--download"){
    std::atomic_bool cancel=false;auto crx=web_store::download(argv[2],cancel);auto entries=web_store::inspect_zip(crx.archive());std::cout<<"Live package verified: "<<crx.extension_id()<<", "<<entries.size()<<" archive entries; nothing installed\n";return 0;
  }
  const std::string id(32,'a'),link="https://chromewebstore.google.com/detail/test/"+id;
  require(web_store::extension_id(link+"?hl=en")==id&&web_store::extension_id("https://chromewebstore.google.com/detail/"+id+"/")==id,"Valid store links");
  for(const auto& bad:{"http://chromewebstore.google.com/detail/test/"+id,"https://chromewebstore.google.com.evil.test/detail/test/"+id,"https://user@chromewebstore.google.com/detail/test/"+id,"https://chromewebstore.google.com:443/detail/test/"+id,link+"/extra",link+"%2f",link+"\n",std::string("file:///manifest.json")})refused([&]{web_store::extension_id(bad);});
  require(web_store::detail::allowed_download_url("https://clients2.googleusercontent.com/crx/test.crx"),"Permitted fixed download origin");
  for(const char* bad:{"http://clients2.google.com/crx/test","https://clients2.google.com.evil.test/crx/test","https://clients2.google.com/other","https://user@clients2.google.com/crx/test","https://clients2.google.com:443/crx/test","https://clients2.googleusercontent.com/crx/test#fragment"})require(!web_store::detail::allowed_download_url(bad),"Reject untrusted redirect");
  for(bool rsa:{true,false}){
    Key key(rsa);const std::string message="Synthetic signed archive";auto sig=key.sign(message,rsa);
    require(web_store::detail::verify_signature(key.spki,sig,message,rsa),"Verify RSA/ECDSA signature");
    require(!web_store::detail::verify_signature(key.spki,sig,message,!rsa),"Reject mismatched proof algorithm");
    require(!web_store::detail::verify_signature(key.spki.substr(0,10),sig,message,rsa),"Reject truncated public key");
    require(!web_store::detail::verify_signature(key.spki,sig,message+"tampered",rsa),"Reject altered archive");sig[3]^=1;require(!web_store::detail::verify_signature(key.spki,sig,message,rsa),"Reject altered signature");
    const auto digest=hash(key.spki);const auto signed_header=field(1,digest.substr(0,16));const auto archive=zip({"manifest.json"});std::string signed_bytes("CRX3 SignedData\0",16);little(signed_bytes,static_cast<uint32_t>(signed_header.size()));signed_bytes+=signed_header+archive;
    const auto header=field(rsa?2:3,field(1,key.spki)+field(2,key.sign(signed_bytes,rsa)))+field(10000,signed_header);
    std::string crx="Cr24";little(crx,3);little(crx,static_cast<uint32_t>(header.size()));crx+=header+archive;
    refused([&]{web_store::verify_crx3(crx,ExtensionStore::generate_id(key.spki));}); // A valid developer proof never substitutes for the store proof.
    refused([&]{web_store::verify_crx3(crx,id);});crx.back()^=1;refused([&]{web_store::verify_crx3(crx,ExtensionStore::generate_id(key.spki));});
  }
  require(web_store::inspect_zip(zip({"manifest.json","scripts/content.js"})).size()==2,"Inspect a normal ZIP");
  require(web_store::inspect_zip(zip({"rulesets/"},0040755u<<16,true)).front().directory,"Accept a deflated empty directory");
  for(const char* bad:{"../escape","/absolute","a/../../escape","a\\escape","a:stream","a//b","AUX.txt","con .txt","a./file","a /file","scripts/../file","a/CONOUT$","a/\xc3\xa9"})refused([&]{web_store::inspect_zip(zip({bad}));});
  refused([&]{web_store::inspect_zip(zip({"a.js","A.js"}));});refused([&]{web_store::inspect_zip(zip({"a","a/b"}));});refused([&]{web_store::inspect_zip(zip({"symlink"},0120777u<<16));});
  auto truncated=zip({"manifest.json"});truncated.pop_back();refused([&]{web_store::inspect_zip(truncated);});
  auto oversized=zip({"manifest.json"});const auto central=oversized.find("PK\1\2");for(int i=0;i<4;++i)oversized[central+24+i]=static_cast<char>(0xff);refused([&]{web_store::inspect_zip(oversized);});
  refused([&]{web_store::verify_crx3(zip({"manifest.json"}),id);});
  const Json manifest={{"permissions",{"cookies","tabs"}},{"host_permissions",{"<all_urls>"}},{"optional_host_permissions",{"https://example.test/*"}},{"content_scripts",Json::array({{{"matches",{"https://content.test/*"}}}})}};
  const auto review=web_store::detail::permission_review(manifest,"Synthetic\r\nname","1",id);for(const char* expected:{"cookies","<all_urls>","optional_host_permissions","https://content.test/*","Full manifest:","passwords","No automatic"})require(review.find(expected)!=std::string::npos,"Complete permission disclosure");
  std::cout<<"Web Store tests passed: links, transport destinations, RSA/ECDSA integrity, required store proof, archive paths/budgets and permission review\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
