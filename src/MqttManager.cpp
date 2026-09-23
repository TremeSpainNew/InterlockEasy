#include "MqttManager.h"
#include "ConfigManager.h"
#include "EthernetManager.h"
#include "IOManager.h"
#include "JsonPayload.h"
#include "SignalManager.h"

MqttManager MQTT;

void MqttManager::begin() {
    mqtt = new PubSubClient(Network.createClient());
    reload();
    mqtt->setCallback(callback);
}

void MqttManager::reload() {
    if (!mqtt) return;
    mqtt->disconnect();
    mqtt->setBufferSize(2048);
    mqtt->setSocketTimeout(1);
    lastReconnect = millis();
    mqtt->setServer(Config.mqtt.host.c_str(), Config.mqtt.port);
    mqtt->setKeepAlive(Config.mqtt.keepAlive);
}

bool MqttManager::connected() {
    return mqtt && mqtt->connected();
}

void MqttManager::reconnect() {
    if (!mqtt || Config.mqtt.host.isEmpty() || !Network.connected() || mqtt->connected()) return;
    if (millis() - lastReconnect < 5000) return;
    lastReconnect = millis();

    bool ok;
    if (!Config.mqtt.username.isEmpty()) {
        ok = mqtt->connect(Config.mqtt.clientId.c_str(),
                           Config.mqtt.username.c_str(),
                           Config.mqtt.password.c_str());
    } else {
        ok = mqtt->connect(Config.mqtt.clientId.c_str());
    }

    if (ok) {
        Serial.println("MQTT conectado");
        subscribeOutputs();
        IO.refreshInputs();
        for (uint8_t i = 0; i < NUM_INPUTS; ++i) publishInput(i, IO.getInput(i));
        if (IO.outputsReady())
            for (uint8_t i = 0; i < NUM_OUTPUTS; ++i) publishOutput(i, IO.getOutput(i));
    } else {
        Serial.printf("MQTT error: %d\n", mqtt->state());
    }
}

void MqttManager::subscribeOutputs() {
    for (const auto& signal : Config.signals)
        if (signal.enabled) mqtt->subscribe(signal.topic.c_str());
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        auto &cfg = Config.outputs[i];
        if (cfg.enabled && !cfg.commandTopic.isEmpty()) {
            mqtt->subscribe(cfg.commandTopic.c_str());
        }
    }
}

void MqttManager::loop() {
    if (!mqtt) return;
    if (!mqtt->connected()) {
        reconnect();
        return;
    }
    mqtt->loop();
}

void MqttManager::publishInput(uint8_t channel, bool state) {
    if (!mqtt || !mqtt->connected() || channel >= NUM_INPUTS) return;
    auto &cfg = Config.inputs[channel];
    if (!cfg.enabled || cfg.topic.isEmpty()) return;

    const String &payload = state ? cfg.payloadOn : cfg.payloadOff;
    mqtt->publish(cfg.topic.c_str(), payload.c_str(), cfg.retain);
}

void MqttManager::publishOutput(uint8_t channel, bool state) {
    if (!mqtt || !mqtt->connected() || channel >= NUM_OUTPUTS) return;
    auto &cfg = Config.outputs[channel];
    if (!cfg.enabled || !cfg.publishState || cfg.stateTopic.isEmpty()) return;

    const String &payload = state ? cfg.stateOn : cfg.stateOff;
    mqtt->publish(cfg.stateTopic.c_str(), payload.c_str(), cfg.retain);
}

void MqttManager::callback(char* topic, byte* payload, unsigned int length) {
    String t(topic), value;
    if (length > 1792) return;
    for (unsigned int i = 0; i < length; i++) value += (char)payload[i];

    Signals.command(t, value);
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        auto &cfg = Config.outputs[i];
        if (Config.relayAssigned(i) || !cfg.enabled || t != cfg.commandTopic) continue;

        if (cfg.payloadJson) {
            DynamicJsonDocument doc(8192);
            if (deserializeJson(doc, value.c_str(), value.length())) continue;
            JsonVariantConst selected;
            if (!JsonPayload::select(doc.as<JsonVariantConst>(), cfg.jsonPath.c_str(), selected)) continue;
            if (JsonPayload::matches(selected, cfg.payloadOn.c_str())) IO.setOutput(i, true);
            else if (JsonPayload::matches(selected, cfg.payloadOff.c_str())) IO.setOutput(i, false);
            continue;
        }
        if (value == cfg.payloadOn) IO.setOutput(i, true);
        else if (value == cfg.payloadOff) IO.setOutput(i, false);
    }
}
