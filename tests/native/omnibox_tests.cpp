#include "xenon/omnibox.hpp"
#include <iostream>
#include <stdexcept>
using namespace xenon;
using omnibox::Kind;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
constexpr int64_t now=1'800'000'000'000;constexpr int64_t day=86'400'000;
Json visit(const std::string& url,const std::string& title,int64_t at){return {{"url",url},{"title",title},{"visitedAt",at}};}
}
int main(){try{
  // Typed text keeps the address box's existing interpretation.
  require(omnibox::typed_destination("https://example.invalid/a b",false)=="https://example.invalid/a b","Explicit HTTPS URLs open as typed");
  require(omnibox::typed_destination("HTTP://example.invalid",false)=="HTTP://example.invalid","Explicit schemes are recognized in any case");
  require(omnibox::typed_destination("about:blank",false)=="about:blank","about:blank opens directly");
  require(omnibox::typed_destination("example",true)=="https://www.example.com","Ctrl+Enter completes a single word");
  require(omnibox::typed_destination("example.org",true)=="https://example.org","Ctrl+Enter leaves dotted input alone");
  require(omnibox::typed_destination("  example.org/path ",false)=="https://example.org/path","Dotted input opens over HTTPS after trimming");
  require(omnibox::typed_destination("hello world",false)=="https://www.google.com/search?q=hello+world","Words become a search");
  require(omnibox::typed_destination("a&b=c ü*",false)=="https://www.google.com/search?q=a%26b%3Dc+%C3%BC*","Queries are escaped like CefURIEncode");
  require(omnibox::typed_destination("localhost:3000",false)=="https://www.google.com/search?q=localhost%3A3000","Colon input without a scheme is unchanged");
  require(omnibox::display("https://www.github.com/")=="github.com"&&omnibox::edit_text("https://www.github.com/")=="www.github.com","Root URLs drop the scheme and slash");
  require(omnibox::display("http://example.invalid/a/")=="example.invalid/a/"&&omnibox::edit_text("http://example.invalid/a/")=="http://example.invalid/a/","HTTP keeps its scheme while edited");
  require(omnibox::display("https://example.invalid?q=/")=="example.invalid?q=/","A query's slash is not a root slash");

  const Json history=Json::array({
    visit("https://github.com/synthetic/repo","Synthetic repository",now-2*day),visit("https://github.com/synthetic/repo","Older title",now-3*day),
    visit("https://github.com/synthetic/repo/pulls","Pull requests · synthetic",now-5*day),visit("https://gitlab.example.invalid/","GitLab fixture",now-40*day),
    visit("http://localhost:3000/app","Local fixture",now-day/2),visit("https://www.example.invalid/","Example home",now-day),
    visit("https://bigsite.invalid/","Big site",now),visit("https://unicode.invalid/","Überblick",now),visit("https://untitled.invalid/","https://untitled.invalid/",now),
    visit("javascript:alert(1)","Script",now),Json::array(),{{"url",7}},{{"url","https://typed.invalid/"},{"visitedAt","yesterday"}}});
  const Json bookmarks=Json::array({{{"url","https://docs.example.invalid/guide"},{"title","Fixture guide"}},{{"url","file:///C:/secret.txt"},{"title","Local file"}}});
  const auto index=omnibox::build(bookmarks,history);
  const auto entry=[&](const std::string& url)->const omnibox::Entry&{const auto found=index.at.find(url);if(found==index.at.end())throw std::runtime_error("Missing synthetic entry: "+url);return index.entries[found->second];};
  require(entry("https://github.com/synthetic/repo").visits==2&&entry("https://github.com/synthetic/repo").title=="Synthetic repository","Repeated visits count once per URL with the latest title");
  require(entry("https://docs.example.invalid/guide").bookmarked&&entry("https://docs.example.invalid/guide").visits==0,"Bookmarks join the index");
  require(entry("https://untitled.invalid/").title.empty()&&entry("https://typed.invalid/").visited==0,"URL-only titles and malformed times are ignored");
  require(!index.at.contains("javascript:alert(1)")&&!index.at.contains("file:///C:/secret.txt")&&index.entries.size()==10,"Only web addresses are suggested");
  {
    // New visits update the index exactly as rebuilding over the new history.
    auto updated=omnibox::build(bookmarks,history);
    omnibox::add_visit(updated,"https://github.com/synthetic/repo","Renamed repository",now+1);omnibox::add_visit(updated,"https://fresh.invalid/","https://fresh.invalid/",now+2);
    omnibox::add_visit(updated,"https://docs.example.invalid/guide","",now+3);omnibox::add_visit(updated,"file:///C:/visited.txt","Local file",now+4);
    Json newer=Json::array({visit("file:///C:/visited.txt","Local file",now+4),visit("https://docs.example.invalid/guide","",now+3),visit("https://fresh.invalid/","https://fresh.invalid/",now+2),visit("https://github.com/synthetic/repo","Renamed repository",now+1)});
    for(const auto& row:history)newer.push_back(row);
    const auto rebuilt=omnibox::build(bookmarks,newer);require(updated.entries.size()==rebuilt.entries.size(),"Incremental visits add the same entries");
    for(const auto& item:rebuilt.entries){const auto& other=updated.entries[updated.at.at(item.url)];
      require(other.visits==item.visits&&other.visited==item.visited&&other.title==item.title&&other.folded==item.folded&&other.bookmarked==item.bookmarked,"Incremental visits match a rebuild");}
    require(rebuilt.entries[rebuilt.at.at("https://docs.example.invalid/guide")].title=="Fixture guide","An untitled visit keeps the bookmark title");
  }

  // Typing a host prefix completes the host and opens the site root.
  auto rows=omnibox::suggest(index,"git",true,now);
  require(!rows.empty()&&rows[0].completion=="hub.com"&&rows[0].fill=="github.com"&&rows[0].destination=="https://github.com/","Host prefixes complete inline to the most visited host");
  require(!rows[0].removable,"An aggregated host is not itself a history entry");
  require(rows.size()>1&&rows[1].kind==Kind::search&&rows[1].destination=="https://www.google.com/search?q=git","A search row follows an address completion");
  require(std::any_of(rows.begin(),rows.end(),[](const auto& row){return row.destination=="https://github.com/synthetic/repo"&&row.kind==Kind::history&&row.removable&&row.fill=="github.com/synthetic/repo";}),"History pages are listed with their address text");
  rows=omnibox::suggest(index,"git",false,now);
  require(rows[0].kind==Kind::search&&rows[0].completion.empty(),"Without inline completion the typed text leads");
  rows=omnibox::suggest(index,"github.com/synthetic/repo/p",true,now);
  require(rows[0].completion=="ulls"&&rows[0].destination=="https://github.com/synthetic/repo/pulls","A typed path completes to a full URL");
  rows=omnibox::suggest(index,"GitHub.com",false,now);
  require(rows[0].completion.empty()&&rows[0].destination=="https://github.com/"&&rows[0].fill=="GitHub.com","An exact visited host leads even without completion");
  rows=omnibox::suggest(index,"local",true,now);
  require(rows[0].completion=="host:3000"&&rows[0].destination=="http://localhost:3000/","A visited HTTP host with a port completes with its scheme");
  rows=omnibox::suggest(index,"ex",true,now);
  require(rows[0].fill=="example.invalid"&&rows[0].destination=="https://www.example.invalid/","Leading www. is implied until typed");
  rows=omnibox::suggest(index,"www.ex",true,now);
  require(rows[0].fill=="www.example.invalid","Typed www. stays in the completion");
  rows=omnibox::suggest(index,"https://git",true,now);
  require(rows[0].fill=="https://github.com"&&rows.size()>1&&rows[1].kind!=Kind::search,"A typed scheme completes and has no search row");
  require(omnibox::suggest(index,"git ",true,now)[0].kind==Kind::search,"Trailing whitespace prevents inline completion");
  require(omnibox::suggest(index,"htt",true,now)[0].completion.empty(),"A partial scheme does not complete");

  // Rows match every term in the URL or title, with case-folded titles.
  rows=omnibox::suggest(index,"pull synthetic",true,now);
  require(rows[0].kind==Kind::search&&std::any_of(rows.begin(),rows.end(),[](const auto& row){return row.destination=="https://github.com/synthetic/repo/pulls";}),"All typed terms must match");
  require(std::none_of(rows.begin(),rows.end(),[](const auto& row){return row.destination=="https://gitlab.example.invalid/";}),"Pages missing a term are not listed");
  rows=omnibox::suggest(index,"über",true,now);
  require(std::any_of(rows.begin(),rows.end(),[](const auto& row){return row.destination=="https://unicode.invalid/"&&row.title=="Überblick";}),"Title matching folds non-ASCII case");
  rows=omnibox::suggest(index,"guide",true,now);
  require(std::any_of(rows.begin(),rows.end(),[](const auto& row){return row.kind==Kind::bookmark&&!row.removable&&row.title=="Fixture guide";}),"Bookmarks are listed but are not history to remove");
  rows=omnibox::suggest(index,"g",true,now);
  require(std::none_of(rows.begin(),rows.end(),[](const auto& row){return row.destination=="https://bigsite.invalid/";}),"One character matches only at word starts");
  rows=omnibox::suggest(index,"example.invalid",true,now);
  require(rows[0].destination=="https://www.example.invalid/"&&rows[0].title=="Example home","A typed address in history shows its title");
  rows=omnibox::suggest(index,"new.invalid",true,now);
  require(rows[0].kind==Kind::typed&&rows[0].destination=="https://new.invalid"&&rows[0].fill=="new.invalid"&&rows[1].kind==Kind::search,"An unknown address leads, then a search");
  require(omnibox::suggest(index,"https://new.invalid",true,now).size()==1,"An explicit URL has no search row");
  require(omnibox::suggest(index,"   ",true,now).empty(),"Blank text has no suggestions");
  require(omnibox::suggest(index,"i",true,now,3).size()<=3,"The row limit is respected");
  for(const auto& [input,inline_completion]:std::vector<std::pair<std::string,bool>>{{"git",true},{"example",true},{"invalid",false},{"synthetic repo",true}}){
    const auto list=omnibox::suggest(index,input,inline_completion,now);std::set<std::string> displays;
    for(const auto& row:list)require(displays.insert(row.kind==Kind::search?"?"+row.destination:row.display).second,"Rows are not duplicated");
  }
  // Frequency, recency and bookmarks order otherwise equal matches.
  const auto ranked=omnibox::build(Json::array({{{"url","https://alpha.invalid/"},{"title","Fixture"}}}),Json::array({
    visit("https://beta.invalid/","Fixture",now-60*day),visit("https://gamma.invalid/","Fixture",now),visit("https://gamma.invalid/","Fixture",now-day),visit("https://delta.invalid/","Fixture",now-60*day)}));
  rows=omnibox::suggest(ranked,"fixture",true,now);
  require(rows.size()==5&&rows[1].destination=="https://gamma.invalid/"&&rows[2].destination=="https://alpha.invalid/"&&rows[3].destination=="https://beta.invalid/","Frequent recent pages rank before bookmarks and old visits");

  require(omnibox::printable(L"Safe\u202Etxt.exe\r\nTitle\u2066")==L"Safetxt.exe  Title","Bidi formatting and line breaks are removed from row text");
  const auto mask=omnibox::emphasis(L"GitHub · git",L"git hub");
  require(mask==std::vector<bool>{true,true,true,true,true,true,false,false,false,true,true,true},"Every typed term is emphasized case-insensitively");
  std::cout<<"Omnibox tests passed: typed destinations, inline completion, matching, ranking and row text\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
