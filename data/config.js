
(() => {
  const form = document.getElementById('config-form');
  const page = form.dataset?.section || 'all';
  const container = document.getElementById('config-fields');
  const message = document.getElementById('config-message');
  const load = document.getElementById('load-config');
  const save = document.getElementById('save-config');
  let controls = null;
  let outputCount = 0;
  let busy = false;
  let signalEditors = [];
  const common = [['enabled','MQTT habilitado','checkbox'],['name','Nombre','text',64],['payloadJson','Payloads en formato JSON','checkbox'],['payloadOn','Payload ON / activo','textarea',256],['payloadOff','Payload OFF / inactivo','textarea',256],['retain','Retener estado en el broker','checkbox']];
  function fields(parent, definitions, values) {
    const grid = document.createElement('div'); grid.className = 'fields'; parent.append(grid);
    const result = {};
    definitions.forEach(([key, title, type, limit]) => {
      const label = document.createElement('label'); label.textContent = title;
      const input = document.createElement(type === 'textarea' ? 'textarea' : 'input'); if (type !== 'textarea') input.type = type; else input.rows = 3; input.name = key;
      if (type === 'checkbox') input.checked = !!values[key];
      else input.value = values[key] ?? '';
      if (limit) input.maxLength = limit;
      if (type === 'number') {input.min = key === 'port' ? 1 : 5; input.max = key === 'port' ? 65535 : 3600; input.required = true;}
      if (key === 'debounceMs') {input.min = 0; input.max = 5000; input.value = values[key] ?? 50;}
      if (key === 'clientId') input.required = true;
      if (type === 'password') input.autocomplete = 'new-password';
      label.append(input); grid.append(label); result[key] = input;
    });
    return result;
  }
  function values(group) {
    return Object.fromEntries(Object.entries(group).map(([key, input]) => [key, input.type === 'checkbox' ? input.checked : input.type === 'number' ? Number(input.value) : input.value]));
  }
  function signalEditor(parent, initial) {
    const box = document.createElement('fieldset'); parent.append(box);
    const legend = document.createElement('legend'); legend.textContent = 'Señal'; box.append(legend);
    const base = fields(box, [['enabled','Señal habilitada','checkbox'],['name','Nombre','text',64],['topic','Topic MQTT de aspectos','text',128],['jsonPath','Campo JSON del aspecto','text',64],['blinkMs','Duración de cada fase de parpadeo (ms)','number']], initial);
    base.blinkMs.min = 250; base.blinkMs.max = 10000;
    const lightsArea = document.createElement('div'); box.append(lightsArea);
    const lights = [];
    const aspects = [];
    const aspectsArea = document.createElement('div');
    function button(label, action) {
      const node = document.createElement('button'); node.type = 'button'; node.textContent = label;
      node.addEventListener('click', action); return node;
    }
    function addLight(item = {name:'Foco ' + (lights.length + 1), relay:lights.length + 1}) {
      if (lights.length >= 8) return;
      const row = document.createElement('fieldset'); lightsArea.append(row);
      const c = fields(row, [['name','Foco ' + (lights.length + 1),'text',32],['relay','Relé RO (1–' + outputCount + ')','number']], item);
      c.relay.min = 1; c.relay.max = outputCount;
      lights.push({row,c});
      aspects.forEach(a => addCell(a, lights.length - 1, 'off'));
    }
    function addCell(aspect, index, state) {
      const label = document.createElement('label'); label.textContent = 'Foco ' + (index + 1);
      const select = document.createElement('select');
      for (const [value,text] of [['off','Apagado'],['on','Fijo'],['blink','Parpadeo']]) {
        const option = document.createElement('option'); option.value = value; option.textContent = text; select.append(option);
      }
      select.value = state; label.append(select); aspect.grid.append(label); aspect.cells.push({label,select});
    }
    (initial.lights || []).forEach(addLight);
    box.append(button('Añadir foco', () => addLight()), button('Quitar último foco', () => {
      if (lights.length <= 1) return;
      lights.pop().row.remove(); aspects.forEach(a => a.cells.pop().label.remove());
    }));
    box.append(aspectsArea);
    function addAspect(item = {value:'',on:[],blink:[]}) {
      if (aspects.length >= 12) return;
      const row = document.createElement('fieldset'); aspectsArea.append(row);
      const c = fields(row, [['value','Valor de Aspecto (ej. VíaLibre)','text',64]], item);
      const grid = document.createElement('div'); grid.className = 'fields'; row.append(grid);
      const aspect = {row,c,grid,cells:[]}; aspects.push(aspect);
      lights.forEach((_,i) => addCell(aspect,i,(item.blink || []).includes(i+1) ? 'blink' : (item.on || []).includes(i+1) ? 'on' : 'off'));
      row.append(button('Eliminar aspecto', () => {aspects.splice(aspects.indexOf(aspect),1); row.remove();}));
    }
    (initial.aspects || []).forEach(addAspect);
    box.append(button('Añadir aspecto', () => addAspect()));
    const editor = {read:() => ({...values(base), lights:lights.map(l=>values(l.c)), aspects:aspects.map(a=>({value:a.c.value.value,on:a.cells.flatMap((c,i)=>c.select.value==='on'?[i+1]:[]),blink:a.cells.flatMap((c,i)=>c.select.value==='blink'?[i+1]:[])}))})};
    box.append(button('Eliminar señal', () => {signalEditors.splice(signalEditors.indexOf(editor),1); box.remove();}));
    signalEditors.push(editor);
  }
  async function request(options) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 15000);
    try {
      const response = await fetch('/api/config', {...options, cache:'no-store', signal:controller.signal});
      if (!response.ok) throw new Error(await response.text());
      return await response.json();
    } finally {clearTimeout(timer);}
  }
  function lock(value) {busy = value; load.disabled = value; save.disabled = value;}
  async function loadConfig() {
    if (busy) return;
    lock(true); message.textContent = 'Cargando…';
    try {
      const data = await request();
      if (!data.mqtt || !Array.isArray(data.inputs) || data.inputs.length > 32 || !Array.isArray(data.outputs) || data.outputs.length > 32) throw new Error('Configuración no válida.');
      outputCount = data.outputs.length;
      container.replaceChildren();
      const broker = document.createElement('fieldset');
      const legend = document.createElement('legend'); legend.textContent = 'Broker MQTT'; broker.append(legend); if (page === 'mqtt' || page === 'all') container.append(broker);
      controls = {mqtt:fields(broker, [['host','Servidor (vacío desactiva MQTT)','text',128],['port','Puerto','number'],['clientId','Client ID','text',64],['username','Usuario','text',128],['password','Nueva contraseña (vacío conserva la actual)','password',128],['keepAlive','Keep alive (segundos)','number'],['clearPassword','Borrar contraseña guardada','checkbox']], data.mqtt), inputs:[], outputs:[]};
      const passwordNote = document.createElement('p'); passwordNote.textContent = data.mqtt.passwordSet ? 'Hay una contraseña guardada.' : 'No hay contraseña guardada.'; broker.append(passwordNote);
      for (const kind of ['inputs','outputs']) {
        const heading = document.createElement('h3'); heading.textContent = kind === 'inputs' ? 'Entradas digitales' : 'Salidas independientes'; if (page === kind || page === 'all') container.append(heading);
        data[kind].forEach((item, i) => {
        const section = document.createElement('details');
        const summary = document.createElement('summary'); summary.textContent = (kind === 'inputs' ? 'Entrada DI' : 'Relé RO') + (i + 1); section.append(summary); if (page === kind || page === 'all') container.append(section);
        const extra = kind === 'inputs' ? [['topic','Topic de publicación','text',128],['inverted','Invertir entrada','checkbox'],['debounceMs','Antirrebote (ms; 0 desactiva)','number']] : [['commandTopic','Topic de mando','text',128],['jsonPath','Campo JSON recibido (vacío: mensaje completo)','text',64],['publishState','Publicar estado','checkbox'],['stateTopic','Topic de estado','text',128],['stateJson','Publicar estado en JSON','checkbox'],['stateOn','Payload de estado ON','textarea',256],['stateOff','Payload de estado OFF','textarea',256]];
        controls[kind].push(fields(section, [...common, ...extra], item));
        });
      }
      signalEditors = [];
      const signalsArea = document.createElement('section'); if (page === 'signals' || page === 'all') container.append(signalsArea);
      const signalsTitle = document.createElement('h3'); signalsTitle.textContent = 'Señales de varios focos'; signalsArea.append(signalsTitle);
      const note = document.createElement('p'); note.textContent = 'Cada foco usa un relé exclusivo. Al guardar, los mandos individuales de los relés de señales habilitadas se desactivan. Los valores de aspecto se escriben sin comillas. El guardado detiene el parpadeo hasta recibir otro aspecto. RO asignados deben cablearse según esta tabla.'; signalsArea.append(note);
      (data.signals || []).forEach(signal => signalEditor(signalsArea, signal));
      const addSignal = document.createElement('button'); addSignal.type = 'button'; addSignal.textContent = 'Añadir señal';
      addSignal.addEventListener('click', () => {
        if (signalEditors.length >= 8) {message.textContent = 'Máximo 8 señales.'; return;}
        signalEditor(signalsArea, {enabled:false,name:'Nueva señal',topic:'',jsonPath:'Aspecto',blinkMs:500,lights:[{name:'Foco 1',relay:1}],aspects:[{value:'Apagada',on:[],blink:[]}]});
      });
      signalsArea.append(addSignal);
      const help = document.createElement('p');
      help.textContent = 'JSON: escribe el documento completo para publicar, por ejemplo {"estado":true}. Para recibir un campo, indica su ruta (estado o datos.estado) y valores JSON como true / false o "ON" / "OFF". Con ruta vacía se compara el documento completo, sin importar espacios ni orden de claves. Máximo 256 bytes por payload.';
      if (page === 'inputs' || page === 'outputs' || page === 'all') container.append(help);
      form.hidden = false; message.textContent = 'Configuración cargada. Edita y guarda para aplicar.';
    } catch(error) {message.textContent = 'No se pudo cargar: ' + error.message;}
    finally {lock(false);}
  }
  load.addEventListener('click', loadConfig);
  if (page !== 'all') loadConfig();
  form.addEventListener('submit', async event => {
    event.preventDefault(); if (busy || !controls) return;
    const data = {mqtt:values(controls.mqtt), inputs:controls.inputs.map(values), outputs:controls.outputs.map(values),signals:signalEditors.map(editor=>editor.read())};
    const reserved = new Set();
    for (const signal of data.signals) {
      const local = new Set();
      for (const light of signal.lights) {
        if (!Number.isInteger(light.relay) || light.relay < 1 || light.relay > outputCount || local.has(light.relay) || (signal.enabled && reserved.has(light.relay))) {message.textContent = "Revisa los relés de las señales: deben ser válidos y exclusivos."; return;}
        local.add(light.relay);
        if (signal.enabled) {reserved.add(light.relay); data.outputs[light.relay-1].enabled = false;}
      }
    }
    if (data.mqtt.clearPassword && data.mqtt.password) {message.textContent = 'Elige entre borrar o sustituir la contraseña.'; return;}
    if (!data.mqtt.clearPassword && !data.mqtt.password) delete data.mqtt.password;
    delete data.mqtt.clearPassword;
    lock(true); message.textContent = 'Guardando…';
    try {
      // Refresh other sections so saving one page cannot overwrite their newer settings.
      let payload = data;
      if (page !== 'all') {
        payload = await request();
        if (!['mqtt','inputs','outputs','signals'].includes(page)) throw new Error('Página no válida.');
        payload[page] = data[page];
        if (page === 'signals') {
          const occupied = new Set();
          for (const signal of payload.signals) if (signal.enabled)
            for (const light of signal.lights) occupied.add(light.relay);
          occupied.forEach(relay => {payload.outputs[relay-1].enabled = false;});
        }
      }
      await request({method:'POST', headers:{'Content-Type':'application/json','X-Interlock':'1'}, body:JSON.stringify(payload)});
      reserved.forEach(relay => {controls.outputs[relay-1].enabled.checked = false;});
      controls.mqtt.password.value = ''; controls.mqtt.clearPassword.checked = false;
      message.textContent = 'Configuración guardada. MQTT aplicará los cambios y volverá a publicar los estados al conectar.';
    } catch(error) {message.textContent = 'No se pudo confirmar el guardado: ' + error.message + '. Puedes cargar la configuración para comprobarla.';}
    finally {lock(false);}
  });
})();
