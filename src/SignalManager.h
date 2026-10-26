#pragma once
#include <Arduino.h>

class SignalManager {
public:
    SignalManager();
    void reload();
    void disconnected(const String& topic);
    void loop();
    bool testAspect(uint8_t signal, const String& value);
    void command(const String& topic, const String& payload);
    String aspect(uint8_t signal) const;
    bool currentMonitored(uint8_t signal, uint8_t light) const;
    bool currentSensorReady(uint8_t signal, uint8_t light) const;
    bool currentReady(uint8_t signal, uint8_t light) const;
    float currentMa(uint8_t signal, uint8_t light) const;
    bool currentOk(uint8_t signal, uint8_t light) const;
private:
    bool initializeSensor(uint8_t address);
    int8_t sensorSlot(uint8_t signal, uint8_t light) const;
    int8_t active[8];
    unsigned long started[8] = {};
    unsigned long lastTick = 0;
    uint8_t sensorSignals[64] = {};
    uint8_t sensorLights[64] = {};
    size_t sensorCount = 0;
    size_t sensorCursor = 0;
    unsigned long lastSensorTick = 0;
    bool sensorInitialized[16] = {};
    unsigned long sensorInitAttempt[16] = {};
    bool sampleValid[64] = {};
    bool sampleOk[64] = {};
    float currentSamples[64] = {};
    bool apply(uint8_t signal, uint8_t aspect, bool phase);
};
extern SignalManager Signals;
