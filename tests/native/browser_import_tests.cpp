#include "xenon/browser_data.hpp"
#include "xenon/browser_import.hpp"
#include "xenon/external_protocol.hpp"
#include "xenon/launch_urls.hpp"
#include "xenon/local_security.hpp"
#include <sqlite3.h>
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace xenon;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
const browser_import::Folder* child(const browser_import::Folder& folder,const std::string& name){for(const auto& item:folder.folders)if(item.name==name)return &item;return nullptr;}
size_t count(const Json& rows,const char* key,const std::string& value){return static_cast<size_t>(std::count_if(rows.begin(),rows.end(),[&](const Json& row){return row.value(key,std::string{})==value;}));}
int main(){try{
  // Links for other apps: only registered-app schemes, escaped for the command line.
  require(external::scheme("mailto:someone@example.invalid")=="mailto"&&external::scheme("ZoomMtg://join?confno=1")=="zoommtg","Ordinary app schemes are offered, lowercased");
  for(const char* denied:{"https://example.invalid/","http://example.invalid/","file:///C:/Windows/","javascript:alert(1)","data:text/html,x","ms-msdt:/id","search-ms:query=x","shell:startup","vbscript:x","chrome://settings","C:\\Windows\\notepad.exe","about:blank","1password:open","mail to:x",":x"})
    require(external::scheme(denied).empty(),denied);
  require(external::scheme("mailto:"+std::string(external::max_url_length,'a')).empty(),"Overlong app links are refused");
  require(external::escape("mailto:a b\"c<d>|e^f`g{h}i\\j")=="mailto:a%20b%22c%3Cd%3E%7Ce%5Ef%60g%7Bh%7Di%5Cj","Characters a command line could split are escaped");
  require(external::escape("mailto:x%20y?subject=a#top")=="mailto:x%20y?subject=a#top","Existing escapes, queries and fragments are kept");
  require(external::escape("x:\xC3\xA9\n")=="x:%C3%A9%0A","Non-ASCII and control bytes are escaped");
  // Links Windows passes to Xenon as the default browser.
  require(launch_url("https://example.invalid/a b")&&launch_url("HTTP://example.invalid")&&!launch_url("https://user@example.invalid/")&&!launch_url("javascript:alert(1)")&&
    !launch_url("https://example.invalid/\nx")&&!launch_url("https:///path")&&!launch_url("ftp://example.invalid/")&&!launch_url("--user-data-dir=C:\\x"),"Only ordinary web addresses open from a launch");
  require(single_argument(L"\"C:\\X\\Xenon.exe\" --single-argument https://example.invalid/?q=\"a b\" --flag")==L"https://example.invalid/?q=\"a b\" --flag","Everything after --single-argument is one argument");
  require(single_argument(L"Xenon.exe --single-argument \"https://example.invalid/\"")==L"https://example.invalid/"&&single_argument(L"Xenon.exe https://example.invalid/").empty(),"Single-argument quotes are removed and absence is empty");
  const auto urls=launch_urls({"https://a.invalid/","--broker-pipe=xenon-x","https://a.invalid/","http://b.invalid/","file:///C:/"});
  require(urls==std::vector<std::string>{"https://a.invalid/","http://b.invalid/"},"Launch addresses are filtered and deduplicated in order");
  // Text cleanup for imported names.
  require(browser_import::clean_text("A\tB\r\nC")=="A B C"&&browser_import::clean_text("x\xE2\x80\xAEy\xE2\x81\xA6z")=="xyz"&&browser_import::clean_text("  trim  ")=="trim","Control and bidirectional characters are removed");
  require(browser_import::clean_text("\xE6\x97\xA5\xE6\x9C\xAC",4)=="\xE6\x97\xA5","Limits cut on a character boundary");
  // A Netscape bookmark file with nesting, entities and unsafe links.
  const std::string html=R"HTML(<!DOCTYPE NETSCAPE-Bookmark-file-1>
<META HTTP-EQUIV="Content-Type" CONTENT="text/html; charset=UTF-8">
<TITLE>Bookmarks</TITLE><H1>Bookmarks</H1>
<DL><p>
  <DT><H3 ADD_DATE="1" PERSONAL_TOOLBAR_FOLDER="true">Bookmarks bar</H3>
  <DL><p>
    <DT><A HREF="https://example.invalid/one" ADD_DATE="1" ICON="data:image/png;base64,AAAA">One &amp; only</A>
    <DT><H3>Work &lt;team&gt;</H3>
    <DL><p>
      <DT><a href='https://example.invalid/two?a=1&amp;b=2'>Two</a>
      <DT><A HREF="javascript:alert(1)">Script</A>
      <DT><A HREF="file:///C:/secret">File</A>
    </DL><p>
    <DT><H3>Empty</H3>
    <DL><p>
    </DL><p>
  </DL><p>
  <DT><A HREF="https://example.invalid/top">&#x65E5;&#26412; &quot;top&quot;</A>
</DL><p>)HTML";
  const auto parsed=browser_import::bookmarks_html(html);
  require(parsed.links.size()==1&&parsed.links[0].title=="\xE6\x97\xA5\xE6\x9C\xAC \"top\"","Top-level links and numeric references are read");
  const auto bar=child(parsed,"Bookmarks bar");require(bar&&bar->links.size()==1&&bar->links[0].title=="One & only","Folders keep their links in order");
  const auto work=bar?child(*bar,"Work <team>"):nullptr;
  require(work&&work->links.size()==1&&work->links[0].url=="https://example.invalid/two?a=1&b=2","Nested folders, quoted attributes and entities in links");
  require(bar&&!child(*bar,"Empty")&&browser_import::link_count(parsed)==3,"Empty folders and non-web links are dropped");
  // Chromium's Bookmarks file.
  const auto chromium=browser_import::chromium_bookmarks(Json::parse(R"({"roots":{
    "bookmark_bar":{"name":"Bookmarks bar","type":"folder","children":[{"type":"url","name":"A","url":"https://a.invalid/"},{"type":"folder","name":"Sub","children":[{"type":"url","name":"B","url":"https://b.invalid/"},{"type":"url","name":"C","url":"chrome://settings"}]}]},
    "other":{"name":"","type":"folder","children":[{"type":"url","name":"D","url":"http://d.invalid/"}]},
    "synced":{"name":"Mobile bookmarks","type":"folder","children":[]}}})"));
  require(chromium.folders.size()==2&&chromium.folders[0].name=="Bookmarks bar"&&chromium.folders[1].name=="Other bookmarks","Chromium roots become named folders; empty roots are skipped");
  require(chromium.folders[0].folders.size()==1&&chromium.folders[0].folders[0].links.size()==1&&browser_import::link_count(chromium)==3,"Nested Chromium folders keep only web links");

  // Folders and history in a private (memory-only) workspace.
  const auto root=std::filesystem::absolute(std::filesystem::path("build")/"browser-import-tests"/local_security::random_hex(8));std::filesystem::create_directories(root);
  BrowserData data(root);const std::string space="folders-fixture";
  require(data.bookmark(space,true,"https://example.invalid/a","Synthetic A")&&data.bookmark(space,true,"https://example.invalid/b","Synthetic B"),"Bookmarks start at the top level");
  const auto work_folder=data.add_folder(space,true,"","Work\n");const auto nested=data.add_folder(space,true,work_folder,"Nested");
  require(!work_folder.empty()&&!nested.empty()&&data.add_folder(space,true,"missing","X").empty()&&data.add_folder(space,true,"","\t").empty(),"Folders need a known parent and a name");
  require(data.move_bookmark(space,true,"https://example.invalid/a",nested)&&!data.move_bookmark(space,true,"https://example.invalid/a","missing"),"Bookmarks move only into known folders");
  require(!data.move_folder(space,true,work_folder,nested)&&!data.move_folder(space,true,work_folder,work_folder),"A folder cannot move into itself");
  require(data.rename_folder(space,true,work_folder,"Projects")&&data.rename_bookmark(space,true,"https://example.invalid/b","Renamed B"),"Folders and bookmarks can be renamed");
  auto listed=data.list(space,true);require(count(listed["folders"],"name","Projects")==1&&count(listed["bookmarks"],"title","Renamed B")==1&&count(listed["bookmarks"],"folder",nested)==1,"Folder changes are stored");
  require(data.remove_folder(space,true,work_folder),"Folders can be removed");listed=data.list(space,true);
  require(listed["folders"].empty()&&listed["bookmarks"].size()==1&&listed["bookmarks"][0]["url"]=="https://example.invalid/b","Removing a folder removes its subfolders and their bookmarks only");
  for(int n=0;n<3;++n)data.visit(space,true,false,"https://example.invalid/visit"+std::to_string(n),"Synthetic visit");
  listed=data.list(space,true);const auto visited=listed["history"][1].value("visitedAt",int64_t{});
  require(data.remove_visits(space,true,{{"https://example.invalid/visit1",visited}})&&data.list(space,true)["history"].size()==2,"Exactly the selected visit is removed");
  require(data.clear_history(space,true,0)&&data.list(space,true)["history"].empty(),"Clearing all history removes every visit");

  // Import into a persistent workspace, then reopen it.
  browser_import::Data imported;imported.bookmarks=parsed;imported.bookmarks_read=true;imported.history_read=true;
  imported.history={{"https://example.invalid/old","Old",1000},{"https://example.invalid/new","New",5000},{"javascript:x","Script",9000}};
  require(data.bookmark("import-fixture",false,"https://example.invalid/one","Already saved"),"Seed an existing bookmark");
  data.visit("import-fixture",false,false,"https://example.invalid/new","Existing visit");
  const auto counts=data.import("import-fixture",false,imported,"Imported from Fixture");
  require(counts.ok&&counts.bookmarks==2&&counts.visits==1,"Import skips URLs already present and non-web history");
  require(counts.folders==3,"Import creates its top folder and the folders that received bookmarks");
  const auto again=data.import("import-fixture",false,imported,"Imported from Fixture");
  require(again.bookmarks==0&&again.folders==0&&again.visits==0,"Importing the same data again adds nothing, not even empty folders");
  BrowserData reopened(root);const auto restored=reopened.list("import-fixture",false);
  require(restored["bookmarks"].size()==3&&count(restored["folders"],"name","Imported from Fixture")==1&&count(restored["folders"],"name","Work <team>")==1,"Imported folders persist");
  require(restored["history"].size()==2&&restored["history"][0]["url"]=="https://example.invalid/new","Imported history is merged newest first");

  // Reading a Chromium profile copies its History, then deletes the copy.
  const auto profile=root/"Synthetic Browser"/"User Data"/"Default";std::filesystem::create_directories(profile);
  {std::ofstream stream(profile/"Bookmarks");stream<<R"({"roots":{"bookmark_bar":{"name":"Bar","type":"folder","children":[{"type":"url","name":"P","url":"https://profile.invalid/"}]}}})";}
  sqlite3* database{};require(sqlite3_open(reinterpret_cast<const char*>((profile/"History").u8string().c_str()),&database)==SQLITE_OK,"Create synthetic profile history");
  require(sqlite3_exec(database,"CREATE TABLE urls(url TEXT,title TEXT,last_visit_time INTEGER); INSERT INTO urls VALUES('https://profile.invalid/page','Profile page',13300000000000000),('chrome://settings','Settings',13300000000000001);",nullptr,nullptr,nullptr)==SQLITE_OK,"Seed profile history");
  // Hold the database open for writing, as a running browser does.
  const HANDLE held=CreateFileW((profile/"History").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
  const auto staging=root/"staging";std::filesystem::create_directories(staging);
  const auto read=browser_import::read_chromium_profile(profile,true,true,staging);
  if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);sqlite3_close(database);
  require(read.bookmarks_read&&browser_import::link_count(read.bookmarks)==1,"Profile bookmarks are read");
  require(read.history_read&&read.history.size()==1&&read.history[0].url=="https://profile.invalid/page"&&read.history[0].visited==1655526400000,"Profile history is read while the file is open, web pages only");
  require(std::filesystem::is_empty(staging),"The private History copy is deleted");
  require(browser_import::chromium_sources(root/"none").empty(),"No sources without browser folders");
  // Profiles come from the browser's Local State names; unsafe directory names are ignored.
  const auto user_data=root/"local"/"Google"/"Chrome"/"User Data";std::filesystem::create_directories(user_data/"Default");std::filesystem::create_directories(user_data/"Profile 2");
  {std::ofstream stream(user_data/"Local State");stream<<R"({"profile":{"info_cache":{"Default":{"name":"Person 1"},"..\\evil":{"name":"Escape"},"Profile 2":{"name":"Work"}}}})";}
  {std::ofstream stream(user_data/"Default"/"Bookmarks");stream<<"{}";}
  const auto sources=browser_import::chromium_sources(root/"local");
  require(sources.size()==1&&sources[0].browser=="Google Chrome"&&sources[0].profile_name=="Person 1"&&sources[0].bookmarks&&!sources[0].history,"Only profiles with bookmarks or history, named from Local State");
  std::filesystem::remove_all(root);std::cout<<"Browser import tests passed: app-link checks, launch addresses, bookmark files, folders, history and import\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
