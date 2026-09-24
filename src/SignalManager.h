#pragma once
#include <Arduino.h>

class SignalManager {
public:
    SignalManager();
    void reload();
    void loop();
    bool testAspect(uint8_t signal, const String& value);
    void command(const String& topic, const String& payload);
    String aspect(uint8_t signal) const;
private:
    int8_t active[8];
    unsigned long started[8] = {};
    unsigned long lastTick = 0;
    bool apply(uint8_t signal, uint8_t aspect, bool phase);
};
extern SignalManager Signals;
