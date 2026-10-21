#include "xenon/extension_manager_policy.hpp"
#include <iostream>
#include <stdexcept>
using namespace xenon;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
  for(const char* url:{"chrome://extensions","chrome://extensions/","chrome://extensions/shortcuts","chrome://extensions/?id=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","https://chromewebstore.google.com/category/extensions","https://chromewebstore.google.com/detail/name/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","https://chrome.google.com/webstore/category/extensions","chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/options.html"})require(extension_manager::page_url(url),"Native manager route rejected");
  for(const char* url:{"http://chromewebstore.google.com/","https://chromewebstore.google.com.evil.test/","https://user@chromewebstore.google.com/","https://chromewebstore.google.com:443/","https://chrome.google.com/accounts/","chrome://extensions.evil/","chrome://extensions-internals/","chrome://password-manager/","chrome://settings/","chrome://extensions\\@evil/","https://chromewebstore.google.com/\n","file:///C:/passwords.txt","javascript:alert(1)","https://example.test/","chrome-extension://invalid/options.html","chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.evil/options.html"})require(!extension_manager::page_url(url),"Unexpected privileged navigation accepted");
  require(!extension_manager::store_url("chrome://extensions/"),"Manager is not a website route");
  require(extension_manager::blocked_destination("https://accounts.google.com/signin?token=secret#private")=="accounts.google.com","Blocked navigation leaked path or query");
  require(extension_manager::blocked_destination("https://user:secret@example.test/private")=="invalid address","Blocked navigation leaked credentials");
  require(extension_manager::blocked_destination("javascript:secret")=="non-web address","Blocked protocol leaked content");
  require(extension_manager::blocked_destination("https://example.test\\secret/private")=="invalid address","Unsafe authority displayed");
  require(extension_manager::blocked_destination("https://example.test:443/private")=="example.test:443","Blocked authority lost port diagnostic");
  require(!extension_manager::page_url("https://accounts.google.com/signin"),"Diagnostic widened navigation authority");
  extension_manager::NavigationPolicy navigation;
  const auto account="https://accounts.google.com/ServiceLogin?continue=https%3A%2F%2Fchromewebstore.google.com%2F";
  require(!navigation.admit(account),"Account page accepted without a Store visit");
  require(navigation.admit(extension_manager::home)&&!navigation.admit(account),"Manager alone enabled account navigation");
  require(navigation.admit(extension_manager::store)&&navigation.admit(account),"Store account redirect was blocked");
  require(navigation.admit("https://accounts.google.com/CheckCookie"),"Account continuation was blocked");
  for(const char* url:{"http://accounts.google.com/","https://accounts.google.com.evil.test/","https://evil@accounts.google.com/","https://accounts.google.com:443/","https://accounts.google.com\\@evil.test/","https://accounts.google.com/\n","https://accounts.google.com.evil/","https://mail.google.com/","https://example.test/","chrome://settings/"})require(!navigation.admit(url),"Store flow accepted an unrelated or malformed account destination");
  require(navigation.admit(extension_manager::store),"Account flow could not return to the Store");
  require(navigation.admit(extension_manager::home)&&!navigation.admit(account),"Returning to manager retained account navigation authority");
  require(navigation.admit(extension_manager::store)&&navigation.admit("chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/options.html")&&!navigation.admit(account),"Extension options retained Store account authority");
  require(!extension_manager::page_url(account),"Account service became a general manager entry point");
  for(const char* path:{"","/","/shortcuts","/?id=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}){
    const auto native=std::string("chrome://extensions")+path,display=std::string("xenon://extensions")+path;
    require(extension_manager::display_url(native)==display,"Manager address is not branded");
    require(extension_manager::xenon_manager_url(display)&&extension_manager::native_url(display)==native,"Native address-bar alias did not round trip");
    require(!extension_manager::page_url(display),"Address alias must not grant page navigation authority");
  }
  for(const char* url:{"xenon://settings/","xenon://extensions.evil/","xenon://user@extensions/","xenon://extensions:443/","xenon://extensions\\@evil/"})require(!extension_manager::xenon_manager_url(url),"Unsafe native alias accepted");
  require(extension_manager::display_url("https://chromewebstore.google.com/")=="https://chromewebstore.google.com/","Website address was disguised");
  std::cout<<"Extension manager policy passed: exact store/WebUI/extension origins and rejection of privileged pages, files, protocols, credentials, ports and lookalikes\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
