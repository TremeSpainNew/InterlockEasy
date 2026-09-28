(() => {
  const form = document.getElementById('detection-form');
  const area = document.getElementById('track-editors');
  const message = document.getElementById('config-message');
  const loadButton = document.getElementById('load-config');
  const saveButton = document.getElementById('save-config');
  const editors = [];
  let inputCount = 0, busy = false;

  async function request(options = {}) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), options.method === 'POST' ? 15000 : 5000);
    try {
      const response = await fetch('/api/config', {...options, cache:'no-store', signal:controller.signal});
      if (!response.ok) throw new Error(await response.text());
      return response.json();
    } finally { clearTimeout(timer); }
  }
  function lock(value) { busy = value; loadButton.disabled = value; saveButton.disabled = value; }
  function field(parent, labelText, kind, value, maxLength) {
    const label = document.createElement('label'); label.textContent = labelText;
    const node = document.createElement(kind === 'textarea' ? 'textarea' : kind === 'select' ? 'select' : 'input');
    if (kind === 'checkbox') { node.type = 'checkbox'; node.checked = !!value; }
    else if (kind === 'number') { node.type = 'number'; node.value = value; node.min = 1; node.max = inputCount; }
    else if (kind !== 'select') { node.value = value ?? ''; if (maxLength) node.maxLength = maxLength; }
    if (kind === 'textarea') node.rows = 3;
    label.append(node); parent.append(label); return node;
  }
  function addEditor(initial = {}) {
    if (editors.length >= 16) { message.textContent = 'Máximo 16 tramos.'; return; }
    const box = document.createElement('fieldset');
    const legend = document.createElement('legend'); legend.textContent = 'Tramo ' + (editors.length + 1); box.append(legend);
    const grid = document.createElement('div'); grid.className = 'fields'; box.append(grid);
    const c = {};
    c.enabled = field(grid, 'Tramo habilitado', 'checkbox', initial.enabled);
    c.type = field(grid, 'Tipo', 'select');
    for (const [value, text] of [['linear','CV lineal'],['axleCounter','Cuenta ejes']]) { const option=document.createElement('option'); option.value=value; option.textContent=text; c.type.append(option); }
    c.type.value = initial.type || 'linear';
    c.name = field(grid, 'Nombre', 'text', initial.name || 'Nuevo tramo', 64);
    c.inputA = field(grid, 'Entrada A / CV', 'number', initial.inputA || 1);
    c.inputB = field(grid, 'Entrada B (cuenta ejes)', 'number', initial.inputB || Math.min(2,inputCount));
    c.stateTopic = field(grid, 'Topic MQTT de estado', 'text', initial.stateTopic || '', 128);
    c.countTopic = field(grid, 'Topic MQTT del contador', 'text', initial.countTopic || '', 128);
    c.payloadOccupied = field(grid, 'Payload ocupado', 'textarea', initial.payloadOccupied || '{"Estado":"Ocupado"}', 256);
    c.payloadFree = field(grid, 'Payload libre', 'textarea', initial.payloadFree || '{"Estado":"Libre"}', 256);
    c.retain = field(grid, 'Retener publicaciones', 'checkbox', initial.retain ?? true);
    const remove = document.createElement('button'); remove.type='button'; remove.textContent='Eliminar tramo'; box.append(remove);
    const editor = {box,c}; editors.push(editor);
    remove.onclick = () => { editors.splice(editors.indexOf(editor),1); box.remove(); };
    const update = () => { const axle=c.type.value==='axleCounter'; c.inputB.parentElement.hidden=!axle; c.countTopic.parentElement.hidden=!axle; };
    c.type.onchange=update; update(); area.append(box);
  }
  function read(editor) {
    const c=editor.c; return {enabled:c.enabled.checked,type:c.type.value,name:c.name.value,inputA:Number(c.inputA.value),inputB:Number(c.inputB.value),stateTopic:c.stateTopic.value,countTopic:c.countTopic.value,payloadOccupied:c.payloadOccupied.value,payloadFree:c.payloadFree.value,retain:c.retain.checked};
  }
  async function load() {
    if (busy) return; lock(true); message.textContent='Cargando configuración…';
    try {
      const data=await request(); if (!Array.isArray(data.inputs) || !Array.isArray(data.trackSections)) throw new Error('Respuesta no válida');
      inputCount=data.inputs.length; editors.splice(0); area.replaceChildren(); data.trackSections.forEach(addEditor);
      form.hidden=false; message.textContent='Configuración cargada.';
    } catch(error) { message.textContent='No se pudo cargar: '+error.message; }
    finally { lock(false); }
  }
  document.getElementById('add-track').onclick=()=>addEditor(); loadButton.onclick=load;
  form.onsubmit=async event => {
    event.preventDefault(); if (busy) return;
    const tracks=editors.map(read), used=new Set();
    for (const track of tracks) {
      const axle=track.type==='axleCounter';
      if (!track.name.trim() || !Number.isInteger(track.inputA) || track.inputA<1 || track.inputA>inputCount || (axle && (!Number.isInteger(track.inputB) || track.inputB<1 || track.inputB>inputCount || track.inputA===track.inputB)) || track.payloadOccupied===track.payloadFree) { message.textContent='Revisa nombres, entradas y payloads.'; return; }
      if (track.enabled && (!track.stateTopic || (axle && !track.countTopic))) { message.textContent='Los tramos habilitados necesitan sus topics MQTT.'; return; }
      if (track.enabled) for (const input of axle?[track.inputA,track.inputB]:[track.inputA]) { if (used.has(input)) {message.textContent='Dos tramos habilitados no pueden compartir entradas.'; return;} used.add(input); }
    }
    lock(true); message.textContent='Guardando…';
    try { const data=await request(); data.trackSections=tracks; await request({method:'POST',headers:{'Content-Type':'application/json','X-Interlock':'1'},body:JSON.stringify(data)}); message.textContent='Configuración guardada y aplicada.'; }
    catch(error) { message.textContent='No se pudo guardar: '+error.message; }
    finally { lock(false); }
  };
  load();
})();
