#pragma once
#include <Arduino.h>
#include <PubSubClient.h>
#include "InputStateQueue.h"

class MqttManager {
public:
    void begin();
    void loop();
    bool connected();
    void reconnect();
    void reload();
    bool inputPending(uint8_t channel) const;
    void publishInput(uint8_t channel, bool state);
    void publishOutput(uint8_t channel, bool state);

private:
    InputStateQueue inputQueue;
    void flushInput();
    PubSubClient* mqtt = nullptr;
    unsigned long lastReconnect = 0;
    void subscribeOutputs();
    static void callback(char* topic, byte* payload, unsigned int length);
};

extern MqttManager MQTT;
