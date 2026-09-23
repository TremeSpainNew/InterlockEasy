const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const script = fs.readFileSync(path.join(__dirname, '../data/dashboard.js'), 'utf8');
const element = () => ({textContent: '', className: '', children: [], append(...children) {this.children.push(...children);}});
const elements = Object.fromEntries(['inputs', 'outputs', 'ip', 'ethernet', 'mqtt', 'relays', 'connection', 'updated'].map(id => [id, element()]));
const channels = Array.from({length: 8}, (_, i) => ({channel: i + 1, name: '', enabled: false, state: false}));
const data = {inputs: structuredClone(channels), outputs: structuredClone(channels), ethernet: {connected: true, ip: '192.168.1.50'}, mqtt: {connected: false, configured: false}, outputsReady: true};
data.inputs[0].name = '<img src=x onerror=alert(1)>';
data.inputs[0].state = true;
data.outputs[7].state = true;
let failure = false;
const timers = new Map();
let sequence = 0;
const context = vm.createContext({
  document: {getElementById: id => elements[id], createElement: element},
  AbortController, Date,
  setTimeout: (fn, ms) => {timers.set(++sequence, {fn, ms}); return sequence;},
  clearTimeout: id => timers.delete(id),
  fetch: async () => {if (failure) throw new Error('offline'); return {ok: true, json: async () => data};},
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
  failure = true;
  await next();
  assert.equal(elements.outputs.children[7].children[2].textContent, 'Sin datos');
  assert.equal(elements.mqtt.textContent, '—');
  failure = false;
  data.outputsReady = false;
  data.outputs.forEach(item => item.state = null);
  await next();
  assert.equal(elements.outputs.children[0].children[2].textContent, 'No disponible');
  data.inputs = [];
  await next();
  assert.equal(elements.inputs.children[0].children[2].textContent, 'Sin datos');
  console.log('OK: states, text rendering, disconnect, recovery and malformed response');
})().catch(error => {console.error(error); process.exitCode = 1;});
