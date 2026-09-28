#include "IOManager.h"
#include "ConfigManager.h"
#include "MqttManager.h"
#include "HardwareIO.h"

IOManager IO;
void IOManager::begin(){lastScan=uint32_t(millis());relayReady=HardwarePorts.begin();for(uint8_t i=0;i<NUM_INPUTS;++i){simulated[i]=false;bool v=false;inputValid[i]=HardwarePorts.read(i,v);sampleValid[i]=inputValid[i];inputState[i]=candidateState[i]=v;candidateSince[i]=uint32_t(millis());}if(!relayReady)Serial.println("ERROR: hardware I/O no inicializado. Salidas bloqueadas.");}
void IOManager::refreshInputs(){loop();}
void IOManager::loop(){
    const uint32_t now=uint32_t(millis());if(uint32_t(now-lastScan)<5)return;const bool gap=uint32_t(now-lastScan)>20;lastScan=now;HardwarePorts.sample();
    for(uint8_t i=0;i<NUM_INPUTS;++i){
        if(simulated[i]&&uint32_t(now-simulationSince[i])>=60000)simulateInput(i,-1);
        bool v=false;if(!HardwarePorts.read(i,v)){inputValid[i]=false;sampleValid[i]=false;candidateSince[i]=now;continue;}
        if(!sampleValid[i]){candidateState[i]=v;candidateSince[i]=now;}sampleValid[i]=true;
        if(v!=candidateState[i]||gap){candidateState[i]=v;candidateSince[i]=now;}
        const uint16_t debounce=Config.inputAssigned(i)?5:Config.inputs[i].debounceMs;
        if((!inputValid[i]||candidateState[i]!=inputState[i])&&uint32_t(now-candidateSince[i])>=debounce){
            inputValid[i]=true;inputState[i]=candidateState[i];const bool logical=getInput(i);Serial.printf("DI%d = %d\n",i+1,logical);
            if(!simulated[i]&&!Config.inputAssigned(i))MQTT.publishInput(i,logical);
        }
    }
}
bool IOManager::getInput(uint8_t ch){if(ch>=NUM_INPUTS)return false;return simulated[ch]?simulatedState[ch]:inputState[ch]!=Config.inputs[ch].inverted;}
bool IOManager::inputSimulated(uint8_t ch)const{return ch<NUM_INPUTS&&simulated[ch];}
bool IOManager::simulateInput(uint8_t ch,int8_t state){if(ch>=NUM_INPUTS||state< -1||state>1)return false;simulated[ch]=state>=0;simulatedState[ch]=state==1;simulationSince[ch]=uint32_t(millis());if(inputReady(ch)&&!Config.inputAssigned(ch))MQTT.publishInput(ch,getInput(ch));return true;}
bool IOManager::getRawInput(uint8_t ch)const{return ch<NUM_INPUTS?candidateState[ch]:false;}
bool IOManager::inputFiltering(uint8_t ch)const{return ch<NUM_INPUTS&&candidateState[ch]!=inputState[ch];}
bool IOManager::getOutput(uint8_t ch){return ch<NUM_OUTPUTS&&(HardwarePorts.states&(uint32_t(1)<<ch));}
void IOManager::setOutput(uint8_t ch,bool state){if(ch>=NUM_OUTPUTS||Config.relayAssigned(ch)||getOutput(ch)==state)return;const uint32_t bit=uint32_t(1)<<ch;if(setOutputs(bit,state?bit:0))MQTT.publishOutput(ch,state);}
bool IOManager::setOutputs(uint32_t mask,uint32_t states){return relayReady&&HardwarePorts.write(mask,states);}
