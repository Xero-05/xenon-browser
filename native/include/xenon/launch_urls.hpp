#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace xenon {
// Web addresses Windows passes to Xenon as the default browser. The installer
// registers `Xenon.exe --single-argument %1`, so a link with spaces or quotes
// arrives as one argument and can never add switches. Only ordinary HTTP(S)
// addresses without credentials or control characters are opened, in a new
// tab the person owns. Everything else in the command line is ignored.
inline bool launch_url(std::string_view url) {
  if(url.empty()||url.size()>16384)return false;
  for(const char c:url)if(static_cast<unsigned char>(c)<0x20||c==0x7f)return false;
  std::string lower(url.substr(0,8));for(auto& c:lower)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
  const size_t start=lower.rfind("https://",0)==0?8:lower.rfind("http://",0)==0?7:0;if(!start)return false;
  const auto end=url.find_first_of("/?#",start);const auto authority=url.substr(start,end==std::string_view::npos?std::string_view::npos:end-start);
  return !authority.empty()&&authority.find('@')==std::string_view::npos&&authority.find_first_of(" \\")==std::string_view::npos;
}
// The text after " --single-argument ", with surrounding quotes removed;
// empty when the command line has no such switch.
inline std::wstring single_argument(std::wstring_view command_line) {
  constexpr std::wstring_view marker=L" --single-argument ";const auto at=command_line.find(marker);if(at==std::wstring_view::npos)return {};
  auto value=command_line.substr(at+marker.size());
  while(!value.empty()&&(value.front()==L' '||value.front()==L'\t'))value.remove_prefix(1);
  while(!value.empty()&&(value.back()==L' '||value.back()==L'\t'))value.remove_suffix(1);
  if(value.size()>=2&&value.front()==L'"'&&value.back()==L'"')value=value.substr(1,value.size()-2);
  return std::wstring(value);
}
// At most a few addresses per launch, in order, without duplicates.
inline std::vector<std::string> launch_urls(const std::vector<std::string>& candidates) {
  std::vector<std::string> result;
  for(const auto& candidate:candidates)if(result.size()<8&&launch_url(candidate)&&std::find(result.begin(),result.end(),candidate)==result.end())result.push_back(candidate);
  return result;
}
}
