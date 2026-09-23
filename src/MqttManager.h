#pragma once
#include <Arduino.h>
#include <PubSubClient.h>

class MqttManager {
public:
    void begin();
    void loop();
    bool connected();
    void reconnect();
    void reload();
    void publishInput(uint8_t channel, bool state);
    void publishOutput(uint8_t channel, bool state);

private:
    PubSubClient* mqtt = nullptr;
    unsigned long lastReconnect = 0;
    void subscribeOutputs();
    static void callback(char* topic, byte* payload, unsigned int length);
};

extern MqttManager MQTT;
