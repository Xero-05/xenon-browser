#pragma once
namespace xenon {
// Fixed native script only; model text is passed as a separate argument. Chrome
// date controls cannot accept Input.insertText. Validate their ISO value before
// touching the live field, then use the same native setter/event convention as
// select actions. Ordinary text retains the existing selection/input path.
inline constexpr const char* form_fill_prepare_script=R"JS(function(secret,text){
  if(!this.isConnected)return false;
  const tag=this.tagName.toLowerCase();
  if(!(tag==='input'||tag==='textarea'||this.isContentEditable)||this.disabled||this.readOnly)return false;
  const sensitive=this.type==='password'||/password|one-time-code/.test(this.autocomplete||'');
  if(sensitive&&!secret)return false;
  if(tag==='input'&&this.type==='date'){
    const probe=document.createElement('input');probe.type='date';probe.value=text;
    if(probe.value!==text)return false;
    this.focus();
    if(!this.isConnected||this.disabled||this.readOnly||this.type!=='date')return false;
    const set=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set;
    set.call(this,text);this.dispatchEvent(new Event('input',{bubbles:true}));
    if(this.isConnected)this.dispatchEvent(new Event('change',{bubbles:true}));
    return 'dispatched';
  }
  this.focus();if(this.select)this.select();else{const r=document.createRange();r.selectNodeContents(this);const s=getSelection();s.removeAllRanges();s.addRange(r)}
  return 'prepared';
})JS";
}
