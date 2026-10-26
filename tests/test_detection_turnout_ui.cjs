const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const element=()=>({children:[],handlers:{},value:'',checked:false,append(...items){this.children.push(...items)},replaceChildren(){this.children=[]},addEventListener(type,fn){this.handlers[type]=fn}});
async function test(name,formId,areaId,addId){
 const nodes=Object.fromEntries([formId,areaId,addId,'ce-editors','add-ce','config-message','save-message','load-config','save-config'].map(id=>[id,element()]));
 const config={mqtt:{},inputs:Array.from({length:8},()=>({enabled:false})),outputs:Array.from({length:8},()=>({enabled:false})),cvs:[],axleCounters:[],turnouts:[]};
 let submitted,fail=false;
 const context=vm.createContext({document:{getElementById:id=>nodes[id],createElement:element},AbortController,setTimeout:()=>1,clearTimeout(){},fetch:async(url,o)=>{
  if(o.method==='POST'){submitted=JSON.parse(o.body);return {ok:!fail,json:async()=>({saved:true,restarting:true}),text:async()=> 'Error NVS'};}
  return {ok:true,json:async()=>structuredClone(config)};
 }});
 vm.runInContext(fs.readFileSync('data/resources.js','utf8'),context);
 vm.runInContext(fs.readFileSync(`data/${name}.js`,'utf8'),context);
 await new Promise(r=>setImmediate(r));
 nodes[addId].onclick();
 const labels=nodes[areaId].children[0].children[1].children;
 labels[0].children[0].checked=true;labels[1].children[0].value='EST';labels[2].children[0].value='A1';
 await nodes[formId].onsubmit({preventDefault(){}});
 assert.ok(submitted);
 if(name==='detection'){assert.equal(submitted.cvs[0].id,'A1');assert.equal(submitted.inputs[0].enabled,false);}
 else {assert.equal(submitted.turnouts[0].id,'A1');assert.equal(submitted.outputs[0].enabled,false);}
 assert.match(nodes['save-message'].textContent,/reiniciando/);
 submitted=undefined;
 if(name==='detection')config.turnouts=[{enabled:true,id:'T1',outputNormal:1,outputReverse:2,inputNormal:1,inputReverse:2}];
 else config.signals=[{enabled:true,name:'S1',lights:[{relay:1}]}];
 await nodes[formId].onsubmit({preventDefault(){}});
 assert.equal(submitted,undefined);
 assert.match(nodes['save-message'].textContent,/ya está usado/);
 config.turnouts=[];config.signals=[];
 fail=true;await nodes[formId].onsubmit({preventDefault(){}});
 assert.match(nodes['save-message'].textContent,/Error NVS/);
 nodes[formId].handlers.invalid({target:{validationMessage:'Valor fuera de rango',parentElement:{firstChild:{textContent:'Entrada'}}}});
 assert.match(nodes['save-message'].textContent,/No se ha enviado: Entrada/);
 assert.equal(nodes['save-message'].textContent,nodes['config-message'].textContent);
}
async function testLoadRetry(name,formId,areaId,addId){
 const nodes=Object.fromEntries([formId,areaId,addId,'ce-editors','add-ce','config-message','save-message','load-config','save-config'].map(id=>[id,element()]));
 const config={mqtt:{},inputs:Array.from({length:8},()=>({enabled:false})),outputs:Array.from({length:8},()=>({enabled:false})),cvs:[],axleCounters:[],turnouts:[]};
 const timers=new Map();let nextTimer=0,configRequests=0;
 const context=vm.createContext({document:{getElementById:id=>nodes[id],createElement:element},AbortController,setTimeout:(fn,delay)=>{const id=++nextTimer;timers.set(id,{fn,delay});return id;},clearTimeout:id=>timers.delete(id),fetch:async url=>{
  if(url==='/api/channels')return{ok:true,json:async()=>({modules:[],inputs:[],outputs:[]})};
  if(++configRequests===1)throw new Error('fallo temporal');
  return{ok:true,json:async()=>structuredClone(config)};
 }});
 vm.runInContext(fs.readFileSync('data/resources.js','utf8'),context);
 vm.runInContext(fs.readFileSync(`data/${name}.js`,'utf8'),context);
 await new Promise(r=>setImmediate(r));
 assert.match(nodes['config-message'].textContent,/Reintentando \(1\/3\)/);
 assert.equal(nodes[formId].hidden,undefined);
 const retry=[...timers.entries()].find(([,timer])=>timer.delay===1000);assert.ok(retry);
 timers.delete(retry[0]);retry[1].fn();await new Promise(r=>setImmediate(r));
 assert.equal(configRequests,2);assert.equal(nodes[formId].hidden,false);
 assert.match(nodes['config-message'].textContent,/Configuración cargada/);
}
(async()=>{await test('detection','detection-form','cv-editors','add-cv');await test('turnouts','turnout-form','turnout-editors','add-turnout');await testLoadRetry('detection','detection-form','cv-editors','add-cv');await testLoadRetry('turnouts','turnout-form','turnout-editors','add-turnout');console.log('OK: CV/turnout submission, channel conflicts, validation and transient load retries');})().catch(e=>{console.error(e);process.exitCode=1});
