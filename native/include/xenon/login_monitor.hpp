#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace xenon {
// Secret-free policy for one native tab. Only a physical-input-qualified edit
// can authorize a submission; navigation never rebinds an existing candidate.
// Returned cancellation IDs must be dismissed from the native vault by caller.
class LoginCaptureState {
 public:
  using Clock = std::chrono::steady_clock;
  using Context = std::pair<std::string, int>;
  void physical_input(Clock::time_point now) { physical_input_ = now; }
  std::optional<std::string> password_edited(const std::string& form, const Context& context,
                                           uint64_t epoch, Clock::time_point now) {
    if(form.empty()||!physical_input_||now<*physical_input_||now-*physical_input_>std::chrono::seconds(2))return std::nullopt;
    auto canceled=discard_candidate();
    edit_form_=form;edit_context_=context;edit_epoch_=epoch;edit_at_=now;
    return canceled;
  }
  bool consume_submission(const std::string& form, const Context& context,
                          uint64_t epoch, Clock::time_point now) {
    if(edit_form_.empty()||edit_form_!=form||edit_context_!=context||edit_epoch_!=epoch||
       now<edit_at_||now-edit_at_>std::chrono::minutes(5))return false;
    clear_edit();return true;
  }
  void clear_edit() { physical_input_.reset();edit_form_.clear();edit_context_={};edit_epoch_=0;edit_at_={}; }
  void context_destroyed(const Context& context) { if(edit_context_==context)clear_edit(); }
  void contexts_cleared(const std::string& session) { if(edit_context_.first==session)clear_edit(); }
  void proposed(std::string id, std::string origin) { candidate_=std::move(id);origin_=std::move(origin);notified_=false; }
  std::string discard_candidate() { auto id=std::exchange(candidate_,{});origin_.clear();notified_=false;return id; }
  std::string navigated(bool verified_https) { clear_edit();return verified_https?std::string{}:discard_candidate(); }
  const std::string& candidate() const { return candidate_; }
  const std::string& origin() const { return origin_; }
  bool notified() const { return notified_; }
  void mark_notified() { notified_=true; }
 private:
  std::optional<Clock::time_point> physical_input_;
  Clock::time_point edit_at_{};
  std::string edit_form_,candidate_,origin_;
  Context edit_context_;
  uint64_t edit_epoch_{};
  bool notified_=false;
};

// Installed in the browser-owned isolated world, never in the website's world.
// The native receiver independently verifies physical-input provenance, frame,
// origin, lock state and pending submission identity. No MCP interface can call it.
inline constexpr const char* login_capture_script = R"XENONJS((()=>{
  if(globalThis.__xenonLoginCaptureInstalled || window!==top || location.protocol!=='https:')return;
  globalThis.__xenonLoginCaptureInstalled=true;
  const tokens=new WeakMap(),passwords=new WeakSet();
  const emit=value=>globalThis.__xenon_login_capture(JSON.stringify(value));
  const visible=e=>e.isConnected&&e.getClientRects().length>0&&getComputedStyle(e).visibility==='visible'&&getComputedStyle(e).display!=='none';
  const editable=e=>visible(e)&&!e.disabled&&!e.readOnly;
  const autocomplete=e=>(e.autocomplete||'').toLowerCase().split(/\s+/);
  const sensitive=e=>e instanceof HTMLInputElement&&(e.type==='password'||autocomplete(e).includes('current-password'));
  const remember=()=>{for(const e of document.querySelectorAll('input'))if(sensitive(e))passwords.add(e)};
  const token=form=>{if(!tokens.has(form))tokens.set(form,crypto.randomUUID());return tokens.get(form)};
  const fields=form=>{
    if(!(form instanceof HTMLFormElement)||!form.isConnected||form.method.toLowerCase()!=='post'||form.target&&form.target!=='_self')return null;
    let action;try{action=new URL(form.action||location.href,location.href)}catch{return null}
    if(action.protocol!=='https:'||action.origin!==location.origin||action.username||action.password)return null;
    const inputs=Array.from(form.elements).filter(e=>e instanceof HTMLInputElement);
    if(inputs.some(e=>autocomplete(e).includes('new-password')||autocomplete(e).includes('one-time-code')))return null;
    const pass=inputs.filter(e=>(passwords.has(e)||sensitive(e))&&editable(e));
    if(pass.length!==1)return null;
    const candidates=inputs.filter(e=>e!==pass[0]&&editable(e)&&['text','email','tel'].includes(e.type));
    const explicit=candidates.filter(e=>autocomplete(e).includes('username'));
    const users=explicit.length?explicit:candidates;
    if(users.length!==1)return null;
    return {password:pass[0],username:users[0]};
  };
  document.addEventListener('input',event=>{
    if(!event.isTrusted)return;
    const target=event.composedPath()[0];if(!(target instanceof HTMLInputElement)||!target.form)return;
    remember();const selected=fields(target.form);
    if(!selected||target!==selected.password)return;
    emit({kind:'edit',form:token(target.form),origin:location.origin});
  },true);
  document.addEventListener('submit',event=>{
    const form=event.target,selected=fields(form);
    if(!event.isTrusted||!selected)return;
    const password=selected.password.value,username=selected.username?.value||'';
    if(!password||password.length>65536||username.length>65536)return;
    const submitter=event.submitter;
    if(submitter&&(submitter.hasAttribute('formaction')||submitter.hasAttribute('formmethod')||submitter.hasAttribute('formtarget')))return;
    const id=token(form);
    // The fixed privacy marker runs before any credential-bearing event.
    globalThis.__xenon_mark_sensitive('protected');
    emit({kind:'submit',form:id,origin:location.origin,username,password});
  },true);
  new MutationObserver(remember).observe(document,{subtree:true,childList:true,attributes:true});
  remember();
})())XENONJS";
}
