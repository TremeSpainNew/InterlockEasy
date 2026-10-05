"""Check absent NVS keys, legacy values and JSON storage without missing reads."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
scope = {'__file__': str(ROOT/'tests/test_config.py')}
exec((ROOT/'tests/test_config.py').read_text().split('TEST =')[0], scope)
STUBS = scope['STUBS']
TEST = r'''#include "ConfigManager.h"
#include <cassert>
int main() {
 Config.begin();
 assert(Config.mqtt.clientId=="InterlockEasy-IO");
 assert(Config.outputs[0].payloadOn=="1" && Config.outputs[0].payloadOff=="0");
 storage["o0ct"]="legacy/command";
 storage["o0on"]="";
 Config.load();
 assert(Config.outputs[0].commandTopic=="legacy/command");
 assert(Config.outputs[0].payloadOn.isEmpty());
 storage.clear();
 Config.load();
 DynamicJsonDocument doc(65536);
 Config.toJson(doc,true);
 doc["mqtt"]["host"]="saved-broker";
 String legacyJson;serializeJson(doc,legacyJson);
 Preferences legacyStore;
 assert(legacyStore.putString("config_v1",legacyJson)==legacyJson.length());
 ConfigManager legacyBoot;legacyBoot.begin();
 assert(legacyBoot.mqtt.host=="saved-broker");
 // Same full snapshot sent by the detection and turnout forms.
 for(int i=0;i<8;++i) {
   doc["inputs"][i]["name"]="Entrada de deteccion del circuito de via de la estacion norte";
   doc["outputs"][i]["name"]="Salida de accionamiento del desvio de entrada estacion norte";
 }
 JsonObject cv=doc["cvs"].createNestedObject();
 cv["enabled"]=true;cv["station"]="EST";cv["id"]="CV1";cv["input"]=1;
 JsonObject t=doc["turnouts"].createNestedObject();
 t["enabled"]=true;t["station"]="EST";t["id"]="A1";
 t["outputNormal"]=1;t["outputReverse"]=2;t["inputNormal"]=2;t["inputReverse"]=3;
 t["drive"]="pulse";t["pulseMs"]=500;
 for(int i=4;i<=7;++i) {
   JsonObject extra=doc["cvs"].createNestedObject();
   extra["enabled"]=true;extra["station"]="EST";extra["id"]=String(i);extra["input"]=i;
 }
 assert(measureJson(doc)>4000);
 String largeJson;serializeJson(doc,largeJson);
 assert(legacyStore.putString("config_v1",largeJson)==0);
 String error;
 assert(Config.applyJson(doc.as<JsonVariantConst>(),error));
 ConfigManager reboot;
 reboot.begin();
 assert(reboot.mqtt.host=="saved-broker");
 assert(reboot.cvs.size()==5 && reboot.cvs[0].id=="CV1");
 assert(reboot.turnouts.size()==1 && reboot.turnouts[0].inputReverse==3);
 const auto previous=storage;
 doc["cvs"][0]["id"]="CV2";failWrite=true;
 assert(!Config.applyJson(doc.as<JsonVariantConst>(),error));
 assert(Config.cvs[0].id=="CV1" && storage==previous);
 failWrite=false;
 assert(Config.save());
}
'''
with tempfile.TemporaryDirectory(prefix='interlock-config-') as directory:
 p=Path(directory)
 for name,content in STUBS.items(): (p/name).write_text(content)
 (p/'test.cpp').write_text(TEST)
 subprocess.run(['c++','-std=c++17',f'-I{p}',f'-I{ROOT / "src"}',f'-I{ROOT / ".pio/libdeps/esp32-s3-devkitc-1/ArduinoJson/src"}',str(ROOT/'src/ConfigManager.cpp'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('OK: missing NVS keys, defaults, legacy empty strings and JSON reboot')
