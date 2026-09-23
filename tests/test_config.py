"""Compile real config validation with ArduinoJson and an in-memory NVS stub."""
from pathlib import Path
import tempfile
import subprocess
ROOT = Path(__file__).resolve().parents[1]
STUBS = {
'Arduino.h': r'''#pragma once
#include <string>
#include <cstdint>
class String : public std::string {
public:
 using std::string::string;
 String(const std::string& s): std::string(s) {}
 String(int n): std::string(std::to_string(n)) {}
 bool isEmpty() const { return empty(); }
 int indexOf(char c) const { auto p=find(c); return p==npos ? -1 : int(p); }
};
struct SerialStub { void println(const char*) {} };
inline SerialStub Serial;
''',
'Preferences.h': r'''#pragma once
#include <map>
inline std::map<std::string, String> storage;
inline bool failWrite = false;
class Preferences {
public:
 bool begin(const char*, bool) {return true;}
 String getString(const char* key, const char* fallback) {return storage.count(key) ? storage[key] : String(fallback);}
 uint16_t getUShort(const char*, uint16_t fallback) {return fallback;}
 bool getBool(const char*, bool fallback) {return fallback;}
 size_t putString(const char* key, const String& value) {if(failWrite) return 0; storage[key]=value; return value.length();}
};
''',
'ArduinoJson.h': r'''#pragma once
#include_next <ArduinoJson.h>
namespace ArduinoJson {
template<> struct Converter<String> {
 static void toJson(const String& s, JsonVariant v) {v.set(static_cast<const std::string&>(s));}
 static String fromJson(JsonVariantConst v) {return v.as<std::string>();}
 static bool checkJson(JsonVariantConst v) {return v.is<const char*>();}
};
}
'''
}
TEST = r'''#include "ConfigManager.h"
#include <cassert>
int main() {
 Config.begin();
 DynamicJsonDocument d(32768);
 String error;
 Config.toJson(d);
 assert(!d["mqtt"].containsKey("password"));
 d["mqtt"]["host"]="broker.local";
 d["mqtt"]["password"]="secret";
 d["inputs"][0]["enabled"]=true;
 d["inputs"][0]["topic"]="input/1";
 assert(Config.applyJson(d.as<JsonVariantConst>(),error));
 assert(Config.mqtt.password=="secret");
 Config.toJson(d);
 assert(d["mqtt"]["passwordSet"]==true && !d["mqtt"].containsKey("password"));
 d["mqtt"]["host"]="new.local";
 assert(Config.applyJson(d.as<JsonVariantConst>(),error));
 assert(Config.mqtt.password=="secret");
 ConfigManager reboot;
 reboot.begin();
 assert(reboot.mqtt.host=="new.local" && reboot.mqtt.password=="secret");
 const auto saved=storage;
 d["mqtt"]["port"]=0;
 assert(!Config.applyJson(d.as<JsonVariantConst>(),error));
 assert(storage==saved && Config.mqtt.port==1883);
 d["mqtt"]["port"]=1883;
 d["inputs"][0]["topic"]="bad/#";
 assert(!Config.applyJson(d.as<JsonVariantConst>(),error));
 d["inputs"][0]["topic"]="input/1";
 d["outputs"][0]["enabled"]=true;
 d["outputs"][0]["commandTopic"]="input/1";
 d["outputs"][0]["stateTopic"]="output/1";
 assert(!Config.applyJson(d.as<JsonVariantConst>(),error));
 d["outputs"][0]["commandTopic"]="command/1";
 d["outputs"][0]["payloadOff"]="1";
 assert(!Config.applyJson(d.as<JsonVariantConst>(),error));
 d["outputs"][0]["payloadOff"]="0";
 d["mqtt"]["host"]="not-saved.local";
 failWrite=true;
 assert(!Config.applyJson(d.as<JsonVariantConst>(),error));
 assert(Config.mqtt.host=="new.local" && storage==saved);
 failWrite=false;
 d["mqtt"]["password"]="";
 assert(Config.applyJson(d.as<JsonVariantConst>(),error));
 assert(Config.mqtt.password.empty());
 d["inputs"][0]["payloadJson"]=true;
 d["inputs"][0]["payloadOn"]="{broken}";
 assert(!Config.applyJson(d.as<JsonVariantConst>(),error));
 d["inputs"][0]["payloadOn"]=R"({"state":true})";
 d["inputs"][0]["payloadOff"]=R"({"state":false})";
 d["outputs"][0]["payloadJson"]=true;
 d["outputs"][0]["jsonPath"]="data.state";
 d["outputs"][0]["payloadOn"]="true";
 d["outputs"][0]["payloadOff"]="false";
 d["outputs"][0]["stateJson"]=true;
 d["outputs"][0]["stateOn"]=R"({"on":true})";
 d["outputs"][0]["stateOff"]=R"({"on":false})";
 assert(Config.applyJson(d.as<JsonVariantConst>(),error));
 ConfigManager jsonReboot; jsonReboot.begin();
 assert(jsonReboot.inputs[0].payloadJson && jsonReboot.outputs[0].stateJson);
 assert(jsonReboot.outputs[0].jsonPath=="data.state");
 d["outputs"][0]["jsonPath"]="data..state";
 assert(!Config.applyJson(d.as<JsonVariantConst>(),error));
 d["outputs"][0]["jsonPath"]="data.state";
 d["inputs"].as<JsonArray>().remove(0);
 assert(!Config.applyJson(d.as<JsonVariantConst>(),error));
 Config.toJson(d);
 d["outputs"][0]["enabled"] = false;
 auto signals = d["signals"].as<JsonArray>();
 auto s = signals.createNestedObject();
 s["name"]="S1"; s["enabled"]=true; s["topic"]="signal/1"; s["jsonPath"]="Aspecto"; s["blinkMs"]=500;
 auto lights=s.createNestedArray("lights");
 for(int i=1;i<=4;++i) {auto l=lights.createNestedObject();l["name"]="Foco";l["relay"]=i;}
 auto aspects=s.createNestedArray("aspects");
 auto a=aspects.createNestedObject(); a["value"]="ViaLibre";a.createNestedArray("on").add(3); a.createNestedArray("blink").add(4);
 assert(Config.applyJson(d.as<JsonVariantConst>(),error));
 ConfigManager signalReboot; signalReboot.begin();
 assert(signalReboot.signals.size()==1 && signalReboot.signals[0].lights.size()==4);
 assert(signalReboot.signals[0].aspects[0].mask==4 && signalReboot.signals[0].aspects[0].blink==8);
 d["outputs"][0]["enabled"]=true;
 assert(!Config.applyJson(d.as<JsonVariantConst>(),error));
 d["outputs"][0]["enabled"]=false;
 signals.add(s);
 assert(!Config.applyJson(d.as<JsonVariantConst>(),error));
 signals.remove(1);
 s["lights"][1]["relay"]=1;
 assert(!Config.applyJson(d.as<JsonVariantConst>(),error));
 s["lights"][1]["relay"]=2;
 s["aspects"][0]["blink"][0]=3;
 assert(!Config.applyJson(d.as<JsonVariantConst>(),error));
}
'''
with tempfile.TemporaryDirectory(prefix='interlock-config-') as directory:
 p=Path(directory)
 for name,content in STUBS.items(): (p/name).write_text(content)
 (p/'test.cpp').write_text(TEST)
 subprocess.run(['c++','-std=c++17',f'-I{p}',f'-I{ROOT / "src"}',f'-I{ROOT / ".pio/libdeps/esp32-s3-devkitc-1/ArduinoJson/src"}',str(ROOT/'src/ConfigManager.cpp'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('OK: validation, password preservation/deletion, reboot and failed NVS write')
