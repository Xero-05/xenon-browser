#include "xenon/screenshot_guard.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace xenon::screenshot {
static bool contains_credentials(const Json& node) {
  if(!node.is_object())throw std::runtime_error("Invalid frame document");
  if(node.contains("attributes")) {
    const auto& attributes=node["attributes"];
    if(!attributes.is_array()||attributes.size()%2)throw std::runtime_error("Invalid frame attributes");
    for(size_t i=0;i+1<attributes.size();i+=2) {
      const auto key=attributes[i].get<std::string>();
      auto value=attributes[i+1].get<std::string>();
      std::transform(value.begin(),value.end(),value.begin(),[](unsigned char ch){return static_cast<char>(std::tolower(ch));});
      if((key=="type"&&value=="password")||(key=="autocomplete"&&
         (value.find("password")!=std::string::npos||value.find("one-time-code")!=std::string::npos)))return true;
    }
  }
  for(const char* key:{"children","shadowRoots"})if(node.contains(key)) {
    if(!node[key].is_array())throw std::runtime_error("Invalid frame children");
    for(const auto& child:node[key])if(contains_credentials(child))return true;
  }
  return node.contains("contentDocument")&&contains_credentials(node["contentDocument"]);
}

static Json protected_result(const char* reason) {
  auto result=failure("screenshot_protected","Credential fields or protected authentication prevent a safe screenshot.");
  result["error"]["reason"]=reason;result["error"]["retryable"]=false;
  return result;
}
static Json unverified_result(const char* reason) {
  auto result=failure("screenshot_unverified","A frame safety check could not complete. No image is returned. After the page settles, a fresh screenshot may be requested.");
  result["error"]["reason"]=reason;result["error"]["retryable"]=true;
  return result;
}
// Reused before capture dispatch and before publishing pixels. A verified scan
// never authorizes a different document, frame set, owner or human-input state.
Json validate(const State& scanned,const State& now,bool check_frames) {
  if(now.protected_auth)return protected_result("protected_authentication");
  if(now.screen_sharing)return failure("screen_sharing","This tab is sharing the person's screen. Screenshots are withheld until sharing stops.");
  if(!now.valid||now.epoch!=scanned.epoch||now.human!=scanned.human)
    return failure("page_changed","Control, human input or the document changed during screenshot inspection. Obtain fresh evidence.");
  if(!now.guards_ready)return failure("privacy_guard_initializing","The credential guard is not ready to capture this page.");
  if(check_frames&&(now.frames!=scanned.frames||now.sessions!=scanned.sessions))
    return unverified_result("frame_changed");
  return success();
}

// One full retry is allowed only for a failed read or changed frame set. Never
// reuse partial results, ignore a failed frame, or retry known credentials.
// The transport callbacks are the same seam used by CEF and deterministic tests.
class Guard : public std::enable_shared_from_this<Guard> {
 public:
  Guard(ReadState read,ReadDocument document,Checked checked)
    :read_(std::move(read)),document_(std::move(document)),checked_(std::move(checked)){}
  void start() {
    const auto initial=read_();
    auto valid=validate(initial,initial);
    if(!valid.value("ok",false)){finish(std::move(valid),initial);return;}
    if(initial.sessions.empty()){finish(unverified_result("frame_scan_failed"),initial);return;}
    struct Scan {size_t pending;bool secret=false,failed=false,timed_out=false;};
    auto scan=std::make_shared<Scan>();scan->pending=initial.sessions.size();
    for(const auto& session:initial.sessions)document_(session,[self=shared_from_this(),scan,initial](Json dom){
      if(dom.contains("error")||!dom.contains("root")){
        scan->failed=true;
        if(dom.value("error",Json::object()).value("xenonTimeout",false))scan->timed_out=true;
      } else {
        try{if(contains_credentials(dom["root"]))scan->secret=true;}
        catch(const std::exception&){scan->failed=true;}
      }
      if(--scan->pending)return;
      const auto now=self->read_();
      if(scan->secret){self->finish(protected_result("credential_fields"),initial);return;}
      auto valid=validate(initial,now,false);
      if(!valid.value("ok",false)){self->finish(std::move(valid),initial);return;}
      const bool changed=initial.frames!=now.frames||initial.sessions!=now.sessions;
      if(scan->failed||changed){
        if(self->attempt_++==0){self->start();return;}
        self->finish(unverified_result(changed?"frame_changed":scan->timed_out?"frame_scan_timeout":"frame_scan_failed"),initial);return;
      }
      self->finish(success(),initial);
    });
  }
 private:
  void finish(Json result,const State& state){auto checked=std::move(checked_);if(checked)checked(std::move(result),state);}
  ReadState read_;ReadDocument document_;Checked checked_;unsigned attempt_=0;
};
void check(ReadState read,ReadDocument document,Checked checked) {
  std::make_shared<Guard>(std::move(read),std::move(document),std::move(checked))->start();
}
}
