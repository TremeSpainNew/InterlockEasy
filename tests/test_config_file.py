"""64-channel configuration on LittleFS: roundtrip and atomic write failures."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
scope={'__file__':str(ROOT/'tests/test_hardware_storage.py')}
exec((ROOT/'tests/test_hardware_storage.py').read_text().split("source=r'''")[0],scope)
stubs=scope['stubs']
source=r'''
#include "ConfigManager.h"
#include "HardwareConfig.h"
#include <LittleFS.h>
#include <cassert>
int main(){
 Hardware.filesystemReady=true;
 Config.inputs.resize(64);Config.outputs.resize(64);
 Config.begin();
 DynamicJsonDocument doc(65536);Config.toJson(doc,true);
 assert(!doc.overflowed());doc["inputs"][63]["name"]="DI64";doc["outputs"][63]["name"]="RO64";
 String error;assert(Config.applyJson(doc.as<JsonVariantConst>(),error));
 assert(files.count("/io-config.json"));const auto saved=files["/io-config.json"];
 ConfigManager reboot;reboot.inputs.resize(64);reboot.outputs.resize(64);reboot.begin();
 assert(reboot.inputs[63].name=="DI64" && reboot.outputs[63].name=="RO64");
 doc["inputs"][63]["name"]="new";shortWrite=true;
 assert(!Config.applyJson(doc.as<JsonVariantConst>(),error));assert(files["/io-config.json"]==saved && Config.inputs[63].name=="DI64");
 shortWrite=false;renameFailure=true;
 assert(!Config.applyJson(doc.as<JsonVariantConst>(),error));assert(files["/io-config.json"]==saved);
 renameFailure=false;assert(Config.applyJson(doc.as<JsonVariantConst>(),error));
 assert(!files.count("/io-config.tmp"));
}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder)
 for n,s in stubs.items():(p/n).parent.mkdir(parents=True,exist_ok=True);(p/n).write_text(s)
 (p/'test.cpp').write_text(source)
 subprocess.run(['c++','-std=c++17','-DARDUINO_ARCH_ESP32',f'-I{p}',f'-I{ROOT/"src"}',f'-I{ROOT/".pio/libdeps/esp32-s3-devkitc-1/ArduinoJson/src"}',str(ROOT/'src/ConfigManager.cpp'),str(ROOT/'src/HardwareConfig.cpp'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('OK: LittleFS 64-channel roundtrip, failed writes/rename preserve file and live config')
