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
    if(!attempted(form,context,epoch,now))return false;
    clear_edit();return true;
  }
  // A script-driven sign-in (Enter or a button, with no form submission) has
  // the same provenance requirement but does not consume it: a failed attempt
  // leaves the form in place for another try.
  bool attempted(const std::string& form, const Context& context,
                 uint64_t epoch, Clock::time_point now) const {
    return !edit_form_.empty()&&edit_form_==form&&edit_context_==context&&edit_epoch_==epoch&&
           now>=edit_at_&&now-edit_at_<=std::chrono::minutes(5);
  }
  void clear_edit() { physical_input_.reset();edit_form_.clear();edit_context_={};edit_epoch_=0;edit_at_={}; }
  void context_destroyed(const Context& context) { if(edit_context_==context)clear_edit(); }
  void contexts_cleared(const std::string& session) { if(edit_context_.first==session)clear_edit(); }
  void proposed(std::string id, std::string origin) { candidate_=std::move(id);origin_=std::move(origin);notified_=false;deferred_form_.clear(); }
  // A script-driven candidate is offered only once its page navigates or
  // removes the submitted password field; until then it can be superseded.
  void proposed_deferred(std::string id, std::string origin, std::string form, const Context& context, uint64_t epoch) {
    proposed(std::move(id),std::move(origin));deferred_form_=std::move(form);deferred_context_=context;deferred_epoch_=epoch;
  }
  bool deferred() const { return !candidate_.empty()&&!deferred_form_.empty(); }
  // The submitted password field left the same document. Returns true when a
  // deferred candidate became ready to offer.
  bool field_gone(const std::string& form, const Context& context, uint64_t epoch) {
    if(!deferred()||form!=deferred_form_||context!=deferred_context_||epoch!=deferred_epoch_)return false;
    deferred_form_.clear();return true;
  }
  // Same-document navigation after a script-driven attempt.
  bool navigated_within_document() { if(!deferred())return false;deferred_form_.clear();return true; }
  std::string discard_candidate() { auto id=std::exchange(candidate_,{});origin_.clear();notified_=false;deferred_form_.clear();return id; }
  // HTTPS navigation keeps (and readies) a candidate; anything else cancels it.
  std::string navigated(bool verified_https) { clear_edit();if(!verified_https)return discard_candidate();deferred_form_.clear();return {}; }
  const std::string& candidate() const { return candidate_; }
  const std::string& origin() const { return origin_; }
  bool notified() const { return notified_; }
  void mark_notified() { notified_=true; }
  // Username-first sign-in: an account typed with physical input on a step
  // without a password field, for a later password step on the same origin
  // that does not repeat it. Memory only; never persisted or exposed.
  void account_entered(std::string origin, std::string account, Clock::time_point now) {
    if(!physical_input_||now<*physical_input_||now-*physical_input_>std::chrono::seconds(2)||origin.empty()||account.empty()||account.size()>1024)return;
    account_origin_=std::move(origin);account_=std::move(account);account_at_=now;
  }
  std::string account(const std::string& origin, Clock::time_point now) const {
    return !account_.empty()&&account_origin_==origin&&now>=account_at_&&now-account_at_<=std::chrono::minutes(10)?account_:std::string{};
  }
  void forget_account() { account_.clear();account_origin_.clear();account_at_={}; }
 private:
  std::optional<Clock::time_point> physical_input_;
  Clock::time_point edit_at_{},account_at_{};
  std::string edit_form_,candidate_,origin_,deferred_form_,account_origin_,account_;
  Context edit_context_,deferred_context_;
  uint64_t edit_epoch_{},deferred_epoch_{};
  bool notified_=false;
};

