const assert = require('node:assert/strict'), fs = require('node:fs'), vm = require('node:vm');
function element(tagName = 'div') {
  return {tagName:tagName.toUpperCase(),children:[],handlers:{},value:'',open:false,
    append(...nodes) {this.children.push(...nodes);},
    replaceChildren(...nodes) {this.children = nodes;},
    addEventListener(type, fn) {this.handlers[type] = fn;},
    removeEventListener(type, fn) {if(this.handlers[type] === fn) delete this.handlers[type];}};
}
const host=element('details'), form=element('form');
const hardware={modules:[{type:'gpio'},{type:'mcp23017',address:32},{type:'modbus',address:7}],inputs:[{module:0,pin:4},{module:1,pin:3},{module:2,pin:63}],outputs:[{module:1,pin:8},{module:2,pin:63},{module:0,pin:9}]};
let fail = false;
const context=vm.createContext({document:{createElement:element,getElementById:id=>id==='resource-map'?host:null},AbortController,setTimeout:()=>1,clearTimeout(){},fetch:async url=>{
  assert.equal(url,'/api/channels');if(fail)throw Error('offline');return {ok:true,json:async()=>structuredClone(hardware)};
}});
vm.runInContext(fs.readFileSync('data/resources.js','utf8')+'\nthis.resources=Resources;',context);
const R=context.resources;
const config={inputs:[{enabled:false},{enabled:true,name:'Panel'},{enabled:false}],outputs:[{enabled:false},{enabled:false},{enabled:false}],cvs:[{enabled:true,id:'CV1',input:1}],axleCounters:[],turnouts:[],signals:[{enabled:false,name:'S1',lights:[{name:'Verde',relay:1}]}]};
const cv=R.select('inputs',1), relay=R.select('outputs',1), feedback=R.select('inputs',0,true);
const bindings=[{node:cv,path:'cvs.0.input'},{node:relay,path:'signals.0.lights.0.relay'},{node:feedback,path:'turnouts.0.inputNormal'}];
const getConfig=()=>{if(config.cvs[0])config.cvs[0].input=Number(cv.value);if(config.signals[0])config.signals[0].lights[0].relay=Number(relay.value);return config;};
const all=node=>[node,...node.children.flatMap(all)];
const text=node=>all(node).map(n=>n.textContent||'').join(' ');
const option=(node,n)=>node.children.find(o=>o.value===String(n));
(async()=>{
  const view=R.editor(form,getConfig,()=>bindings);await view.ready;
  assert.equal(option(cv,1).disabled,false);assert.match(option(cv,1).textContent,/Selección actual.*GPIO4/);
  assert.equal(option(cv,2).disabled,true);assert.match(option(cv,2).textContent,/Panel.*MCP23017 @0x20 · P3/);
  assert.match(option(cv,3).textContent,/Libre.*Modbus 7 · DI64/);
  assert.equal(option(feedback,0).textContent,'Sin comprobación');
  assert.equal(option(feedback,1).disabled,true);
  assert.match(text(host),/CV CV1/);assert.match(text(host),/Señal S1 · Verde \(deshabilitado\)/);
  host.open=true;cv.value='3';form.handlers.change();
  assert.equal(host.open,true);assert.match(option(cv,1).textContent,/Libre/);assert.equal(option(feedback,1).disabled,false);
  assert.equal(option(feedback,3).disabled,true);
  // Occupied values already present remain visible, so they can be corrected.
  cv.value='2';form.handlers.change();assert.equal(option(cv,2).disabled,false);assert.match(text(host),/Conflicto/);
  config.cvs=[];bindings.splice(0,1);view.refresh();assert.equal(option(feedback,3).disabled,false);
  config.signals=[];bindings.splice(0,1);view.refresh();assert.match(text(host),/Relés RO · 0\/3 en uso/);
  view.dispose();assert.equal(form.handlers.change,undefined);
  fail=true;const fallback=R.editor(form,()=>({inputs:[],outputs:[]}),()=>[]);await fallback.ready;
  assert.match(text(host),/No se pudo cargar el mapa físico/);assert.match(text(host),/Sin canales configurados/);
  fallback.dispose();
  console.log('OK: selectors, own assignment, disabled occupied options, live release, physical pins, conflicts, empty/offline map and cleanup');
})().catch(e=>{console.error(e);process.exitCode=1;});
