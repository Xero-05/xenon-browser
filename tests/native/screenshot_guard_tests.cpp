#include "xenon/screenshot_guard.hpp"
#include <deque>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace xenon;
namespace shot=xenon::screenshot;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
const Json safe={{"root",{{"children",Json::array()}}}};
const Json timeout={{"error",{{"message","Browser operation timed out; outcome may be unknown"},{"xenonTimeout",true}}}};
struct Harness {
  shot::State state;
  std::deque<std::pair<std::string,Reply>> pending;
  Json result;
  int completed=0,requests=0;
  Harness(){state.sessions={"","frame"};}
  void start(){
    shot::check([this]{return state;},[this](const std::string& session,Reply reply){
      ++requests;pending.emplace_back(session,std::move(reply));
    },[this](Json value,shot::State){++completed;result=std::move(value);});
  }
  void answer(Json value){require(!pending.empty(),"Expected document request");auto reply=std::move(pending.front().second);pending.pop_front();reply(std::move(value));}
};
int main(){
  int failures=0;
  auto test=[&](const char* name,const std::function<void()>& run){
    try{run();std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
  };
  test("transient frame scan recovery",[]{
    Harness h;h.start();h.answer(safe);h.answer(timeout);
    require(h.completed==0,"A transient frame failure must start one complete fresh scan, not report credential protection");
    require(h.pending.size()==2,"Recovery must recheck every frame, including the previously safe main frame");
    h.answer(safe);h.answer(safe);
    require(h.completed==1&&h.result.value("ok",false),"A fully verified recovery must permit capture");
  });
  test("persistent failure is bounded and does not claim credentials",[]{
    Harness h;h.start();h.answer(safe);h.answer(timeout);
    require(h.completed==0,"Must allow one retry");
    h.answer(safe);h.answer(timeout);
    require(h.completed==1&&h.requests==4,"Exactly two scans and one completion");
    require(h.result["error"]["code"]=="screenshot_unverified","Unverified frames need their own error");
    require(h.result["error"]["reason"]=="frame_scan_timeout","Timeout reason must survive");
    require(h.result["error"]["retryable"]==true,"A later read may retry after frames settle");
  });
  test("a recovered main frame must be rescanned for new credentials",[]{
    Harness h;h.start();h.answer(safe);h.answer(timeout);
    require(h.completed==0,"Retry expected");
    h.answer({{"root",{{"attributes",Json::array({"type","PASSWORD"})}}}});
    h.answer(safe);
    require(h.completed==1&&h.result["error"]["code"]=="screenshot_protected","New password input must prevent capture");
    require(h.requests==4,"Credential denial must not trigger further retries");
  });
  test("secret and timeout together remain a credential denial",[]{
    Harness h;h.start();
    h.answer({{"root",{{"shadowRoots",Json::array({{{"attributes",Json::array({"autocomplete","one-time-code"})}}})}}}});
    h.answer(timeout);
    require(h.completed==1&&h.requests==2,"Known credentials must not be retried");
    require(h.result["error"]["code"]=="screenshot_protected","Secret has precedence over timeout");
  });
  test("navigation during inspection withholds capture",[]{
    Harness h;h.start();h.answer(safe);++h.state.epoch;h.answer(safe);
    require(h.completed==1&&!h.result.value("ok",false),"Old document scan cannot authorize new document pixels");
    require(h.requests==2,"Navigation must not be automatically followed");
  });
  test("human input or authority loss stops recovery",[]{
    for(bool revoke:{false,true}){
      Harness h;h.start();h.answer(safe);
      if(revoke)h.state.valid=false;else ++h.state.human;
      h.answer(timeout);
      require(h.completed==1&&!h.result.value("ok",false)&&h.requests==2,"Interrupted inspection must not retry");
    }
  });
  test("frame replacement requires a new complete scan",[]{
    Harness h;h.start();h.answer(safe);
    ++h.state.frames;h.state.sessions={"","replacement"};h.answer(timeout);
    require(h.completed==0&&h.pending.size()==2,"Replacement must be inspected");
    require(h.pending.back().first=="replacement","Retry must use current sessions");
    h.answer(safe);h.answer(safe);
    require(h.completed==1&&h.result.value("ok",false),"Verified replacement can be captured");
  });
  test("protected login and screen sharing cannot be retried",[]{
    for(bool sharing:{false,true}){
      Harness h;h.start();h.answer(safe);
      if(sharing)h.state.screen_sharing=true;else h.state.protected_auth=true;
      h.answer(timeout);
      require(h.completed==1&&!h.result.value("ok",false)&&h.requests==2,"Protection change must stop inspection");
    }
  });
  test("missing and malformed frame replies fail closed",[]{
    for(const auto& response:{Json::object(),Json{{"root",nullptr}},Json{{"root",{{"attributes",Json::array({42,42})}}}}}){
      Harness h;h.start();h.answer(safe);h.answer(response);
      require(h.completed==0,"Unverified frame needs a new complete scan");
      h.answer(safe);h.answer(response);
      require(h.completed==1&&h.result["error"]["code"]=="screenshot_unverified","Malformed frame must never authorize pixels");
    }
  });
  test("a second frame change cannot cause an unbounded retry",[]{
    Harness h;h.start();h.answer(safe);++h.state.frames;h.answer(safe);
    require(h.completed==0,"First topology change can restart inspection");
    h.answer(safe);++h.state.frames;h.answer(safe);
    require(h.completed==1&&h.requests==4&&h.result["error"]["reason"]=="frame_changed","Repeated changes must withhold pixels");
  });
  test("capture continuation rejects changed state",[]{
    shot::State scanned;scanned.sessions={"","frame"};
    require(shot::validate(scanned,scanned).value("ok",false),"Matching scanned state remains usable");
    for(int change=0;change<8;++change){
      auto now=scanned;
      switch(change){
        case 0:++now.epoch;break;
        case 1:++now.human;break;
        case 2:++now.frames;break;
        case 3:now.sessions.push_back("new-frame");break;
        case 4:now.protected_auth=true;break;
        case 5:now.screen_sharing=true;break;
        case 6:now.valid=false;break;
        case 7:now.guards_ready=false;break;
      }
      require(!shot::validate(scanned,now).value("ok",false),"Changed state must not publish pixels");
    }
  });
  test("unsafe initial state never reads a document",[]{
    for(int condition=0;condition<5;++condition){
      Harness h;
      switch(condition){
        case 0:h.state.protected_auth=true;break;
        case 1:h.state.screen_sharing=true;break;
        case 2:h.state.guards_ready=false;break;
        case 3:h.state.valid=false;break;
        case 4:h.state.sessions.clear();break;
      }
      h.start();
      require(h.completed==1&&h.requests==0&&!h.result.value("ok",false),"Unsafe start must fail immediately");
    }
  });
  test("nested shadow and embedded document secrets remain protected",[]{
    const Json password={{"attributes",Json::array({"type","password"})}};
    for(const auto& root:{Json{{"children",Json::array({password})}},Json{{"shadowRoots",Json::array({password})}},Json{{"contentDocument",password}}}){
      Harness h;h.start();h.answer({{"root",root}});h.answer(safe);
      require(h.completed==1&&h.result["error"]["code"]=="screenshot_protected"&&h.requests==2,"All supported document containers must be checked");
    }
  });
  return failures?1:0;
}
