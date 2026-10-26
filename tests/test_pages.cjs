const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const dir=path.join(__dirname,'../data');
const script=fs.readFileSync(path.join(dir,'config.js'),'utf8');
const element=()=>({children:[],handlers:{},value:'',checked:false,append(...items){this.children.push(...items)},replaceChildren(){this.children=[]},addEventListener(type,fn){this.handlers[type]=fn}});
const all=node=>[node,...node.children.flatMap(all)];
async function test(page,count=8){
 const elements=Object.fromEntries(['config-form','config-fields','config-message','load-config','save-config'].map(id=>[id,element()]));
 elements['config-form'].dataset={section:page};
 const config={mqtt:{host:'old',port:1883,clientId:'test',username:'',keepAlive:30},inputs:Array.from({length:count},()=>({enabled:false,name:'',topic:'',payloadOn:'1',payloadOff:'0',inverted:true,retain:true})),outputs:Array.from({length:count},()=>({enabled:false,name:'',commandTopic:'',payloadOn:'1',payloadOff:'0',publishState:true,stateTopic:'',stateOn:'1',stateOff:'0',retain:true})),signals:[{id:1,numeric:false,enabled:false,name:'S1',topic:'s1',jsonPath:'Aspecto',blinkMs:500,lights:[{name:'Rojo',relay:1}],aspects:[{value:'Parada',on:[1],blink:[]}]},{id:2,numeric:true,enabled:false,name:'A1',topic:'a1',jsonPath:'Valor',blinkMs:500,lights:[{name:'60',relay:1}],aspects:[{value:'60',on:[1],blink:[]}]}],beacons:[{enabled:false,name:'B1',signalId:1,relays:[2],aspects:[{value:'Parada',on:[1],blink:[]}]}]};
 if (!count) {config.signals=[];config.beacons=[];}
 let submitted;
 const context=vm.createContext({document:{getElementById:id=>elements[id],createElement:element},AbortController,setTimeout:()=>1,clearTimeout:()=>{},fetch:async(url,options)=>{if(options.method==='POST'){submitted=JSON.parse(options.body);return{ok:true,json:async()=>({saved:true})}}return{ok:true,json:async()=>structuredClone(config)}}});
 vm.runInContext(fs.readFileSync('data/resources.js','utf8'),context);
 vm.runInContext(script,context);
 await new Promise(r=>setImmediate(r));
 const controls=all(elements['config-fields']);
 const names=controls.filter(n=>n.name).map(n=>n.name);
 assert.equal(names.includes('host'),page==='mqtt');
 assert.equal(names.includes('debounceMs'),page==='inputs' && count>0);
 assert.equal(names.includes('commandTopic'),page==='outputs' && count>0);
 assert.equal(controls.some(n=>n.textContent==='Añadir señal'),page==='signals');
 assert.ok(controls.filter(n=>n.name==='value').every(n=>n.type==='text'));
 if(page==='beacons'&&count){
  assert.ok(controls.some(n=>n.textContent==='Relé baliza 1'));
  const addRelay=controls.find(n=>n.textContent==='Añadir relé');
  assert.ok(addRelay);
  addRelay.handlers.click();
 }
 if(page==='signals'&&count) controls.find(n=>n.name==='value').value='ParadaModificada';
 config.mqtt.host='newer-broker';if(count)config.inputs[0].name='Changed elsewhere';
 await elements['config-form'].handlers.submit({preventDefault(){}});
 assert.ok(submitted);
 if (count) {
 assert.equal(submitted.signals.length,2);
 assert.equal(submitted.signals.find(s=>s.name==='A1').numeric,true);
 assert.equal(submitted.signals.find(s=>s.name==='A1').aspects[0].value,'60');
 assert.equal(submitted.signals.find(s=>s.name==='S1').aspects[0].value,page==='signals'?'ParadaModificada':'Parada');
 assert.equal(submitted.beacons.length,1);
 assert.equal(submitted.beacons[0].signalId,1);
 assert.deepEqual(submitted.beacons[0].relays,page==='beacons'?[2,3]:[2]);
 assert.equal(submitted.beacons[0].aspects[0].value,page==='signals'?'ParadaModificada':'Parada');
 }
 if(page!=='mqtt')assert.equal(submitted.mqtt.host,'newer-broker');
 if(count && page!=='inputs')assert.equal(submitted.inputs[0].name,'Changed elsewhere');
 assert.equal('password' in submitted.mqtt,false);
}
(async()=>{
 for(const count of [0,8,32,64]) for(const page of ['mqtt','inputs','outputs','signals','alphanumerics','beacons']) await test(page,count);
 for(const file of fs.readdirSync(dir).filter(f=>f.endsWith('.html'))){
  const html=fs.readFileSync(path.join(dir,file),'utf8');
  for(const match of html.matchAll(/(?:src|href)="\/([^"#]+)"/g))assert.ok(match[1]==='api/backup' ? true : match[1]==='firmware.html' ? fs.existsSync(path.join(dir,'../src/FirmwarePage.h')) : fs.existsSync(path.join(dir,match[1])),match[1]);
 }
 assert.ok(!fs.readFileSync(path.join(dir,'index.html'),'utf8').includes('config-form'));
 console.log('OK: separate pages, automatic load, isolated forms, preservation of other sections and asset links');
})().catch(e=>{console.error(e);process.exitCode=1});
