const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const script = fs.readFileSync(path.join(__dirname, '../data/dashboard.js'), 'utf8');
const element = () => ({textContent: '', className: '', children: [], replaceChildren() {this.children=[];}, append(...children) {this.children.push(...children);}});
const elements = Object.fromEntries(['inputs', 'outputs', 'ip', 'ethernet', 'mqtt', 'relays', 'connection', 'updated'].map(id => [id, element()]));
const channels = Array.from({length: 8}, (_, i) => ({channel: i + 1, name: '', enabled: false, state: false}));
const data = {inputs: structuredClone(channels), outputs: structuredClone(channels), ethernet: {connected: true, ip: '192.168.1.50'}, mqtt: {connected: false, configured: false}, outputsReady: true};
data.inputs[0].name = '<img src=x onerror=alert(1)>';
data.inputs[0].state = true;
Object.assign(data.inputs[0], {raw:false, debounceMs:50, filtering:true, publishPending:true});
data.outputs[7].state = true;
let failure = false;
const commands = [];
const timers = new Map();
let sequence = 0;
const context = vm.createContext({
  document: {getElementById: id => elements[id], createElement: element},
  AbortController, Date,
  setTimeout: (fn, ms) => {timers.set(++sequence, {fn, ms}); return sequence;},
  clearTimeout: id => timers.delete(id),
  fetch: async (url, options) => {if (url === '/api/test') {commands.push({url, options}); return {ok:true};} if (failure) throw new Error('offline'); return {ok: true, json: async () => data};},
});
const flush = () => new Promise(resolve => setImmediate(resolve));
async function next() {
  const entry = [...timers.entries()].find(([, value]) => value.ms === 1000);
  assert.ok(entry, 'next poll scheduled');
  timers.delete(entry[0]);
  await entry[1].fn();
  await flush();
}
(async () => {
  vm.runInContext(script, context);
  await flush();
  assert.equal(elements.inputs.children.length, 8);
  assert.equal(elements.outputs.children.length, 8);
  assert.equal(elements.inputs.children[0].children[1].textContent, data.inputs[0].name);
  assert.equal(elements.inputs.children[0].children[2].textContent, 'Activa');
  assert.equal(elements.outputs.children[7].children[2].textContent, 'ON');
  assert.equal(elements.mqtt.textContent, 'Sin configurar');
  assert.equal(elements.inputs.children[0].children[5].children.length,3);
  assert.equal(elements.outputs.children[0].children[5].children.length,2);
  assert.equal(elements.outputs.children[0].children[5].children[0].disabled,false);
  assert.match(elements.inputs.children[0].children[4].textContent, /GPIO bajo.*50 ms.*filtrando.*envío pendiente/);
  await elements.inputs.children[0].children[5].children[0].onclick();
  await elements.inputs.children[0].children[5].children[2].onclick();
  await elements.outputs.children[7].children[5].children[0].onclick();
  assert.deepEqual(commands.map(c => JSON.parse(c.options.body)), [
    {type:'input', channel:1, state:true},
    {type:'input', channel:1, state:null},
    {type:'relay', channel:8, state:true}
  ]);
  assert.ok(commands.every(c => c.options.method === 'POST' && c.options.headers['X-Interlock'] === '1'));
  failure = true;
  await next();
  assert.equal(elements.outputs.children[7].children[2].textContent, 'Sin datos');
  assert.equal(elements.mqtt.textContent, '—');
  assert.equal(elements.inputs.children[0].children[5].children[0].disabled,true);
  assert.equal(elements.inputs.children[0].children[4].textContent, '');
  failure = false;
  data.inputs[0].simulated = true;
  data.outputsReady = false;
  data.outputs.forEach(item => item.state = null);
  await next();
  assert.equal(elements.outputs.children[0].children[2].textContent, 'No disponible');
  assert.match(elements.inputs.children[0].children[2].textContent,/SIMULADA/);
  assert.equal(elements.outputs.children[0].children[5].children[0].disabled,true);
  data.inputs = Array.from({length:32}, (_,i)=>({...channels[0],channel:i+1}));
  data.outputs = Array.from({length:16}, (_,i)=>({...channels[0],channel:i+1}));
  data.outputsReady=true;
  await next();
  assert.equal(elements.inputs.children.length,32);
  assert.equal(elements.outputs.children.length,16);
  await elements.inputs.children[31].children[5].children[0].onclick();
  assert.equal(JSON.parse(commands.at(-1).options.body).channel,32);
  data.inputs = null;
  await next();
  assert.equal(elements.inputs.children[0].children[2].textContent, 'Sin datos');
  console.log('OK: states, text rendering, disconnect, recovery and malformed response');
})().catch(error => {console.error(error); process.exitCode = 1;});
