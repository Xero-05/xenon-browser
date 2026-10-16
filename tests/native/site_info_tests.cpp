#include "xenon/site_info.hpp"
#include <iostream>
#include <stdexcept>
using namespace xenon;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(){try{
  require(site::origin("https://Example.invalid/path?q=1#top")=="https://example.invalid","Origins drop the path and lowercase the host");
  require(site::origin("http://user:secret@example.invalid:8080/")=="http://example.invalid:8080","Origins never keep user information");
  require(site::origin("about:blank").empty()&&site::origin("chrome-extension://abc/page.html").empty()&&site::origin("file:///C:/a.txt").empty(),"Only web pages have an origin");
  require(site::origin("https:///missing").empty()&&site::origin("https://bad host/").empty(),"Malformed authorities have no origin");
  require(site::host("https://example.invalid:8443")=="example.invalid"&&site::host("http://[::1]:3000")=="[::1]"&&site::host("https://example.invalid")=="example.invalid","Hosts drop ports, including IPv6");

  // The toolbar state follows Chromium's connection levels.
  require(site::security_state("https://example.invalid/",false,true,true,0,0)=="secure","A valid TLS page is secure");
  require(site::security_state("https://example.invalid/",false,true,true,1u<<16|1u<<17,0)=="secure","EV and revocation-checking notices are not errors");
  require(site::security_state("https://example.invalid/",false,true,true,1u<<5,0)=="secure","An unavailable revocation check is a minor notice");
  require(site::security_state("https://example.invalid/",false,true,true,1u<<2,0)=="dangerous","An untrusted certificate authority is dangerous");
  require(site::security_state("https://example.invalid/",false,true,true,1u<<24,0)=="dangerous","High-bit certificate errors are dangerous");
  require(site::security_state("https://example.invalid/",false,true,true,0,site::displayed_insecure_content)=="mixed","Displayed HTTP images make a page mixed");
  require(site::security_state("https://example.invalid/",false,true,true,0,site::ran_insecure_content|site::displayed_insecure_content)=="insecure","HTTP scripts make a page insecure");
  require(site::security_state("https://example.invalid/",false,true,false,0,0)=="insecure","HTTPS without a secure connection is insecure");
  require(site::security_state("https://example.invalid/",false,false,false,0,0)=="pending","An unknown TLS state is pending");
  require(site::security_state("http://example.invalid/",false,true,false,0,0)=="insecure","HTTP is not secure");
  require(site::security_state("http://localhost:3000/",false,true,false,0,0)=="local"&&site::security_state("http://127.0.0.1:9/",false,false,false,0,0)=="local"
    &&site::security_state("http://app.localhost/",false,false,false,0,0)=="local","HTTP from this computer is local");
  require(site::security_state("https://example.invalid/",true,true,true,0,0)=="protected","Protected sign-in hides site details");
  require(site::security_state("about:blank",false,false,false,0,0)=="internal","Blank pages have no site state");
  require(site::protocol_name(6)=="TLS 1.3"&&site::protocol_name(5)=="TLS 1.2"&&site::protocol_name(7)=="QUIC"&&site::protocol_name(0).empty(),"TLS versions are named");

  // Cookies of the site and its subdomains, never of unrelated sites.
  require(site::cookie_for_host("example.invalid","example.invalid")&&site::cookie_for_host(".example.invalid","www.example.invalid")&&site::cookie_for_host("EXAMPLE.invalid","www.example.invalid"),"Host and domain cookies match");
  require(site::cookie_for_host("accounts.example.invalid","example.invalid"),"Subdomain cookies belong to the site");
  require(!site::cookie_for_host("badexample.invalid","example.invalid")&&!site::cookie_for_host("example.invalid","notexample.invalid")&&!site::cookie_for_host("other.invalid","example.invalid"),"Suffix look-alikes do not match");
  require(!site::cookie_for_host("accounts.example.invalid","www.example.invalid")&&!site::cookie_for_host("",".")&&!site::cookie_for_host(".","example.invalid"),"Sibling and empty domains do not match");

  require(site::setting("geolocation")&&site::setting("sound")&&!site::setting("javascript")&&std::size(site::settings)==6,"Only listed site settings exist");
  require(site::permission("camera")&&!site::permission("popups"),"Permissions are the prompted settings");
  require(site::choice("default")&&site::choice("allow")&&site::choice("block")&&!site::choice("ask")&&!site::choice("session_only"),"Only default, allow and block can be chosen");
  require(std::wstring(site::value_label("sound","block"))==L"Mute"&&std::wstring(site::value_label("popups","default"))==L"Block (default)"&&std::wstring(site::value_label("camera","ask"))==L"Ask","Values are labeled per setting");
  require(site::bytes_text(0)=="0 B"&&site::bytes_text(1536)=="1.5 KB"&&site::bytes_text(5u*1024*1024)=="5.0 MB"&&site::bytes_text(300ull*1024*1024)=="300 MB","Storage sizes are readable");
  std::string bytes;require(site::hex(std::string("\x00\x7f\xff",3))=="007fff"&&site::unhex("007FFF",bytes)&&bytes==std::string("\x00\x7f\xff",3),"Certificate bytes round-trip as hex");
  require(!site::unhex("abc",bytes)&&!site::unhex("zz",bytes),"Malformed hex is rejected");
  std::cout<<"Site info tests passed: origins, connection states, cookie scope, settings and certificate encoding\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
