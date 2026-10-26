#!/usr/bin/env python3
"""Parche reversible HTTP multicliente para Interlock Easy (Ethernet 2.0.2).
Uso desde la raíz del proyecto: python3 aplicar_http_v2.py
No requiere dependencias externas. Crea .bak antes de modificar.
"""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]) if len(sys.argv)>1 else Path.cwd()
h = root/'src/WebManager.h'
c = root/'src/WebManager.cpp'
if not h.is_file() or not c.is_file():
    sys.exit('ERROR: ejecuta desde la raíz de InterlockEasy (debe existir src/WebManager.cpp).')
header=h.read_text(encoding='utf-8')
source=c.read_text(encoding='utf-8')
if 'HTTP_V2_MULTICLIENT' in header or 'HTTP_V2_MULTICLIENT' in source:
    sys.exit('HTTP v2 ya aplicado. No se ha modificado nada.')

slot_struct='''
    // HTTP_V2_MULTICLIENT: hasta cuatro clientes HTTP en paralelo.
    // Se reservan los otros sockets del W5500 para MQTT, Modbus, etc.
    static constexpr uint8_t HTTP_SLOTS = 4;
    struct HttpSlot {
        EthernetClient ethernetClient;
        WiFiClient wifiClient;
        File file;
        bool occupied = false;
        bool responding = false;
        unsigned long started = 0;
        String request;
        String pending;
        size_t offset = 0;
        bool readingBody = false;
        size_t contentLength = 0;
        String body;
        bool testRequest = false;
        bool hardwareRequest = false;
        bool firmwareRequest = false;
        bool filesystemRequest = false;
        size_t firmwareReceived = 0;
    };
    HttpSlot httpSlots[HTTP_SLOTS];
    uint8_t nextHttpSlot = 0;
    void swapHttpSlot(HttpSlot& slot);
    void processHttpClient();
'''
needle='    EthernetServer ethernetServer{80};'
if header.count(needle)!=1: sys.exit('ERROR: versión de WebManager.h desconocida.')
header=header.replace(needle,slot_struct+'\n'+needle,1)

# Intercambia el contexto de cada sesión con los campos ya existentes.
# De esta forma se conservan intactos dispatch(), OTA, JSON y LittleFS.
swap_code='''
// HTTP_V2_MULTICLIENT: guarda/restaura el estado de cada cliente sin
// duplicar las funciones existentes de despacho y transmisión.
void WebManager::swapHttpSlot(HttpSlot& slot) {
    using std::swap;
    swap(ethernetClient, slot.ethernetClient);
    swap(wifiClient, slot.wifiClient);
    swap(file, slot.file);
    swap(responding, slot.responding);
    swap(started, slot.started);
    swap(request, slot.request);
    swap(pending, slot.pending);
    swap(offset, slot.offset);
    swap(readingBody, slot.readingBody);
    swap(contentLength, slot.contentLength);
    swap(body, slot.body);
    swap(testRequest, slot.testRequest);
    swap(hardwareRequest, slot.hardwareRequest);
    swap(firmwareRequest, slot.firmwareRequest);
    swap(filesystemRequest, slot.filesystemRequest);
    swap(firmwareReceived, slot.firmwareReceived);
}

void WebManager::loop() {
    if(restartPending && (long)(millis()-restartAt)>=0) {
        restartPending=false;
        Serial.println("Reiniciando Interlock Easy...");
        ESP.restart();
        return;
    }

    // Recoger una conexión nueva por vuelta si hay espacio. No bloquear.
    for(uint8_t i=0; i<HTTP_SLOTS; ++i) {
        HttpSlot& slot=httpSlots[i];
        if(slot.occupied) continue;
        if(Connectivity.wifi()) {
            slot.wifiClient=wifiServer.accept();
            if(!slot.wifiClient) continue;
        } else {
            slot.ethernetClient=ethernetServer.accept();
            if(!slot.ethernetClient) continue;
        }
        slot.occupied=true;
        slot.started=millis();
        break;
    }

    // Round-robin: cada cliente recibe un presupuesto limitado por vuelta.
    // Las tareas de MQTT, Modbus y E/S siguen ejecutándose en main.cpp.
    for(uint8_t step=0; step<HTTP_SLOTS; ++step) {
        const uint8_t i=(nextHttpSlot+step)%HTTP_SLOTS;
        HttpSlot& slot=httpSlots[i];
        if(!slot.occupied) continue;
        swapHttpSlot(slot);
        client=Connectivity.wifi()?static_cast<Client*>(&wifiClient):static_cast<Client*>(&ethernetClient);
        processHttpClient();
        slot.occupied=(client!=nullptr);
        client=nullptr;
        swapHttpSlot(slot);
    }
    nextHttpSlot=(nextHttpSlot+1)%HTTP_SLOTS;
}

void WebManager::processHttpClient(){
'''
# Retain the original HTTP processing loop, replacing only the accept/start block.
start=source.find('void WebManager::loop(){')
end=source.find('\nvoid WebManager::hardwareConfig(){',start)
if start<0 or end<0:sys.exit('ERROR: no se encontró el bucle HTTP original.')
loop=source[start:end]
if 'receiveFirmware()' not in loop or 'availableForWrite()' not in loop:
    sys.exit('ERROR: bucle HTTP no reconocido; no se modifica nada.')
accept_pattern=r'\s*if\(!client\)\{if\(Connectivity\.wifi\(\)\)\{wifiClient=wifiServer\.accept\(\);if\(wifiClient\)client=&wifiClient;\}else\{ethernetClient=ethernetServer\.(?:available|accept)\(\);if\(ethernetClient\)client=&ethernetClient;\}if\(!client\)return;started=millis\(\);\}'
new_loop,n=re.subn(accept_pattern,'\n    if(!client)return;',loop,count=1)
if n!=1:sys.exit('ERROR: bloque de aceptación no reconocido; no se modifica nada.')
# Turn the old loop body into the per-client processor.
new_loop=new_loop.replace('void WebManager::loop(){','',1)
# Restart is a global concern handled by the new loop, not each client.
new_loop,n=re.subn(r'\s*if\(restartPending&&\(long\)\(millis\(\)-restartAt\)>=0\)\{restartPending=false;Serial\.println\("Reiniciando Interlock Easy\.\.\."\);ESP\.restart\(\);return;\}', '', new_loop,count=1)
if n!=1:sys.exit('ERROR: bloque de reinicio no reconocido.')
# Ensure source has std::swap available.
if '#include <utility>' not in source: source=source.replace('#include "WebManager.h"','#include "WebManager.h"\n#include <utility>',1)
source=source[:source.find('void WebManager::loop(){')]+swap_code+new_loop+source[end:]

# Backup before touching either file.
for path in (h,c):
    backup=path.with_suffix(path.suffix+'.bak')
    if not backup.exists():backup.write_bytes(path.read_bytes())
h.write_text(header,encoding='utf-8')
c.write_text(source,encoding='utf-8')
print('HTTP v2 aplicado a:')
print('  ',h)
print('  ',c)
print('Copias originales: WebManager.h.bak y WebManager.cpp.bak')
print('Compila con: pio run')
