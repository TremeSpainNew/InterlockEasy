// Shared ownership rules for the configuration editors.
const Resources = (() => {
  function usage(config, drafts = false) {
    const inputs = new Map(), outputs = new Map();
    function claim(map, channel, owner, count) {
      if (!Number.isInteger(channel) || channel < 1 || channel > count)
        throw new Error(owner + ': canal fuera de rango (' + channel + ').');
      if (map.has(channel) && !drafts)
        throw new Error((map === inputs ? 'DI' : 'RO') + channel + ' ya está usado por ' + map.get(channel) + '; conflicto con ' + owner + '.');
      map.set(channel, owner);
    }
    const di = (n, owner) => claim(inputs, n, owner, config.inputs.length);
    const ro = (n, owner) => claim(outputs, n, owner, config.outputs.length);
    (config.inputs || []).forEach((x,i) => {if (x.enabled) di(i+1, 'entrada MQTT');});
    (config.outputs || []).forEach((x,i) => {if (x.enabled) ro(i+1, 'salida MQTT');});
    (config.cvs || []).forEach((x,i) => {if (drafts || x.enabled) di(x.input, 'CV ' + (x.id || i+1));});
    (config.axleCounters || []).forEach((x,i) => {if (drafts || x.enabled) {di(x.inputA, 'CE ' + (x.id || i+1)); di(x.inputB, 'CE ' + (x.id || i+1));}});
    (config.signals || []).forEach((x,i) => {if (drafts || x.enabled) (x.lights || []).forEach(l => ro(l.relay, 'señal ' + (x.name || i+1)));});
    (config.beacons || []).forEach((x,i) => {if (drafts || x.enabled) (x.relays || []).forEach(relay => ro(relay, 'baliza ' + (x.name || i+1)));});
    (config.turnouts || []).forEach((x,i) => {if (drafts || x.enabled) {
      const owner = 'desvío ' + (x.id || i+1);
      ro(x.outputNormal, owner); ro(x.outputReverse, owner);
      if (x.inputNormal) di(x.inputNormal, owner);
      if (x.inputReverse) di(x.inputReverse, owner);
    }});
    return {inputs, outputs};
  }
  function free(config, kind, count = 1) {
    const occupied = usage(config, true)[kind], result = [];
    for (let n = 1; n <= config[kind].length && result.length < count; n++)
      if (!occupied.has(n)) result.push(n);
    if (result.length < count) throw new Error('No hay suficientes ' + (kind === 'inputs' ? 'entradas' : 'relés') + ' libres.');
    return result;
  }
  // Paths identify a field, so its own assignment stays selectable while editing.
  function entries(config) {
    const result = [];
    const add = (kind, channel, owner, path, enabled) => {
      if (channel) result.push({kind, channel, owner, path, enabled:!!enabled});
    };
    for (const kind of ['inputs','outputs']) (config[kind] || []).forEach((x,i) => {
      if (x.enabled) add(kind,i+1,(kind === 'inputs' ? 'Entrada MQTT' : 'Salida MQTT') + (x.name ? ' · ' + x.name : ''),`${kind}.${i}.enabled`,true);
    });
    (config.cvs || []).forEach((x,i) => add('inputs',x.input,'CV ' + [x.station,x.id || i+1].filter(Boolean).join('/'),`cvs.${i}.input`,x.enabled));
    (config.axleCounters || []).forEach((x,i) => {
      for (const key of ['inputA','inputB']) add('inputs',x[key],'CE ' + [x.station,x.id || i+1].filter(Boolean).join('/') + (key === 'inputA' ? ' · A' : ' · B'),`axleCounters.${i}.${key}`,x.enabled);
    });
    (config.signals || []).forEach((x,i) => (x.lights || []).forEach((l,j) => add('outputs',l.relay,(x.numeric ? 'Alfanumérica ' : 'Señal ') + (x.name || i+1) + ' · ' + (l.name || 'Foco ' + (j+1)),`signals.${i}.lights.${j}.relay`,x.enabled)));
    (config.beacons || []).forEach((x,i) => (x.relays || []).forEach((relay,j) => add('outputs',relay,'Baliza ' + (x.name || i+1) + ' · Relé ' + (j+1),`beacons.${i}.relays.${j}`,x.enabled)));
    (config.turnouts || []).forEach((x,i) => {
      for (const key of ['outputNormal','outputReverse','inputNormal','inputReverse'])
        add(key.startsWith('input') ? 'inputs' : 'outputs',x[key],'Desvío ' + [x.station,x.id || i+1].filter(Boolean).join('/') + (key.endsWith('Normal') ? ' · normal' : ' · invertida'),`turnouts.${i}.${key}`,x.enabled);
    });
    return result;
  }
  function physical(hardware, kind, index) {
    const channel = hardware?.[kind]?.[index], module = hardware?.modules?.[channel?.module];
    if (!channel || !module) return 'Pin físico no disponible';
    if (module.type === 'gpio') return 'GPIO' + channel.pin;
    if (module.type === 'modbus') return 'Modbus ' + module.address + ' · ' + (kind === 'inputs' ? 'DI' : 'RO') + (channel.pin+1);
    return module.type.toUpperCase() + ' @0x' + Number(module.address).toString(16).padStart(2,'0') + ' · P' + channel.pin;
  }
  function select(kind, value, optional = false) {
    const node = document.createElement('select');
    node.channelKind = kind; node.channelOptional = optional; node.required = true;
    const option = document.createElement('option'); option.value = String(value); option.textContent = String(value);
    node.append(option); node.value = String(value);
    return node;
  }
  function editor(form, getConfig, getBindings) {
    const host = document.getElementById('resource-map');
    let hardware = null, hardwareMessage = 'Cargando mapa físico…', disposed = false;
    function refresh() {
      if (disposed) return;
      const config = getConfig(), all = entries(config);
      const grouped = {inputs:new Map(),outputs:new Map()};
      for (const entry of all) {
        const group = grouped[entry.kind];
        if (!group.has(entry.channel)) group.set(entry.channel,[]);
        group.get(entry.channel).push(entry);
      }
      const owners = (kind,n) => grouped[kind].get(n) || [];
      const describe = x => x.owner + (x.enabled ? '' : ' (deshabilitado)');
      for (const {node, path} of getBindings()) {
        const kind = node.channelKind, current = Number(node.value);
        const options = [];
        function option(n,text,disabled = false) {
          const item = document.createElement('option'); item.value = String(n); item.textContent = text; item.disabled = disabled; options.push(item);
        }
        if (node.channelOptional) option(0,'Sin comprobación');
        for (let n=1; n<=config[kind].length; n++) {
          const other = owners(kind,n).filter(x => x.path !== path);
          const state = other.length ? 'Ocupado: ' + other.map(describe).join('; ') : n === current ? 'Selección actual' : 'Libre';
          option(n,(kind === 'inputs' ? 'DI' : 'RO') + n + ' — ' + state + (hardware ? ' · ' + physical(hardware,kind,n-1) : ''),other.length > 0 && n !== current);
        }
        if ((!node.channelOptional || current !== 0) && (current < 1 || current > config[kind].length)) option(current,'Canal no válido: ' + current);
        node.replaceChildren(...options); node.value = String(current);
      }
      if (!host) return;
      const summary = document.createElement('summary'); summary.textContent = 'Mapa de pines y relés';
      const note = document.createElement('p'); note.className = 'muted';
      note.textContent = 'Refleja los cambios del formulario antes de guardar. Las asignaciones deshabilitadas se muestran como referencia y se evitan en los selectores. ' + hardwareMessage;
      const content = document.createElement('div'); content.className = 'resource-groups';
      for (const kind of ['inputs','outputs']) {
        const section = document.createElement('section'), title = document.createElement('h3');
        const count = config[kind].length;
        const used = new Set(all.filter(x => x.kind === kind && x.enabled).map(x => x.channel)).size;
        title.textContent = (kind === 'inputs' ? 'Entradas DI' : 'Relés RO') + ' · ' + used + '/' + count + ' en uso'; section.append(title);
        const grid = document.createElement('div'); grid.className = 'resource-grid';
        for (let i=0; i<count; i++) {
          const assigned = owners(kind,i+1), active = assigned.filter(x => x.enabled);
          const card = document.createElement('article'); card.className = 'resource-card ' + (active.length > 1 ? 'conflict' : active.length ? 'occupied' : 'available');
          const name = document.createElement('strong'); name.textContent = (kind === 'inputs' ? 'DI' : 'RO') + (i+1);
          const pin = document.createElement('p'); pin.textContent = physical(hardware,kind,i);
          const owner = document.createElement('p'); owner.textContent = (active.length > 1 ? 'Conflicto · ' : '') + (assigned.length ? assigned.map(describe).join('; ') : 'Libre');
          card.append(name,pin,owner); grid.append(card);
        }
        if (!count) {const empty = document.createElement('p'); empty.textContent = 'Sin canales configurados.'; grid.append(empty);}
        section.append(grid); content.append(section);
      }
      host.replaceChildren(summary,note,content);
    }
    const onClick = event => {if (event.target?.tagName === 'BUTTON') refresh();};
    for (const event of ['input','change']) form.addEventListener(event,refresh);
    form.addEventListener('click',onClick);
    const controller = new AbortController(), timer = setTimeout(() => controller.abort(),5000);
    const ready = (async () => {
      try {
        const response = await fetch('/api/channels',{cache:'no-store',signal:controller.signal});
        if (!response.ok) throw new Error('Mapa no disponible');
        const data = await response.json();
        if (!Array.isArray(data.modules) || !Array.isArray(data.inputs) || !Array.isArray(data.outputs)) throw new Error('Mapa no válido');
        hardware = data; hardwareMessage = 'Pines del hardware activo.';
      } catch {hardwareMessage = 'No se pudo cargar el mapa físico. Puedes seguir editando los canales lógicos.';}
      finally {clearTimeout(timer); refresh();}
    })();
    refresh();
    return {refresh,ready,dispose() {disposed = true; controller.abort(); for (const event of ['input','change']) form.removeEventListener?.(event,refresh); form.removeEventListener?.('click',onClick);}};
  }
  return {usage, free, entries, physical, select, editor};
})();
