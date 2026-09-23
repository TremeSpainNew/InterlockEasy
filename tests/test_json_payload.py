from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = r'''
#include "JsonPayload.h"
#include <cassert>
int main() {
 DynamicJsonDocument d(2048);
 assert(!deserializeJson(d, R"({"state":true,"channel":1})"));
 assert(JsonPayload::matches(d.as<JsonVariantConst>(), R"({ "channel":1, "state":true })"));
 assert(!JsonPayload::matches(d.as<JsonVariantConst>(), R"({"state":true})"));
 assert(!JsonPayload::matches(d["state"], "1"));
 assert(JsonPayload::matches(d["state"], "true"));
 assert(!JsonPayload::validPair("broken", "false"));
 assert(!JsonPayload::validPair(R"({"a":1,"b":2})", R"({"b":2,"a":1})"));
 assert(JsonPayload::validPair("true", "false"));
 assert(JsonPayload::validPair("9007199254740992", "9007199254740993"));
 JsonVariantConst selected;
 assert(!deserializeJson(d, R"({"data":{"state":true},"extra":17})"));
 assert(JsonPayload::select(d.as<JsonVariantConst>(), "data.state", selected));
 assert(JsonPayload::matches(selected, "true"));
 assert(!JsonPayload::select(d.as<JsonVariantConst>(), "missing", selected));
 assert(!JsonPayload::select(d.as<JsonVariantConst>(), "data..state", selected));
 assert(!JsonPayload::select(d.as<JsonVariantConst>(), "data.state.extra", selected));
 assert(JsonPayload::select(d.as<JsonVariantConst>(), "", selected));
 assert(JsonPayload::validPair("1", "\"1\""));
 assert(!deserializeJson(d, R"([1,{"a":false},null])"));
 assert(JsonPayload::matches(d.as<JsonVariantConst>(), R"([1, {"a":false}, null])"));
 assert(!JsonPayload::matches(d.as<JsonVariantConst>(), R"([1, {"a":0}, null])"));
}
'''
with tempfile.TemporaryDirectory(prefix='interlock-json-') as directory:
 p = Path(directory)
 (p/'test.cpp').write_text(source)
 subprocess.run(['c++','-std=c++17',f'-I{root / "src"}',f'-I{root / ".pio/libdeps/esp32-s3-devkitc-1/ArduinoJson/src"}',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('OK: JSON structure, key order, whitespace, arrays, types and invalid payloads')
