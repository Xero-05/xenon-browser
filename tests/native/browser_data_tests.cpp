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
  BrowserData restarted(root);const auto restored=restarted.list("fixture",false);require(restored["bookmarks"].size()==2&&restored["history"].size()==2,"Native metadata restores without duplicating migration");
  require(restarted.list("private-fixture",true)["bookmarks"].empty(),"Private data disappears on restart");
  std::filesystem::remove_all(root);std::cout<<"Browser data tests passed: migration, encryption, history protection and private memory-only storage\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
