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
    for (int i = 0; i < 8; i++) {
        pinMode(INPUT_PINS[i], INPUT);
        inputState[i] = readInput(i);
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
    bool value = digitalRead(INPUT_PINS[channel]);
    if (Config.inputs[channel].inverted) value = !value;
    return value;
}

void IOManager::refreshInputs() {
    for (uint8_t i = 0; i < 8; ++i) inputState[i] = readInput(i);
}

void IOManager::loop() {
    if (millis() - lastScan < 20) return;
    lastScan = millis();

    for (int i = 0; i < 8; i++) {
        bool value = readInput(i);
        if (value != inputState[i]) {
            inputState[i] = value;
            Serial.printf("DI%d = %d\n", i + 1, value);
            MQTT.publishInput(i, value);
        }
    }
}

bool IOManager::getInput(uint8_t channel) {
    return channel < 8 ? inputState[channel] : false;
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
