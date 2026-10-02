#include "xenon/login_monitor.hpp"
#include "xenon/local_security.hpp"
#include "xenon/vault.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace xenon;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
const LoginCaptureState::Context context{"",17};
const auto start=LoginCaptureState::Clock::time_point(10s);
void edit(LoginCaptureState& state,LoginCaptureState::Clock::time_point now=start) {
  state.physical_input(now);
  require(state.password_edited("form",context,3,now).has_value(),"Physical password edit denied");
}
void provenance() {
  LoginCaptureState state;
  require(!state.password_edited("form",context,3,start),"Synthetic/agent input qualified without physical input");
  require(!state.consume_submission("form",context,3,start),"Submission qualified without password edit");
  state.physical_input(start);
  require(!state.password_edited("form",context,3,start+2001ms),"Expired physical input authorized editing");
  require(!state.password_edited("form",context,3,start-1ms),"Future physical input authorized editing");
  edit(state);
  require(!state.consume_submission("other",context,3,start),"Different form inherited provenance");
  require(!state.consume_submission("form",{"",18},3,start),"Different execution context inherited provenance");
  require(!state.consume_submission("form",{"child",17},3,start),"Different session inherited provenance");
  require(!state.consume_submission("form",context,4,start),"Different document inherited provenance");
  require(!state.consume_submission("form",context,3,start+5min+1ms),"Expired password edit authorized submission");
  require(state.consume_submission("form",context,3,start+5min),"Valid physical provenance denied");
  require(!state.consume_submission("form",context,3,start+5min),"Submission provenance was replayable");
}
void transitions() {
  LoginCaptureState state;edit(state);
  require(state.consume_submission("form",context,3,start),"Qualified submission denied");
  state.proposed("source-candidate","https://authentication.example.invalid");
  state.mark_notified();
  for(const auto* url:{"https://mfa.example.invalid/challenge","https://portal.example.invalid/account"}) {
    require(state.navigated(Vault::normalize_https_origin(url).has_value()).empty(),"HTTPS SSO navigation canceled candidate");
    require(state.origin()=="https://authentication.example.invalid"&&state.candidate()=="source-candidate"&&state.notified(),"SSO rebound origin or repeated prompt");
    state.physical_input(start+1min); // e.g. clicking Duo or typing an OTP, not a password edit.
    require(state.candidate()=="source-candidate","Ordinary MFA physical input discarded candidate");
    require(!state.consume_submission("form",context,3,start+1min),"Navigation retained source edit provenance");
  }
  state.physical_input(start+2min);
  const auto canceled=state.password_edited("new-password-form",{"",25},8,start+2min);
  require(canceled&&*canceled=="source-candidate"&&state.candidate().empty(),"New qualified password editing did not cancel old prompt");
  state.proposed("new-candidate","https://portal.example.invalid");
  require(state.navigated(Vault::normalize_https_origin("http://portal.example.invalid/").has_value())=="new-candidate","HTTP downgrade did not cancel candidate");
  require(state.origin().empty()&&state.candidate().empty()&&!state.notified(),"Canceled state retained native metadata");
  for(const auto* url:{"about:blank","file:///C:/example.html","data:text/html,example"}) {
    state.proposed("candidate","https://authentication.example.invalid");
    require(state.navigated(Vault::normalize_https_origin(url).has_value())=="candidate","Non-HTTPS navigation retained candidate");
  }
  edit(state);state.context_destroyed({"",18});
  require(state.consume_submission("form",context,3,start),"Unrelated context destruction removed provenance");
  edit(state);state.context_destroyed(context);
  require(!state.consume_submission("form",context,3,start),"Destroyed context retained provenance");
  edit(state);state.contexts_cleared("");
  require(!state.consume_submission("form",context,3,start),"Cleared context retained provenance");
  edit(state);state.proposed("candidate","https://authentication.example.invalid");
  state.clear_edit(); // Same reset used before agent mutation, tab close and Windows lock.
  require(state.discard_candidate()=="candidate"&&!state.consume_submission("form",context,3,start),"Cancellation retained a candidate or edit provenance");
}
void native_confirmation() {
  const auto parent=std::filesystem::absolute(std::filesystem::current_path()/"test_state");
  const auto root=parent/("login-capture-"+local_security::random_hex(8));
  std::filesystem::create_directories(root);
  struct Cleanup {
    std::filesystem::path root,parent;
    ~Cleanup(){if(root.parent_path()==parent&&root.filename().wstring().starts_with(L"login-capture-")){std::error_code ec;std::filesystem::remove_all(root,ec);}}
  } cleanup{root,parent};
  auto now=start;Vault vault(root/"vault.sqlite3",[&]{return now;});LoginCaptureState state;
  const std::string origin="https://authentication.example.invalid",user="PUBLIC_SSO_USER_CANARY",password="PUBLIC_SSO_WRONG_PASSWORD_CANARY";
  edit(state);require(state.consume_submission("form",context,3,start),"Qualified submission denied");
  const auto candidate=vault.propose_login(origin,user,password);
  require(candidate.has_value(),"Submitted attempt did not produce a native candidate");
  state.proposed(candidate->candidate_id,candidate->origin);
  state.navigated(Vault::normalize_https_origin("https://duo.example.invalid/").has_value());state.physical_input(start+1min);
  state.navigated(Vault::normalize_https_origin("https://portal.example.invalid/").has_value());
  require(vault.list_accounts().at("result").at("accounts").empty(),"Submission or redirect saved without human confirmation");
  require(vault.pending_logins().at(0).origin==origin,"Pending vault origin rebound to SSO destination");
  // No authentication success is asserted. A human may explicitly save even a
  // mistaken attempt; neither a website's error nor its success text decides.
  const auto accepted=vault.accept_login(state.candidate());
  require(accepted.at("ok").get<bool>(),"Native explicit Save failed");
  const auto account=accepted.at("result").at("accountId").get<std::string>();
  require(vault.get_secret(account,origin)->password()==password,"Captured password changed before confirmation");
  require(!vault.get_secret(account,"https://duo.example.invalid")&&!vault.get_secret(account,"https://portal.example.invalid"),"SSO destination could obtain source password");
  const auto next=vault.propose_login(origin,user,password+"-new");state.proposed(next->candidate_id,origin);
  const auto canceled=state.navigated(false);vault.dismiss_login(canceled);
  require(vault.pending_logins().empty()&&!vault.accept_login(canceled).at("ok").get<bool>(),"Downgrade cancellation left an acceptable candidate");
  const auto expired=vault.propose_login(origin,user,password+"-expiry");now+=Vault::kPendingLoginLifetime;
  require(vault.pending_logins().empty()&&!vault.accept_login(expired->candidate_id).at("ok").get<bool>(),"SSO candidate outlived its bounded lifetime");
  const auto locked=vault.propose_login(origin,user,password+"-lock");vault.set_locked(true);vault.set_locked(false);
  require(vault.pending_logins().empty()&&!vault.accept_login(locked->candidate_id).at("ok").get<bool>(),"Lock/unlock revived pending SSO credential");
}
}
int main() {
  try { provenance();transitions();native_confirmation();std::cout<<"login capture policy tests passed\n";return 0; }
  catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
