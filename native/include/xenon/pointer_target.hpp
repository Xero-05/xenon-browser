#pragma once
namespace xenon {
// Fixed native script; callers supply fractions, never executable source.
inline constexpr const char* kPointerTarget=R"JS(function(fx,fy,fallback){
  return new Promise(resolve=>{
    let finished=false;
    const finish=value=>{if(!finished){finished=true;resolve(value)}};
    const initial=this.getBoundingClientRect();
    setTimeout(()=>finish({error:'render_unready'}),1500);
    requestAnimationFrame(()=>requestAnimationFrame(()=>{
      if(!this.isConnected){finish({error:'detached'});return}
      const r=this.getBoundingClientRect();
      if(['x','y','width','height'].some(key=>Math.abs(r[key]-initial[key])>.25)){finish({error:'layout_moving'});return}
      let opacity=1;
      for(let node=this;node;node=node.parentElement||node.getRootNode().host){
        const style=getComputedStyle(node);opacity*=Number(style.opacity);
        if(style.display==='none'||style.visibility==='hidden'||style.visibility==='collapse'||opacity<.5){finish({error:'not_visible'});return}
      }
      if(r.width<=0||r.height<=0){finish({error:'not_visible'});return}
      const left=Math.max(0,r.left),right=Math.min(innerWidth,r.right);
      const top=Math.max(0,r.top),bottom=Math.min(innerHeight,r.bottom);
      if(right-left<1||bottom-top<1){finish({error:'outside_viewport'});return}
      let outer=this,root=this.getRootNode();
      while(root.host){outer=root.host;root=outer.getRootNode()}
      const local=this.getRootNode();
      const samples=fallback?[[fx,fy],[.5,.5],[1-fx,1-fy],[fx,1-fy],[1-fx,fy],[.35,.35],[.65,.65],[.35,.65],[.65,.35]]:[[fx,fy]];
      for(const [sx,sy] of samples){
        const x=left+(right-left)*(right-left<8?.5:sx);
        const y=top+(bottom-top)*(bottom-top<8?.5:sy);
        if(!Number.isFinite(x)||!Number.isFinite(y)||x<=r.left||x>=r.right||y<=r.top||y>=r.bottom)continue;
        const hit=this.ownerDocument.elementFromPoint(x,y);
        if(hit!==outer&&!outer.contains(hit))continue;
        const h=local.elementFromPoint?local.elementFromPoint(x,y):hit;
        if(h!==this&&!this.contains(h))continue;
        finish({x,y,fx:(x-r.left)/r.width,fy:(y-r.top)/r.height,sampleFx:sx,sampleFy:sy});return;
      }
      finish({error:'occluded'});
    }));
  });
})JS";
}
