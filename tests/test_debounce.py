from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
scope = {'__file__': str(ROOT/'tests/test_relays.py')}
exec((ROOT/'tests/test_relays.py').read_text().split('TEST =')[0], scope)
stubs = scope['STUBS'].copy()
stubs['Arduino.h'] = stubs['Arduino.h'].replace('inline int digitalRead(int) { return 0; }', 'inline int pins[20] = {};\ninline uint32_t clockMs = 0;\ninline int digitalRead(int pin) { return pins[pin]; }').replace('inline unsigned long millis() { return 0; }','inline unsigned long millis() { return clockMs; }')
stubs['Wire.h'] = stubs['Wire.h'].replace('bool beginOk = true;', 'bool beginOk = true; bool readOk=true; int readValue=0;').replace('return n;', 'return readOk?n:0;').replace('int read() { return 0; }','int read() { return readValue; }')
source = r'''
#include "HardwareDefaults.h"
#include "IOManager.h"
#include "ConfigManager.h"
#include "MqttManager.h"
#include <cassert>
#include <Wire.h>
#include <vector>
#include <utility>
ConfigManager Config;
bool ConfigManager::relayAssigned(uint8_t) const {return false;}
MqttManager MQTT;
std::vector<std::pair<int,bool>> events;
void MqttManager::publishInput(uint8_t c,bool s) {events.emplace_back(c,s);}
void MqttManager::publishOutput(uint8_t,bool) {}
void tick(uint32_t time) {
 while (uint32_t(time-clockMs)>5) {clockMs+=5; IO.loop();}
 clockMs=time; IO.loop();
}
int main() {
 setupHardware();
 for (auto& input : Config.inputs) input.inverted = false;
 IO.begin();
 pins[4]=1;tick(5);
 pins[4]=0;tick(15);
 pins[4]=1;tick(20);
 tick(65);assert(!IO.getInput(0) && events.empty());
 tick(70);assert(IO.getInput(0) && events.size()==1);
 tick(90);assert(events.size()==1);
 pins[4]=0;tick(95);pins[4]=1;tick(110);pins[4]=0;tick(120);
 clockMs=140;IO.refreshInputs();assert(IO.getInput(0));
 tick(170);assert(!IO.getInput(0) && events.size()==2);
 Config.inputs[0].inverted=true;IO.refreshInputs();assert(IO.getInput(0));
 Config.inputs[1].debounceMs=0; pins[5]=1;tick(175);assert(IO.getInput(1));
 Config.inputs[2].debounceMs=100; pins[6]=1;tick(180);tick(275);assert(!IO.getInput(2));tick(280);assert(IO.getInput(2));
 clockMs=1000;IO=IOManager{};IO.begin();
 pins[7]=1;tick(1005);clockMs=2000;IO.loop();
 assert(!IO.getInput(3) && IO.inputFiltering(3));
 tick(2045);assert(!IO.getInput(3));tick(2050);assert(IO.getInput(3));
 pins[7]=0;clockMs=0xffffffe0U;IO=IOManager{};IO.begin();
 tick(0xffffffebU);pins[7]=1;tick(0xfffffff0U);tick(0x21U);assert(!IO.getInput(3));tick(0x26U);assert(IO.getInput(3));
 assert(!IO.getInput(8));
 assert(IO.simulateInput(0,0));assert(IO.inputSimulated(0) && !IO.getInput(0));
 assert(events.back()==std::make_pair(0,false));
 const auto count=events.size();
 pins[4]=1;tick(clockMs+100);assert(!IO.getInput(0));assert(events.size()==count);
 assert(IO.simulateInput(0,1));assert(IO.getInput(0));
 assert(IO.simulateInput(0,-1));assert(!IO.inputSimulated(0) && !IO.getInput(0));
 assert(IO.simulateInput(0,1));clockMs+=60000;IO.loop();
 assert(!IO.inputSimulated(0) && !IO.getInput(0));
 assert(!IO.simulateInput(8,1) && !IO.simulateInput(0,2));
 Hardware.modules={{ModuleType::PCF8575,32}};
 Hardware.inputs={{0,0,false,true}};Hardware.outputs.clear();
 Config.inputs.resize(1);Config.outputs.clear();
 Wire.readValue=1;IO=IOManager{};IO.begin();assert(IO.inputReady(0) && !IO.getInput(0));
 auto before=events.size();Wire.readOk=false;tick(clockMs+5);
 assert(!IO.inputReady(0) && events.size()==before);
 Wire.readOk=true;Wire.readValue=0;tick(clockMs+5);
 assert(!IO.inputReady(0));tick(clockMs+45);assert(!IO.inputReady(0));
 tick(clockMs+5);assert(IO.inputReady(0) && IO.getInput(0) && events.size()==before+1);
}
'''
with tempfile.TemporaryDirectory(prefix='interlock-debounce-') as directory:
 p=Path(directory)
 for name,value in stubs.items(): (p/name).write_text(value)
 (p/'test.cpp').write_text(source)
 subprocess.run(['c++','-std=c++17',f'-I{p}',f'-I{ROOT / "src"}',str(ROOT/'src/IOManager.cpp'),str(ROOT/'src/HardwareIO.cpp'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('OK: bouncing ON/OFF, independent intervals, inversion, refresh, disabled filter and timer rollover')
