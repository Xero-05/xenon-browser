#pragma once

#include <cstdint>
#include <string_view>

namespace xenon {
// Validates text for screenshot-grounded focused-element text entry (`page.type`).
// Requires 1..65536 bytes of valid UTF-8 and permits printable text plus tab/LF/CR
// while rejecting C0/C1 controls and invisible bidi/tag formatting characters
// (aligned with the adapter's text-safety filter).
inline bool valid_type_text(std::string_view text) {
  if (text.empty() || text.size() > 65536) return false;
  for (size_t i = 0; i < text.size();) {
    const auto lead = static_cast<unsigned char>(text[i]);
    const size_t len = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xe ? 3 : (lead >> 3) == 0x1e ? 4 : 0;
    if (!len || i + len > text.size()) return false;
    uint32_t cp = len == 1 ? lead : len == 2 ? lead & 0x1fU : len == 3 ? lead & 0x0fU : lead & 0x07U;
    for (size_t j = 1; j < len; ++j) {
      const auto byte = static_cast<unsigned char>(text[i + j]);
      if ((byte & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (byte & 0x3fU);
    }
    if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10ffff)) ||
        (cp >= 0xd800 && cp <= 0xdfff)) return false;
    if ((cp < 0x20 && cp != 0x09 && cp != 0x0a && cp != 0x0d) || (cp >= 0x7f && cp <= 0x9f) ||
        cp == 0x061c || cp == 0x180e || cp == 0x200b || cp == 0x200e || cp == 0x200f ||
        (cp >= 0x202a && cp <= 0x202e) || (cp >= 0x2060 && cp <= 0x206f) || cp == 0xfeff ||
        (cp >= 0xe0000 && cp <= 0xe007f)) return false;
    i += len;
  }
  return true;
}

// Fixed isolated-world script verifying the currently focused element before
// screenshot-grounded text insertion. Never reads values, focuses elements, or
// accepts caller-supplied JavaScript. Refuses credential/masked fields,
// embedded frames, disabled/readonly/inert controls, hidden/low-opacity/offscreen
// targets, and occluded elements.
inline constexpr const char* focused_type_check_script = R"JS(function(){
  let el=document.activeElement;
  let hops=0;
  while(el&&el.shadowRoot&&el.shadowRoot.activeElement&&hops++<32){
    el=el.shadowRoot.activeElement;
  }
  if(!el||el===document.body||el===document.documentElement||!el.isConnected){
    return {ok:false,reason:'no_focused_editable'};
  }
  const tag=(el.tagName||'').toUpperCase();
  if(tag==='IFRAME'||tag==='FRAME'||tag==='OBJECT'||tag==='EMBED'){
    return {ok:false,reason:'embedded_frame'};
  }
  const allowedInputTypes=new Set(['text','search','email','url','tel','number']);
  let editable=false;
  if(tag==='TEXTAREA'){
    editable=true;
  }else if(tag==='INPUT'){
    const type=(el.type||'text').toLowerCase();
    if(type==='password')return {ok:false,reason:'protected_field'};
    editable=allowedInputTypes.has(type);
  }else if(el.isContentEditable){
    editable=true;
  }
  if(!editable||el.disabled||el.readOnly||(el.closest&&el.closest('[inert]'))){
    return {ok:false,reason:'not_editable'};
  }
  if(/password|one-time-code/i.test(el.autocomplete||'')){
    return {ok:false,reason:'protected_field'};
  }
  const selfStyle=getComputedStyle(el);
  const textSecurity=(selfStyle.webkitTextSecurity||(selfStyle.getPropertyValue?selfStyle.getPropertyValue('-webkit-text-security'):'')||'none').toLowerCase();
  if(textSecurity&&textSecurity!=='none'){
    return {ok:false,reason:'protected_field'};
  }
  let opacity=1;
  let depth=0;
  for(let node=el;node&&depth++<128;node=node.parentElement||(node.getRootNode&&node.getRootNode().host)||null){
    const style=getComputedStyle(node);
    const op=Number(style.opacity);
    if(!Number.isFinite(op))return {ok:false,reason:'not_visible'};
    opacity*=op;
    if(style.display==='none'||style.visibility==='hidden'||style.visibility==='collapse'||style.contentVisibility==='hidden'||opacity<0.5){
      return {ok:false,reason:'not_visible'};
    }
  }
  if(depth>128)return {ok:false,reason:'not_visible'};
  const r=el.getBoundingClientRect();
  if(!r||!Number.isFinite(r.left)||!Number.isFinite(r.top)||r.width<4||r.height<4){
    return {ok:false,reason:'not_visible'};
  }
  const vv=typeof visualViewport!=='undefined'?visualViewport:null;
  const viewLeft=vv?vv.offsetLeft:0;
  const viewTop=vv?vv.offsetTop:0;
  const viewRight=vv?vv.offsetLeft+vv.width:innerWidth;
  const viewBottom=vv?vv.offsetTop+vv.height:innerHeight;
  const left=Math.max(viewLeft,r.left);
  const right=Math.min(viewRight,r.right);
  const top=Math.max(viewTop,r.top);
  const bottom=Math.min(viewBottom,r.bottom);
  if(right-left<4||bottom-top<4){
    return {ok:false,reason:'outside_viewport'};
  }
  let outer=el;
  let root=el.getRootNode?el.getRootNode():document;
  while(root&&root.host){
    outer=root.host;
    root=outer.getRootNode?outer.getRootNode():document;
  }
  const local=el.getRootNode?el.getRootNode():document;
  const samples=[[0.5,0.5],[0.25,0.25],[0.75,0.75],[0.25,0.75],[0.75,0.25],[0.5,0.25],[0.5,0.75]];
  for(const [sx,sy] of samples){
    const x=left+(right-left)*sx;
    const y=top+(bottom-top)*sy;
    if(!Number.isFinite(x)||!Number.isFinite(y))continue;
    const hit=document.elementFromPoint(x,y);
    if(!hit||(hit!==outer&&!outer.contains(hit)))continue;
    const localHit=local.elementFromPoint?local.elementFromPoint(x,y):hit;
    if(!localHit||(localHit!==el&&!el.contains(localHit)))continue;
    return {ok:true,vx:x-viewLeft,vy:y-viewTop};
  }
  return {ok:false,reason:'occluded'};
})JS";
}
