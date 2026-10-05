#include "WebManager.h"
#include "ConfigManager.h"
#include "EthernetManager.h"
#include "IOManager.h"
#include "MqttManager.h"
#include "SignalManager.h"
#include "DetectionManager.h"
#include "TurnoutManager.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Update.h>
#include "FirmwarePage.h"
#include "HardwareConfig.h"

WebManager Web;

void WebManager::begin(){
    filesystemReady=Hardware.filesystemReady;
    if(!filesystemReady)Serial.println("LittleFS no disponible: cargar con pio run -t uploadfs.");
    if(Connectivity.wifi())wifiServer.begin();else ethernetServer.begin();
    Serial.println("HTTP disponible en puerto 80.");
}
void WebManager::close(){
    if(firmwareRequest && Update.isRunning())Update.abort();
    firmwareRequest=false;firmwareReceived=0;
    if(file)file.close();if(client)client->stop();client=nullptr;request="";pending="";offset=0;
    responding=false;readingBody=false;testRequest=false;hardwareRequest=false;contentLength=0;body="";
}
void WebManager::headers(int code,const char* reason,const char* type,size_t length){
    pending="HTTP/1.1 "+String(code)+" "+reason+"\r\nContent-Type: "+type;
    pending+="\r\nContent-Length: "+String(length);
    pending+="\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n";
    if(code==405)pending+="Allow: GET, POST\r\n";
    pending+="\r\n";offset=0;responding=true;
}
void WebManager::respond(int code,const char* reason,const char* type,const String& value){headers(code,reason,type,value.length());pending+=value;}

