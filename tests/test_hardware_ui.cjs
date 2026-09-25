const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const ids=['hardware-json','hardware-message','hardware-save','hardware-format','hardware-download','hardware-load','hardware-form'];
const elements=Object.fromEntries(ids.map(id=>[id,{value:'',disabled:false,textContent:''}]));
let fail=false,posts=[];
const context=vm.createContext({document:{getElementById:id=>elements[id]},AbortController,TextEncoder,confirm:()=>true,setTimeout:()=>1,clearTimeout:()=>{},fetch:async(url,options)=>{
 assert.equal(url,'/api/hardware');
 if(options.method==='POST'){posts.push(options);return {ok:!fail,text:async()=>fail?'Mapa no válido':'{"saved":true,"restartRequired":true}'};}
 return {ok:true,text:async()=>'{"version":1}'};
}});
(async()=>{
 vm.runInContext(fs.readFileSync('data/hardware.js','utf8'),context);
 await new Promise(r=>setImmediate(r));assert.equal(elements['hardware-save'].disabled,false);
 const submit=()=>elements['hardware-form'].onsubmit({preventDefault(){}});
 elements['hardware-json'].value='{broken';await submit();assert.equal(posts.length,0);
 elements['hardware-json'].value='{"version":1}';await submit();assert.equal(posts.length,1);
 assert.equal(posts[0].headers['X-Interlock'],'1');assert.match(elements['hardware-message'].textContent,/Reinicia/);
 fail=true;await submit();assert.match(elements['hardware-message'].textContent,/Mapa no válido/);
 assert.equal(elements['hardware-json'].value,'{"version":1}');assert.equal(elements['hardware-save'].disabled,false);
 elements['hardware-json'].value=JSON.stringify({value:'á'.repeat(9000)});await submit();assert.equal(posts.length,2);assert.match(elements['hardware-message'].textContent,/16 KiB/);
 console.log('OK: editor load, invalid JSON, UTF-8 limit, save headers, restart notice and error recovery');
})().catch(error=>{console.error(error);process.exitCode=1});
