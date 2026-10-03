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
