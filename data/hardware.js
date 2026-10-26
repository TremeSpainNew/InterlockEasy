(() => {
  const byId = id => document.getElementById(id);
  const editor = byId('hardware-json'), message = byId('hardware-message');
  let busy = false, loaded = false, retryCount = 0, retryTimer;
  function lock(value) {
    busy = value; editor.disabled = value || !loaded;
    for (const action of ['save','format','download']) byId('hardware-' + action).disabled = value || !loaded;
    byId('hardware-load').disabled = value;
  }
  async function request(options = {}) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 15000);
    try {
      const response = await fetch('/api/hardware', {...options, cache:'no-store', signal:controller.signal});
      const text = await response.text();
      if (!response.ok) throw new Error(text);
      return text;
    } finally { clearTimeout(timeout); }
  }
  async function load(manual = false) {
    if (busy) return;
    if (manual) {clearTimeout(retryTimer); retryCount = 0;}
    lock(true); message.textContent = 'Cargando archivo guardado…';
    try {
      const text = await request();
      try {editor.value = JSON.stringify(JSON.parse(text), null, 2);} catch {editor.value = text;}
      loaded = true; retryCount = 0;
      message.textContent = 'Archivo cargado. Puede contener cambios pendientes de reinicio.';
    } catch(error) {
      retryCount++;
      if (retryCount <= 3) {
        message.textContent = 'No se pudo cargar. Reintentando (' + retryCount + '/3)…';
        retryTimer = setTimeout(() => load(), retryCount * 1000);
      } else message.textContent = 'No se pudo cargar: ' + error.message + '. Pulsa «Recargar archivo» para volver a intentarlo.';
    }
    finally {lock(false);}
  }
  byId('hardware-load').onclick = () => {
    if (!loaded || confirm('Recargar descartará los cambios del editor. ¿Continuar?')) load(true);
  };
  byId('hardware-format').onclick = () => {
    try {editor.value = JSON.stringify(JSON.parse(editor.value), null, 2); message.textContent = 'JSON formateado; todavía no guardado.';}
    catch(error) {message.textContent = 'JSON no válido: ' + error.message;}
  };
  byId('hardware-download').onclick = () => {
    const url = URL.createObjectURL(new Blob([editor.value], {type:'application/json'}));
    const link = document.createElement('a'); link.href = url; link.download = 'config.json'; link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  };
  byId('hardware-form').onsubmit = async event => {
    event.preventDefault(); if (busy || !loaded) return;
    let body;
    try {body = JSON.stringify(JSON.parse(editor.value));}
    catch(error) {message.textContent = 'JSON no válido: ' + error.message; return;}
    if (new TextEncoder().encode(body).length > 32768) {message.textContent = 'El JSON supera 32 KiB.'; return;}
    lock(true); message.textContent = 'Validando y guardando…';
    try {
      const result = JSON.parse(await request({method:'POST', headers:{'Content-Type':'application/json','X-Interlock':'1'}, body}));
      if (result.saved !== true) throw new Error('Respuesta inesperada');
      message.textContent = result.restarting
        ? 'Guardado. Reiniciando el módulo para aplicar la configuración…'
        : 'Guardado. Reinicia el módulo para aplicar la configuración.';
    } catch(error) {message.textContent = 'No se pudo confirmar el guardado: ' + error.message + '. Conserva tu copia y recarga el archivo para comprobarlo.';}
    finally {lock(false);}
  };
  load();
})();
