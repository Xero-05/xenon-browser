#pragma once
#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

namespace xenon::external {
// Links a website asks Windows to open in another app, such as mailto: or
// zoommtg:. Native human chrome only: a link reaches the registered app only
// after the person confirms a native prompt, and never from MCP.
//
// Schemes Xenon itself loads, schemes Windows or other apps treat as local
// file or shell access, and schemes with a record of abuse are never handed
// out. The list follows Chromium's built-in denials plus Windows handlers that
// run diagnostics, searches or installers.
inline constexpr std::string_view denied_schemes[]{
  "about","afp","blob","chrome","chrome-error","chrome-extension","chrome-untrusted","data","devtools","disk","disks",
  "file","filesystem","ftp","hcp","help","http","https","ie.http","its","javascript","mk","ms-appinstaller","ms-cxh",
  "ms-cxh-full","ms-help","ms-its","ms-msdt","ms-officecmd","ms-search","nntp","res","search","search-ms","shell",
  "vbscript","view-source","vnd.ms.radio","ws","wss"};
// Windows' ShellExecute limit for a URL argument that Chromium also applies.
inline constexpr size_t max_url_length=2048;

// The lowercase scheme of `url` when Xenon may offer it to Windows, else "".
// Single letters are drive letters (C:\...), never schemes.
inline std::string scheme(std::string_view url) {
  const auto colon=url.find(':');if(colon==std::string_view::npos||colon<2||colon>64||url.size()>max_url_length)return {};
  std::string value(url.substr(0,colon));
  for(auto& c:value){if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='+'||c=='-'||c=='.'))return {};}
  if(value[0]<'a'||value[0]>'z')return {};
  if(std::find(std::begin(denied_schemes),std::end(denied_schemes),value)!=std::end(denied_schemes))return {};
  return value;
}
// Percent-escapes what a command line could split or reinterpret, as
// Chromium's EscapeExternalHandlerValue does: controls, spaces, non-ASCII
// bytes and " < > \ ^ ` { | }. Existing escapes, '#' and reserved
// characters are kept so the receiving app sees the link it was sent.
inline std::string escape(std::string_view url) {
  constexpr char hex[]="0123456789ABCDEF";std::string out;out.reserve(url.size());
  for(const char raw:url){const auto c=static_cast<unsigned char>(raw);
    if(c<=0x20||c>=0x7f||c=='"'||c=='<'||c=='>'||c=='\\'||c=='^'||c=='`'||c=='{'||c=='|'||c=='}'){out+='%';out+=hex[c>>4];out+=hex[c&15];}
    else out+=raw;}
  return out;
}
}
