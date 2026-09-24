#include "IOManager.h"
#include "ConfigManager.h"
#include "MqttManager.h"
#include <Wire.h>

IOManager IO;

static const uint8_t INPUT_PINS[8] = {4,5,6,7,8,9,10,11};
static constexpr uint8_t RELAY_ADDRESS = 0x20;
static constexpr uint8_t RELAY_OUTPUT = 0x01;
static constexpr uint8_t RELAY_CONFIG = 0x03;

void IOManager::begin() {
    lastScan = uint32_t(millis());
    for (int i = 0; i < 8; i++) {
        // Bias the optocoupler input high at rest (active-low board inputs).
        pinMode(INPUT_PINS[i], INPUT_PULLUP);
        simulated[i] = false;
        inputState[i] = readInput(i);
        candidateState[i] = inputState[i];
        candidateSince[i] = uint32_t(millis());
        outputState[i] = false;
    }

    relayReady = false;
    relayMask = 0;
    if (!Wire.begin(42, 41, 100000)) {
        Serial.println("ERROR: no se pudo inicializar I2C para los reles.");
        return;
    }
    Wire.setTimeOut(50);
    // Preload OFF before enabling outputs: TCA9554 powers up with latch bits HIGH.
    if (!writeRelayRegister(RELAY_OUTPUT, 0x00) ||
        !writeRelayRegister(RELAY_CONFIG, 0x00)) {
        Serial.println("ERROR: TCA9554 no inicializado; mandos de reles bloqueados.");
        return;
    }
    relayReady = true;
    Serial.println("TCA9554 listo; RO1..RO8 apagados.");
}

bool IOManager::readInput(uint8_t channel) {
    return digitalRead(INPUT_PINS[channel]);
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
    for (uint8_t i = 0; i < 8; ++i) {
        if (simulated[i] && uint32_t(now - simulationSince[i]) >= 60000) simulateInput(i, -1);
        const bool value = readInput(i);
        if (value != candidateState[i] || samplingGap) {
            candidateState[i] = value;
            candidateSince[i] = now;
        }
        if (candidateState[i] != inputState[i] &&
            uint32_t(now - candidateSince[i]) >= Config.inputs[i].debounceMs) {
            inputState[i] = candidateState[i];
            const bool logical = getInput(i);
            Serial.printf("DI%d = %d\n", i + 1, logical);
            if (!simulated[i]) MQTT.publishInput(i, logical);
        }
    }
}

bool IOManager::getInput(uint8_t channel) {
    if (channel >= 8) return false;
    return simulated[channel] ? simulatedState[channel] : inputState[channel] != Config.inputs[channel].inverted;
}

bool IOManager::inputSimulated(uint8_t channel) const {
    return channel < 8 && simulated[channel];
}

bool IOManager::simulateInput(uint8_t channel, int8_t state) {
    if (channel >= 8 || state < -1 || state > 1) return false;
    simulated[channel] = state >= 0;
    simulatedState[channel] = state == 1;
    simulationSince[channel] = uint32_t(millis());
    MQTT.publishInput(channel, getInput(channel));
    return true;
}

bool IOManager::getRawInput(uint8_t channel) const {
    return channel < 8 ? candidateState[channel] : false;
}

bool IOManager::inputFiltering(uint8_t channel) const {
    return channel < 8 && candidateState[channel] != inputState[channel];
}

bool IOManager::getOutput(uint8_t channel) {
    return channel < 8 ? outputState[channel] : false;
}

void IOManager::setOutput(uint8_t channel, bool state) {
    if (channel >= 8 || Config.relayAssigned(channel)) return;
    if (!relayReady) {
        Serial.println("ERROR: TCA9554 no disponible.");
        return;
    }
    if (outputState[channel] == state) return;

    if (!writeRelay(channel, state)) return;
    outputState[channel] = state;
    MQTT.publishOutput(channel, state);
}

bool IOManager::writeRelayRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(RELAY_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    const uint8_t error = Wire.endTransmission();
    if (error != 0) {
        Serial.printf("ERROR TCA9554: registro 0x%02X, I2C=%u\n", reg, error);
        return false;
    }
    return true;
}

bool IOManager::writeRelay(uint8_t channel, bool state) {
    // EXIO1..EXIO8 correspond to bits 0..7; HIGH energizes the relay.
    const uint8_t bit = uint8_t(1U << channel);
    const uint8_t nextMask = state ? (relayMask | bit) : (relayMask & ~bit);
    if (!writeRelayRegister(RELAY_OUTPUT, nextMask)) return false;
    relayMask = nextMask;
    Serial.printf("RO%d -> %s\n", channel + 1, state ? "ON" : "OFF");
    return true;
}

bool IOManager::setOutputs(uint8_t mask, uint8_t states) {
    if (!relayReady) return false;
    const uint8_t nextMask = (relayMask & ~mask) | (states & mask);
    if (nextMask == relayMask) return true;
    if (!writeRelayRegister(RELAY_OUTPUT, nextMask)) return false;
    relayMask = nextMask;
    for (uint8_t i = 0; i < 8; ++i) outputState[i] = (relayMask & (1U << i)) != 0;
    return true;
}
