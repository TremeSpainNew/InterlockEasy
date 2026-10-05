"""Exercise the real MQTT manager with an in-memory broker transport."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
scope = {'__file__': str(ROOT/'tests/test_signals.py')}
exec((ROOT/'tests/test_signals.py').read_text().split("test = r'''")[0], scope)
stubs = scope['stubs'].copy()
stubs['EthernetManager.h'] = '''#pragma once
class EthernetManager {public: bool connected(){return true;} int createClient(){return 0;}};
inline EthernetManager Connectivity;
'''
stubs['PubSubClient.h'] = r'''#pragma once
#include <vector>
struct Message {String topic, value; bool retained;};
class PubSubClient {
public:
 inline static PubSubClient* instance;
 bool online=false; String willTopic,willValue; int willQos; bool willRetain;
 std::vector<Message> messages; std::vector<String> subscriptions;
 void (*callback)(char*,byte*,unsigned int)=nullptr;
 PubSubClient(int){instance=this;}
 void disconnect(){online=false;}
 bool connected(){return online;}
 bool setBufferSize(int){return true;}
 void setSocketTimeout(int){} void setServer(const char*,int){} void setKeepAlive(int){}
 void setCallback(void (*cb)(char*,byte*,unsigned int)){callback=cb;}
 bool connect(const char*,const char* t,int q,bool r,const char* v){willTopic=t;willValue=v;willQos=q;willRetain=r;return online=true;}
 int state(){return 0;} bool loop(){return online;}
 bool subscribe(const char* t,int=0){subscriptions.push_back(t);return true;}
 bool publish(const char* t,const char* v,bool r){messages.push_back({t,v,r});return online;}
 bool beginPublish(const char* t,size_t,bool r){messages.push_back({t,"",r});return true;}
 size_t write(const uint8_t* p,size_t n){messages.back().value.append((const char*)p,n);return n;}
 bool endPublish(){return true;}
 void receive(String topic,String value){callback(topic.data(),(byte*)value.data(),value.size());}
};
'''
test = r'''
#include "MqttManager.h"
#include "ConfigManager.h"
#include "HardwareConfig.h"
#include "IOManager.h"
#include "SignalManager.h"
#include "TurnoutManager.h"
#include "DetectionManager.h"
#include <cassert>
ConfigManager Config; HardwareConfig Hardware; IOManager IO;
SignalManager Signals; TurnoutManager Turnouts; DetectionManager Detections;
int commands=0, stops=0;
bool ConfigManager::inputAssigned(uint8_t)const{return false;}
bool ConfigManager::relayAssigned(uint8_t)const{return false;}
void IOManager::refreshInputs(){} bool IOManager::getInput(uint8_t){return false;}
bool IOManager::getOutput(uint8_t){return false;}
bool IOManager::outputReady(uint8_t)const{return true;}
void IOManager::setOutput(uint8_t,bool){++commands;}
bool IOManager::setOutputs(uint64_t,uint64_t){++stops;return true;}
SignalManager::SignalManager(){} void SignalManager::reload(){}
void SignalManager::command(const String&,const String&){++commands;}
void SignalManager::disconnected(const String&){++stops;}
void TurnoutManager::stop(const String&){++stops;}
bool TurnoutManager::command(const String&,const String&){++commands;return true;}
void TurnoutManager::publishAllFeedback(){}
void DetectionManager::publishAll(){}
int main(){
 Hardware.inputs.resize(1);Hardware.outputs.resize(1);
 Config.mqtt.host="broker";Config.mqtt.clientId="module1";
 Config.inputs[0].enabled=true;Config.inputs[0].topic="di/1";
 MQTT.begin();clockMs=5000;MQTT.loop();auto& broker=*PubSubClient::instance;
 assert(broker.willTopic=="desconexion" && broker.willValue=="module1");
 assert(broker.willQos==1 && !broker.willRetain);
 assert(broker.messages[0].topic=="desconexion/module1");
 assert(broker.messages[0].value=="di/1" && broker.messages[0].retained);
 assert(broker.subscriptions[0]=="gestor_conexion");
 broker.receive("gestor_conexion","off");int before=commands;
 broker.receive("command","1");assert(commands==before);
 broker.online=false;clockMs+=5000;MQTT.loop();
 broker.receive("command","1");assert(commands==before);
 broker.receive("gestor_conexion","on");broker.receive("command","1");assert(commands>before);
 int stopped=stops;broker.receive("command","\"desconexion\"");assert(stops>stopped);
 stopped=stops;broker.online=false;MQTT.loop();assert(stops>stopped);
}
'''
with tempfile.TemporaryDirectory(prefix='interlock-mqtt-') as directory:
    p=Path(directory)
    for name,content in stubs.items(): (p/name).write_text(content)
    (p/'MqttManager.cpp').write_text((ROOT/'src/MqttManager.cpp').read_text())
    (p/'test.cpp').write_text(test)
    subprocess.run(['c++','-std=c++17',f'-I{p}',f'-I{ROOT/"src"}',f'-I{ROOT/".pio/libdeps/esp32-s3-devkitc-1/ArduinoJson/src"}',str(p/'MqttManager.cpp'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('OK: last will, retained registry, manager off/on, reconnect blocking and disconnection')