// Installed in the browser-owned isolated world, never in the website's world.
// The native receiver independently verifies physical-input provenance, frame,
// origin, lock state and pending submission identity. No MCP interface can call it.
inline constexpr const char* login_capture_script = R"XENONJS((()=>{
  if(globalThis.__xenonLoginCaptureInstalled || window!==top || location.protocol!=='https:')return;
  globalThis.__xenonLoginCaptureInstalled=true;
  const tokens=new WeakMap(),passwords=new WeakSet(),submitted=new WeakSet();
  const emit=value=>globalThis.__xenon_login_capture(JSON.stringify(value));
  const visible=e=>e.isConnected&&e.getClientRects().length>0&&getComputedStyle(e).visibility==='visible'&&getComputedStyle(e).display!=='none';
  const editable=e=>visible(e)&&!e.disabled&&!e.readOnly;
  const autocomplete=e=>(e.autocomplete||'').toLowerCase().split(/\s+/);
  const sensitive=e=>e instanceof HTMLInputElement&&(e.type==='password'||autocomplete(e).includes('current-password'));
  const userLike=e=>['text','email','tel'].includes(e.type);
  const remember=()=>{for(const e of document.querySelectorAll('input'))if(sensitive(e))passwords.add(e)};
  // A login is scoped to its form, or to the document's controls outside any
  // form, as on script-driven sign-in pages.
  const scopeOf=e=>e.form||document;
  const inputsOf=scope=>scope===document?Array.from(document.querySelectorAll('input')).filter(e=>!e.form):Array.from(scope.elements).filter(e=>e instanceof HTMLInputElement);
  const token=scope=>{if(!tokens.has(scope))tokens.set(scope,crypto.randomUUID());return tokens.get(scope)};
  const fields=scope=>{
    if(scope!==document&&!(scope instanceof HTMLFormElement&&scope.isConnected))return null;
    const inputs=inputsOf(scope);
    if(inputs.some(e=>autocomplete(e).includes('new-password')||autocomplete(e).includes('one-time-code')))return null;
    const pass=inputs.filter(e=>(passwords.has(e)||sensitive(e))&&editable(e));
    if(pass.length!==1)return null;
    const candidates=inputs.filter(e=>e!==pass[0]&&editable(e)&&userLike(e));
    const explicit=candidates.filter(e=>autocomplete(e).includes('username'));
    // Outside a form, only explicitly marked or email fields name the account.
    const users=explicit.length?explicit:scope===document?candidates.filter(e=>e.type==='email'):candidates;
    if(users.length>1)return null;
    if(users.length===1)return {password:pass[0],username:users[0],repeated:''};
    // Multi-step sign-in may repeat the account in a non-editable field.
    const repeated=[...new Set(inputs.filter(e=>e!==pass[0]&&!editable(e)&&e.type!=='hidden'&&e.value&&(autocomplete(e).includes('username')||e.type==='email')).map(e=>e.value))];
    return {password:pass[0],username:null,repeated:repeated.length===1?repeated[0]:''};
  };
  // Values are read when the person submits; a page may clear them afterward.
  const read=scope=>{
    const selected=fields(scope);if(!selected)return null;
    const password=selected.password.value,username=selected.username?selected.username.value:selected.repeated;
    if(!password||password.length>65536||username.length>65536)return null;
    return {field:selected.password,password,username,missing:!selected.username&&!selected.repeated};
  };
  const send=(scope,values,deferred)=>{
    // The fixed privacy marker runs before any credential-bearing event.
    globalThis.__xenon_mark_sensitive('protected');
    const message={kind:'submit',form:token(scope),origin:location.origin,username:values.username,password:values.password};
    if(values.missing)message.usernameMissing=true;
    if(deferred)message.deferred=true;
    emit(message);
    if(deferred)watch(scope,values.field);
  };
  // A deferred attempt is offered once the page navigates (native) or the
  // submitted password field leaves the page. A still-visible form stays quiet.
  const watch=(scope,field)=>{
    const form=token(scope);let checks=0;
    const check=()=>{if(!field.isConnected||!visible(field)){emit({kind:'gone',form,origin:location.origin});return}if(++checks<80)setTimeout(check,250)};
    setTimeout(check,250);
  };
  // Script-driven sign-in: Enter or a button press, without a form submission
  // in the same turn. A conventional submission takes precedence.
  const attempt=scope=>{const values=read(scope);if(!values)return;setTimeout(()=>{if(!submitted.has(scope))send(scope,values,true)},0)};
  // Username-first sign-in: report an account typed on a step without a
  // password field. Native code keeps it for this origin and tab only.
  const account=target=>{
    if(!(target instanceof HTMLInputElement)||!editable(target)||!userLike(target)||!(autocomplete(target).includes('username')||target.type==='email'))return;
    if(inputsOf(scopeOf(target)).some(e=>(passwords.has(e)||sensitive(e))&&visible(e)))return;
    const value=target.value.trim();if(value&&value.length<=1024)emit({kind:'username',origin:location.origin,username:value});
  };
  const conventional=(form,submitter)=>{
    if(String(form.method).toLowerCase()!=='post'||form.target&&form.target!=='_self')return false;
    if(submitter&&(submitter.hasAttribute('formaction')||submitter.hasAttribute('formmethod')||submitter.hasAttribute('formtarget')))return false;
    let action;try{action=new URL(form.action||location.href,location.href)}catch{return false}
    return action.protocol==='https:'&&action.origin===location.origin&&!action.username&&!action.password;
  };
  document.addEventListener('input',event=>{
    if(!event.isTrusted)return;
    const target=event.composedPath()[0];if(!(target instanceof HTMLInputElement))return;
    remember();const scope=scopeOf(target),selected=fields(scope);
    if(!selected||target!==selected.password)return;
    emit({kind:'edit',form:token(scope),origin:location.origin});
  },true);
  document.addEventListener('change',event=>{if(event.isTrusted){remember();account(event.composedPath()[0])}},true);
  document.addEventListener('keydown',event=>{
    if(!event.isTrusted||event.key!=='Enter'||event.isComposing)return;
    const target=event.composedPath()[0];if(!(target instanceof HTMLInputElement))return;
    remember();const scope=scopeOf(target);
    if(fields(scope))attempt(scope);else account(target);
  },true);
  const control='button,input[type=submit],input[type=button],input[type=image],[role=button]';
  // Controls that reveal, cancel or leave a sign-in are not attempts.
  const auxiliary=/forgot|reset|show|hide|cancel|back|another|help|create|sign up|register/i;
  document.addEventListener('click',event=>{
    if(!event.isTrusted)return;
    const pressed=event.composedPath().find(e=>e instanceof Element&&e.matches(control));
    if(!pressed||pressed.disabled||['aria-pressed','aria-expanded','aria-haspopup'].some(name=>pressed.hasAttribute(name)))return;
    if(auxiliary.test(`${pressed.textContent||''} ${pressed.value||''} ${pressed.getAttribute('aria-label')||''}`))return;
    remember();const scope=('form' in pressed?pressed.form:pressed.closest('form'))||document;
    if(fields(scope)){attempt(scope);return}
    const accounts=inputsOf(scope).filter(e=>editable(e)&&userLike(e)&&(autocomplete(e).includes('username')||e.type==='email'));
    if(accounts.length===1)account(accounts[0]);
  },true);
  document.addEventListener('submit',event=>{
    const form=event.target;
    if(!event.isTrusted||!(form instanceof HTMLFormElement))return;
    submitted.add(form);setTimeout(()=>submitted.delete(form),0);
    const values=read(form);if(!values)return;
    if(conventional(form,event.submitter)){send(form,values,false);return}
    // A page that cancels the browser's own submission sends the credentials
    // from script; otherwise this form is not a supported sign-in.
    setTimeout(()=>{if(event.defaultPrevented)send(form,values,true)},0);
  },true);
  new MutationObserver(remember).observe(document,{subtree:true,childList:true,attributes:true});
  remember();
})())XENONJS";
}
