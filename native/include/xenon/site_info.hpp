#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace xenon::site {
// Native page information for the human's site-info bubble and panels. None
// of it is exposed through MCP; cookie values are never read into it.
inline std::string lower(std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return value;}
// "scheme://host[:port]" of an http(s) URL as Chromium serializes it, else empty.
inline std::string origin(const std::string& url){
  const auto mark=url.find("://");if(mark==std::string::npos)return {};
  const auto scheme=lower(url.substr(0,mark));if(scheme!="http"&&scheme!="https")return {};
  const auto start=mark+3,end=url.find_first_of("/?#",start);auto authority=url.substr(start,end==std::string::npos?std::string::npos:end-start);
  if(const auto at=authority.rfind('@');at!=std::string::npos)authority.erase(0,at+1);
  if(authority.empty()||authority.find_first_of(" \t\r\n\\")!=std::string::npos)return {};
  return scheme+"://"+lower(authority);
}
inline std::string host(const std::string& site){
  const auto mark=site.find("://");if(mark==std::string::npos)return {};const auto authority=site.substr(mark+3);
  if(!authority.empty()&&authority[0]=='['){const auto close=authority.find(']');return close==std::string::npos?authority:authority.substr(0,close+1);}
  return authority.substr(0,authority.rfind(':'));
}
inline bool loopback(const std::string& name){return name=="localhost"||name.ends_with(".localhost")||name.rfind("127.",0)==0||name=="[::1]";}
// Chromium treats certificate status bits 0-15 and 24-31 as errors, except the
// two revocation-check notices. Content status bits match cef_ssl_content_status_t.
inline constexpr uint32_t certificate_errors=0xFF00FFFFu&~((1u<<4)|(1u<<5));
inline constexpr uint32_t displayed_insecure_content=1u<<0,ran_insecure_content=1u<<1;
// The toolbar state: "secure"; "mixed" (HTTPS that displayed HTTP images or
// media); "insecure" (HTTP, a failed TLS state or HTTPS that ran HTTP scripts);
// "dangerous" (a certificate error); "local" (HTTP from this computer);
// "pending" (HTTPS before its TLS state is known); "protected" (a protected
// sign-in hides its address); "internal" (no website).
inline std::string security_state(const std::string& url,bool protected_auth,bool known,bool secure,uint32_t certificate_status,uint32_t content_status){
  if(protected_auth)return "protected";
  const auto site=origin(url);if(site.empty())return "internal";
  if(site.rfind("http://",0)==0)return loopback(host(site))?"local":"insecure";
  if(!known)return "pending";
  if(!secure)return "insecure";
  if(certificate_status&certificate_errors)return "dangerous";
  if(content_status&ran_insecure_content)return "insecure";
  return content_status&displayed_insecure_content?"mixed":"secure";
}
// Net errors for which Chromium commits its certificate warning in place of
// the page: ERR_CERT_COMMON_NAME_INVALID (-200) through the pinned Chromium's
// last certificate error (-219), and ERR_SSL_PINNED_KEY_NOT_IN_CERT_CHAIN.
inline bool certificate_error(int code){return (code<=-200&&code>-220)||code==-150;}
// Google's Chrome Help Center, where Chromium's warnings send "Learn more".
inline bool chrome_help(const std::string& url){
  if(origin(url)!="https://support.google.com")return false;
  const auto path=url.find_first_of("/?#",url.find("://")+3);
  if(path==std::string::npos||url.compare(path,7,"/chrome")!=0)return false;
  const auto next=path+7<url.size()?url[path+7]:'/';
  return next=='/'||next=='?'||next=='#';
}
// Xenon's explanation of the certificate warning, opened in its place.
inline constexpr char certificate_help_url[]="https://github.com/Xero-05/xenon-browser/blob/main/docs/USER_GUIDE.md#certificate-warnings";
// Names cef_ssl_version_t values (TLS1 is 3, QUIC is 7).
inline std::string protocol_name(int version){
  switch(version){case 1:return "SSL 2.0";case 2:return "SSL 3.0";case 3:return "TLS 1.0";case 4:return "TLS 1.1";case 5:return "TLS 1.2";case 6:return "TLS 1.3";case 7:return "QUIC";default:return {};}
}
// Cookies this site sets or receives: host-only and domain cookies sent to the
// host, plus cookies of its subdomains. Unrelated sites' cookies never match.
inline bool cookie_for_host(std::string domain,const std::string& name){
  domain=lower(domain);if(!domain.empty()&&domain[0]=='.')domain.erase(0,1);const auto site=lower(name);if(domain.empty()||site.empty())return false;
  const auto under=[](const std::string& inner,const std::string& outer){return inner.size()>outer.size()&&inner.compare(inner.size()-outer.size(),outer.size(),outer)==0&&inner[inner.size()-outer.size()-1]=='.';};
  return domain==site||under(site,domain)||under(domain,site);
}
// Site settings Xenon offers, in display order. `fallback` names the browser
// default that "default" stands for.
struct Setting {const char* name;const wchar_t* label;const wchar_t* fallback;};
inline constexpr Setting settings[]{
  {"geolocation",L"Location",L"Ask (default)"},{"camera",L"Camera",L"Ask (default)"},{"microphone",L"Microphone",L"Ask (default)"},
  {"notifications",L"Notifications",L"Ask (default)"},{"popups",L"Pop-ups and redirects",L"Block (default)"},{"sound",L"Sound",L"Allow (default)"}};
inline const Setting* setting(std::string_view name){for(const auto& item:settings)if(name==item.name)return &item;return nullptr;}
inline bool permission(std::string_view name){return name=="geolocation"||name=="camera"||name=="microphone"||name=="notifications";}
// Values a person can choose. "ask" can also be reported for a permission.
inline bool choice(std::string_view value){return value=="default"||value=="allow"||value=="block";}
inline const wchar_t* value_label(std::string_view name,std::string_view value){
  if(value=="allow")return L"Allow";if(value=="block")return name=="sound"?L"Mute":L"Block";if(value=="ask")return L"Ask";
  const auto* item=setting(name);return item?item->fallback:L"Default";
}
inline std::string bytes_text(uint64_t bytes){
  if(bytes<1024)return std::to_string(bytes)+" B";
  const char* units[]{"KB","MB","GB","TB"};double value=static_cast<double>(bytes)/1024;size_t unit=0;while(value>=1024&&unit<3){value/=1024;++unit;}
  char text[32]{};std::snprintf(text,sizeof(text),value<10?"%.1f %s":"%.0f %s",value,units[unit]);return text;
}
inline std::string hex(std::string_view bytes){
  constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(bytes.size()*2);
  for(const unsigned char c:bytes){out+=digits[c>>4];out+=digits[c&15];}return out;
}
inline bool unhex(std::string_view text,std::string& bytes){
  if(text.size()%2)return false;bytes.clear();bytes.reserve(text.size()/2);
  const auto value=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
  for(size_t n=0;n<text.size();n+=2){const int high=value(text[n]),low=value(text[n+1]);if(high<0||low<0)return false;bytes+=static_cast<char>(high*16+low);}
  return true;
}
}
