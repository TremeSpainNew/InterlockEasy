const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const element=()=>({children:[],handlers:{},value:'',checked:false,append(...items){this.children.push(...items)},replaceChildren(){this.children=[]},addEventListener(type,fn){this.handlers[type]=fn}});
async function test(name,formId,areaId,addId){
 const nodes=Object.fromEntries([formId,areaId,addId,'ce-editors','add-ce','config-message','save-message','load-config','save-config'].map(id=>[id,element()]));
 const config={mqtt:{},inputs:Array.from({length:8},()=>({enabled:true})),outputs:Array.from({length:8},()=>({enabled:true})),cvs:[],axleCounters:[],turnouts:[]};
 let submitted,fail=false;
 const context=vm.createContext({document:{getElementById:id=>nodes[id],createElement:element},AbortController,setTimeout:()=>1,clearTimeout(){},fetch:async(url,o)=>{
  if(o.method==='POST'){submitted=JSON.parse(o.body);return {ok:!fail,json:async()=>({saved:true,restarting:true}),text:async()=> 'Error NVS'};}
  return {ok:true,json:async()=>structuredClone(config)};
 }});
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
 fail=true;await nodes[formId].onsubmit({preventDefault(){}});
 assert.match(nodes['save-message'].textContent,/Error NVS/);
 nodes[formId].handlers.invalid({target:{validationMessage:'Valor fuera de rango',parentElement:{firstChild:{textContent:'Entrada'}}}});
 assert.match(nodes['save-message'].textContent,/No se ha enviado: Entrada/);
 assert.equal(nodes['save-message'].textContent,nodes['config-message'].textContent);
}
(async()=>{await test('detection','detection-form','cv-editors','add-cv');await test('turnouts','turnout-form','turnout-editors','add-turnout');console.log('OK: CV/turnout submission, reserved channels, success/error and native validation messages');})().catch(e=>{console.error(e);process.exitCode=1});
