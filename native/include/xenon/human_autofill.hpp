#pragma once
#include <string>

namespace xenon {
// Native human UI only. Inspect must use returnByValue=false and retain its
// remote object in the isolated world. Its username snapshot is never native
// UI metadata or MCP data. Native code independently binds the origin, document,
// physical focus/confirmation, vault lock and transaction lifetime.
// Keep validation identical before offering and immediately before filling.
inline constexpr const char* human_autofill_validation_script = R"HUMANJS(
const inspect=(expected,focusedOnly)=>{
  const unavailable=reason=>({eligible:false,reason});
  if(window!==top||location.protocol!=='https:'||location.origin!==expected)return unavailable('origin');
  try{if(new URL(expected).origin!==expected)return unavailable('origin')}catch{return unavailable('origin')}
  const tokens=e=>(e.autocomplete||'').toLowerCase().trim().split(/\s+/).filter(Boolean);
  const visible=e=>{
    if(!e.isConnected||e.ownerDocument!==document||!e.getClientRects().length)return false;
    for(let p=e;p;p=p.parentElement){const s=getComputedStyle(p);if(p.hidden||p.inert||s.display==='none'||s.visibility==='hidden'||s.visibility==='collapse'||s.contentVisibility==='hidden'||Number(s.opacity)===0)return false}
    const r=e.getBoundingClientRect();return r.width>0&&r.height>0&&r.right>0&&r.bottom>0&&r.left<innerWidth&&r.top<innerHeight;
  };
  const actionable=e=>{
    if(!visible(e)||e.disabled||e.readOnly||e.matches(':disabled'))return false;
    const r=e.getBoundingClientRect(),x=(Math.max(0,r.left)+Math.min(innerWidth,r.right))/2,y=(Math.max(0,r.top)+Math.min(innerHeight,r.bottom))/2;
    const hit=document.elementFromPoint(x,y);return hit===e||e.contains(hit);
  };
  const inputs=Array.from(document.querySelectorAll('input')).filter(e=>e.type!=='hidden'&&visible(e));
  const forbidden=e=>{const a=tokens(e);return a.includes('one-time-code')||a.includes('new-password')};
  const userCandidate=e=>!forbidden(e)&&['text','email','tel'].includes(e.type)&&!tokens(e).includes('current-password');
  const passwords=inputs.filter(e=>e.type==='password'||e.type==='text'&&tokens(e).includes('current-password'));
  // As in browser password parsers, unique explicit semantics take precedence
  // over unrelated text fields. Never guess between two marked usernames.
  const usersFor=scope=>{const candidates=inputs.filter(e=>e.form===scope&&userCandidate(e)),explicit=candidates.filter(e=>tokens(e).includes('username'));return explicit.length?explicit:candidates};
  let form,user=null,password=null,phase;
  if(passwords.length>1)return unavailable('ambiguous');
  if(passwords.length===1){
    password=passwords[0];form=password.form;
    if(forbidden(password))return unavailable('unsupported');
    const users=usersFor(form);
    if(users.length>1)return unavailable('ambiguous');
    if(users.length===1){user=users[0];phase='credentials'}
    else{if(!tokens(password).includes('current-password'))return unavailable('unsupported');phase='password'}
  }else{
    const users=inputs.filter(e=>userCandidate(e)&&tokens(e).includes('username'));
    if(users.length!==1)return unavailable('ambiguous');
    user=users[0];form=user.form;phase='username';
  }
  if(inputs.some(e=>e.form===form&&forbidden(e)))return unavailable('unsupported');
  let action='',target='';
  if(form){
    if(!form.isConnected||form.ownerDocument!==document)return unavailable('form');
    let url;try{url=new URL(form.action||location.href,location.href)}catch{return unavailable('form')}
    if(url.protocol!=='https:'||url.origin!==expected||url.username||url.password||String(form.method).toLowerCase()!=='post')return unavailable('form');
    if(form.target&&form.target.toLowerCase()!=='_self')return unavailable('form');
    action=url.href;target=form.target||'';
  }else{
    // Unowned controls form one conservative synthetic group. Human fill is
    // permitted only for explicit login semantics; it never clicks or submits.
    if((user&&!tokens(user).includes('username'))||(password&&!tokens(password).includes('current-password')))return unavailable('unsupported');
  }
  if((user&&!actionable(user))||(password&&!actionable(password)))return unavailable('not_actionable');
  if(focusedOnly&&!((user&&document.activeElement===user)||(password&&document.activeElement===password)))return unavailable('focus');
  if(password&&password.value!=='')return unavailable('password_not_empty');
  return {eligible:true,form,user,password,origin:expected,document,phase,usernameValue:user?user.value:'',passwordEmpty:true,
    action,target,userType:user?user.type:'',userAutocomplete:user?user.autocomplete:'',
    passwordType:password?password.type:'',passwordAutocomplete:password?password.autocomplete:'',used:false};
};
)HUMANJS";

inline const std::string human_autofill_inspect_script =
std::string(R"HUMANJS(function(expectedOrigin,focusedOnly){
)HUMANJS") + human_autofill_validation_script +
R"HUMANJS(try{return inspect(expectedOrigin,!!focusedOnly)}catch{return {eligible:false,reason:'form'}}
})HUMANJS";

inline const std::string human_autofill_fill_script =
std::string(R"HUMANJS(function(username,password,expectedOrigin){
)HUMANJS") + human_autofill_validation_script +
R"HUMANJS(
  const unavailable=reason=>({filled:false,error:reason,submitted:false});
  if(!this||this.eligible!==true||this.used)return unavailable('stale_offer');
  this.used=true;
  if(typeof username!=='string'||typeof password!=='string'||username.length>65536||password.length>65536)return unavailable('credential');
  try{
    const now=inspect(expectedOrigin,false);
    if(now.eligible!==true)return unavailable(now.reason);
    for(const key of ['document','form','user','password','origin','phase','action','target','userType','userAutocomplete','passwordType','passwordAutocomplete'])
      if(now[key]!==this[key])return unavailable('changed_form');
    if(now.usernameValue!==this.usernameValue||!now.passwordEmpty)return unavailable('changed_values');
    if(this.user&&this.usernameValue!==''&&this.usernameValue!==username)return unavailable('username_conflict');
    if(this.password&&password==='')return unavailable('credential');
    const setter=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set;
    // No page handlers run between validation and either native value setter.
    // Both destinations are the original bound nodes; never query replacements.
    if(this.user)setter.call(this.user,username);
    if(this.password)setter.call(this.password,password);
    for(const field of [this.user,this.password])if(field){field.dispatchEvent(new Event('input',{bubbles:true}));field.dispatchEvent(new Event('change',{bubbles:true}))}
    const retained=(field,value)=>!field||(field.isConnected&&field.ownerDocument===this.document&&field.form===this.form&&field.value===value);
    if((this.form&&(!this.form.isConnected||this.form.ownerDocument!==this.document))||!retained(this.user,username)||!retained(this.password,password))return unavailable('changed_after_fill');
    return {filled:true,phase:this.phase,submitted:false};
  }catch{return unavailable('form')}
})HUMANJS";

} // namespace xenon
