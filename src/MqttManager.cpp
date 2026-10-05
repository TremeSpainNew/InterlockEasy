#include "MqttManager.h"
#include "ConfigManager.h"
#include "EthernetManager.h"
#include "IOManager.h"
#include "JsonPayload.h"
#include "SignalManager.h"
#include "DetectionManager.h"
#include "TurnoutManager.h"

MqttManager MQTT;

void MqttManager::begin(){mqtt=new PubSubClient(Connectivity.createClient());reload();mqtt->setCallback(callback);}
void MqttManager::reload(){if(!mqtt)return;if(mqtt->connected())mqtt->publish("desconexion",Config.mqtt.clientId.c_str(),false);mqtt->disconnect();safeOutputs();inputQueue=InputStateQueue{};mqtt->setBufferSize(2048);mqtt->setSocketTimeout(1);lastReconnect=millis();mqtt->setServer(Config.mqtt.host.c_str(),Config.mqtt.port);mqtt->setKeepAlive(Config.mqtt.keepAlive);}
bool MqttManager::connected(){return mqtt&&mqtt->connected();}

void MqttManager::reconnect(){
    if(!mqtt||Config.mqtt.host.isEmpty()||!Connectivity.connected()||mqtt->connected())return;
    if(millis()-lastReconnect<5000)return;
    lastReconnect=millis();
    bool ok=mqtt->connect(Config.mqtt.clientId.c_str(), "desconexion", 1, false, Config.mqtt.clientId.c_str());
    if(ok){
        Serial.println("MQTT conectado");
        wasConnected=true;
        if(!registerTopics() || !mqtt->subscribe("gestor_conexion",1)){
            mqtt->publish("desconexion",Config.mqtt.clientId.c_str(),false);
            mqtt->disconnect(); safeOutputs(); return;
        }
        subscribeOutputs(); IO.refreshInputs();
        for(uint8_t i=0;i<NUM_INPUTS;++i)
            if(IO.inputReady(i)&&!Config.inputAssigned(i))publishInput(i,IO.getInput(i));
        if(IO.outputsReady())for(uint8_t i=0;i<NUM_OUTPUTS;++i)publishOutput(i,IO.getOutput(i));
        Detections.publishAll();
        Turnouts.publishAllFeedback();
    }else Serial.printf("MQTT error: %d\n",mqtt->state());
}

void MqttManager::subscribeOutputs(){
    for(const auto& signal:Config.signals)if(signal.enabled)mqtt->subscribe(signal.topic.c_str());
    for(const auto& turnout:Config.turnouts)if(turnout.enabled)mqtt->subscribe(turnout.commandTopic().c_str());
    for(int i=0;i<NUM_OUTPUTS;i++){auto& c=Config.outputs[i];if(c.enabled&&!c.commandTopic.isEmpty())mqtt->subscribe(c.commandTopic.c_str());}
}
void MqttManager::safeOutputs(){
    Signals.reload();
    Turnouts.stop();
    IO.setOutputs(UINT64_MAX,0);
}

bool MqttManager::registerTopics(){
    String topics;
    auto add=[&](const String& topic){
        if(topic.isEmpty())return;
        if(!topics.isEmpty())topics+='\n';
        topics+=topic;
    };
    for(uint8_t i=0;i<NUM_INPUTS;++i)
        if(Config.inputs[i].enabled&&!Config.inputAssigned(i))add(Config.inputs[i].topic);
    for(uint8_t i=0;i<NUM_OUTPUTS;++i){
        const auto& c=Config.outputs[i];
        if(c.enabled&&c.publishState&&!Config.relayAssigned(i))add(c.stateTopic);
    }
    for(const auto& c:Config.cvs)if(c.enabled)add(c.topic());
    for(const auto& c:Config.axleCounters)if(c.enabled)add(c.topic());
    for(const auto& c:Config.turnouts)if(c.enabled)add(c.feedbackTopic());
    const String topic="desconexion/"+Config.mqtt.clientId;
    // The registry can exceed the ordinary MQTT packet buffer.
    if(!mqtt->beginPublish(topic.c_str(),topics.length(),true))return false;
    const size_t written=mqtt->write(reinterpret_cast<const uint8_t*>(topics.c_str()),topics.length());
    return mqtt->endPublish()&&written==topics.length();
}

