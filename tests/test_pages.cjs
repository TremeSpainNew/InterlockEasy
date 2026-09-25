const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const dir=path.join(__dirname,'../data');
const script=fs.readFileSync(path.join(dir,'config.js'),'utf8');
const element=()=>({children:[],handlers:{},value:'',checked:false,append(...items){this.children.push(...items)},replaceChildren(){this.children=[]},addEventListener(type,fn){this.handlers[type]=fn}});
const all=node=>[node,...node.children.flatMap(all)];
async function test(page,count=8){
 const elements=Object.fromEntries(['config-form','config-fields','config-message','load-config','save-config'].map(id=>[id,element()]));
 elements['config-form'].dataset={section:page};
 const config={mqtt:{host:'old',port:1883,clientId:'test',username:'',keepAlive:30},inputs:Array.from({length:count},()=>({enabled:false,name:'',topic:'',payloadOn:'1',payloadOff:'0',inverted:true,retain:true})),outputs:Array.from({length:count},()=>({enabled:false,name:'',commandTopic:'',payloadOn:'1',payloadOff:'0',publishState:true,stateTopic:'',stateOn:'1',stateOff:'0',retain:true})),signals:[]};
 let submitted;
 const context=vm.createContext({document:{getElementById:id=>elements[id],createElement:element},AbortController,setTimeout:()=>1,clearTimeout:()=>{},fetch:async(url,options)=>{if(options.method==='POST'){submitted=JSON.parse(options.body);return{ok:true,json:async()=>({saved:true})}}return{ok:true,json:async()=>structuredClone(config)}}});
 vm.runInContext(script,context);
 await new Promise(r=>setImmediate(r));
 const controls=all(elements['config-fields']);
 const names=controls.filter(n=>n.name).map(n=>n.name);
 assert.equal(names.includes('host'),page==='mqtt');
 assert.equal(names.includes('debounceMs'),page==='inputs' && count>0);
 assert.equal(names.includes('commandTopic'),page==='outputs' && count>0);
 assert.equal(controls.some(n=>n.textContent==='Añadir señal'),page==='signals');
 config.mqtt.host='newer-broker';if(count)config.inputs[0].name='Changed elsewhere';
 await elements['config-form'].handlers.submit({preventDefault(){}});
 assert.ok(submitted);
 if(page!=='mqtt')assert.equal(submitted.mqtt.host,'newer-broker');
 if(count && page!=='inputs')assert.equal(submitted.inputs[0].name,'Changed elsewhere');
 assert.equal('password' in submitted.mqtt,false);
}
(async()=>{
 for(const count of [0,8,32]) for(const page of ['mqtt','inputs','outputs','signals']) await test(page,count);
 for(const file of fs.readdirSync(dir).filter(f=>f.endsWith('.html'))){
  const html=fs.readFileSync(path.join(dir,file),'utf8');
  for(const match of html.matchAll(/(?:src|href)="\/([^"#]+)"/g))assert.ok(fs.existsSync(path.join(dir,match[1])),match[1]);
 }
 assert.ok(!fs.readFileSync(path.join(dir,'index.html'),'utf8').includes('config-form'));
 console.log('OK: separate pages, automatic load, isolated forms, preservation of other sections and asset links');
})().catch(e=>{console.error(e);process.exitCode=1});
