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
inline constexpr const char* kPointerTarget=R"JS(function(fx,fy,fallback){
  return new Promise(resolve=>{
    let finished=false;
    const finish=value=>{if(!finished){finished=true;resolve(value)}};
    // StaticText references resolve to DOM Text nodes. Keep the hit area on
    // that exact rendered text, never the larger parent control/container.
    const text=this.nodeType===3,element=text?this.parentElement:this;
    if(!element){finish({error:'detached'});return}
    let range;
    if(text){range=this.ownerDocument.createRange();range.selectNodeContents(this)}
    const bounds=()=>{
      if(!text)return this.getBoundingClientRect();
      const boxes=Array.from(range.getClientRects()).filter(box=>box.width>0&&box.height>0);
      return boxes.length===1?boxes[0]:null;
    };
    const initial=bounds();
    if(!initial){finish({error:'fragmented_text'});return}
    setTimeout(()=>finish({error:'render_unready'}),1500);
    requestAnimationFrame(()=>requestAnimationFrame(()=>{
      if(!this.isConnected||(text&&this.parentElement!==element)){finish({error:'detached'});return}
      const r=bounds();
      if(!r||['x','y','width','height'].some(key=>Math.abs(r[key]-initial[key])>.25)){finish({error:'layout_moving'});return}
      let opacity=1;
      for(let node=element;node;node=node.parentElement||node.getRootNode().host){
        const style=getComputedStyle(node);opacity*=Number(style.opacity);
        if(style.display==='none'||style.visibility==='hidden'||style.visibility==='collapse'||opacity<.5){finish({error:'not_visible'});return}
      }
      if(r.width<=0||r.height<=0){finish({error:'not_visible'});return}
      const left=Math.max(0,r.left),right=Math.min(innerWidth,r.right);
      const top=Math.max(0,r.top),bottom=Math.min(innerHeight,r.bottom);
      if(right-left<1||bottom-top<1){finish({error:'outside_viewport'});return}
      let outer=element,root=element.getRootNode();
      while(root.host){outer=root.host;root=outer.getRootNode()}
      const local=element.getRootNode();
      const samples=fallback?[[fx,fy],[.5,.5],[1-fx,1-fy],[fx,1-fy],[1-fx,fy],[.35,.35],[.65,.65],[.35,.65],[.65,.35]]:[[fx,fy]];
      for(const [sx,sy] of samples){
        const x=left+(right-left)*(right-left<8?.5:sx);
        const y=top+(bottom-top)*(bottom-top<8?.5:sy);
        if(!Number.isFinite(x)||!Number.isFinite(y)||x<=r.left||x>=r.right||y<=r.top||y>=r.bottom)continue;
        const hit=this.ownerDocument.elementFromPoint(x,y);
        if(hit!==outer&&!outer.contains(hit))continue;
        const h=local.elementFromPoint?local.elementFromPoint(x,y):hit;
        if(h!==element&&(text||!element.contains(h)))continue;
        finish({x,y,fx:(x-r.left)/r.width,fy:(y-r.top)/r.height,sampleFx:sx,sampleFy:sy});return;
      }
      finish({error:'occluded'});
    }));
  });
})JS";
}
