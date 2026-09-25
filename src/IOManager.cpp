#include "IOManager.h"
#include "ConfigManager.h"
#include "MqttManager.h"
#include "HardwareIO.h"

IOManager IO;

void IOManager::begin() {
    lastScan = uint32_t(millis());
    relayReady = HardwarePorts.begin();
    for (uint8_t i=0;i<NUM_INPUTS;++i) {
        simulated[i]=false;
        bool value=false; inputValid[i]=HardwarePorts.read(i,value);
        sampleValid[i]=inputValid[i]; inputState[i]=candidateState[i]=value; candidateSince[i]=uint32_t(millis());
    }
    if(!relayReady)Serial.println("ERROR: hardware I/O no inicializado. Salidas bloqueadas.");
}

void IOManager::refreshInputs() {
    // Reconnection/configuration changes must not bypass the debounce filter.
    loop();
}

void IOManager::loop() {
    const uint32_t now = uint32_t(millis());
    if (uint32_t(now - lastScan) < 5) return;
    const bool samplingGap = uint32_t(now - lastScan) > 20;
    lastScan = now;
    HardwarePorts.sample();
    for (uint8_t i = 0; i < NUM_INPUTS; ++i) {
        if (simulated[i] && uint32_t(now - simulationSince[i]) >= 60000) simulateInput(i, -1);
        bool value=false;
        if (!HardwarePorts.read(i,value)) { inputValid[i]=false; sampleValid[i]=false; candidateSince[i]=now; continue; }
        if (!sampleValid[i]) { candidateState[i]=value; candidateSince[i]=now; }
        sampleValid[i]=true;
        if (value != candidateState[i] || samplingGap) {
            candidateState[i] = value;
            candidateSince[i] = now;
        }
        if ((!inputValid[i] || candidateState[i] != inputState[i]) &&
            uint32_t(now - candidateSince[i]) >= Config.inputs[i].debounceMs) {
            inputValid[i] = true;
            inputState[i] = candidateState[i];
            const bool logical = getInput(i);
            Serial.printf("DI%d = %d\n", i + 1, logical);
            if (!simulated[i]) MQTT.publishInput(i, logical);
        }
    }
}

bool IOManager::getInput(uint8_t channel) {
    if (channel >= NUM_INPUTS) return false;
    return simulated[channel] ? simulatedState[channel] : inputState[channel] != Config.inputs[channel].inverted;
}

bool IOManager::inputSimulated(uint8_t channel) const {
    return channel < NUM_INPUTS && simulated[channel];
}

bool IOManager::simulateInput(uint8_t channel, int8_t state) {
    if (channel >= NUM_INPUTS || state < -1 || state > 1) return false;
    simulated[channel] = state >= 0;
    simulatedState[channel] = state == 1;
    simulationSince[channel] = uint32_t(millis());
    if (inputReady(channel)) MQTT.publishInput(channel, getInput(channel));
    return true;
}

bool IOManager::getRawInput(uint8_t channel) const {
    return channel < NUM_INPUTS ? candidateState[channel] : false;
}

bool IOManager::inputFiltering(uint8_t channel) const {
    return channel < NUM_INPUTS && candidateState[channel] != inputState[channel];
}

bool IOManager::getOutput(uint8_t channel) {
    return channel < NUM_OUTPUTS && (HardwarePorts.states & (uint32_t(1)<<channel));
}
void IOManager::setOutput(uint8_t channel, bool state) {
    if(channel>=NUM_OUTPUTS || Config.relayAssigned(channel) || getOutput(channel)==state)return;
    const uint32_t bit=uint32_t(1)<<channel;
    if(setOutputs(bit,state?bit:0))MQTT.publishOutput(channel,state);
}
bool IOManager::setOutputs(uint32_t mask, uint32_t states) {
    return relayReady && HardwarePorts.write(mask,states);
}
