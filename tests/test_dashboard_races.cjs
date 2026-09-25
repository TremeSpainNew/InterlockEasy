const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const node=()=>({children:[],append(...v){this.children.push(...v)},replaceChildren(){this.children=[]}});
const elements=Object.fromEntries(['inputs','outputs','ip','ethernet','mqtt','relays','connection','updated'].map(id=>[id,node()]));
const pending=[],timers=new Map();let sequence=0;
const data={inputs:[],outputs:[],ethernet:{connected:true,ip:'test'},mqtt:{connected:true},outputsReady:true};
const context=vm.createContext({document:{getElementById:id=>elements[id],createElement:node},AbortController,Date,
 setTimeout:(fn,ms)=>{timers.set(++sequence,{fn,ms});return sequence},clearTimeout:id=>timers.delete(id),
 fetch:(url,options)=>new Promise(resolve=>pending.push({url,options,resolve}))});
const flush=()=>new Promise(r=>setImmediate(r));
(async()=>{
 vm.runInContext(fs.readFileSync('data/dashboard.js','utf8'),context);
 assert.equal(pending.length,1);
 const command=vm.runInContext('sendTest({type:"relay",channel:1,state:true})',context);
 await flush();assert.equal(pending.length,1,'command waits for status');
 pending[0].resolve({ok:true,json:async()=>data});await flush();
 assert.equal(pending.length,2);assert.equal(pending[1].url,'/api/test');
 assert.equal([...timers.values()].filter(t=>t.ms===1000).length,0);
 await vm.runInContext('refresh()',context);assert.equal(pending.length,2,'poll paused during command');
 pending[1].resolve({ok:true});await flush();
 assert.equal(pending.length,3);assert.equal(pending[2].url,'/api/status','immediate status after command');
 const extra=vm.runInContext('refresh()',context);assert.equal(pending.length,3,'coalesce duplicate reads');
 pending[2].resolve({ok:true,json:async()=>data});await command;await extra;
 assert.equal([...timers.values()].filter(t=>t.ms===1000).length,1,'one polling loop remains');
 console.log('OK: status/command serialization, immediate readback and single polling timer');
})().catch(e=>{console.error(e);process.exitCode=1});
