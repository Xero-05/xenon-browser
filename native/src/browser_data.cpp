#include "xenon/browser_data.hpp"
#include "xenon/local_security.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

namespace xenon {
namespace {
bool web(const std::string& url){return url.size()<=16384&&(url.rfind("http://",0)==0||url.rfind("https://",0)==0)&&url.find_first_of("\r\n\t\\")==std::string::npos;}
bool regular(const std::filesystem::path& path){const auto flags=GetFileAttributesW(path.c_str());return flags!=INVALID_FILE_ATTRIBUTES&&!(flags&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY));}
std::filesystem::path directory(const std::filesystem::path& root,const std::string& workspace){auto path=root/("workspace-"+local_security::sha256(workspace));
  const auto flags=GetFileAttributesW(path.c_str());if(flags!=INVALID_FILE_ATTRIBUTES&&(flags&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Unsafe browser metadata directory");return path;}
std::vector<unsigned char> protect(const std::string& bytes,bool decrypt){
  DATA_BLOB input{static_cast<DWORD>(bytes.size()),reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data()))},output{};
  const bool ok=decrypt?CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)!=FALSE:
    CryptProtectData(&input,L"Xenon browser metadata",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)!=FALSE;
  if(!ok)throw std::runtime_error("Browser metadata encryption unavailable");
  std::vector<unsigned char> result(output.pbData,output.pbData+output.cbData);SecureZeroMemory(output.pbData,output.cbData);LocalFree(output.pbData);return result;
}
void import_bookmarks(const Json& node,Json& output,std::set<std::string>& seen,size_t depth=0){
  if(depth>32||output.size()>=10000)return;
  if(node.is_object()){const auto url=node.value("url",std::string{});if(web(url)&&seen.insert(url).second)output.push_back({{"url",url},{"title",node.value("name",url).substr(0,1024)}});
    for(const auto& item:node.items())if(item.value().is_object()||item.value().is_array())import_bookmarks(item.value(),output,seen,depth+1);
  }else if(node.is_array())for(const auto& child:node)import_bookmarks(child,output,seen,depth+1);
}
}
BrowserData::Document& BrowserData::load(const std::string& workspace,bool private_mode){
  if(auto found=documents_.find(workspace);found!=documents_.end())return found->second;
  Document document{{{"version",1},{"bookmarks",Json::array()},{"history",Json::array()},{"legacyImported",false}},private_mode,false};
  if(!private_mode){const auto path=directory(root_,workspace);const auto state=path/"Xenon metadata.dpapi";
    if(std::filesystem::exists(state)){
      if(!regular(state)||std::filesystem::file_size(state)>32*1024*1024)throw std::runtime_error("Browser metadata is unavailable");
      std::ifstream input(state,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});auto clear=protect(bytes,true);
      try{document.value=Json::parse(clear.begin(),clear.end());}catch(...){SecureZeroMemory(clear.data(),clear.size());throw;}
      SecureZeroMemory(clear.data(),clear.size());if(document.value.value("version",0)!=1||!document.value["bookmarks"].is_array()||!document.value["history"].is_array())throw std::runtime_error("Invalid browser metadata");
    }
    if(!document.value.value("legacyImported",false)){
      bool complete=true;const auto bookmarks=path/"Bookmarks",history=path/"History";std::set<std::string> seen;
      for(const auto& bookmark:document.value["bookmarks"])seen.insert(bookmark.value("url",std::string{}));
      if(std::filesystem::exists(bookmarks)){
        if(!regular(bookmarks)||std::filesystem::file_size(bookmarks)>16*1024*1024)complete=false;
        else try{Json value;std::ifstream(bookmarks)>>value;import_bookmarks(value,document.value["bookmarks"],seen);}catch(...){complete=false;}
      }
      if(std::filesystem::exists(history)){
        sqlite3* database=nullptr;
        if(!regular(history)||sqlite3_open_v2(reinterpret_cast<const char*>(history.u8string().c_str()),&database,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK)complete=false;
        else {
          sqlite3_busy_timeout(database,50);sqlite3_stmt* query=nullptr;
          if(sqlite3_prepare_v2(database,"SELECT url,title,last_visit_time FROM urls ORDER BY last_visit_time DESC LIMIT 100000",-1,&query,nullptr)!=SQLITE_OK)complete=false;
          else {std::set<std::string> visits;for(const auto& row:document.value["history"])visits.insert(row.value("url",std::string{}));int status=SQLITE_OK;
            while((status=sqlite3_step(query))==SQLITE_ROW){const auto url_bytes=sqlite3_column_text(query,0),title_bytes=sqlite3_column_text(query,1);if(!url_bytes)continue;
              std::string url(reinterpret_cast<const char*>(url_bytes));if(web(url)&&visits.insert(url).second)document.value["history"].push_back({{"url",url},{"title",title_bytes?std::string(reinterpret_cast<const char*>(title_bytes)).substr(0,1024):url},{"visitedAt",sqlite3_column_int64(query,2)/1000-11644473600000LL}});}
            if(status!=SQLITE_DONE)complete=false;sqlite3_finalize(query);
          }
        }if(database)sqlite3_close(database);
      }
      document.value["legacyImported"]=complete;document.dirty=true;
    }
  }
  return documents_.emplace(workspace,std::move(document)).first->second;
}
Json BrowserData::list(const std::string& workspace,bool private_mode){return load(workspace,private_mode).value;}
bool BrowserData::bookmark(const std::string& workspace,bool private_mode,const std::string& url,const std::string& title){
  if(!web(url))return false;auto& document=load(workspace,private_mode);for(auto& item:document.value["bookmarks"])if(item["url"]==url){item["title"]=title.substr(0,1024);document.dirty=true;return flush();}
  if(document.value["bookmarks"].size()>=10000)return false;document.value["bookmarks"].push_back({{"url",url},{"title",title.substr(0,1024)}});document.dirty=true;return flush();
}
bool BrowserData::remove_bookmark(const std::string& workspace,bool private_mode,const std::string& url){auto& document=load(workspace,private_mode);
  auto& entries=document.value["bookmarks"];for(auto item=entries.begin();item!=entries.end();)if(item->value("url",std::string{})==url)item=entries.erase(item);else ++item;document.dirty=true;return flush();}
