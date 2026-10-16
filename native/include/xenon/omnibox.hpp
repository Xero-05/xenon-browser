#pragma once
#include "xenon/contracts.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xenon::omnibox {
// Address-bar suggestions from one workspace's native bookmarks and history.
// Human browser chrome only, never exposed through MCP. Matching is local:
// typed text leaves the device only when the person opens a row or presses
// Enter, and there are no remote search suggestions.
enum class Kind {typed,search,history,bookmark};
struct Match {
  Kind kind{Kind::typed};
  // destination: what opening the row loads. fill: the address text while the
  // row is selected. display: the row's URL or query. completion: the inline
  // remainder appended to the typed text (first row only).
  std::string destination,fill,display,title,completion;
  bool removable{}; // Backed by history entries the person can delete.
};
// One distinct URL. `lower` is the ASCII-lowercased URL (Chromium serializes
// URLs as ASCII); `folded` is the case-folded title.
struct Entry {std::string url,lower,title,folded;size_t host{},path{},visits{};int64_t visited{};bool www{},bookmarked{};};
struct Index {std::vector<Entry> entries;std::unordered_map<std::string,size_t> at;};
inline constexpr size_t default_limit=8;
inline constexpr std::string_view search_prefix="https://www.google.com/search?q=";

inline std::string lower_ascii(std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return value;}
inline std::wstring widen(const std::string& value){
  if(value.empty())return {};const int count=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
  std::wstring out(static_cast<size_t>(std::max(count,0)),L'\0');if(count>0)MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),count);return out;
}
inline std::string narrow(const std::wstring& value){
  if(value.empty())return {};const int count=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
  std::string out(static_cast<size_t>(std::max(count,0)),'\0');if(count>0)WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),count,nullptr,nullptr);return out;
}
inline std::wstring lower(std::wstring value){if(!value.empty())CharLowerBuffW(value.data(),static_cast<DWORD>(value.size()));return value;}
inline std::string fold(const std::string& value){return narrow(lower(widen(value)));}
inline std::string trim(const std::string& value){
  const auto first=value.find_first_not_of(" \t\r\n");if(first==std::string::npos)return {};
  return value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
}
inline bool web(const std::string& value){const auto scheme=lower_ascii(value.substr(0,8));return scheme.rfind("http://",0)==0||scheme.rfind("https://",0)==0;}
inline bool explicit_url(const std::string& input){return input=="about:blank"||web(input);}
// Text that names an address rather than a search: a dotted word with no
// space or colon, or an explicit URL.
inline bool address_like(const std::string& input){
  return explicit_url(input)||(!input.empty()&&input.find(' ')==std::string::npos&&input.find('.')!=std::string::npos&&input.find(':')==std::string::npos);
}
// Escapes a query as CefURIEncode(text,true) does: everything except
// alphanumerics and -_.!~*'() becomes %XX, and spaces become '+'.
inline std::string search_url(const std::string& query){
  std::string out(search_prefix);constexpr char hex[]="0123456789ABCDEF";
  for(const unsigned char c:query){
    if(c==' ')out+='+';
    else if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||std::string_view("-_.!~*'()").find(static_cast<char>(c))!=std::string_view::npos)out+=static_cast<char>(c);
    else {out+='%';out+=hex[c>>4];out+=hex[c&15];}
  }
  return out;
}
// What typed text opens: an explicit http(s) URL as typed, a dotted word over
// HTTPS, Ctrl+Enter completing a single word as www.<word>.com, and anything
// else as a Google search.
inline std::string typed_destination(const std::string& raw,bool control){
  const auto input=trim(raw);
  if(explicit_url(input))return input;
  if(control&&!input.empty()&&input.find_first_of(" .:/")==std::string::npos)return "https://www."+input+".com";
  if(address_like(input))return "https://"+input;
  return search_url(input);
}
inline size_t host_of(const std::string& url){const auto at=url.find("://");return at==std::string::npos?0:at+3;}
inline size_t path_of(const std::string& url){const auto at=url.find_first_of("/?#",host_of(url));return at==std::string::npos?url.size():at;}
inline std::string without_root_slash(std::string text){
  if(text.size()>1&&text.back()=='/'&&text.find_first_of("/?#")==text.size()-1)text.pop_back();return text;
}
// The address text while a URL's row is selected; https:// and a bare root
// slash are implied, so typing over it still opens the same site.
inline std::string edit_text(const std::string& url){return without_root_slash(lower_ascii(url.substr(0,8))=="https://"?url.substr(8):url);}
// A row's URL as Chromium shows it: no scheme or leading "www.".
inline std::string display(const std::string& url){
  auto text=url.substr(host_of(url));if(lower_ascii(text.substr(0,4))=="www.")text.erase(0,4);return without_root_slash(std::move(text));
}
inline std::string string_of(const Json& row,const char* key){const auto found=row.find(key);return found!=row.end()&&found->is_string()?found->get<std::string>():std::string{};}
inline Entry& entry_for(Index& index,const std::string& url){
  const auto [found,added]=index.at.try_emplace(url,index.entries.size());
  if(added){Entry value;value.url=url;value.lower=lower_ascii(url);value.host=host_of(url);value.path=path_of(url);value.www=value.lower.compare(value.host,4,"www.")==0;index.entries.push_back(std::move(value));}
  return index.entries[found->second];
}
// A title that is only the URL is no title.
inline std::string title_for(const Json& row,const std::string& url){auto title=string_of(row,"title");return title==url?std::string{}:title;}
inline Index build(const Json& bookmarks,const Json& history){
  Index index;
  // History is newest first, so the first title seen is the latest one.
  if(history.is_array())for(const auto& row:history){
    if(!row.is_object())continue;const auto url=string_of(row,"url");if(!web(url))continue;auto& item=entry_for(index,url);++item.visits;
    if(const auto at=row.find("visitedAt");at!=row.end()&&at->is_number_integer())item.visited=std::max(item.visited,at->get<int64_t>());
    if(item.title.empty())item.title=title_for(row,url);
  }
  if(bookmarks.is_array())for(const auto& row:bookmarks){
    if(!row.is_object())continue;const auto url=string_of(row,"url");if(!web(url))continue;auto& item=entry_for(index,url);item.bookmarked=true;
    if(item.title.empty())item.title=title_for(row,url);
  }
  for(auto& item:index.entries)item.folded=fold(item.title);
  return index;
}
// Records one visit newer than every indexed one, as build() would over the
// updated history, without rebuilding a large index after each page load.
inline void add_visit(Index& index,const std::string& url,const std::string& title,int64_t at){
  if(!web(url))return;auto& item=entry_for(index,url);++item.visits;item.visited=std::max(item.visited,at);
  if(!title.empty()&&title!=url){item.title=title;item.folded=fold(title);}
}
// Frequent, recent and bookmarked pages rank first.
inline double frecency(double visits,int64_t visited,bool bookmarked,int64_t now){
  double value=120*std::log2(1+visits)+(bookmarked?150:0);
  if(visited>0&&now>=visited){const double days=(now-visited)/86400000.0;value+=days<1?150:days<7?100:days<30?50:0;}
  return value;
}
// Where typed text is compared with a URL: from the scheme only once "://" is
// typed, and from "www." only once that is typed, as in Chromium.
inline size_t compared_from(const Entry& entry,const std::string& input){
  if(input.find("://")!=std::string::npos)return 0;if(input.rfind("www.",0)==0)return entry.host;return entry.host+(entry.www?4:0);
}
struct Completion {std::string destination,completion,title;const Entry* exact{};double score{};bool found{};};
// The best URL that starts with the typed text. Without a typed path, the
// completion stops at the host and opens the site's root, like Chromium.
inline Completion complete(const Index& index,const std::string& typed,int64_t now){
  Completion best;const auto input=lower_ascii(typed);
  if(input.empty()||input.find_first_of(" \t\r\n")!=std::string::npos)return best;
  const auto scheme=input.find("://");const bool host_only=input.find_first_of("/?#",scheme==std::string::npos?0:scheme+3)==std::string::npos;
  const auto consider=[&](const std::string& destination,std::string completion,double score,const Entry* exact,const std::string& title){
    if(best.found&&(score<best.score||(score==best.score&&(destination.size()>best.destination.size()||(destination.size()==best.destination.size()&&destination>=best.destination)))))return;
    best={destination,std::move(completion),title,exact,score,true};
  };
  struct Host {double visits{};int64_t visited{};bool bookmarked{};size_t entry{};};std::unordered_map<std::string,Host> hosts;
  for(size_t n=0;n<index.entries.size();++n){
    const auto& entry=index.entries[n];const auto from=compared_from(entry,input);
    if(from>entry.lower.size()||entry.lower.compare(from,input.size(),input)!=0)continue;
    const auto end=from+input.size();
    if(!host_only){consider(entry.url,entry.url.substr(end),frecency(static_cast<double>(entry.visits),entry.visited,entry.bookmarked,now),&entry,entry.title);continue;}
    if(end>entry.path)continue;
    auto& host=hosts[entry.url.substr(0,entry.path)+"/"];host.visits+=static_cast<double>(entry.visits);host.visited=std::max(host.visited,entry.visited);host.bookmarked=host.bookmarked||entry.bookmarked;host.entry=n;
  }
  for(const auto& [root,host]:hosts){
    const auto& entry=index.entries[host.entry];const auto end=compared_from(entry,input)+input.size();
    const auto exact=index.at.find(root);const Entry* page=exact==index.at.end()?nullptr:&index.entries[exact->second];
    consider(root,entry.url.substr(end,entry.path-end),frecency(host.visits,host.visited,host.bookmarked,now),page,page?page->title:std::string{});
  }
  return best;
}
inline bool word_start(std::string_view text,size_t at){
  if(at==0)return true;const auto c=static_cast<unsigned char>(text[at-1]);return c<0x80&&!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'));
}
inline int position_score(std::string_view text,std::string_view term,int start,int word,int inside){
  int best=0;
  for(auto at=text.find(term);at!=std::string_view::npos;at=text.find(term,at+1)){best=std::max(best,at==0?start:word_start(text,at)?word:inside);if(best==start)break;}
  return best;
}
// Every typed term must appear in the URL (after the scheme and "www.") or
// the title. Matches at the start of the host or a word rank above others.
inline double relevance(const Entry& entry,const std::vector<std::string>& terms,bool single){
  const auto url=std::string_view(entry.lower).substr(std::min(entry.lower.size(),entry.host+(entry.www?4:0)));
  double total=0;
  for(const auto& term:terms){const int value=std::max(position_score(url,term,900,600,300),position_score(entry.folded,term,700,500,250));if(!value)return -1;total+=value;}
  total/=static_cast<double>(terms.size());
  // A single character matches only at the start of a word.
  return single&&total<500?-1:total;
}
inline std::vector<Match> suggest(const Index& index,const std::string& raw,bool inline_completion,int64_t now,size_t limit=default_limit){
  std::vector<Match> result;const auto input=trim(raw);if(input.empty()||!limit)return result;
  std::set<std::string> shown;
  const auto add=[&](Match match){if(result.size()<limit&&shown.insert(match.kind==Kind::search?match.destination:match.display).second)result.push_back(std::move(match));};
  const auto from_entry=[](const Entry& entry,Match& match){match.title=entry.title;match.kind=entry.bookmarked?Kind::bookmark:Kind::history;match.removable=entry.visits>0;};
  Match search{Kind::search,search_url(input),raw,input};
  // The first row is what Enter opens. Inline completion follows typing at the
  // end of untrimmed text only; an exact visited host needs no completion.
  if(const auto best=complete(index,raw==input?input:std::string{},now);best.found&&(inline_completion||best.completion.empty())){
    Match match{Kind::history,best.destination,raw+best.completion,display(best.destination),best.title,best.completion};
    if(best.exact)from_entry(*best.exact,match);
    add(std::move(match));
  }else if(address_like(input)){
    Match match{Kind::typed,typed_destination(input,false),raw};match.display=display(match.destination);
    // A typed address already in history shows its title.
    for(const auto& url:{match.destination,match.destination+"/"})if(const auto found=index.at.find(url);found!=index.at.end()){match.destination=url;from_entry(index.entries[found->second],match);break;}
    add(std::move(match));
  }
  if(!explicit_url(input))add(search);
  // Rows compare URLs after the scheme and "www.", so typed ones are dropped too.
  auto query=web(input)?input.substr(host_of(input)):input;if(lower_ascii(query.substr(0,4))=="www.")query.erase(0,4);
  std::vector<std::string> terms;const auto folded=fold(query);
  for(size_t start=0;start<folded.size();){const auto end=std::min(folded.find_first_of(" \t",start),folded.size());if(end>start)terms.push_back(folded.substr(start,end-start));start=end+1;}
  if(terms.empty())return result;
  std::vector<std::pair<double,size_t>> ranked;
  for(size_t n=0;n<index.entries.size();++n){const auto& entry=index.entries[n];const auto score=relevance(entry,terms,input.size()==1);
    if(score>=0)ranked.emplace_back(score+frecency(static_cast<double>(entry.visits),entry.visited,entry.bookmarked,now),n);}
  const auto order=[&](const auto& a,const auto& b){const auto& x=index.entries[a.second].url;const auto& y=index.entries[b.second].url;
    return a.first!=b.first?a.first>b.first:x.size()!=y.size()?x.size()<y.size():x<y;};
  const auto keep=std::min(ranked.size(),limit*2);std::partial_sort(ranked.begin(),ranked.begin()+static_cast<std::ptrdiff_t>(keep),ranked.end(),order);
  for(size_t n=0;n<keep&&result.size()<limit;++n){const auto& entry=index.entries[ranked[n].second];
    Match match{Kind::history,entry.url,edit_text(entry.url),display(entry.url)};from_entry(entry,match);add(std::move(match));}
  return result;
}
// Row text from websites and typed URLs, without line breaks, controls or
// bidirectional formatting characters that could disguise an address.
inline std::wstring printable(const std::wstring& text){
  std::wstring out;out.reserve(text.size());
  for(const wchar_t c:text){if(c<0x20||c==0x7f)out+=L' ';else if(c==0x061c||(c>=0x200e&&c<=0x200f)||(c>=0x202a&&c<=0x202e)||(c>=0x2066&&c<=0x2069))continue;else out+=c;}
  return out;
}
// The characters of `text` that match a typed term, drawn in bold.
inline std::vector<bool> emphasis(const std::wstring& text,const std::wstring& typed){
  std::vector<bool> mask(text.size());const auto haystack=lower(text),needle=lower(typed);
  for(size_t start=0;start<needle.size();){
    const auto end=std::min(needle.find_first_of(L" \t",start),needle.size());
    if(end>start){const auto term=needle.substr(start,end-start);for(auto at=haystack.find(term);at!=std::wstring::npos;at=haystack.find(term,at+term.size()))std::fill(mask.begin()+static_cast<std::ptrdiff_t>(at),mask.begin()+static_cast<std::ptrdiff_t>(at+term.size()),true);}
    start=end+1;
  }
  return mask;
}
}
