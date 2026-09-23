#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <vector>

#define NUM_INPUTS 8
#define NUM_OUTPUTS 8

struct InputConfig {
    bool enabled = false;
    String name;
    String topic;
    bool payloadJson = false;
    String payloadOn = "1";
    String payloadOff = "0";
    bool inverted = false;
    bool retain = true;
};

struct OutputConfig {
    bool enabled = false;
    String name;
    String commandTopic;
    String jsonPath;
    bool payloadJson = false;
    String payloadOn = "1";
    String payloadOff = "0";
    bool publishState = true;
    String stateTopic;
    bool stateJson = false;
    String stateOn = "1";
    String stateOff = "0";
    bool retain = true;
};

struct MQTTConfig {
    String host = "";
    uint16_t port = 1883;
    String clientId = "InterlockEasy-IO";
    String username = "";
    String password = "";
    uint16_t keepAlive = 30;
};

struct SignalLight {
    String name;
    uint8_t relay = 1; // Physical RO number, 1..8.
};
struct SignalAspect {
    String value;
    uint8_t mask = 0; // Fixed lights, local indices.
    uint8_t blink = 0;
};
struct SignalConfig {
    bool enabled = false;
    String name;
    String topic;
    String jsonPath = "Aspecto";
    uint16_t blinkMs = 500; // Duration of each ON/OFF phase.
    std::vector<SignalLight> lights;
    std::vector<SignalAspect> aspects;
    uint8_t relayMask() const {
        uint8_t mask = 0;
        for (const auto& light : lights) mask |= uint8_t(1U << (light.relay - 1));
        return mask;
    }
};

class ConfigManager {
public:
    MQTTConfig mqtt;
    std::vector<SignalConfig> signals;
    bool relayAssigned(uint8_t channel) const;
    InputConfig inputs[NUM_INPUTS];
    OutputConfig outputs[NUM_OUTPUTS];

    void begin();
    void load();
    bool save();
    void toJson(JsonDocument& doc, bool secrets = false) const;
    bool applyJson(JsonVariantConst doc, String& error, bool persist = true);

private:
    Preferences prefs;
};

extern ConfigManager Config;
