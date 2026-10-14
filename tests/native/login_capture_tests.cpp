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
void script_driven() {
  LoginCaptureState state;edit(state);
  // Script-driven sign-in keeps edit provenance, so a failed attempt can retry.
  require(state.attempted("form",context,3,start),"Qualified script-driven attempt denied");
  require(state.attempted("form",context,3,start+1min),"Script-driven retry lost provenance");
  require(!state.attempted("other",context,3,start)&&!state.attempted("form",{"",18},3,start)&&!state.attempted("form",context,4,start),"Another form, context or document inherited attempt provenance");
  require(!state.attempted("form",context,3,start+5min+1ms),"Expired edit authorized a script-driven attempt");
  const std::string origin="https://authentication.example.invalid";
  state.proposed_deferred("deferred",origin,"form",context,3);
  require(state.deferred()&&!state.notified(),"Deferred candidate was ready before the page moved on");
  require(!state.field_gone("other",context,3)&&!state.field_gone("form",{"",18},3)&&!state.field_gone("form",context,4),"Another form, context or document readied a deferred candidate");
  require(state.field_gone("form",context,3)&&!state.deferred()&&state.candidate()=="deferred","Removed password field did not ready the candidate");
  require(!state.field_gone("form",context,3),"A ready candidate was readied twice");
  state.proposed_deferred("single-page",origin,"form",context,3);
  require(state.navigated_within_document()&&!state.deferred()&&state.candidate()=="single-page","Same-document navigation did not ready the candidate");
  require(!state.navigated_within_document(),"Same-document navigation readied a candidate that was not deferred");
  state.proposed_deferred("navigation",origin,"form",context,3);
  require(state.navigated(true).empty()&&state.candidate()=="navigation"&&!state.deferred(),"HTTPS navigation discarded or did not ready a deferred candidate");
  state.proposed_deferred("downgrade",origin,"form",context,3);
  require(state.navigated(false)=="downgrade"&&state.candidate().empty(),"Non-HTTPS navigation kept a deferred candidate");
  edit(state);state.proposed_deferred("retyped",origin,"form",context,3);
  state.physical_input(start+1s);const auto canceled=state.password_edited("form",context,3,start+1s);
  require(canceled&&*canceled=="retyped"&&!state.deferred()&&state.candidate().empty(),"Retyping the password did not cancel the deferred attempt");
}
void username_first() {
  LoginCaptureState state;const std::string origin="https://authentication.example.invalid",account="PUBLIC_ACCOUNT_CANARY";
  state.account_entered(origin,account,start);
  require(state.account(origin,start).empty(),"Account without physical input was remembered");
  state.physical_input(start);state.account_entered(origin,account,start+2001ms);
  require(state.account(origin,start+2001ms).empty(),"Stale physical input qualified an account");
  state.account_entered(origin,account,start);
  require(state.account(origin,start+10min)==account,"Typed account was not remembered for its origin");
  require(state.account("https://other.example.invalid",start).empty(),"Account crossed origins");
  require(state.account(origin,start+10min+1ms).empty(),"Account outlived its bounded lifetime");
  state.navigated(true);
  require(state.account(origin,start+1min)==account,"Navigation between sign-in steps forgot the account");
  state.forget_account();require(state.account(origin,start).empty(),"Forgetting retained the account");
  state.physical_input(start);state.account_entered(origin,std::string(1025,'a'),start);
  require(state.account(origin,start).empty(),"Oversized account was remembered");
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
  try { provenance();transitions();script_driven();username_first();native_confirmation();std::cout<<"login capture policy tests passed\n";return 0; }
  catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
