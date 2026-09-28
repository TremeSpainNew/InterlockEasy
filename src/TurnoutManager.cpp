#include "TurnoutManager.h"
#include "ConfigManager.h"
#include "IOManager.h"
#include "MqttManager.h"

TurnoutManager Turnouts;

void TurnoutManager::reload(){
    states.assign(Config.turnouts.size(),TurnoutState{});
    for(uint8_t i=0;i<Config.turnouts.size();++i){
        if(!Config.turnouts[i].enabled)continue;
        states[i].feedback=Config.turnouts[i].hasFeedback()?readFeedback(i):-1;
    }
}

const TurnoutState* TurnoutManager::state(uint8_t index)const{
    return index<states.size()?&states[index]:nullptr;
}

void TurnoutManager::drive(uint8_t index,uint8_t position){
    if(index>=Config.turnouts.size()||index>=states.size())return;
    const auto& c=Config.turnouts[index];
    auto& s=states[index];
    const uint32_t n=uint32_t(1)<<(c.outputNormal-1);
    const uint32_t r=uint32_t(1)<<(c.outputReverse-1);
    const uint32_t mask=n|r;
    const uint32_t selected=position==0?n:r;

    // Nunca se energizan simultaneamente las dos salidas.
    IO.setOutputs(mask,selected);
    s.commanded=position;

    if(c.drive==TurnoutDrive::PULSE){
        s.pulseActive=true;
        s.pulseUntil=uint32_t(millis())+c.pulseMs;
    }else{
        s.pulseActive=false;
    }
}

void TurnoutManager::stopPulse(uint8_t index){
    if(index>=Config.turnouts.size()||index>=states.size())return;
    const auto& c=Config.turnouts[index];
    const uint32_t mask=(uint32_t(1)<<(c.outputNormal-1))|(uint32_t(1)<<(c.outputReverse-1));
    IO.setOutputs(mask,0);
    states[index].pulseActive=false;
}

bool TurnoutManager::command(const String& topic,const String& payload){
    for(uint8_t i=0;i<Config.turnouts.size();++i){
        const auto& c=Config.turnouts[i];
        if(!c.enabled||topic!=c.commandTopic())continue;

        String value=payload;
        value.trim();
        if(value=="0"){drive(i,0);return true;}
        if(value=="1"){drive(i,1);return true;}
        return false;
    }
    return false;
}

int8_t TurnoutManager::readFeedback(uint8_t index)const{
    if(index>=Config.turnouts.size())return -2;
    const auto& c=Config.turnouts[index];
    if(!c.enabled||!c.hasFeedback())return -1;

    const uint8_t normal=c.inputNormal-1;
    const uint8_t reverse=c.inputReverse-1;
    if(!IO.inputReady(normal)||!IO.inputReady(reverse))return -2;

    const bool n=IO.getInput(normal);
    const bool r=IO.getInput(reverse);

    if(n&&!r)return 0;
    if(!n&&r)return 1;

    // Ninguna o ambas comprobaciones activas = sin comprobacion valida.
    return -1;
}

void TurnoutManager::publishFeedback(uint8_t index,bool force){
    if(index>=Config.turnouts.size()||index>=states.size())return;
    const auto& c=Config.turnouts[index];
    auto& s=states[index];
    if(!c.enabled)return;

    const int8_t feedback=c.hasFeedback()?readFeedback(index):-1;
    s.feedback=feedback;

    String payload;
    if(feedback==0)payload="0";
    else if(feedback==1)payload="1";
    else payload="";

    const uint32_t now=uint32_t(millis());
    if(force||payload!=s.lastFeedbackPayload||uint32_t(now-s.lastFeedbackPublish)>=30000){
        // No se usa retain: en MQTT un retained con payload vacio borra
        // el retained, y el protocolo define precisamente payload vacio
        // para "sin comprobacion".
        if(MQTT.publishValue(c.feedbackTopic(),payload,false)){
            s.lastFeedbackPayload=payload;
            s.lastFeedbackPublish=now;
        }
    }
}

void TurnoutManager::publishAllFeedback(){
    for(uint8_t i=0;i<Config.turnouts.size();++i)
        if(Config.turnouts[i].enabled)publishFeedback(i,true);
}

void TurnoutManager::loop(){
    const uint32_t now=uint32_t(millis());
    for(uint8_t i=0;i<Config.turnouts.size();++i){
        if(!Config.turnouts[i].enabled)continue;
        auto& s=states[i];
        publishFeedback(i,false);
        if(s.pulseActive){
            const int8_t feedback=readFeedback(i);
            if((feedback==s.commanded) || (long)(now-s.pulseUntil)>=0)
                stopPulse(i);
        }
    }
}
