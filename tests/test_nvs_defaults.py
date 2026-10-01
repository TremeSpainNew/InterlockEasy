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
 String error;
 assert(Config.applyJson(doc.as<JsonVariantConst>(),error));
 ConfigManager reboot;
 reboot.begin();
 assert(reboot.mqtt.host=="saved-broker");
}
'''
with tempfile.TemporaryDirectory(prefix='interlock-config-') as directory:
 p=Path(directory)
 for name,content in STUBS.items(): (p/name).write_text(content)
 (p/'test.cpp').write_text(TEST)
 subprocess.run(['c++','-std=c++17',f'-I{p}',f'-I{ROOT / "src"}',f'-I{ROOT / ".pio/libdeps/esp32-s3-devkitc-1/ArduinoJson/src"}',str(ROOT/'src/ConfigManager.cpp'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('OK: missing NVS keys, defaults, legacy empty strings and JSON reboot')
