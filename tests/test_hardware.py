"""Hardware schema and real I2C driver, with bus faults and 32-channel profiles."""
from pathlib import Path
import subprocess,tempfile,json
ROOT=Path(__file__).resolve().parents[1]
scope={'__file__':str(ROOT/'tests/test_config.py')}
exec((ROOT/'tests/test_config.py').read_text().split('TEST =')[0],scope)
stubs=scope['STUBS'].copy()
stubs['Arduino.h']+='''
constexpr int INPUT=0,OUTPUT=1,INPUT_PULLUP=2,HIGH=1,LOW=0;
inline int gpio[49]={};
inline void pinMode(int,int){}
inline void digitalWrite(int p,int v){gpio[p]=v;}
inline int digitalRead(int p){return gpio[p];}
'''
stubs['driver/gpio.h']='''#pragma once
#define GPIO_NUM_MAX 49
#define GPIO_IS_VALID_GPIO(n) ((n)>=0 && (n)<49)
#define GPIO_IS_VALID_OUTPUT_GPIO(n) GPIO_IS_VALID_GPIO(n)
'''
stubs['Wire.h']='''#pragma once
#include <vector>
struct Transaction {unsigned address;std::vector<uint8_t> data;};
struct WireStub {
 std::vector<Transaction> writes;
 int failure=-1,shortRead=-1;
 uint16_t input=0;unsigned pos=0;
 bool begin(int,int,int){return true;}
 void setTimeOut(int){}
 void beginTransmission(uint8_t a){writes.push_back({a,{}});}
 void write(uint8_t v){writes.back().data.push_back(v);}
 uint8_t endTransmission(bool=true){return int(writes.back().address)==failure?2:0;}
 uint8_t requestFrom(uint8_t a,uint8_t count){pos=0;return int(a)==shortRead?0:count;}
 int read(){return (input>>(8*pos++))&255;}
};
inline WireStub Wire;
'''
source=r'''
#include "HardwareConfig.h"
#include "HardwareIO.h"
#include <Wire.h>
#include <cassert>
#include <fstream>
#include <sstream>
int main(int argc,char** argv){
 assert(argc==3);std::ifstream file(argv[1]);std::stringstream text;text<<file.rdbuf();
 DynamicJsonDocument doc(24576);assert(!deserializeJson(doc,text.str()));String error;
 assert(Hardware.parse(doc.as<JsonVariantConst>(),error));
 assert(Hardware.inputs.size()==8 && Hardware.outputs.size()==8);
 doc["outputs"][0]["module"]=0;doc["outputs"][0]["pin"]=4;
 assert(!Hardware.parse(doc.as<JsonVariantConst>(),error)); // duplicate physical input/output
 doc["outputs"][0]["module"]=1;doc["outputs"][0]["pin"]=0;
 doc["inputCount"]=9;assert(!Hardware.parse(doc.as<JsonVariantConst>(),error));doc["inputCount"]=8;
 doc["modules"][1]["type"]="unknown";assert(!Hardware.parse(doc.as<JsonVariantConst>(),error));
 doc["modules"][1]["type"]="pcf8574";doc["outputs"][0]["pin"]=8;assert(!Hardware.parse(doc.as<JsonVariantConst>(),error));
 doc["outputs"][0]["pin"]=0;doc["modules"][1]["address"]=128;assert(!Hardware.parse(doc.as<JsonVariantConst>(),error));
 doc["modules"][1]["address"]=32;doc["inputs"][0]["pin"]=42;assert(!Hardware.parse(doc.as<JsonVariantConst>(),error));
 doc["inputs"][0]["module"]=1;doc["inputs"][0]["pin"]=7;doc["modules"][1]["type"]="mcp23017";
 assert(!Hardware.parse(doc.as<JsonVariantConst>(),error));
 assert(Hardware.inputs[0].pin==4); // rejected profile leaves live map untouched
 for(auto type:{ModuleType::PCF8574,ModuleType::PCF8575,ModuleType::MCP23017,ModuleType::TCA9554}) {
  Hardware.modules={{type,32}};Hardware.inputs={{0,0,false,true}};Hardware.outputs={{0,1,true,false}};
  Wire={};assert(HardwarePorts.begin());
  assert(HardwarePorts.write(1,1));assert(HardwarePorts.states==1);
  auto bytes=Wire.writes.back().data;
  if(type==ModuleType::MCP23017)assert((bytes==std::vector<uint8_t>{0x14,0xfd,0xff}));
  if(type==ModuleType::TCA9554)assert((bytes==std::vector<uint8_t>{1,0xfd}));
  if(type==ModuleType::PCF8574)assert((bytes==std::vector<uint8_t>{0xfd}));
  if(type==ModuleType::PCF8575)assert((bytes==std::vector<uint8_t>{0xfd,0xff}));
  Wire.input=1;HardwarePorts.sample();bool value=false;assert(HardwarePorts.read(0,value)&&value);
  Wire.shortRead=32;HardwarePorts.sample();assert(!HardwarePorts.read(0,value));
  Wire.failure=32;assert(!HardwarePorts.write(1,0));assert(HardwarePorts.states==1);
  Wire.failure=-1;assert(HardwarePorts.write(1,0));assert(HardwarePorts.states==0);
 }
 Hardware.modules={{ModuleType::PCF8575,32},{ModuleType::MCP23017,33}};
 Hardware.inputs.clear();Hardware.outputs.clear();
 for(uint8_t i=0;i<32;++i)Hardware.outputs.push_back({uint8_t(i/16),uint8_t(i%16),true,false});
 Wire={};assert(HardwarePorts.begin());
 assert(HardwarePorts.write(0xffffffffU,0x80018001U));assert(HardwarePorts.states==0x80018001U);
 Wire.failure=33;assert(!HardwarePorts.write(0xffffffffU,0));assert(HardwarePorts.states==0x80010000U);
 Wire.failure=-1;assert(HardwarePorts.write(0xffffffffU,0));assert(HardwarePorts.states==0);
 Hardware.modules={{ModuleType::GPIO,0}};Hardware.inputs.clear();Hardware.outputs={{0,5,true,false}};
 assert(HardwarePorts.begin());assert(gpio[5]==1);assert(HardwarePorts.write(1,1));assert(gpio[5]==0);
 std::ifstream mixed(argv[2]);std::stringstream mixedText;mixedText<<mixed.rdbuf();
 assert(!deserializeJson(doc,mixedText.str()));assert(Hardware.parse(doc.as<JsonVariantConst>(),error));
 assert(Hardware.inputs.size()==16 && Hardware.outputs.size()==32);
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory)
 for name,value in stubs.items():
  (p/name).parent.mkdir(exist_ok=True,parents=True);(p/name).write_text(value)
 (p/'test.cpp').write_text(source)
 subprocess.run(['c++','-std=c++17',f'-I{p}',f'-I{ROOT/"src"}',f'-I{ROOT/".pio/libdeps/esp32-s3-devkitc-1/ArduinoJson/src"}',str(ROOT/'src/HardwareConfig.cpp'),str(ROOT/'src/HardwareIO.cpp'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),str(ROOT/'data/config.json'),str(ROOT/'examples/config-mixed-16di-32ro.json')],check=True)
print('OK: hardware validation, GPIO, TCA9554, PCF8574/75, MCP23017, read/write errors, 32 outputs and partial writes')