void MqttManager::loop(){
    if(!mqtt)return;
    if(!Connectivity.connected()||!mqtt->connected()){
        if(wasConnected || !Config.mqtt.host.isEmpty()){safeOutputs();wasConnected=false;}
        reconnect();return;
    }
    mqtt->loop();
    if(!mqtt->connected()){safeOutputs();wasConnected=false;return;}
    if(!managerAvailable)safeOutputs();
    flushInput();
}
void MqttManager::publishInput(uint8_t ch,bool state){
    if(ch>=NUM_INPUTS)return;
    if(Config.inputAssigned(ch)||!Config.inputs[ch].enabled||Config.inputs[ch].topic.isEmpty()){inputQueue.clear(ch);return;}
    inputQueue.set(ch,state);
}
bool MqttManager::inputPending(uint8_t ch)const{return ch<NUM_INPUTS&&!Config.inputAssigned(ch)&&Config.inputs[ch].enabled&&inputQueue.pending(ch);}
void MqttManager::flushInput(){
    const int ch=inputQueue.next(uint32_t(millis()));if(ch<0)return;
    const auto& c=Config.inputs[ch];
    if(Config.inputAssigned(ch)||!c.enabled||c.topic.isEmpty()){inputQueue.clear(ch);return;}
    const String& p=inputQueue.value(ch)?c.payloadOn:c.payloadOff;
    if(mqtt->publish(c.topic.c_str(),p.c_str(),c.retain))inputQueue.clear(ch);
}
void MqttManager::publishOutput(uint8_t ch,bool state){
    if(!mqtt||!mqtt->connected()||ch>=NUM_OUTPUTS)return;auto& c=Config.outputs[ch];
    if(!IO.outputReady(ch)||!c.enabled||!c.publishState||c.stateTopic.isEmpty())return;
    const String& p=state?c.stateOn:c.stateOff;mqtt->publish(c.stateTopic.c_str(),p.c_str(),c.retain);
}
bool MqttManager::publishValue(const String& topic,const String& payload,bool retain){return mqtt&&mqtt->connected()&&!topic.isEmpty()&&mqtt->publish(topic.c_str(),payload.c_str(),retain);}
void MqttManager::callback(char* topic,byte* payload,unsigned int length){
    String t(topic),value;if(length>1792)return;for(unsigned int i=0;i<length;i++)value+=(char)payload[i];
    if(t=="gestor_conexion"){
        if(value=="off"){MQTT.managerAvailable=false;MQTT.safeOutputs();}
        else if(value=="on")MQTT.managerAvailable=true;
        return;
    }
    if(!MQTT.managerAvailable)return;
    if(value=="\"desconexion\""){
        Signals.disconnected(t);
        Turnouts.stop(t);
        for(uint8_t i=0;i<NUM_OUTPUTS;++i)
            if(Config.outputs[i].enabled&&Config.outputs[i].commandTopic==t)IO.setOutput(i,false);
        return;
    }
    Signals.command(t,value);
    Turnouts.command(t,value);
    for(int i=0;i<NUM_OUTPUTS;i++){auto& c=Config.outputs[i];if(Config.relayAssigned(i)||!c.enabled||t!=c.commandTopic)continue;
        if(c.payloadJson){DynamicJsonDocument doc(8192);if(deserializeJson(doc,value.c_str(),value.length()))continue;JsonVariantConst selected;
            if(!JsonPayload::select(doc.as<JsonVariantConst>(),c.jsonPath.c_str(),selected))continue;
            if(JsonPayload::matches(selected,c.payloadOn.c_str()))IO.setOutput(i,true);else if(JsonPayload::matches(selected,c.payloadOff.c_str()))IO.setOutput(i,false);continue;}
        if(value==c.payloadOn)IO.setOutput(i,true);else if(value==c.payloadOff)IO.setOutput(i,false);
    }
}