void WebManager::status(){
    DynamicJsonDocument doc(49152);
    doc["inputCount"]=NUM_INPUTS;doc["outputCount"]=NUM_OUTPUTS;doc["uptimeMs"]=millis();
    doc["ethernet"]["connected"]=Connectivity.connected();doc["ethernet"]["ip"]=Connectivity.ip().toString();
    doc["network"]["type"]=Connectivity.wifi()?"wifi":"ethernet";
    doc["mqtt"]["connected"]=MQTT.connected();doc["mqtt"]["configured"]=!Config.mqtt.host.isEmpty();
    doc["modbus"]["role"]=Hardware.modbus.role==ModbusRole::MASTER?"master":Hardware.modbus.role==ModbusRole::SLAVE?"slave":"off";
    doc["filesystemReady"]=filesystemReady;doc["outputsReady"]=IO.outputsReady();

    JsonArray ins=doc.createNestedArray("inputs");
    for(uint8_t i=0;i<NUM_INPUTS;++i){
        JsonObject x=ins.createNestedObject();x["channel"]=i+1;x["name"]=Config.inputs[i].name;x["enabled"]=Config.inputs[i].enabled;
        if(IO.inputReady(i))x["state"]=IO.getInput(i);else x["state"]=nullptr;
        x["simulated"]=IO.inputSimulated(i);x["raw"]=IO.getRawInput(i);x["filtering"]=IO.inputFiltering(i);
        x["publishPending"]=MQTT.inputPending(i);x["inverted"]=Config.inputs[i].inverted;x["debounceMs"]=Config.inputs[i].debounceMs;
        x["reserved"]=Config.inputAssigned(i);
    }

    JsonArray outs=doc.createNestedArray("outputs");
    for(uint8_t i=0;i<NUM_OUTPUTS;++i){
        JsonObject x=outs.createNestedObject();x["channel"]=i+1;x["name"]=Config.outputs[i].name;x["enabled"]=Config.outputs[i].enabled;
        for(const auto& s:Config.signals)if(s.enabled&&(s.relayMask()&(uint64_t(1)<<i)))x["signal"]=s.name;
        if(IO.outputReady(i))x["state"]=IO.getOutput(i);else x["state"]=nullptr;
    }

    JsonArray signals=doc.createNestedArray("signals");
    for(size_t i=0;i<Config.signals.size();++i){
        const auto& s=Config.signals[i];JsonObject x=signals.createNestedObject();
        x["name"]=s.name;x["enabled"]=s.enabled;x["aspect"]=Signals.aspect(i);x["channel"]=i+1;
        JsonArray aspects=x.createNestedArray("aspects");for(const auto& a:s.aspects)aspects.add(a.value);
        JsonArray lights=x.createNestedArray("lights");for(const auto& l:s.lights){JsonObject e=lights.createNestedObject();e["name"]=l.name;e["relay"]=l.relay;if(IO.outputReady(l.relay-1))e["state"]=IO.getOutput(l.relay-1);else e["state"]=nullptr;}
    }

    JsonArray cvs=doc.createNestedArray("cvs");
    for(size_t i=0;i<Config.cvs.size();++i){
        const auto& c=Config.cvs[i];const auto* s=Detections.cvState(i);JsonObject x=cvs.createNestedObject();
        x["channel"]=i+1;x["enabled"]=c.enabled;x["station"]=c.station;x["id"]=c.id;x["input"]=c.input;x["topic"]=c.topic();
        x["valid"]=s?s->valid:false;if(s&&s->valid)x["occupied"]=s->occupied;else x["occupied"]=nullptr;
    }

    JsonArray ces=doc.createNestedArray("axleCounters");
    for(size_t i=0;i<Config.axleCounters.size();++i){
        const auto& c=Config.axleCounters[i];const auto* s=Detections.axleState(i);JsonObject x=ces.createNestedObject();
        x["channel"]=i+1;x["enabled"]=c.enabled;x["station"]=c.station;x["id"]=c.id;x["inputA"]=c.inputA;x["inputB"]=c.inputB;x["topic"]=c.topic();
        x["lastEvent"]=s?s->lastEvent:"";x["error"]=s?s->error:false;
    }

    JsonArray turnouts=doc.createNestedArray("turnouts");
    for(size_t i=0;i<Config.turnouts.size();++i){
        const auto& c=Config.turnouts[i];const auto* st=Turnouts.state(i);JsonObject x=turnouts.createNestedObject();
        x["channel"]=i+1;x["enabled"]=c.enabled;x["station"]=c.station;x["id"]=c.id;
        x["outputNormal"]=c.outputNormal;x["outputReverse"]=c.outputReverse;
        x["inputNormal"]=c.inputNormal;x["inputReverse"]=c.inputReverse;
        x["drive"]=c.drive==TurnoutDrive::MAINTAINED?"maintained":"pulse";
        x["commandTopic"]=c.commandTopic();x["feedbackTopic"]=c.feedbackTopic();
        x["commanded"]=st?st->commanded:-1;x["feedback"]=st?st->feedback:-2;
    }

    if(doc.overflowed()){respond(500,"Internal Server Error","application/json","{\"error\":\"status capacity exceeded\"}");return;}
    String value;serializeJson(doc,value);respond(200,"OK","application/json; charset=utf-8",value);
}

