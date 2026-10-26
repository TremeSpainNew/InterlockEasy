"""Compile the real HTTP map serializer with a 64 DI/64 RO hardware fixture."""
from pathlib import Path
import subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
scope={'__file__':str(ROOT/'tests/test_config.py')}
exec((ROOT/'tests/test_config.py').read_text().split('TEST =')[0],scope)
source=(ROOT/'src/WebManager.cpp').read_text()
method=source[source.index('void WebManager::channelMap()'):source.index('void WebManager::saveHardwareConfig()')]
fixture=r'''
#include "HardwareConfig.h"
#include <cassert>
HardwareConfig Hardware;
struct WebManager {
    String body; int status=0;
    void channelMap();
    void respond(int code,const char*,const char*,const String& value){status=code;body=value;}
};
'''+method+r'''
int main(){
    Hardware.wifiPassword="secret";
    Hardware.modules={{ModuleType::GPIO,0},{ModuleType::TCA9554,32},{ModuleType::PCF8574,33},{ModuleType::PCF8575,34},{ModuleType::MCP23017,35},{ModuleType::MODBUS,7}};
    for(unsigned i=0;i<64;++i){Hardware.inputs.push_back({5,uint8_t(i)});Hardware.outputs.push_back({5,uint8_t(i)});}
    WebManager web;web.channelMap();assert(web.status==200);
    DynamicJsonDocument doc(16384);assert(!deserializeJson(doc,web.body));
    assert(doc["inputs"].size()==64);assert(doc["outputs"].size()==64);
    assert(doc["outputs"][63]["module"]==5);assert(doc["outputs"][63]["pin"]==63);
    assert(doc["modules"][0]["type"]=="gpio");assert(doc["modules"][1]["type"]=="tca9554");
    assert(doc["modules"][2]["type"]=="pcf8574");assert(doc["modules"][3]["type"]=="pcf8575");
    assert(doc["modules"][4]["type"]=="mcp23017");assert(doc["modules"][5]["type"]=="modbus");
    assert(doc["modules"][5]["address"]==7);
    assert(!doc.containsKey("network"));assert(!doc.containsKey("mqtt"));assert(web.body.find("secret")==std::string::npos);
    Hardware.inputs.clear();Hardware.outputs.clear();Hardware.modules.clear();web.channelMap();
    assert(web.status==200);assert(!deserializeJson(doc,web.body));assert(doc["inputs"].size()==0);
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory)
    for name,value in scope['STUBS'].items():(p/name).write_text(value)
    (p/'test.cpp').write_text(fixture)
    subprocess.run(['c++','-std=c++17',f'-I{p}',f'-I{ROOT/"src"}',f'-I{ROOT/".pio/libdeps/esp32-s3-devkitc-1/ArduinoJson/src"}',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('OK: active hardware map, all module types, 64 DI/RO, empty map and credential exclusion')
