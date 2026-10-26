
(() => {
  const form = document.getElementById('config-form');
  const page = form.dataset?.section || 'all';
  const container = document.getElementById('config-fields');
  const message = document.getElementById('config-message');
  const load = document.getElementById('load-config');
  const save = document.getElementById('save-config');
  let controls = null;
  let snapshot;
  let resourceView;
  const draft = () => ({...snapshot, inputs:controls.inputs.map(values), outputs:controls.outputs.map(values), signals:[...otherSignals,...signalEditors.map(e=>e.read())], beacons:[...otherBeacons,...beaconEditors.map(e=>e.read())]});
  let outputCount = 0;
  let busy = false;
  let retryTimer = null;
  let retryCount = 0;
  let signalEditors = [];
  let otherSignals = [];
  let beaconEditors = [];
  let otherBeacons = [];
  const numericPage = page === 'alphanumerics';
  const common = [['enabled','MQTT habilitado','checkbox'],['name','Nombre','text',64],['payloadJson','Payloads en formato JSON','checkbox'],['payloadOn','Payload ON / activo','textarea',256],['payloadOff','Payload OFF / inactivo','textarea',256],['retain','Retener estado en el broker','checkbox']];
  function fields(parent, definitions, values) {
    const grid = document.createElement('div'); grid.className = 'fields'; parent.append(grid);
    const result = {};
    definitions.forEach(([key, title, type, limit]) => {
      const label = document.createElement('label'); label.textContent = title;
      const input = key === 'relay' ? Resources.select('outputs',values[key]) : document.createElement(type === 'textarea' ? 'textarea' : 'input'); if (key !== 'relay') {if (type !== 'textarea') input.type = type; else input.rows = 3;} input.name = key;
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
    return Object.fromEntries(Object.entries(group).map(([key, input]) => [key, input.type === 'checkbox' ? input.checked : (input.type === 'number' || input.channelKind) ? Number(input.value) : input.value]));
  }
  function signalEditor(parent, initial) {
    const box = document.createElement('fieldset'); parent.append(box);
    const legend = document.createElement('legend'); legend.textContent = numericPage ? 'Alfanumérica' : 'Señal'; box.append(legend);
    const base = fields(box, [['enabled','Señal habilitada','checkbox'],['name','Nombre','text',64],['topic','Topic MQTT de aspectos','text',128],['jsonPath','Campo JSON del aspecto','text',64],['blinkMs','Duración de cada fase de parpadeo (ms)','number']].filter(([key]) => !initial.numeric || key !== 'jsonPath'), initial);
    base.blinkMs.min = 250; base.blinkMs.max = 10000;
    const lightsArea = document.createElement('div'); box.append(lightsArea);
    const lights = [];
    const aspects = [];
    const aspectsArea = document.createElement('div');
    function button(label, action) {
      const node = document.createElement('button'); node.type = 'button'; node.textContent = label;
      node.addEventListener('click', action); return node;
    }
    function addLight(item) {
      if (!item) {
        try {item = {name:'Foco ' + (lights.length + 1), relay:Resources.free(draft(),'outputs')[0]};}
        catch (error) {message.textContent = error.message; return;}
      }
      if (lights.length >= 8) return;
      const row = document.createElement('fieldset'); lightsArea.append(row);
      item = {...item, currentMonitor:item.currentMonitor ?? false, ina219Address:item.ina219Address ?? 64, minCurrentMa:item.minCurrentMa ?? 10, maxCurrentMa:item.maxCurrentMa ?? 2000};
      const c = fields(row, [['name','Foco ' + (lights.length + 1),'text',32],['relay','Relé RO (1–' + outputCount + ')','number'],['currentMonitor','Supervisar corriente con INA219','checkbox'],['ina219Address','Dirección INA219 (64–79; 0x40–0x4F)','number'],['minCurrentMa','Corriente mínima esperada (mA)','number'],['maxCurrentMa','Corriente máxima esperada (mA)','number']], item);
      c.relay.min = 1; c.relay.max = outputCount;
      c.ina219Address.min = 64; c.ina219Address.max = 79; c.ina219Address.step = 1;
      c.minCurrentMa.min = 0; c.minCurrentMa.max = 2000; c.minCurrentMa.step = 'any';
      c.maxCurrentMa.min = 0; c.maxCurrentMa.max = 2000; c.maxCurrentMa.step = 'any';
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
      const c = fields(row, [['value',numericPage ? 'Texto del aspecto (ej. VíaLibre)' : 'Valor de Aspecto (ej. VíaLibre)','text',64]], item);
      if (numericPage) c.value.required = true;
      const grid = document.createElement('div'); grid.className = 'fields'; row.append(grid);
      const aspect = {row,c,grid,cells:[]}; aspects.push(aspect);
      lights.forEach((_,i) => addCell(aspect,i,(item.blink || []).includes(i+1) ? 'blink' : (item.on || []).includes(i+1) ? 'on' : 'off'));
      row.append(button('Eliminar aspecto', () => {aspects.splice(aspects.indexOf(aspect),1); row.remove();}));
    }
    (initial.aspects || []).forEach(addAspect);
    box.append(button('Añadir aspecto', () => addAspect()));
    const editor = {bindings:index => lights.map((l,j)=>({node:l.c.relay,path:`signals.${index}.lights.${j}.relay`})), read:() => ({...values(base), ...(initial.id ? {id:initial.id} : {}), numeric:!!initial.numeric, ...(initial.numeric ? {jsonPath:''} : {}), lights:lights.map(l=>values(l.c)), aspects:aspects.map(a=>({value:a.c.value.value,on:a.cells.flatMap((c,i)=>c.select.value==='on'?[i+1]:[]),blink:a.cells.flatMap((c,i)=>c.select.value==='blink'?[i+1]:[])}))})};
    box.append(button('Eliminar señal', () => {signalEditors.splice(signalEditors.indexOf(editor),1); box.remove();}));
    signalEditors.push(editor);
  }
  function beaconEditor(parent, initial, signalChoices) {
    const box = document.createElement('fieldset'); parent.append(box);
    const legend = document.createElement('legend'); legend.textContent = 'Baliza'; box.append(legend);
    const base = fields(box, [['enabled','Baliza habilitada','checkbox'],['name','Nombre','text',64]], initial);
    const signalLabel = document.createElement('label'); signalLabel.textContent = 'Señal asociada';
    const signal = document.createElement('select'); signal.required = true;
    signalChoices.forEach(s => {
      const option = document.createElement('option'); option.value = String(s.id);
      option.textContent = s.name + ' · ' + (s.topic || 'sin topic') + (s.enabled ? '' : ' · deshabilitada'); signal.append(option);
    });
    signal.value = String(initial.signalId ?? signalChoices[0]?.id ?? '');
    signalLabel.append(signal); box.append(signalLabel);
    const relayArea = document.createElement('div'); box.append(relayArea);
    const relays = [];
    const aspectArea = document.createElement('div'); box.append(aspectArea);
    let aspectRows = [];
    function button(label, action) {
      const node = document.createElement('button'); node.type = 'button'; node.textContent = label;
      node.addEventListener('click', action); return node;
    }
    function aspectValues() {
      return aspectRows.map(row => ({
        value:row.value,
        on:row.cells.flatMap((cell,index) => cell.select.value === 'on' ? [index+1] : []),
        blink:row.cells.flatMap((cell,index) => cell.select.value === 'blink' ? [index+1] : [])
      }));
    }
    function addRelay(value) {
      if (relays.length >= 2) return;
      if (!value) {
        try {value = Resources.free(draft(),'outputs')[0];}
        catch (error) {message.textContent = error.message; return;}
      }
      const row = document.createElement('label'); row.textContent = 'Relé baliza ' + (relays.length+1);
      const select = Resources.select('outputs',value); row.append(select); relayArea.append(row);
      relays.push({row,select});
      renderAspects();
    }
    function renderAspects() {
      const saved = new Map(aspectValues().map(item => [item.value,item]));
      aspectArea.replaceChildren(); aspectRows = [];
      const selected = signalChoices.find(s => s.id === Number(signal.value));
      (selected?.aspects || []).forEach(item => {
        const value = typeof item === 'string' ? item : item.value;
        const row = document.createElement('fieldset'); aspectArea.append(row);
        const title = document.createElement('legend'); title.textContent = value; row.append(title);
        const grid = document.createElement('div'); grid.className = 'fields'; row.append(grid);
        const previous = saved.get(value) || (initial.aspects || []).find(a => a.value === value) || {on:[],blink:[]};
        const cells = relays.map((_,index) => {
          const label = document.createElement('label'); label.textContent = 'Relé ' + (index+1);
          const select = document.createElement('select');
          for (const [key,text] of [['off','Apagado'],['on','Fijo'],['blink','Parpadeo']]) {
            const option = document.createElement('option'); option.value = key; option.textContent = text; select.append(option);
          }
          select.value = (previous.blink || []).includes(index+1) ? 'blink' : (previous.on || []).includes(index+1) ? 'on' : 'off';
          label.append(select); grid.append(label); return {select};
        });
        aspectRows.push({value,cells});
      });
    }
    signal.addEventListener('change',renderAspects);
    (initial.relays || []).forEach(addRelay);
    box.append(button('Añadir relé',() => addRelay()),button('Quitar último relé',() => {
      if (relays.length <= 1) return;
      relays.pop().row.remove(); renderAspects();
    }));
    renderAspects();
    const editor = {
      bindings:index => relays.map((relay,j) => ({node:relay.select,path:`beacons.${index}.relays.${j}`})),
      read:() => ({...values(base),signalId:Number(signal.value),relays:relays.map(x => Number(x.select.value)),aspects:aspectValues()})
    };
    box.append(button('Eliminar baliza',() => {beaconEditors.splice(beaconEditors.indexOf(editor),1);box.remove();}));
    beaconEditors.push(editor);
  }
  async function request(options = {}) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), options.method === 'POST' ? 15000 : 5000);
    try {
      const response = await fetch('/api/config', {...options, cache:'no-store', signal:controller.signal});
      if (!response.ok) throw new Error(await response.text());
      return await response.json();
    } finally {clearTimeout(timer);}
  }
  function lock(value) {busy = value; load.disabled = value; save.disabled = value;}
  async function loadConfig(manual = false) {
    if (busy) return;
    if (manual) {clearTimeout(retryTimer); retryCount = 0;}
    lock(true); message.textContent = 'Cargando…';
    try {
      const data = await request();
      if (!data.mqtt || !Array.isArray(data.inputs) || data.inputs.length > 64 || !Array.isArray(data.outputs) || data.outputs.length > 64) throw new Error('Configuración no válida.');
      resourceView?.dispose();
      snapshot = data;
      outputCount = data.outputs.length;
      otherBeacons = page === 'beacons' || page === 'all' ? [] : (data.beacons || []);
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
      const signalsArea = document.createElement('section'); if (page === 'signals' || numericPage || page === 'all') container.append(signalsArea);
      const signalsTitle = document.createElement('h3'); signalsTitle.textContent = numericPage ? 'Señales alfanuméricas' : 'Señales de varios focos'; signalsArea.append(signalsTitle);
      const note = document.createElement('p'); note.textContent = 'Cada foco usa un relé exclusivo. Los relés ocupados no se pueden reutilizar; libera antes su asignación anterior. La supervisión INA219 es opcional; configura un sensor y un rango en mA por foco. El rango se comprueba mientras el relé está encendido. Los valores de aspecto se escriben sin comillas. El guardado detiene el parpadeo hasta recibir otro aspecto. RO asignados deben cablearse según esta tabla.'; signalsArea.append(note);
      otherSignals = page === 'beacons' ? (data.signals || []) : (data.signals || []).filter(signal => page !== 'all' && !!signal.numeric !== numericPage);
      if (page !== 'beacons')
        (data.signals || []).filter(signal => page === 'all' || !!signal.numeric === numericPage).forEach(signal => signalEditor(signalsArea, signal));
      const addSignal = document.createElement('button'); addSignal.type = 'button'; addSignal.textContent = numericPage ? 'Añadir alfanumérica' : 'Añadir señal';
      addSignal.addEventListener('click', () => {
        if (signalEditors.length + otherSignals.length >= 8) {message.textContent = 'Máximo 8 señales entre señales y alfanuméricas.'; return;}
        let relay;
        try {relay = Resources.free(draft(),'outputs')[0];} catch (error) {message.textContent = error.message; return;}
        signalEditor(signalsArea, {numeric:numericPage,enabled:false,name:numericPage ? 'Nueva alfanumérica' : 'Nueva señal',topic:'',jsonPath:numericPage ? '' : 'Aspecto',blinkMs:500,lights:[{name:'Foco 1',relay}],aspects:[{value:'Apagada',on:[],blink:[]}]});
      });
      if (page !== 'beacons') signalsArea.append(addSignal);
      beaconEditors = [];
      const beaconArea = document.createElement('section');
      if (page === 'beacons' || page === 'all') container.append(beaconArea);
      if (page === 'beacons' || page === 'all') {
        const title = document.createElement('h3'); title.textContent = 'Balizas vinculadas a una señal'; beaconArea.append(title);
        const note = document.createElement('p'); note.textContent = 'Cada baliza usa uno o dos relés exclusivos. Se activa con el aspecto de su señal y comparte su cadencia de parpadeo.'; beaconArea.append(note);
        (data.beacons || []).forEach(beacon => beaconEditor(beaconArea,beacon,data.signals || []));
        const addBeacon = document.createElement('button'); addBeacon.type = 'button'; addBeacon.textContent = 'Añadir baliza';
        addBeacon.addEventListener('click',() => {
          if (!(data.signals || []).length) {message.textContent = 'Crea y guarda primero una señal para poder vincular la baliza.'; return;}
          if (beaconEditors.length + otherBeacons.length >= 16) {message.textContent = 'Máximo 16 balizas.'; return;}
          let relay;
          try {relay = Resources.free(draft(),'outputs')[0];}
          catch (error) {message.textContent = error.message; return;}
          const linkedSignal = data.signals.find(signal => signal.enabled) || data.signals[0];
          beaconEditor(beaconArea,{enabled:false,name:'Nueva baliza',signalId:linkedSignal.id,relays:[relay],aspects:(linkedSignal.aspects || []).map(a => ({value:typeof a==='string'?a:a.value,on:[],blink:[]}))},data.signals || []);
        });
        beaconArea.append(addBeacon);
      } else {
        beaconEditors = [];
      }
      const help = document.createElement('p');
      help.textContent = 'JSON: escribe el documento completo para publicar, por ejemplo {"estado":true}. Para recibir un campo, indica su ruta (estado o datos.estado) y valores JSON como true / false o "ON" / "OFF". Con ruta vacía se compara el documento completo, sin importar espacios ni orden de claves. Máximo 256 bytes por payload.';
      if (page === 'inputs' || page === 'outputs' || page === 'all') container.append(help);
      resourceView = Resources.editor(form,draft,()=>[
        ...signalEditors.flatMap((editor,i)=>editor.bindings(otherSignals.length+i)),
        ...beaconEditors.flatMap((editor,i)=>editor.bindings(otherBeacons.length+i))
      ]);
      retryCount = 0;
      form.hidden = false; message.textContent = 'Configuración cargada. Edita y guarda para aplicar.';
    } catch(error) {
      retryCount++;
      if (retryCount <= 3) {
        message.textContent = 'No se pudo cargar la configuración. Reintentando (' + retryCount + '/3)…';
        retryTimer = setTimeout(() => loadConfig(), 1000 * retryCount);
      } else {
        message.textContent = 'No se pudo cargar: ' + error.message + '. Pulsa «Recargar configuración» para volver a intentarlo.';
      }
    }
    finally {lock(false);}
  }
  load.addEventListener('click', () => loadConfig(true));
  if (page !== 'all') loadConfig();
  form.addEventListener('submit', async event => {
    event.preventDefault(); if (busy || !controls) return;
    const data = {...snapshot, mqtt:values(controls.mqtt), inputs:controls.inputs.map(values), outputs:controls.outputs.map(values),signals:[...otherSignals,...signalEditors.map(editor=>editor.read())],beacons:[...otherBeacons,...beaconEditors.map(editor=>editor.read())]};
    const reserved = new Set();
    const reservedSensors = new Set();
    for (const signal of data.signals) {
      const local = new Set();
      for (const light of signal.lights) {
        if (!Number.isInteger(light.relay) || light.relay < 1 || light.relay > outputCount || local.has(light.relay) || (signal.enabled && reserved.has(light.relay))) {message.textContent = "Revisa los relés de las señales: deben ser válidos y exclusivos."; return;}
        local.add(light.relay);
        if (signal.enabled) {reserved.add(light.relay);}
        if (light.currentMonitor) {
          if (!Number.isInteger(light.ina219Address) || light.ina219Address < 64 || light.ina219Address > 79 || !Number.isFinite(light.minCurrentMa) || !Number.isFinite(light.maxCurrentMa) || light.minCurrentMa < 0 || light.maxCurrentMa <= light.minCurrentMa || light.maxCurrentMa > 2000 || reservedSensors.has(light.ina219Address)) {message.textContent = 'Revisa la dirección INA219 y el rango de corriente de cada foco supervisado.'; return;}
          reservedSensors.add(light.ina219Address);
        }
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
        if (!['mqtt','inputs','outputs','signals','alphanumerics','beacons'].includes(page)) throw new Error('Página no válida.');
        if (page === 'signals' || numericPage) {
          const edited = signalEditors.map(editor => editor.read());
          const previousSignals = payload.signals || [];
          const nextSignals = [...previousSignals.filter(signal => !!signal.numeric !== numericPage), ...edited];
          const valueOf = aspect => typeof aspect === 'string' ? aspect : aspect.value;
          for (const beacon of payload.beacons || []) {
            const previous = previousSignals.find(signal => signal.id === beacon.signalId);
            const updated = nextSignals.find(signal => signal.id === beacon.signalId);
            if (!previous || !updated) continue;
            const states = new Map((beacon.aspects || []).map(aspect => [aspect.value,aspect]));
            beacon.aspects = updated.aspects.map((aspect,index) => {
              const state = states.get(valueOf(aspect)) ||
                states.get(valueOf((previous.aspects || [])[index] || ''));
              return {value:valueOf(aspect),on:state?.on || [],blink:state?.blink || []};
            });
          }
          payload.signals = nextSignals;
        } else payload[page] = data[page];

      }
      Resources.usage(payload);
      await request({method:'POST', headers:{'Content-Type':'application/json','X-Interlock':'1'}, body:JSON.stringify(payload)});
      controls.mqtt.password.value = ''; controls.mqtt.clearPassword.checked = false;
      message.textContent = 'Configuración guardada. MQTT aplicará los cambios y volverá a publicar los estados al conectar.';
    } catch(error) {message.textContent = 'No se pudo confirmar el guardado: ' + error.message + '. Puedes cargar la configuración para comprobarla.';}
    finally {lock(false);}
  });
})();
