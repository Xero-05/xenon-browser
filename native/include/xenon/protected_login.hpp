#pragma once

namespace xenon {
// Fixed isolated-world function. Only bounded outcomes return to the broker;
// credentials, form data and page strings are never part of its result.
inline constexpr const char* protected_login_script = R"LOGINJS(function(username,pass,expected,submit,usernameDone){
  if(window!==top||location.protocol!=='https:'||location.origin!==expected)return {error:'origin_changed'};
  const visible=e=>{
    if(!e.isConnected||e.ownerDocument!==document||!e.getClientRects().length||e.disabled||e.matches(':disabled'))return false;
    for(let p=e;p;p=p.parentElement){const s=getComputedStyle(p);if(p.hidden||p.inert||s.display==='none'||s.visibility==='hidden'||s.visibility==='collapse'||Number(s.opacity)===0)return false}
    const r=e.getBoundingClientRect();return r.width>0&&r.height>0&&r.right>0&&r.bottom>0&&r.left<innerWidth&&r.top<innerHeight;
  };
  const actionable=e=>{if(!visible(e))return false;const r=e.getBoundingClientRect(),x=(Math.max(0,r.left)+Math.min(innerWidth,r.right))/2,y=(Math.max(0,r.top)+Math.min(innerHeight,r.bottom))/2;const hit=document.elementFromPoint(x,y);return hit===e||e.contains(hit)};
  const inputs=Array.from(document.querySelectorAll('input'));
  const passwords=inputs.filter(e=>e.type==='password'&&visible(e));
  if(passwords.length>1)return {error:'ambiguous_password_form'};
  const userCandidate=e=>visible(e)&&['text','email'].includes(e.type);
  let form,password,user;
  if(passwords.length===1){
    password=passwords[0];form=password.form;if(!form)return {error:'form_required'};
    const users=inputs.filter(e=>e.form===form&&userCandidate(e));
    if(users.length>1||(!usernameDone&&users.length!==1))return {error:'ambiguous_username_form'};user=users[0];
  }else{
    if(usernameDone)return {waiting:true};
    const users=inputs.filter(userCandidate);
    if(users.length!==1||!users[0].form)return {error:'ambiguous_username_form'};user=users[0];form=user.form;
  }
  if([user,password].some(e=>e&&e.readOnly))return {error:'readonly_form'};
  if(inputs.some(e=>e.form===form&&visible(e)&&/(?:^|\s)(?:one-time-code|new-password)(?:\s|$)/.test((e.autocomplete||'').toLowerCase())))return {error:'unsupported_credentials'};
  if([user,password].some(e=>e&&!actionable(e)))return {error:'not_actionable'};
  // requestSubmit() without a submitter omits the login button's name/value
  // (for example _eventId_proceed). Use the single visible native submit control.
  // Never guess between multiple actions or silently fall back to an unsafe one.
  const submitters=Array.from(form.elements).filter(e=>((e.tagName==='BUTTON'&&e.type==='submit')||(e.tagName==='INPUT'&&e.type==='submit'))&&visible(e));
  if(submit&&submitters.length>1)return {error:'ambiguous_submitter'};
  const button=submit?submitters[0]:null;
  if(button&&!actionable(button))return {error:'not_actionable'};
  const destination=()=>{
    const action=new URL(button&&button.hasAttribute('formaction')?button.formAction:form.action||location.href,location.href);
    const method=button&&button.hasAttribute('formmethod')?button.formMethod:form.method;
    const target=button&&button.hasAttribute('formtarget')?button.formTarget:form.target;
    if(action.protocol!=='https:'||action.origin!==expected||action.username||action.password||String(method).toLowerCase()!=='post'||(target&&target.toLowerCase()!=='_self'))return null;
    return action.href;
  };
  // The base form remains constrained even when a submitter overrides it.
  const base=new URL(form.action||location.href,location.href);
  if(base.protocol!=='https:'||base.origin!==expected||base.username||base.password||String(form.method).toLowerCase()!=='post'||(form.target&&form.target.toLowerCase()!=='_self'))return {error:'unsupported_form'};
  const action=destination();if(!action)return {error:'unsupported_submitter'};
  const connected=e=>!e||(e.isConnected&&e.ownerDocument===document&&e.form===form);
  const set=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set;
  if(user)set.call(user,username);if(password)set.call(password,pass);
  for(const e of [user,password])if(e){e.dispatchEvent(new Event('input',{bubbles:true}));e.dispatchEvent(new Event('change',{bubbles:true}))}
  if(!form.isConnected||form.ownerDocument!==document||!connected(user)||!connected(password)||!connected(button)||
     (user&&user.value!==username)||(password&&password.value!==pass)||destination()!==action||
     [user,password,button].some(e=>e&&!actionable(e)))return {error:'changed_after_fill'};
  const submitted=!!submit&&(form.noValidate||(button&&button.formNoValidate)||HTMLFormElement.prototype.checkValidity.call(form));
  if(submitted)HTMLFormElement.prototype.requestSubmit.call(form,button||undefined);
  return password?{filled:true,submitted}:{usernameFilled:true,submitted};
})LOGINJS";
} // namespace xenon
