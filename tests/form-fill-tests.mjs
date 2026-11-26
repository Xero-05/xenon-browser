// Exact production preparation script in a synthetic DOM. Live benchmark tests
// separately establish Chrome's actual date sanitization and website outcome.
import assert from 'node:assert/strict';
import test from 'node:test';
import vm from 'node:vm';
import { readFile } from 'node:fs/promises';
const header=await readFile(new URL('../native/include/xenon/form_fill.hpp',import.meta.url),'utf8');
const source=header.match(/R"JS\(([\s\S]*?)\)JS"/u)?.[1];assert(source);
function fixture(type='date'){
  const events=[],writes=[];
  class Input {
    constructor(){this.type=type;this.tagName='INPUT';this.isConnected=true;this.disabled=false;this.readOnly=false;this.autocomplete='';this.current='2026-10-03';}
    get value(){return this.current;}
    set value(value){writes.push(value);this.current=value;}
    focus(){this.focused=true;this.onfocus?.();}
    select(){this.selected=true;}
    dispatchEvent(event){events.push(event.type);this.oninput?.(event);}
  }
  const document={createElement(){return {type:'',current:'',get value(){return this.current;},set value(value){const valid=/^\d{4}-\d{2}-\d{2}$/u.test(value)&&!Number.isNaN(Date.parse(value+'T00:00:00Z'))&&new Date(value+'T00:00:00Z').toISOString().startsWith(value);this.current=valid?value:'';}};}};
  const run=vm.runInNewContext('('+source+')',{document,HTMLInputElement:Input,Event:class {constructor(type){this.type=type;}}});
  const input=new Input();return {input,events,writes,fill:(value,secret=false)=>run.call(input,secret,value)};
}
test('date preparation dispatches ISO value and input/change without text insertion',()=>{
  const f=fixture();assert.equal(f.fill('2027-01-04'),'dispatched');assert.equal(f.input.value,'2027-01-04');assert.deepEqual(f.events,['input','change']);assert.deepEqual(f.writes,['2027-01-04']);
});
test('invalid or noncanonical dates preserve the existing value and focus',()=>{
  for(const value of ['01/04/2027','2027-02-29','2027-13-01','arbitrary text']){const f=fixture();assert.equal(f.fill(value),false);assert.equal(f.input.value,'2026-10-03');assert.equal(f.input.focused,undefined);assert.deepEqual(f.events,[]);assert.deepEqual(f.writes,[]);}
});
test('disconnected, disabled, read-only and credential fields reject before focus',()=>{
  for(const change of [input=>input.isConnected=false,input=>input.disabled=true,input=>input.readOnly=true,input=>input.type='password',input=>input.autocomplete='one-time-code']){const f=fixture();change(f.input);assert.equal(f.fill('2027-01-04'),false);assert.deepEqual(f.writes,[]);assert.equal(f.input.focused,undefined);}
});
test('focus handler changing the bound date field prevents the write',()=>{
  for(const change of [input=>input.isConnected=false,input=>input.disabled=true,input=>input.readOnly=true,input=>input.type='password']){const f=fixture();f.input.onfocus=()=>change(f.input);assert.equal(f.fill('2027-01-04'),false);assert.deepEqual(f.writes,[]);assert.deepEqual(f.events,[]);}
});
test('detachment during input prevents a change event on the detached field',()=>{
  const f=fixture();f.input.oninput=()=>{f.input.isConnected=false;};assert.equal(f.fill('2027-01-04'),'dispatched');assert.deepEqual(f.events,['input']);
});
test('ordinary text still prepares selection for native Input.insertText',()=>{
  const f=fixture('text');assert.equal(f.fill('ordinary text'),'prepared');assert.equal(f.input.selected,true);assert.deepEqual(f.writes,[]);assert.deepEqual(f.events,[]);
});