void WebManager::dispatch(){
    const int end=request.indexOf("\r\n");const String line=request.substring(0,end);
    const int first=line.indexOf(' '),second=line.indexOf(' ',first+1);
    if(end<0||first<=0||second<=first+1||(line.substring(second+1)!="HTTP/1.1"&&line.substring(second+1)!="HTTP/1.0")){respond(400,"Bad Request","text/plain","Peticion no valida.");return;}
    const String method=line.substring(0,first);String path=request.substring(first+1,second);const int query=path.indexOf('?');if(query>=0)path.remove(query);

    if(method=="POST"&&(path=="/api/config"||path=="/api/test"||path=="/api/hardware"||path=="/api/firmware")){
        testRequest=path=="/api/test";hardwareRequest=path=="/api/hardware";firmwareRequest=path=="/api/firmware";bool hasLength=false,json=false,binary=false,token=false;
        for(int pos=end+2;pos<int(request.length())-2;){
            const int next=request.indexOf("\r\n",pos);if(next<0)break;String h=request.substring(pos,next);const int colon=h.indexOf(':');
            if(colon<=0){respond(400,"Bad Request","text/plain","Cabecera no valida.");return;}
            String key=h.substring(0,colon),value=h.substring(colon+1);key.toLowerCase();value.trim();
            if(key=="transfer-encoding"){respond(400,"Bad Request","text/plain","Transfer-Encoding no admitido.");return;}
            if(key=="content-length"){if(hasLength||value.isEmpty()||value.length()>8){respond(400,"Bad Request","text/plain","Content-Length no valido.");return;}for(size_t i=0;i<value.length();++i)if(value[i]<'0'||value[i]>'9'){respond(400,"Bad Request","text/plain","Content-Length no valido.");return;}contentLength=value.toInt();hasLength=true;}
            if(key=="content-type"){value.toLowerCase();const int s=value.indexOf(';');if(s>=0)value.remove(s);value.trim();json=value=="application/json";binary=value=="application/octet-stream";}
            if(key=="x-interlock")token=value=="1";pos=next+2;
        }
        if(!hasLength||!contentLength){respond(411,"Length Required","text/plain","Falta Content-Length.");return;}
        const size_t maxSize=firmwareRequest?8388608U:testRequest?512U:(hardwareRequest?32768U:57344U);if(contentLength>maxSize){respond(413,"Content Too Large","text/plain","Configuracion demasiado grande.");return;}
        if(!(firmwareRequest?binary:json)){respond(415,"Unsupported Media Type","text/plain","Content-Type no admitido.");return;}
        if(!token){respond(403,"Forbidden","text/plain","Falta X-Interlock: 1.");return;}
        if(firmwareRequest){
            if(restartPending){respond(409,"Conflict","text/plain","Reinicio pendiente.");return;}
            if(!Update.begin(contentLength,U_FLASH)){
                firmwareRequest=false;
                respond(400,"Bad Request","text/plain",String("No se puede iniciar la actualizacion: ")+Update.errorString());return;
            }
            firmwareReceived=0;readingBody=true;started=millis();return;
        }
        readingBody=true;body.reserve(contentLength);return;
    }

    if(method!="GET"){respond(405,"Method Not Allowed","text/plain","Metodo no admitido.");return;}
    if(path=="/firmware.html"){respond(200,"OK","text/html; charset=utf-8",FIRMWARE_PAGE);return;}
    if(path=="/api/hardware"){hardwareConfig();return;}
    if(path=="/api/backup"){config(true);return;}
    if(path=="/api/config"){config();return;}
    if(path=="/api/status"){status();return;}

    if(path=="/"||path=="/index.html"||path=="/mqtt.html"||path=="/inputs.html"||path=="/outputs.html"||path=="/signals.html"||path=="/detection.html"||path=="/turnouts.html"||path=="/hardware.html"||path=="/hardware.js"||path=="/style.css"||path=="/dashboard.js"||path=="/config.js"||path=="/detection.js"||path=="/turnouts.js"){
        const String asset=path=="/"?String("/index.html"):path;if(filesystemReady)file=LittleFS.open(asset.c_str(),"r");
        if(!file){respond(503,"Service Unavailable","text/plain; charset=utf-8","Interfaz no disponible. Carga LittleFS con: pio run -t uploadfs. API: /api/status");return;}
        const char* type=path.endsWith(".css")?"text/css; charset=utf-8":path.endsWith(".js")?"application/javascript; charset=utf-8":"text/html; charset=utf-8";
        headers(200,"OK",type,file.size());return;
    }
    respond(404,"Not Found","text/plain","Recurso no encontrado.");
}