void BrowserData::visit(const std::string& workspace,bool private_mode,bool protected_auth,const std::string& url,const std::string& title){
  if(protected_auth||!web(url))return;auto& document=load(workspace,private_mode);auto& history=document.value["history"];
  if(!history.empty()&&history[0].value("url",std::string{})==url)return;
  const auto time=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  if(history.size()>=100000)history.erase(history.end()-1);
  history.insert(history.begin(),Json{{"url",url},{"title",title.substr(0,1024)},{"visitedAt",time}});document.dirty=true;
}
bool BrowserData::flush(){bool success=true;
  for(auto& [workspace,document]:documents_)if(document.dirty){if(document.private_mode){document.dirty=false;continue;}
    try{const auto path=directory(root_,workspace);std::filesystem::create_directories(path);const auto destination=path/"Xenon metadata.dpapi",temporary=path/("Xenon metadata-"+local_security::random_hex(8)+".tmp");
      if(std::filesystem::exists(destination)&&!regular(destination))throw std::runtime_error("Unsafe browser metadata file");
      const auto bytes=protect(document.value.dump(),false);if(bytes.size()>32*1024*1024)throw std::runtime_error("Browser metadata limit reached");
      HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create browser metadata staging file");
      DWORD written{};const bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file);CloseHandle(file);
      local_security::restrict_path(temporary);
      if(!ok||!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temporary.c_str());throw std::runtime_error("Cannot save browser metadata");}document.dirty=false;
    }catch(...){success=false;}
  }return success;
}
}
