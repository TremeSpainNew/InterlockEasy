"""Real signal/relay/config code with simulated clock, I2C and MQTT."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
# Reuse the small host adapters without running that test's main program.
scope = {'__file__': str(ROOT/'tests/test_config.py')}
exec((ROOT/'tests/test_config.py').read_text().split('TEST =')[0], scope)
stubs = scope['STUBS'].copy()
stubs['Arduino.h'] = stubs['Arduino.h'].replace('void println(const char*) {}', 'void println(const char*) {} template<class... T> void printf(const char*, T...) {}') + '''
using byte = uint8_t;
inline unsigned long clockMs = 0;
inline unsigned long millis() {return clockMs;}
constexpr int INPUT = 0;
constexpr int OUTPUT = 1, HIGH = 1, LOW = 0;
inline void digitalWrite(int,int) {}
constexpr int INPUT_PULLUP = 2;
inline void pinMode(int,int) {}
inline int digitalRead(int) {return 0;}
'''
stubs['PubSubClient.h'] = '#pragma once\nclass PubSubClient {};\n'
stubs['Wire.h'] = '''#pragma once
#include <vector>
struct WireStub {
 bool fail=false;
 std::vector<uint8_t> values;
 bool begin(int,int,int) {return true;}
 void setTimeOut(int) {}
 void beginTransmission(uint8_t) {values.clear();}
 void write(uint8_t v) {values.push_back(v);}
 uint8_t requestFrom(uint8_t,uint8_t n) {return n;}
 int read() {return 0;}
 uint8_t endTransmission(bool = true) {return fail?2:0;}
};
inline WireStub Wire;
'''
scope2={'__file__':str(ROOT/'tests/test_relays.py')}
exec((ROOT/'tests/test_relays.py').read_text().split('TEST =')[0],scope2)
stubs['HardwareDefaults.h']=scope2['STUBS']['HardwareDefaults.h']
test = r'''
#include "ConfigManager.h"
#include "HardwareDefaults.h"
#include "IOManager.h"
#include "SignalManager.h"
#include "MqttManager.h"
#include <Wire.h>
#include <cassert>
MqttManager MQTT;
void MqttManager::publishInput(uint8_t,bool) {}
void MqttManager::publishOutput(uint8_t,bool) {}
int main() {
 setupHardware();
 IO.begin();
 IO.setOutput(7,true);
 SignalConfig signal; signal.name="S1";signal.enabled=true;signal.topic="s1";
 for(int i=1;i<=4;++i) {SignalLight l;l.relay=i;signal.lights.push_back(l);}
 SignalAspect stop;stop.value="Parada";stop.mask=1;
 SignalAspect go;go.value="ViaLibre";go.mask=4;
 SignalAspect blink;blink.value="Precaucion";blink.mask=1;blink.blink=2;
 signal.aspects={stop,go,blink}; Config.signals.push_back(signal);
 Signals.command("s1",R"({"Aspecto":"ViaLibre","AspectoAnterior":"Parada"})");
 assert(IO.getOutput(2) && IO.getOutput(7) && !IO.getOutput(0));
 assert(Signals.aspect(0)=="ViaLibre");
 IO.setOutput(0,true);assert(!IO.getOutput(0)); // Reserved from standalone commands.
 Signals.command("s1",R"({"Aspecto":"Unknown"})");
 Signals.command("s1",R"({"AspectoAnterior":"Parada"})");
 Signals.command("s1","invalid");
 assert(IO.getOutput(2));
 Wire.fail=true;Signals.command("s1",R"({"Aspecto":"Parada"})");
 assert(IO.getOutput(2) && Signals.aspect(0)=="ViaLibre");
 Wire.fail=false;Signals.command("s1",R"({"Aspecto":"Parada"})");
 assert(IO.getOutput(0) && !IO.getOutput(2) && IO.getOutput(7));
 clockMs=100;Signals.command("s1",R"({"Aspecto":"Precaucion"})");
 assert(IO.getOutput(0) && IO.getOutput(1));
 clockMs=600;Signals.loop();assert(IO.getOutput(0) && !IO.getOutput(1));
 Signals.command("s1",R"({"Aspecto":"Precaucion"})");
 assert(!IO.getOutput(1)); // Duplicate command does not restart phase.
 clockMs=1100;Wire.fail=true;Signals.loop();assert(!IO.getOutput(1));
 clockMs=1120;Wire.fail=false;Signals.loop();assert(IO.getOutput(1) && IO.getOutput(7));
 assert(!Signals.testAspect(7,"Parada"));
 assert(!Signals.testAspect(0,"Unknown"));
 assert(Signals.testAspect(0,"ViaLibre"));assert(IO.getOutput(2) && !IO.getOutput(1));
 assert(Signals.testAspect(0,"Precaucion"));
 Signals.reload();clockMs=1600;Signals.loop();assert(Signals.aspect(0).isEmpty());
 assert(IO.getOutput(1)); // Save stops animation, holds the current physical command.
 Hardware.modules={{ModuleType::PCF8575,32},{ModuleType::PCF8575,33}};
 Hardware.inputs.clear();Hardware.outputs.clear();Config.inputs.clear();Config.outputs.resize(32);
 for(uint8_t i=0;i<32;++i)Hardware.outputs.push_back({uint8_t(i/16),uint8_t(i%16),true,false});
 Config.signals[0].lights[2].relay=32;
 IO.begin();assert(Signals.testAspect(0,"ViaLibre"));assert(IO.getOutput(31) && !IO.getOutput(2));
 IO.setOutput(31,false);assert(IO.getOutput(31)); // High relay bit is also reserved.
 assert(Signals.testAspect(0,"Parada"));assert(!IO.getOutput(31) && IO.getOutput(0));
}
'''
with tempfile.TemporaryDirectory(prefix='interlock-signals-') as directory:
 p=Path(directory)
 for name,content in stubs.items(): (p/name).write_text(content)
 (p/'test.cpp').write_text(test)
 subprocess.run(['c++','-std=c++17',f'-I{p}',f'-I{ROOT / "src"}',f'-I{ROOT / ".pio/libdeps/esp32-s3-devkitc-1/ArduinoJson/src"}',*[str(ROOT/'src'/name) for name in ['ConfigManager.cpp','IOManager.cpp','HardwareIO.cpp','SignalManager.cpp']],str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('OK: signal aspects, exclusive relays, grouped writes, blink phases and I2C failure/retry')