void WebManager::config(bool secrets){DynamicJsonDocument doc(65536);Config.toJson(doc,secrets);if(doc.overflowed()){respond(500,"Internal Server Error","text/plain","Sin memoria.");return;}String value;serializeJson(doc,value);respond(200,"OK","application/json; charset=utf-8",value);}

void WebManager::saveConfig(){
    DynamicJsonDocument doc(65536);String error;
    if(deserializeJson(doc,body)){respond(400,"Bad Request","text/plain","JSON no valido o demasiado grande.");return;}
    if(!Config.applyJson(doc.as<JsonVariantConst>(),error)){respond(400,"Bad Request","text/plain; charset=utf-8",error);return;}
    // Se reinicia igual que al guardar hardware: evita estados parciales y
    // cumple el comportamiento de guardado definido para Interlock Easy.
    respond(200,"OK","application/json","{\"saved\":true,\"restarting\":true}");
    restartPending=true;restartAt=millis()+1000;
}

void WebManager::testControl(){
    if(Hardware.modbus.role==ModbusRole::SLAVE){respond(409,"Conflict","text/plain","Las E/S del esclavo se controlan desde el maestro.");return;}
    DynamicJsonDocument doc(1024);
    if(deserializeJson(doc,body)||!doc["type"].is<const char*>()||!doc["channel"].is<unsigned int>()||doc["channel"].as<unsigned int>()<1||doc["channel"].as<unsigned int>()>64){respond(400,"Bad Request","text/plain","Mando de prueba no valido.");return;}
    const uint8_t channel=doc["channel"].as<unsigned int>()-1;const String type=doc["type"].as<String>();
    if((type=="input"&&channel>=NUM_INPUTS)||(type=="relay"&&channel>=NUM_OUTPUTS)||(type=="signal"&&channel>=Config.signals.size())){respond(400,"Bad Request","text/plain","Canal fuera del hardware configurado.");return;}
    bool ok=false;
    if(type=="input"&&doc.containsKey("state")&&(doc["state"].isNull()||doc["state"].is<bool>())){
        if(Config.inputAssigned(channel)){respond(409,"Conflict","text/plain; charset=utf-8","Entrada reservada para deteccion.");return;}
        ok=IO.simulateInput(channel,doc["state"].isNull()?-1:(doc["state"].as<bool>()?1:0));
    }else if(type=="relay"&&doc["state"].is<bool>()){
        if(Config.relayAssigned(channel)){respond(409,"Conflict","text/plain; charset=utf-8","Rele reservado: prueba un aspecto de su senal.");return;}
        if(IO.outputsReady()){IO.setOutput(channel,doc["state"].as<bool>());ok=IO.getOutput(channel)==doc["state"].as<bool>();}
    }else if(type=="signal"&&doc["aspect"].is<const char*>())ok=Signals.testAspect(channel,doc["aspect"].as<String>());
    else {respond(400,"Bad Request","text/plain","Tipo o valor no valido.");return;}
    if(!ok){respond(409,"Conflict","text/plain","No se pudo aplicar la prueba. Comprueba canal, aspecto y controlador.");return;}
    respond(200,"OK","application/json","{\"applied\":true}");
}

