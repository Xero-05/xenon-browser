#pragma once
namespace xenon {
// Fixed internal classifier. allowText comes from a revalidated native AX role
// or a rendered DOM Text identity, never caller parameters. No input lookup or
// page activation here.
inline constexpr const char* kUploadEntryKind=R"JS(function(allowText){
  if(!this.isConnected)return 'unsupported';
  const element=this.nodeType===3?this.parentElement:this;
  if(!element||element.nodeType!==1)return 'unsupported';
  let ancestor=element,depth=0;
  for(;ancestor&&depth<64;ancestor=ancestor.parentElement||ancestor.getRootNode().host,++depth){
    if(ancestor.disabled||ancestor.inert||ancestor.getAttribute('aria-disabled')==='true'||ancestor.matches(':disabled'))return 'unsupported';
  }
  if(ancestor)return 'unsupported';
  if(element===this&&element.tagName==='INPUT'&&element.type==='file')return element.webkitdirectory?'directory':'input';
  if(allowText&&(this.nodeType===3||this.nodeType===1))return 'entry';
  return element.tagName==='BUTTON'||element.tagName==='LABEL'||element.tagName==='A'||element.getAttribute('role')==='button'||(element.tagName==='INPUT'&&['button','submit'].includes(element.type))?'entry':'unsupported';
})JS";
}
