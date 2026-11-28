#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
namespace xenon {
// The guard samples fractions in getBoundingClientRect's screen-oriented box.
// A reflected CDP quad has reversed corner order, not reversed fractions.
inline std::pair<double,double> pointer_box_point(const std::array<double,8>& q,double fx,double fy) {
  if(q[0]==q[6]&&q[2]==q[4]&&q[1]==q[3]&&q[5]==q[7]) {
    const double left=std::min(q[0],q[2]),top=std::min(q[1],q[5]);
    return {left+std::abs(q[2]-q[0])*fx,top+std::abs(q[5]-q[1])*fy};
  }
  return {q[0]+(q[2]-q[0])*fx+(q[6]-q[0])*fy,q[1]+(q[3]-q[1])*fx+(q[7]-q[1])*fy};
}
// Fixed native script; callers supply fractions, never executable source.
inline constexpr const char* kPointerTarget=R"JS(function(fx,fy,fallback,allowFileOverlay,immediatePoint){
  return new Promise(resolve=>{
    let finished=false;
    const finish=value=>{if(!finished){finished=true;resolve(value)}};
    // StaticText references resolve to DOM Text nodes. Keep the hit area on
    // that exact rendered text, never the larger parent control/container.
    const text=this.nodeType===3,element=text?this.parentElement:this;
    if(!element||!this.isConnected){finish({error:'detached'});return}
    let range;
    if(text){range=this.ownerDocument.createRange();range.selectNodeContents(this)}
    const bounds=()=>{
      if(!text)return {rect:this.getBoundingClientRect()};
      const boxes=Array.from(range.getClientRects()).filter(box=>box.width>0&&box.height>0);
      return boxes.length===1?{rect:boxes[0]}:{error:boxes.length===0?'not_visible':'fragmented_text'};
    };
    const initial=bounds();
    if(initial.error){finish({error:initial.error});return}
    const verify=()=>{
      if(!this.isConnected||(text&&this.parentElement!==element)){finish({error:'detached'});return}
      let opacity=1;
      for(let node=element;node;node=node.parentElement||(node.getRootNode&&node.getRootNode().host)){
        const style=getComputedStyle(node);opacity*=Number(style.opacity);
        if(style.display==='none'||style.visibility==='hidden'||style.visibility==='collapse'||opacity<.5){finish({error:'not_visible'});return}
      }
      const current=bounds();
      if(current.error){finish({error:current.error});return}
      const r=current.rect;
      if(!immediatePoint&&['x','y','width','height'].some(key=>Math.abs(r[key]-initial.rect[key])>.25)){finish({error:'layout_moving'});return}
      if(r.width<=0||r.height<=0){finish({error:'not_visible'});return}
      const left=Math.max(0,r.left),right=Math.min(innerWidth,r.right);
      const top=Math.max(0,r.top),bottom=Math.min(innerHeight,r.bottom);
      if(right-left<1||bottom-top<1){finish({error:'outside_viewport'});return}
      let outer=element,root=element.getRootNode();
      while(root.host){outer=root.host;root=outer.getRootNode()}
      const local=element.getRootNode();
      // pointer-events: none on a visible text span passes pointer hits through
      // to its own clickable ancestor without widening the text's hit bounds.
      const passthrough=(from,to)=>{
        if(!to||from===to)return false;
        let cur=from,depth=0;
        while(cur&&cur!==to&&depth<64){
          if(getComputedStyle(cur).pointerEvents!=='none')return false;
          cur=cur.parentElement||(cur.getRootNode&&cur.getRootNode().host);++depth;
        }
        return cur===to;
      };
      // During xenon_upload only, a visually transparent same-document enabled
      // <input type="file"> directly over the verified text is a legitimate
      // custom upload overlay rather than an unrelated occluder.
      const fileOverlay=node=>{
        if(!allowFileOverlay||!node||node.nodeType!==1)return false;
        if(node.ownerDocument!==this.ownerDocument||node.tagName!=='INPUT'||node.type!=='file'||node.webkitdirectory)return false;
        let op=1,cur=node,depth=0;
        for(;cur&&depth<64;cur=cur.parentElement||(cur.getRootNode&&cur.getRootNode().host),++depth){
          if(cur.disabled||cur.inert||(cur.getAttribute&&cur.getAttribute('aria-disabled')==='true')||(cur.matches&&cur.matches(':disabled')))return false;
          const style=getComputedStyle(cur);
          if(style.display==='none'||style.visibility==='hidden'||style.visibility==='collapse')return false;
          op*=Number(style.opacity);
        }
        return !cur&&op===0;
      };
      const hitOk=(x,y)=>{
        const hit=this.ownerDocument.elementFromPoint(x,y);
        if(fileOverlay(hit))return true;
        const docPass=passthrough(element,hit);
        if(hit!==outer&&!outer.contains(hit)&&!docPass)return false;
        if(docPass)return true;
        const h=local.elementFromPoint?local.elementFromPoint(x,y):hit;
        if(fileOverlay(h))return true;
        return h===element||passthrough(element,h)||(!text&&element.contains(h));
      };
      if(immediatePoint){
        const x=immediatePoint.x,y=immediatePoint.y;
        if(!Number.isFinite(x)||!Number.isFinite(y)||x<=left||x>=right||y<=top||y>=bottom){finish({error:'layout_moving'});return}
        if(!hitOk(x,y)){finish({error:'occluded'});return}
        finish({x,y,fx:(x-r.left)/r.width,fy:(y-r.top)/r.height});return;
      }
      const samples=fallback?[[fx,fy],[.5,.5],[1-fx,1-fy],[fx,1-fy],[1-fx,fy],[.35,.35],[.65,.65],[.35,.65],[.65,.35]]:[[fx,fy]];
      for(const [sx,sy] of samples){
        const x=left+(right-left)*(right-left<8?.5:sx);
        const y=top+(bottom-top)*(bottom-top<8?.5:sy);
        if(!Number.isFinite(x)||!Number.isFinite(y)||x<=r.left||x>=r.right||y<=r.top||y>=r.bottom)continue;
        if(!hitOk(x,y))continue;
        finish({x,y,fx:(x-r.left)/r.width,fy:(y-r.top)/r.height,sampleFx:sx,sampleFy:sy});return;
      }
      finish({error:'occluded'});
    };
    if(immediatePoint){verify();return}
    setTimeout(()=>finish({error:'render_unready'}),1500);
    requestAnimationFrame(()=>requestAnimationFrame(verify));
  });
})JS";
}