void WebManager::loop(){
    if(restartPending&&(long)(millis()-restartAt)>=0){restartPending=false;Serial.println("Reiniciando Interlock Easy...");ESP.restart();return;}
    if(!client){if(Connectivity.wifi()){wifiClient=wifiServer.accept();if(wifiClient)client=&wifiClient;}else{ethernetClient=ethernetServer.available();if(ethernetClient)client=&ethernetClient;}if(!client)return;started=millis();}
    if((!client->connected()&&!client->available())||millis()-started>(firmwareRequest?30000UL:10000UL)){close();return;}
    if(!responding){
        if(firmwareRequest && readingBody){receiveFirmware();return;}
        for(int budget=0;budget<256&&client->available();++budget){
            if(readingBody){body+=char(client->read());if(body.length()==contentLength){if(testRequest)testControl();else if(hardwareRequest)saveHardwareConfig();else saveConfig();body="";break;}continue;}
            request+=char(client->read());if(request.length()>2048){respond(431,"Request Header Fields Too Large","text/plain","Cabeceras demasiado grandes.");break;}
            if(request.endsWith("\r\n\r\n")){dispatch();request="";break;}
        }return;
    }
    size_t sendBudget=4096;
    while(sendBudget>0&&client&&*client){
        // ESP32 NetworkClient inherits Print::availableForWrite(), which returns
        // zero even on a writable WiFi socket. Write bounded chunks directly;
        // preserve offsets when write() accepts only part of a chunk.
        const int available=Connectivity.wifi()?512:client->availableForWrite();if(available<=0)return;size_t capacity=static_cast<size_t>(available);if(capacity>512)capacity=512;if(capacity>sendBudget)capacity=sendBudget;
        if(offset<pending.length()){const size_t remaining=pending.length()-offset,wanted=remaining<capacity?remaining:capacity;const size_t sent=client->write(reinterpret_cast<const uint8_t*>(pending.c_str())+offset,wanted);if(!sent)return;offset+=sent;sendBudget-=sent;continue;}
        if(file&&file.available()){uint8_t buffer[512];const size_t position=file.position(),count=file.read(buffer,capacity);if(!count){close();return;}const size_t sent=client->write(buffer,count);if(sent<count)file.seek(position+sent);if(!sent)return;sendBudget-=sent;continue;}
        close();return;
    }
}

void WebManager::hardwareConfig(){
    if(filesystemReady&&LittleFS.exists("/config.json")){file=LittleFS.open("/config.json","r");if(!file||file.size()>32768){if(file)file.close();respond(500,"Internal Server Error","text/plain","No se pudo leer config.json.");return;}headers(200,"OK","application/json; charset=utf-8",file.size());return;}
    DynamicJsonDocument doc(49152);Hardware.toJson(doc);if(doc.overflowed()){respond(500,"Internal Server Error","text/plain","Sin memoria.");return;}String value;serializeJson(doc,value);respond(200,"OK","application/json; charset=utf-8",value);
}
void WebManager::saveHardwareConfig(){
    DynamicJsonDocument doc(49152);if(deserializeJson(doc,body)){respond(400,"Bad Request","text/plain","JSON no valido o demasiado grande.");return;}String error;
    if(!Hardware.saveJson(doc.as<JsonVariantConst>(),error)){respond(400,"Bad Request","text/plain; charset=utf-8",error);return;}
    respond(200,"OK","application/json","{\"saved\":true,\"restarting\":true}");restartPending=true;restartAt=millis()+1000;
}

// Stream directly to the inactive application partition; never hold the image in RAM.
void WebManager::receiveFirmware(){
    uint8_t buffer[1024];
    size_t count=0;
    while(count<sizeof(buffer) && firmwareReceived+count<contentLength && client->available()){
        const int value=client->read();
        if(value<0)break;
        buffer[count++]=uint8_t(value);
    }
    if(!count)return;
    started=millis(); // Upload timeout measures inactivity, not total upload time.
    if(Update.write(buffer,count)!=count){
        const String error=String("Error al escribir firmware: ")+Update.errorString();
        Update.abort();firmwareRequest=false;readingBody=false;
        respond(400,"Bad Request","text/plain",error);return;
    }
    firmwareReceived+=count;
    if(firmwareReceived!=contentLength)return;
    const bool ok=Update.end(); // Verifies the complete image before selecting it for boot.
    firmwareRequest=false;readingBody=false;
    if(!ok){respond(400,"Bad Request","text/plain",String("Firmware no valido: ")+Update.errorString());return;}
    respond(200,"OK","application/json","{\"updated\":true,\"restarting\":true}");
    restartPending=true;restartAt=millis()+2000;
}
