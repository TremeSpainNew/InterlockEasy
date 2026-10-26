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
stubs['Adafruit_INA219.h'] = '''#pragma once
#include <Wire.h>
class Adafruit_INA219 {
public:
 explicit Adafruit_INA219(uint8_t address=0x40): address(address) {}
 bool begin(TwoWire*) {return beginSuccess;}
 void setCalibration_32V_2A() {}
 float getCurrent_mA() {return current;}
 uint8_t address;
 static inline bool beginSuccess=true;
 static inline float current=100;
};
'''
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
using TwoWire = WireStub;
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
#include "JsonPayload.h"
#include "MqttManager.h"
#include <Adafruit_INA219.h>
#include <Wire.h>
#include <cassert>
MqttManager MQTT;
void MqttManager::publishInput(uint8_t,bool) {}
void MqttManager::publishOutput(uint8_t,bool) {}
int main() {
 setupHardware();
 IO.begin();
 IO.setOutput(7,true);
 SignalConfig signal; signal.id=1;signal.name="S1";signal.enabled=true;signal.topic="s1";
 for(int i=1;i<=4;++i) {SignalLight l;l.relay=i;signal.lights.push_back(l);}
 SignalAspect stop;stop.value="Parada";stop.mask=1;
 SignalAspect go;go.value="ViaLibre";go.mask=4;
 SignalAspect blink;blink.value="Precaucion";blink.mask=1;blink.blink=2;
 signal.aspects={stop,go,blink}; Config.signals.push_back(signal);
 BeaconConfig beacon;beacon.enabled=true;beacon.name="B1";beacon.signalId=1;beacon.relays={5,6};
 BeaconAspect beaconStop;beaconStop.value="Parada";
 BeaconAspect beaconGo;beaconGo.value="ViaLibre";beaconGo.mask=1;
 BeaconAspect beaconBlink;beaconBlink.value="Precaucion";beaconBlink.blink=2;
 beacon.aspects={beaconStop,beaconGo,beaconBlink};Config.beacons.push_back(beacon);
 Signals.command("s1",R"({"Aspecto":"ViaLibre","AspectoAnterior":"Parada"})");
 assert(IO.getOutput(2) && IO.getOutput(7) && IO.getOutput(4) && !IO.getOutput(0));
 assert(Signals.aspect(0)=="ViaLibre");
 IO.setOutput(0,true);assert(!IO.getOutput(0)); // Reserved from standalone commands.
 Signals.command("s1",R"({"Aspecto":"Unknown"})");
 Signals.command("s1",R"({"AspectoAnterior":"Parada"})");
 Signals.command("s1","invalid");
 assert(IO.getOutput(2));
 Wire.fail=true;Signals.command("s1",R"({"Aspecto":"Parada"})");
 assert(IO.getOutput(2) && Signals.aspect(0)=="ViaLibre");
 Wire.fail=false;Signals.command("s1",R"({"Aspecto":"Parada"})");
 assert(IO.getOutput(0) && !IO.getOutput(2) && !IO.getOutput(4) && IO.getOutput(7));
 clockMs=100;Signals.command("s1",R"({"Aspecto":"Precaucion"})");
 assert(IO.getOutput(0) && IO.getOutput(1) && IO.getOutput(5));
 clockMs=600;Signals.loop();assert(IO.getOutput(0) && !IO.getOutput(1) && !IO.getOutput(5));
 Signals.command("s1",R"({"Aspecto":"Precaucion"})");
 assert(!IO.getOutput(1)); // Duplicate command does not restart phase.
 clockMs=1100;Wire.fail=true;Signals.loop();assert(!IO.getOutput(1));
 clockMs=1120;Wire.fail=false;Signals.loop();assert(IO.getOutput(1) && IO.getOutput(7) && IO.getOutput(5));
 assert(!Signals.testAspect(7,"Parada"));
 assert(!Signals.testAspect(0,"Unknown"));
 assert(Signals.testAspect(0,"ViaLibre"));assert(IO.getOutput(2) && !IO.getOutput(1));
 assert(Signals.testAspect(0,"Precaucion"));
 Signals.reload();clockMs=1600;Signals.loop();assert(Signals.aspect(0).isEmpty());
 assert(IO.getOutput(1)); // Save stops animation, holds the current physical command.
 Hardware.modules={{ModuleType::PCF8575,32},{ModuleType::PCF8575,33},{ModuleType::PCF8575,34},{ModuleType::PCF8575,35}};
 Hardware.inputs.clear();Hardware.outputs.clear();Config.inputs.clear();Config.outputs.resize(64);
 for(uint8_t i=0;i<64;++i)Hardware.outputs.push_back({uint8_t(i/16),uint8_t(i%16),true,false});
 Config.signals[0].lights[2].relay=64;
 IO.begin();assert(Signals.testAspect(0,"ViaLibre"));assert(IO.getOutput(63) && !IO.getOutput(2));
 IO.setOutput(63,false);assert(IO.getOutput(63)); // High relay bit is also reserved.
 assert(Signals.testAspect(0,"Parada"));assert(!IO.getOutput(63) && IO.getOutput(0));
 // Alphanumeric signals receive plain text, ignoring the legacy JSON path.
 Config.signals[0].numeric=true;
 Config.signals[0].aspects[0].value="ViaLibre";
 Config.signals[0].aspects[1].value="60";
 Config.signals[0].aspects[1].mask=1;
 Signals.reload();
 Signals.command("s1","ViaLibre");
 assert(Signals.aspect(0)=="ViaLibre" && IO.getOutput(0));
 Signals.command("s1","60.0");
 assert(Signals.aspect(0)=="ViaLibre"); // Text comparisons do not normalize numbers.
 Signals.command("s1","60");
 assert(Signals.aspect(0)=="60");
 Signals.command("s1",R"({"Aspecto":"ViaLibre"})");
 Signals.command("s1",R"({"Aspecto":true})");
 Signals.command("s1","61");
 Signals.command("s1","true");
 Signals.command("s1",R"({"Aspecto":"ViaLibre"})");
 Signals.command("s1",R"({"Aspecto":60})");
 Signals.command("s1",R"("60")");
 assert(Signals.aspect(0)=="60" && IO.getOutput(0));
 Config.signals[0].aspects[0].value="Parada";
 Config.signals[0].aspects[1].value="ViaLibre";
 Config.signals[0].aspects[1].mask=4;
 assert(JsonPayload::numericText("-12.5"));
 assert(!JsonPayload::numericText("true"));
 assert(!JsonPayload::numericText("\"60\""));
 assert(JsonPayload::sameNumber("60", "6e1"));
 Signals.disconnected("other");assert(IO.getOutput(0));
 Signals.disconnected("s1");assert(!IO.getOutput(0) && !IO.getOutput(4) && !IO.getOutput(5) && Signals.aspect(0).isEmpty());
 clockMs+=1000;Signals.loop();assert(!IO.getOutput(0));
 Config.signals[0].numeric=false;
 Config.signals[0].aspects[0].value="Parada";
 Config.signals[0].lights[0].currentMonitor=true;
 Config.signals[0].lights[0].ina219Address=0x40;
 Config.signals[0].lights[0].minCurrentMa=80;
 Config.signals[0].lights[0].maxCurrentMa=120;
 Signals.reload();
 assert(Signals.currentMonitored(0,0) && Signals.currentSensorReady(0,0));
 assert(Signals.testAspect(0,"Parada"));
 clockMs+=50;Signals.loop();
 assert(Signals.currentReady(0,0) && Signals.currentMa(0,0)==100 && Signals.currentOk(0,0));
 Adafruit_INA219::current=10;
 clockMs+=50;Signals.loop();
 assert(Signals.currentReady(0,0) && !Signals.currentOk(0,0));
 assert(Signals.testAspect(0,"ViaLibre"));
 clockMs+=50;Signals.loop();
 assert(!Signals.currentReady(0,0));
}
'''
with tempfile.TemporaryDirectory(prefix='interlock-signals-') as directory:
 p=Path(directory)
 for name,content in stubs.items(): (p/name).write_text(content)
 (p/'test.cpp').write_text('#include "'+str(ROOT/'tests/modbus_stub.h')+'"\n'+test)
 subprocess.run(['c++','-std=c++17',f'-I{p}',f'-I{ROOT / "src"}',f'-I{ROOT / ".pio/libdeps/esp32-s3-devkitc-1/ArduinoJson/src"}',*[str(ROOT/'src'/name) for name in ['ConfigManager.cpp','IOManager.cpp','HardwareIO.cpp','SignalManager.cpp']],str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('OK: signal aspects, exclusive relays, grouped writes, blink phases and I2C failure/retry')
