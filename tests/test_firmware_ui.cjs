const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync('src/FirmwarePage.h','utf8');
const nodes=Object.fromEntries(['update-form','firmware','upload','progress','message'].map(id=>[id,{}]));
let xhr;
class XHR {
 constructor(){xhr=this;this.upload={};this.headers={};}
 open(method,url){assert.equal(method,'POST');assert.equal(url,'/api/firmware');}
 setRequestHeader(k,v){this.headers[k]=v;}
 send(file){this.file=file;}
}
vm.runInNewContext(html.match(/<script>([\s\S]*?)<\/script>/)[1],{document:{getElementById:id=>nodes[id]},XMLHttpRequest:XHR,window:{addEventListener(){}}});
const submit=()=>nodes['update-form'].onsubmit({preventDefault(){}});
nodes.firmware.files=[{name:'bad.txt',size:100}];submit();assert.equal(xhr,undefined);
const file={name:'firmware.bin',size:1024};nodes.firmware.files=[file];submit();
assert.equal(xhr.file,file);assert.equal(xhr.headers['X-Interlock'],'1');assert.equal(xhr.headers['Content-Type'],'application/octet-stream');
assert.equal(nodes.upload.disabled,true);
xhr.upload.onprogress({lengthComputable:true,loaded:512,total:1024});assert.equal(nodes.progress.value,50);
xhr.status=400;xhr.responseText='Invalid image';xhr.onload();assert.match(nodes.message.textContent,/Invalid image/);assert.equal(nodes.upload.disabled,false);
submit();xhr.status=200;xhr.onload();assert.match(nodes.message.textContent,/reiniciando/);assert.equal(nodes.progress.value,100);
console.log('OK: firmware selection, binary request, progress, errors and success');
