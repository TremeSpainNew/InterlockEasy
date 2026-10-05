#pragma once

// Embedded so firmware recovery does not depend on the LittleFS web assets.
static const char FIRMWARE_PAGE[] = R"HTML(<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Actualizar firmware · Interlock Easy</title>
<style>body{font:16px system-ui,sans-serif;max-width:680px;margin:40px auto;padding:0 20px;background:#f4f7fa;color:#183344}form{background:white;padding:24px;border-radius:12px}button,input,progress{display:block;margin:18px 0;max-width:100%}button{padding:12px 20px}progress{width:100%}#message{white-space:pre-wrap}</style></head>
<body><a href="/">← Volver al resumen</a><h1>Actualizar firmware</h1>
<p>Selecciona el archivo <strong>firmware.bin</strong> de Interlock Easy para tu placa. La actualización conserva la configuración y los archivos de la interfaz web.</p>
<p>El módulo se reiniciará al finalizar. Realiza la actualización con la instalación detenida y mantén la alimentación durante el proceso.</p>
<form id="update-form"><label for="firmware">Firmware (.bin)</label><input id="firmware" type="file" accept=".bin,application/octet-stream" required>
<button id="upload" type="submit">Subir e instalar firmware</button><progress id="progress" max="100" value="0" aria-label="Progreso de subida"></progress><p id="message" role="status" aria-live="polite"></p></form>
<script>
const form=document.getElementById('update-form'),input=document.getElementById('firmware'),button=document.getElementById('upload'),progress=document.getElementById('progress'),message=document.getElementById('message');
let busy=false;
form.onsubmit=event=>{
 event.preventDefault();if(busy)return;const file=input.files[0];
 if(!file||!file.name.toLowerCase().endsWith('.bin')||!file.size){message.textContent='Selecciona un archivo firmware.bin válido.';return;}
 busy=true;button.disabled=true;input.disabled=true;progress.value=0;message.textContent='Subiendo firmware…';
 const xhr=new XMLHttpRequest();xhr.open('POST','/api/firmware');xhr.timeout=300000;
 xhr.setRequestHeader('Content-Type','application/octet-stream');xhr.setRequestHeader('X-Interlock','1');
 xhr.upload.onprogress=e=>{if(e.lengthComputable){progress.value=100*e.loaded/e.total;if(e.loaded===e.total)message.textContent='Subida completa. Verificando e instalando…';}};
 const unlock=()=>{busy=false;button.disabled=false;input.disabled=false;};
 xhr.onload=()=>{if(xhr.status===200){progress.value=100;message.textContent='Firmware instalado. El módulo se está reiniciando. Espera unos segundos y vuelve al resumen.';busy=false;}else{message.textContent='No se pudo actualizar: '+xhr.responseText;unlock();}};
 xhr.onerror=()=>{message.textContent='Se perdió la conexión. Comprueba el módulo antes de volver a intentarlo.';unlock();};
 xhr.ontimeout=()=>{message.textContent='Tiempo de espera agotado. Comprueba el módulo antes de volver a intentarlo.';unlock();};
 xhr.send(file);
};
window.addEventListener('beforeunload',e=>{if(busy){e.preventDefault();e.returnValue='';}});
</script></body></html>)HTML";
