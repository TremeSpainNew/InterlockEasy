
const byId = id => document.getElementById(id);
function createCards(id, prefix, label) {
  return Array.from({length: 8}, (_, index) => {
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
    card.append(badge, name, state, enabled);
    byId(id).append(card);
    return {name, state, enabled};
  });
}
const inputs = createCards('inputs', 'DI', 'Entrada');
const outputs = createCards('outputs', 'RO', 'Relé');
function render(cards, values, label, on, off) {
  values.forEach((item, i) => {
    cards[i].name.textContent = item.name || label + ' ' + item.channel;
    cards[i].state.textContent = item.state === null ? 'No disponible' : item.state ? on : off;
    cards[i].state.className = 'state' + (item.state === true ? ' active' : '');
    cards[i].enabled.textContent = item.signal ? 'Asignado a señal: ' + item.signal : item.enabled ? 'MQTT habilitado' : 'MQTT deshabilitado';
  });
}
function validChannels(items) {
  return Array.isArray(items) && items.length === 8 && items.every((item, i) =>
    item && item.channel === i + 1 && typeof item.name === 'string' &&
    typeof item.enabled === 'boolean' && (typeof item.state === 'boolean' || item.state === null));
}
async function refresh() {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 4000);
  try {
    const response = await fetch('/api/status', {cache: 'no-store', signal: controller.signal});
    if (!response.ok) throw new Error('HTTP ' + response.status);
    const data = await response.json();
    if (!validChannels(data.inputs) || !validChannels(data.outputs) || !data.ethernet || !data.mqtt)
      throw new Error('Respuesta no válida');
    render(inputs, data.inputs, 'Entrada', 'Activa', 'Inactiva');
    render(outputs, data.outputs, 'Relé', 'ON', 'OFF');
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
        signalArea.append(card);
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
      card.state.textContent = 'Sin datos';
      card.state.className = 'state';
      card.enabled.textContent = 'MQTT: —';
    }
    if (byId('signals')) byId('signals').textContent = 'Sin datos de señales';
    for (const id of ['ip', 'ethernet', 'mqtt', 'relays']) byId(id).textContent = '—';
  } finally {
    clearTimeout(timeout);
    setTimeout(refresh, 1000);
  }
}
refresh();
