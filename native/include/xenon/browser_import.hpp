#pragma once
#include "xenon/contracts.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace xenon::browser_import {
// Bookmarks and history another browser exported or keeps in its profile,
// read only after the person chooses a source in native UI. Passwords,
// cookies and other profile data are never read. Imported text is the
// person's own data, but it is bounded and cleaned like website titles.
struct Link {std::string url,title;};
struct Folder {std::string name;std::vector<Folder> folders;std::vector<Link> links;};
struct Visit {std::string url,title;int64_t visited{};};
struct Data {Folder bookmarks;std::vector<Visit> history;bool bookmarks_read{},history_read{};};
inline constexpr size_t max_links=10000,max_folders=2000,max_depth=32,max_visits=100000;

// Ordinary web addresses only, as Xenon's own bookmarks and history store.
inline bool web_link(std::string_view url){
  if(url.size()>16384||url.find_first_of("\r\n\t\\")!=std::string_view::npos)return false;
  return url.rfind("http://",0)==0||url.rfind("https://",0)==0;
}
// One line of text without control or bidirectional formatting characters, at
// most `limit` bytes of UTF-8 cut on a character boundary.
inline std::string clean_text(std::string_view text,size_t limit=1024){
  std::string out;out.reserve(std::min(text.size(),limit));
  for(size_t at=0;at<text.size();){
    const auto c=static_cast<unsigned char>(text[at]);size_t length=c<0x80?1:(c>>5)==6?2:(c>>4)==14?3:(c>>3)==30?4:1;if(at+length>text.size())length=1;
    const auto piece=text.substr(at,length);at+=length;
    if(length==1&&(c<0x20||c==0x7f)){if(!out.empty()&&out.back()!=' ')out+=' ';continue;}
    // U+200E/F, U+202A-E and U+2066-9 reorder or hide text.
    if(length==3&&static_cast<unsigned char>(piece[0])==0xE2&&((static_cast<unsigned char>(piece[1])==0x80&&(static_cast<unsigned char>(piece[2])==0x8E||static_cast<unsigned char>(piece[2])==0x8F||
       (static_cast<unsigned char>(piece[2])>=0xAA&&static_cast<unsigned char>(piece[2])<=0xAE)))||(static_cast<unsigned char>(piece[1])==0x81&&static_cast<unsigned char>(piece[2])>=0xA6&&static_cast<unsigned char>(piece[2])<=0xA9)))continue;
    if(out.size()+piece.size()>limit)break;out.append(piece);
  }
  while(!out.empty()&&out.back()==' ')out.pop_back();while(!out.empty()&&out.front()==' ')out.erase(out.begin());return out;
}
namespace detail {
struct Budget {size_t links{},folders{};};
inline void chromium_node(const Json& node,Folder& into,Budget& budget,size_t depth){
  if(depth>max_depth||!node.is_object())return;const auto type=node.value("type",std::string{});
  if(type=="url"){const auto url=node.value("url",std::string{});if(budget.links<max_links&&web_link(url)){into.links.push_back({url,clean_text(node.value("name",url))});++budget.links;}return;}
  if(type!="folder"||budget.folders>=max_folders)return;++budget.folders;
  Folder folder{clean_text(node.value("name",std::string{}),256)};if(folder.name.empty())folder.name="Folder";
  if(const auto children=node.find("children");children!=node.end()&&children->is_array())for(const auto& child:*children)chromium_node(child,folder,budget,depth+1);
  if(!folder.links.empty()||!folder.folders.empty())into.folders.push_back(std::move(folder));
}
inline std::string lower(std::string_view text){std::string out(text);for(auto& c:out)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return out;}
inline void append_utf8(std::string& out,uint32_t code){
  if(code==0||code>0x10FFFF||(code>=0xD800&&code<=0xDFFF))return;
  if(code<0x80)out+=static_cast<char>(code);
  else if(code<0x800){out+=static_cast<char>(0xC0|(code>>6));out+=static_cast<char>(0x80|(code&63));}
  else if(code<0x10000){out+=static_cast<char>(0xE0|(code>>12));out+=static_cast<char>(0x80|((code>>6)&63));out+=static_cast<char>(0x80|(code&63));}
  else{out+=static_cast<char>(0xF0|(code>>18));out+=static_cast<char>(0x80|((code>>12)&63));out+=static_cast<char>(0x80|((code>>6)&63));out+=static_cast<char>(0x80|(code&63));}
}
// The HTML character references bookmark exports use.
inline std::string decode(std::string_view text){
  std::string out;out.reserve(text.size());
  for(size_t at=0;at<text.size();){
    if(text[at]!='&'){out+=text[at++];continue;}
    const auto end=text.find(';',at);if(end==std::string_view::npos||end-at>10){out+=text[at++];continue;}
    const auto name=lower(text.substr(at+1,end-at-1));
    if(name=="amp")out+='&';else if(name=="lt")out+='<';else if(name=="gt")out+='>';else if(name=="quot")out+='"';else if(name=="apos")out+='\'';else if(name=="nbsp")out+=' ';
    else if(name.size()>1&&name[0]=='#'){uint32_t code=0;bool valid=true;const bool hex=name[1]=='x';
      for(size_t n=hex?2:1;n<name.size();++n){const char c=name[n];const int digit=c>='0'&&c<='9'?c-'0':hex&&c>='a'&&c<='f'?c-'a'+10:-1;if(digit<0||code>0x10FFFF){valid=false;break;}code=code*(hex?16:10)+static_cast<uint32_t>(digit);}
      if(!valid||name.size()<=(hex?2u:1u)){out+=text[at++];continue;}append_utf8(out,code);}
    else{out+=text[at++];continue;}
    at=end+1;
  }
  return out;
}
// The value of attribute `name` inside one start tag, decoded.
inline std::string attribute(std::string_view tag,std::string_view name){
  const auto lowered=lower(tag);
  for(size_t at=lowered.find(name);at!=std::string::npos;at=lowered.find(name,at+1)){
    if(at==0||(lowered[at-1]!=' '&&lowered[at-1]!='\t'&&lowered[at-1]!='\n'&&lowered[at-1]!='\r'))continue;
    size_t next=at+name.size();while(next<tag.size()&&(tag[next]==' '||tag[next]=='\t'))++next;if(next>=tag.size()||tag[next]!='=')continue;
    ++next;while(next<tag.size()&&(tag[next]==' '||tag[next]=='\t'))++next;if(next>=tag.size())return {};
    if(tag[next]=='"'||tag[next]=='\''){const auto quote=tag[next];const auto close=tag.find(quote,next+1);return decode(tag.substr(next+1,close==std::string_view::npos?std::string_view::npos:close-next-1));}
    const auto close=tag.find_first_of(" \t\r\n>",next);return decode(tag.substr(next,close==std::string_view::npos?std::string_view::npos:close-next));
  }
  return {};
}
}
// Chromium's "Bookmarks" file (Chrome, Edge, Brave, Vivaldi). Each non-empty
// root becomes a folder named as in that browser.
inline Folder chromium_bookmarks(const Json& value){
  Folder result;detail::Budget budget;const auto roots=value.find("roots");if(roots==value.end()||!roots->is_object())return result;
  for(const auto& [key,fallback]:{std::pair<const char*,const char*>{"bookmark_bar","Bookmarks bar"},{"other","Other bookmarks"},{"synced","Mobile bookmarks"}}){
    const auto root=roots->find(key);if(root==roots->end()||!root->is_object())continue;
    Json node=*root;node["type"]="folder";if(node.value("name",std::string{}).empty())node["name"]=fallback;
    detail::chromium_node(node,result,budget,0);
  }
  return result;
}
// A Netscape bookmark file, the HTML format every major browser exports.
inline Folder bookmarks_html(std::string_view html){
  Folder root;detail::Budget budget;std::vector<Folder> stack;std::string pending;bool have_pending=false,opened=false;
  const auto current=[&]()->Folder&{return stack.empty()?root:stack.back();};
  for(size_t at=html.find('<');at!=std::string_view::npos;at=html.find('<',at)){
    const auto close=html.find('>',at);if(close==std::string_view::npos)break;
    const auto tag=html.substr(at+1,close-at-1);const bool closing=!tag.empty()&&tag[0]=='/';const auto body=closing?tag.substr(1):tag;
    const auto name=(closing?std::string("/"):std::string{})+detail::lower(body.substr(0,body.find_first_of(" \t\r\n/>")));
    // Exports escape '<' in titles, so a title is the text up to the next tag.
    const auto text=[&]{const auto end=html.find('<',close+1);return html.substr(close+1,end==std::string_view::npos?std::string_view::npos:end-close-1);};
    at=close+1;
    if(name=="h3"){pending=clean_text(detail::decode(text()),256);if(pending.empty())pending="Folder";have_pending=true;}
    else if(name=="a"){const auto url=detail::attribute(tag,"href");if(budget.links<max_links&&web_link(url)){const auto title=clean_text(detail::decode(text()));current().links.push_back({url,title.empty()?url:title});++budget.links;}}
    else if(name=="dl"){
      if(have_pending&&stack.size()<max_depth&&budget.folders<max_folders){stack.push_back(Folder{pending});++budget.folders;}
      else if(opened||have_pending)stack.push_back(Folder{});
      opened=true;have_pending=false;
    }
    else if(name=="/dl"&&!stack.empty()){auto done=std::move(stack.back());stack.pop_back();auto& parent=current();
      // An unnamed list merges into its parent; empty folders are dropped.
      if(done.name.empty()){for(auto& link:done.links)parent.links.push_back(std::move(link));for(auto& folder:done.folders)parent.folders.push_back(std::move(folder));}
      else if(!done.links.empty()||!done.folders.empty())parent.folders.push_back(std::move(done));}
  }
  while(!stack.empty()){auto done=std::move(stack.back());stack.pop_back();if(!done.links.empty()||!done.folders.empty()){if(done.name.empty())done.name="Folder";current().folders.push_back(std::move(done));}}
  return root;
}
inline size_t link_count(const Folder& folder){size_t count=folder.links.size();for(const auto& child:folder.folders)count+=link_count(child);return count;}

// A Chromium-family profile the person can import from.
struct Source {std::string browser,profile_name;std::filesystem::path profile;bool bookmarks{},history{};};
// Profiles under the user's local application data folder that have a
// bookmarks or history file. Reads only each browser's "Local State" names.
std::vector<Source> chromium_sources(const std::filesystem::path& local_app_data);
// Reads a profile's Bookmarks and History. History is first copied into
// `staging` (a private directory), since a running browser keeps its file
// open, and the copy is deleted afterwards.
Data read_chromium_profile(const std::filesystem::path& profile,bool bookmarks,bool history,const std::filesystem::path& staging);
// Reads a bookmark HTML file of at most 32 MiB.
Data read_bookmarks_html(const std::filesystem::path& file);
}
