#pragma once
#include <algorithm>
#include <string_view>
#include <string>

namespace xenon::extension_manager {
// Native-only surface. Never accept an arbitrary chrome:// page, external
// protocol, file URL, lookalike host, credentials, port or control character.
inline bool store_url(std::string_view url) {
  if(url.size()>8192||std::any_of(url.begin(),url.end(),[](unsigned char c){return c<=32||c>=127||c=='\\';}))return false;
  return url.starts_with("https://chromewebstore.google.com/")||url=="https://chromewebstore.google.com"||url.starts_with("https://chrome.google.com/webstore/");
}
inline bool manager_url(std::string_view url) {
  if(url.size()>8192||std::any_of(url.begin(),url.end(),[](unsigned char c){return c<=32||c>=127||c=='\\';}))return false;
  return url=="chrome://extensions"||url.starts_with("chrome://extensions/")||url.starts_with("chrome://extensions?");
}
// This alias is accepted only by the native address bar, never by page or MCP
// navigation. Chromium must keep its own WebUI origin and security bindings.
inline bool xenon_manager_url(std::string_view url) {
  return url.starts_with("xenon://")&&manager_url(std::string("chrome://")+std::string(url.substr(8)));
}
inline std::string display_url(const std::string& url) {
  return manager_url(url)?"xenon://"+url.substr(9):url;
}
inline std::string native_url(const std::string& url) {
  return xenon_manager_url(url)?"chrome://"+url.substr(8):url;
}
inline bool extension_url(std::string_view url) {
  constexpr std::string_view prefix="chrome-extension://";
  if(!url.starts_with(prefix)||url.size()>8192)return false;url.remove_prefix(prefix.size());
  const auto slash=url.find('/');const auto id=url.substr(0,slash);
  return id.size()==32&&std::all_of(id.begin(),id.end(),[](char c){return c>='a'&&c<='p';})&&
    std::none_of(url.begin(),url.end(),[](unsigned char c){return c<=32||c>=127||c=='\\';});
}
inline bool page_url(std::string_view url) {return manager_url(url)||store_url(url)||extension_url(url);}
inline bool account_url(std::string_view url) {
  if(url.size()>8192||std::any_of(url.begin(),url.end(),[](unsigned char c){return c<=32||c>=127||c=='\\';}))return false;
  return url=="https://accounts.google.com"||url.starts_with("https://accounts.google.com/");
}
// Google may route a Store visit through its account service before returning
// to the Store. Enable only after a Store navigation, and revoke on leaving it.
struct NavigationPolicy {
  bool store_flow=false;
  bool permits(std::string_view url) const {return page_url(url)||(store_flow&&account_url(url));}
  bool admit(std::string_view url) {
    if(!permits(url))return false;
    if(store_url(url))store_flow=true;
    else if(!account_url(url))store_flow=false;
    return true;
  }
};
// Native status only: never retain paths, queries, fragments or user info from
// a rejected navigation. This describes the failure without granting authority.
inline std::string blocked_destination(std::string_view url) {
  const auto separator=url.find("://");
  if(separator==std::string_view::npos)return "non-web address";
  const auto scheme=url.substr(0,separator);
  if(scheme!="https"&&scheme!="http"&&scheme!="chrome")return "non-web address";
  auto authority=url.substr(separator+3);authority=authority.substr(0,authority.find_first_of("/?#"));
  if(authority.empty()||authority.size()>253||authority.find('@')!=std::string_view::npos||
    std::any_of(authority.begin(),authority.end(),[](unsigned char c){return !(c>='a'&&c<='z')&&!(c>='A'&&c<='Z')&&!(c>='0'&&c<='9')&&c!='.'&&c!='-'&&c!=':';}))return "invalid address";
  return std::string(authority);
}
inline constexpr const char* home="chrome://extensions/";
inline constexpr const char* store="https://chromewebstore.google.com/category/extensions";
}