const typeHeader=await readFile(new URL('../native/include/xenon/focused_type.hpp',import.meta.url),'utf8');
const typeSource=typeHeader.match(/R"JS\(([\s\S]*?)\)JS"/u)?.[1];assert(typeSource);
function typeFixture(overrides={}){
  const body={tagName:'BODY',isConnected:true};
  const docEl={tagName:'HTML',isConnected:true};
  const el={
    tagName:'TEXTAREA',type:'textarea',isConnected:true,disabled:false,readOnly:false,
    isContentEditable:false,autocomplete:'',parentElement:null,shadowRoot:null,
    rect:{left:20,top:100,right:420,bottom:220,width:400,height:120},
    style:{display:'block',visibility:'visible',opacity:'1',contentVisibility:'visible',webkitTextSecurity:'none'},
    inert:false,
    getBoundingClientRect(){return this.rect;},
    getRootNode(){return this._root??document;},
    closest(sel){return sel==='[inert]'&&this.inert?this:null;},
    contains(other){return other===this;},
    ...overrides,
  };
  let hitTarget=overrides.hitTarget!==undefined?overrides.hitTarget:el;
  const document={
    body,documentElement:docEl,
    activeElement:overrides.activeElement!==undefined?overrides.activeElement:el,
    elementFromPoint(){return hitTarget;},
  };
  const getComputedStyle=node=>node.style??{display:'block',visibility:'visible',opacity:'1',contentVisibility:'visible',webkitTextSecurity:'none'};
  const run=vm.runInNewContext('('+typeSource+')',{
    document,getComputedStyle,innerWidth:800,innerHeight:600,
    visualViewport:overrides.visualViewport??{offsetLeft:0,offsetTop:0,width:800,height:600},
  });
  return {el,document,run:()=>({...run()}),setHit(t){hitTarget=t;}};
}
test('focused_type_check_script accepts visible enabled textarea, text inputs, contenteditable and open shadow textarea',()=>{
  assert.deepEqual(typeFixture().run(),{ok:true,vx:220,vy:160});
  for(const type of ['text','search','email','url','tel','number']){
    const r=typeFixture({tagName:'INPUT',type}).run();
    assert.equal(r.ok,true,`input[type=${type}] should be accepted`);
  }
  assert.equal(typeFixture({tagName:'DIV',isContentEditable:true}).run().ok,true);
  // Open shadow root activeElement
  const inner={
    tagName:'TEXTAREA',isConnected:true,disabled:false,readOnly:false,isContentEditable:false,autocomplete:'',
    parentElement:null,shadowRoot:null,
    rect:{left:40,top:80,right:340,bottom:180,width:300,height:100},
    style:{display:'block',visibility:'visible',opacity:'1',contentVisibility:'visible',webkitTextSecurity:'none'},
    getBoundingClientRect(){return this.rect;},
    closest(){return null;},
    contains(other){return other===inner;},
  };
  const host={
    tagName:'DIV',isConnected:true,parentElement:null,
    style:{display:'block',visibility:'visible',opacity:'1',contentVisibility:'visible'},
    shadowRoot:{activeElement:inner,elementFromPoint:()=>inner},
    contains(other){return other===host;},
  };
  inner.getRootNode=()=>({host,elementFromPoint:()=>inner});
  const shadowRes=typeFixture({activeElement:host,hitTarget:host}).run();
  assert.deepEqual(shadowRes,{ok:true,vx:190,vy:130});
});
test('focused_type_check_script rejects credential, non-editable, embedded, hidden, offscreen and occluded focus',()=>{
  const cases=[
    ['password input',{tagName:'INPUT',type:'password'},'protected_field'],
    ['current-password autocomplete',{tagName:'INPUT',type:'text',autocomplete:'current-password'},'protected_field'],
    ['one-time-code autocomplete',{tagName:'INPUT',type:'text',autocomplete:'one-time-code'},'protected_field'],
    ['css text-security mask',{tagName:'INPUT',type:'text',style:{display:'block',visibility:'visible',opacity:'1',contentVisibility:'visible',webkitTextSecurity:'disc'}},'protected_field'],
    ['file input',{tagName:'INPUT',type:'file'},'not_editable'],
    ['date input',{tagName:'INPUT',type:'date'},'not_editable'],
    ['checkbox input',{tagName:'INPUT',type:'checkbox'},'not_editable'],
    ['button',{tagName:'BUTTON'},'not_editable'],
    ['disabled textarea',{disabled:true},'not_editable'],
    ['readonly textarea',{readOnly:true},'not_editable'],
    ['inert textarea',{inert:true},'not_editable'],
    ['embedded iframe',{tagName:'IFRAME'},'embedded_frame'],
    ['body focused',{activeElement:{tagName:'BODY',isConnected:true}},'not_editable'],
    ['disconnected',{isConnected:false},'no_focused_editable'],
    ['display none',{style:{display:'none',visibility:'visible',opacity:'1',contentVisibility:'visible',webkitTextSecurity:'none'}},'not_visible'],
    ['visibility hidden',{style:{display:'block',visibility:'hidden',opacity:'1',contentVisibility:'visible',webkitTextSecurity:'none'}},'not_visible'],
    ['low opacity',{style:{display:'block',visibility:'visible',opacity:'0.4',contentVisibility:'visible',webkitTextSecurity:'none'}},'not_visible'],
    ['tiny 2x2 box',{rect:{left:20,top:20,right:22,bottom:22,width:2,height:2}},'not_visible'],
    ['off-viewport',{rect:{left:20,top:700,right:420,bottom:820,width:400,height:120}},'outside_viewport'],
    ['occluded by cover',{hitTarget:{tagName:'DIV',contains:()=>false}},'occluded'],
  ];
  for(const [label,overrides,expected] of cases){
    const r=typeFixture(overrides).run();
    assert.equal(r.ok,false,`${label} must be rejected`);
    assert.equal(r.reason,expected,`${label} returned unexpected reason`);
  }
  // Explicitly check document.body rejection
  const f=typeFixture();f.document.activeElement=f.document.body;
  assert.deepEqual(f.run(),{ok:false,reason:'no_focused_editable'});
});
