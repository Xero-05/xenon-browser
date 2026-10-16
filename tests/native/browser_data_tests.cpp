#include "xenon/browser_data.hpp"
#include "xenon/local_security.hpp"
#include <sqlite3.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace xenon;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
  const auto root=std::filesystem::absolute(std::filesystem::path("build")/"browser-data-tests"/local_security::random_hex(8));
  const auto profile=root/("workspace-"+local_security::sha256("fixture"));std::filesystem::create_directories(profile);
  const std::string original=R"({"roots":{"bookmark_bar":{"children":[{"name":"Synthetic saved page","url":"https://example.invalid/saved","type":"url"}]}}})";
  {std::ofstream stream(profile/"Bookmarks");stream<<original;}
  sqlite3* database{};require(sqlite3_open(reinterpret_cast<const char*>((profile/"History").u8string().c_str()),&database)==SQLITE_OK,"Create synthetic historical database");
  require(sqlite3_exec(database,"CREATE TABLE urls(url TEXT,title TEXT,last_visit_time INTEGER); INSERT INTO urls VALUES('https://example.invalid/old','Historical synthetic page',13300000000000000);",nullptr,nullptr,nullptr)==SQLITE_OK,"Seed historical page");sqlite3_close(database);
  BrowserData data(root);const auto imported=data.list("fixture",false);require(imported["bookmarks"].size()==1&&imported["history"].size()==1,"Legacy bookmarks and history imported non-destructively");
  data.visit("fixture",false,true,"https://example.invalid/protected","Synthetic protected authentication");require(data.list("fixture",false)["history"].size()==1,"Protected authentication excluded from new history");
  data.visit("fixture",false,false,"https://example.invalid/new","Synthetic public page");require(data.bookmark("fixture",false,"https://example.invalid/new","Synthetic new bookmark"),"Bookmark is durable");
  require(data.bookmark("private-fixture",true,"https://example.invalid/private","Synthetic private bookmark"),"Private bookmark works in memory");data.visit("private-fixture",true,false,"https://example.invalid/private","Synthetic private visit");require(data.flush(),"Metadata flush succeeds");
  require(!std::filesystem::exists(root/("workspace-"+local_security::sha256("private-fixture"))),"Private browsing writes no native metadata");
  {std::ifstream stream(profile/"Bookmarks");const std::string retained{std::istreambuf_iterator<char>(stream),{}};require(retained==original,"Migration preserves original bookmarks bytes");}
  {std::ifstream stream(profile/"Xenon metadata.dpapi",std::ios::binary);const std::string encrypted{std::istreambuf_iterator<char>(stream),{}};require(encrypted.find("example.invalid")==std::string::npos,"Native browsing metadata is encrypted");}
  // Address suggestions use only the workspace's own metadata and follow its changes.
  const auto has=[](const std::vector<omnibox::Match>& rows,const std::string& url){return std::any_of(rows.begin(),rows.end(),[&](const auto& row){return row.destination==url;});};
  const auto rows=data.suggest("fixture",false,"exa",true);
  require(!rows.empty()&&rows[0].fill=="example.invalid"&&rows[0].destination=="https://example.invalid/","Workspace history completes a host inline");
  require(!has(data.suggest("fixture",false,"private",true),"https://example.invalid/private"),"Private visits stay out of other workspaces' suggestions");
  require(has(data.suggest("private-fixture",true,"private",true),"https://example.invalid/private"),"Private workspaces suggest their in-memory metadata");
  require(!has(data.suggest("private-fixture",true,"later",true),"https://later.invalid/"),"Unknown pages are not suggested");
  require(data.bookmark("private-fixture",true,"https://later.invalid/","Synthetic later bookmark")&&has(data.suggest("private-fixture",true,"later",true),"https://later.invalid/"),"Suggestions follow new bookmarks");
  data.visit("private-fixture",true,false,"https://visited-later.invalid/","Synthetic later visit");
  require(has(data.suggest("private-fixture",true,"visited",true),"https://visited-later.invalid/"),"Suggestions follow new visits without a rebuild");
  BrowserData restarted(root);const auto restored=restarted.list("fixture",false);require(restored["bookmarks"].size()==2&&restored["history"].size()==2,"Native metadata restores without duplicating migration");
  require(restarted.list("private-fixture",true)["bookmarks"].empty(),"Private data disappears on restart");
  require(has(restarted.suggest("fixture",false,"example.invalid/o",true),"https://example.invalid/old"),"Migrated history is suggested");
  require(restarted.remove_history("fixture",false,"https://example.invalid/old"),"History removal is saved");
  require(!has(restarted.suggest("fixture",false,"example.invalid/o",true),"https://example.invalid/old"),"Removed history is no longer suggested");
  BrowserData reopened(root);const auto pruned=reopened.list("fixture",false);
  require(pruned["history"].size()==1&&pruned["bookmarks"].size()==2,"Removed history stays removed after restart and bookmarks are kept");
  std::filesystem::remove_all(root);std::cout<<"Browser data tests passed: migration, encryption, history protection, private memory-only storage and address suggestions\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
