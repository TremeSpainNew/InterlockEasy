
const byId = id => document.getElementById(id);
let testBusy = false;
let refreshTask = null;
let pollTimer = null;
async function sendTest(command) {
  if (testBusy) return;
  testBusy = true;
  clearTimeout(pollTimer);
  // Let a pending status read finish before issuing a hardware command.
  if (refreshTask) await refreshTask;
  const feedback = byId('test-feedback');
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 12000);
  if (feedback) feedback.textContent = 'Aplicando prueba…';
  try {
    const response = await fetch('/api/test', {method:'POST', headers:{'Content-Type':'application/json','X-Interlock':'1'}, body:JSON.stringify(command), signal:controller.signal});
    if (!response.ok) throw new Error(await response.text());
    if (feedback) feedback.textContent = 'Prueba aplicada. El estado se actualizará en la siguiente lectura.';
  } catch(error) {
    if (feedback) feedback.textContent = 'No se pudo confirmar la prueba: ' + error.message + '. Comprueba el estado antes de repetir.';
  } finally {
    clearTimeout(timer); testBusy = false;
    await refresh();
  }
}
function testButton(label, command) {
  const button = document.createElement('button'); button.type = 'button'; button.textContent = label;
  button.disabled = true; button.onclick = () => sendTest(command); return button;
}
function createCards(id, prefix, label, count) {
  return Array.from({length: count}, (_, index) => {
    const card = document.createElement('div');
    card.className = 'card';
    const badge = document.createElement('span');
    badge.className = 'badge';
    badge.textContent = prefix + (index + 1);
    const name = document.createElement('b');
    name.textContent = label + ' ' + (index + 1);
    const state = document.createElement('p');
    state.className = 'state';
    state.textContent = 'Sin datos';
    const enabled = document.createElement('p');
    enabled.className = 'muted';
    enabled.textContent = 'MQTT: —';
    const diagnostic = document.createElement('p'); diagnostic.className = 'muted';
    card.append(badge, name, state, enabled, diagnostic);
    const actions = document.createElement('div'); actions.className = 'test-controls';
    const buttons = id === 'inputs' ? [
      testButton('Simular activa', {type:'input',channel:index+1,state:true}),
      testButton('Simular inactiva', {type:'input',channel:index+1,state:false}),
      testButton('Lectura física', {type:'input',channel:index+1,state:null})
    ] : [testButton('ON', {type:'relay',channel:index+1,state:true}), testButton('OFF', {type:'relay',channel:index+1,state:false})];
    actions.append(...buttons); card.append(actions);
    byId(id).append(card);
    return {name, state, enabled, diagnostic, buttons};
  });
}
let inputs = [], outputs = [];
function render(cards, values, label, on, off, ready = true) {
  values.forEach((item, i) => {
    cards[i].name.textContent = item.name || label + ' ' + item.channel;
    cards[i].state.textContent = item.state === null ? 'No disponible' : item.state ? on : off;
    if (item.simulated) cards[i].state.textContent += ' · SIMULADA';
    cards[i].buttons.forEach(button => {button.disabled = !ready || !!item.signal || testBusy;});
    cards[i].state.className = 'state' + (item.state === true ? ' active' : '');
    cards[i].diagnostic.textContent = typeof item.raw === 'boolean' ? 'GPIO ' + (item.raw ? 'alto' : 'bajo') + ' · ' + item.debounceMs + ' ms' + (item.filtering ? ' · filtrando' : '') + (item.publishPending ? ' · envío pendiente' : '') : '';
    cards[i].enabled.textContent = item.signal ? 'Asignado a señal: ' + item.signal : item.enabled ? 'MQTT habilitado' : 'MQTT deshabilitado';
  });
}
function validChannels(items) {
  return Array.isArray(items) && items.length <= 32 && items.every((item, i) =>
    item && item.channel === i + 1 && typeof item.name === 'string' &&
    typeof item.enabled === 'boolean' && (typeof item.state === 'boolean' || item.state === null));
}
function refresh() {
  if (refreshTask) return refreshTask;
  if (testBusy) return Promise.resolve();
  clearTimeout(pollTimer);
  refreshTask = refreshStatus().finally(() => {refreshTask = null;});
  return refreshTask;
}
async function refreshStatus() {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 4000);
  try {
    const response = await fetch('/api/status', {cache: 'no-store', signal: controller.signal});
    if (!response.ok) throw new Error('HTTP ' + response.status);
    const data = await response.json();
    if (!validChannels(data.inputs) || !validChannels(data.outputs) || !data.ethernet || !data.mqtt)
      throw new Error('Respuesta no válida');
    if (inputs.length !== data.inputs.length) { byId('inputs').replaceChildren(); inputs = createCards('inputs', 'DI', 'Entrada', data.inputs.length); }
    if (outputs.length !== data.outputs.length) { byId('outputs').replaceChildren(); outputs = createCards('outputs', 'RO', 'Relé', data.outputs.length); }
    if (byId('hardware-count')) byId('hardware-count').textContent = data.inputs.length + ' DI / ' + data.outputs.length + ' RO';
    if (byId('input-count')) byId('input-count').textContent = data.inputs.length + ' canales';
    if (byId('output-count')) byId('output-count').textContent = data.outputs.length + ' canales';
    render(inputs, data.inputs, 'Entrada', 'Activa', 'Inactiva');
    render(outputs, data.outputs, 'Relé', 'ON', 'OFF', data.outputsReady);
    const signalArea = byId('signals');
    if (signalArea) {
      signalArea.replaceChildren();
      for (const signal of data.signals || []) {
        const card = document.createElement('div'); card.className = 'card';
        const title = document.createElement('b'); title.textContent = signal.name;
        const aspect = document.createElement('p'); aspect.textContent = !signal.enabled ? 'Deshabilitada' : signal.aspect || 'Sin aspecto recibido';
        card.append(title, aspect);
        for (const light of signal.lights) {
          const row = document.createElement('p');
          row.textContent = light.name + ' · RO' + light.relay + ': ' + (light.state === null ? 'No disponible' : light.state ? 'ON' : 'OFF');
          row.className = light.state ? 'active' : 'muted'; card.append(row);
        }
        const actions = document.createElement('div'); actions.className = 'test-controls';
        for (const value of signal.aspects || []) {
          const button = testButton(value, {type:'signal',channel:signal.channel,aspect:value});
          button.disabled = !signal.enabled || !data.outputsReady || testBusy; actions.append(button);
        }
        card.append(actions); signalArea.append(card);
      }
      if (!(data.signals || []).length) signalArea.textContent = 'Sin señales configuradas.';
    }
    byId('ip').textContent = data.ethernet.ip;
    byId('ethernet').textContent = data.ethernet.connected ? 'Conectado' : 'Desconectado';
    byId('mqtt').textContent = data.mqtt.connected ? 'Conectado' : data.mqtt.configured ? 'Desconectado' : 'Sin configurar';
    byId('relays').textContent = data.outputsReady ? 'Controlador inicializado' : 'No disponible';
    byId('connection').className = 'note online';
    byId('connection').textContent = 'Conectado al módulo · actualización cada segundo';
    byId('updated').textContent = 'Última lectura: ' + new Date().toLocaleTimeString('es-ES');
  } catch (error) {
    byId('connection').className = 'note offline';
    byId('connection').textContent = 'Sin comunicación con el módulo. Reintentando…';
    for (const card of [...inputs, ...outputs]) {
      card.buttons.forEach(button => {button.disabled = true;});
      card.state.textContent = 'Sin datos';
      card.state.className = 'state';
      card.enabled.textContent = 'MQTT: —';
      card.diagnostic.textContent = '';
    }
    if (byId('signals')) byId('signals').textContent = 'Sin datos de señales';
    for (const id of ['ip', 'ethernet', 'mqtt', 'relays']) byId(id).textContent = '—';
  } finally {
    clearTimeout(timeout);
    if (!testBusy) pollTimer = setTimeout(refresh, 1000);
  }
}
refresh();
