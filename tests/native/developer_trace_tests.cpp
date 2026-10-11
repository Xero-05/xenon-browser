#include "xenon/developer_trace.hpp"
#include <stdexcept>
#include <thread>
#include <vector>
using namespace xenon;
void require(bool value){if(!value)throw std::runtime_error("developer trace regression");}
int main(){
  DeveloperTrace trace;
  Json request{{"id",7},{"method","page.inspect"},{"params",{{"tabId","tab_fixture"},{"token","synthetic-token"},{"steps",Json::array({{{"text","synthetic-password"}}})}}}};
  require(trace.begin("connection",request)==0);
  trace.enable(true);const auto first=trace.begin("connection",request);
  const auto second=trace.begin("other",{{"method","page.screenshot"}});
  trace.finish(second,success({{"mimeType","image/png"},{"data","synthetic-image"}}));
  auto entries=trace.snapshot()["entries"];
  require(entries[0]["status"]=="pending"&&entries[1]["screenshot"]=="synthetic-image");
  trace.finish(first,success({{"text","Rendered fixture evidence"},{"password","synthetic-password"}}));
  entries=trace.snapshot()["entries"];
  require(entries[0]["response"]["result"]["text"]=="Rendered fixture evidence");
  const auto encoded=entries.dump();require(encoded.find("synthetic-token")==std::string::npos&&encoded.find("synthetic-password")==std::string::npos);
  require(entries[1]["response"]["result"]["data"].get<std::string>().find("omitted")!=std::string::npos);
  trace.clear();trace.finish(first,success({{"late",true}}));require(trace.snapshot()["entries"].empty());
  const auto pending=trace.begin("connection",request);trace.enable(false);trace.enable(true);
  const auto fresh=trace.begin("connection",request);require(fresh!=pending);
  trace.finish(pending,success({}));require(trace.snapshot()["entries"][0]["status"]=="pending");
  trace.finish(fresh,failure("DENIED","Synthetic denial"));require(trace.snapshot()["entries"][0]["status"]=="error");
  trace.clear();std::vector<std::thread> threads;
  for(int n=0;n<4;++n)threads.emplace_back([&]{for(int j=0;j<100;++j){auto id=trace.begin("fixture",request);trace.finish(id,success({}));}});
  for(auto& thread:threads)thread.join();
  require(trace.snapshot()["entries"].size()==128);
  trace.clear();auto large=trace.begin("fixture",{{"method","fixture"},{"params",{{"payload",std::string(3*1024*1024,'x')}}}});
  require(trace.snapshot()["entries"][0]["truncated"]==true);trace.finish(large,success({}));
  trace.clear();for(int i=0;i<20;++i){auto id=trace.begin("fixture",request);trace.finish(id,success({{"payload",std::string(600*1024,'x')}}));}
  require(trace.snapshot()["entries"].dump().size()<8*1024*1024);
}
