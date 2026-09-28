#include "DetectionManager.h"
#include "ConfigManager.h"
#include "IOManager.h"
#include "MqttManager.h"

DetectionManager Detections;

void DetectionManager::reload() {
    states.assign(Config.trackSections.size(), TrackSectionState{});
    for (size_t i = 0; i < states.size(); ++i) {
        const auto& cfg = Config.trackSections[i];
        states[i].lastA = cfg.inputA && IO.inputReady(cfg.inputA - 1) && IO.getInput(cfg.inputA - 1);
        states[i].lastB = cfg.inputB && IO.inputReady(cfg.inputB - 1) && IO.getInput(cfg.inputB - 1);
        if (cfg.type == TrackSectionType::AXLE_COUNTER) {
            states[i].uncertain = true;
            states[i].occupied = true;
        }
    }
}

const TrackSectionState* DetectionManager::state(uint8_t index) const {
    return index < states.size() ? &states[index] : nullptr;
}

void DetectionManager::publish(uint8_t index, bool includeCount) {
    if (index >= states.size()) return;
    const auto& cfg = Config.trackSections[index];
    if (!cfg.enabled) return;
    MQTT.publishValue(cfg.stateTopic, states[index].occupied ? cfg.payloadOccupied : cfg.payloadFree, cfg.retain);
    if (includeCount && cfg.type == TrackSectionType::AXLE_COUNTER && !cfg.countTopic.isEmpty())
        MQTT.publishValue(cfg.countTopic, String(states[index].count), cfg.retain);
}

void DetectionManager::publishAll() {
    for (uint8_t i = 0; i < states.size(); ++i) publish(i);
}

bool DetectionManager::reset(uint8_t index) {
    if (index >= states.size() || Config.trackSections[index].type != TrackSectionType::AXLE_COUNTER) return false;
    auto& value = states[index];
    value.count = 0; value.entrance = -1; value.uncertain = false; value.occupied = false;
    publish(index);
    return true;
}

void DetectionManager::loop() {
    for (uint8_t i = 0; i < states.size(); ++i) {
        const auto& cfg = Config.trackSections[i];
        if (!cfg.enabled || cfg.inputA < 1 || cfg.inputA > NUM_INPUTS || !IO.inputReady(cfg.inputA - 1)) continue;
        auto& value = states[i];
        const bool a = IO.getInput(cfg.inputA - 1);
        if (cfg.type == TrackSectionType::LINEAR) {
            if (a != value.occupied) { value.occupied = a; publish(i, false); }
            value.lastA = a;
            continue;
        }
        if (cfg.inputB < 1 || cfg.inputB > NUM_INPUTS || !IO.inputReady(cfg.inputB - 1)) continue;
        const bool b = IO.getInput(cfg.inputB - 1);
        const bool edgeA = a && !value.lastA, edgeB = b && !value.lastB;
        value.lastA = a; value.lastB = b;
        if (value.uncertain || (!edgeA && !edgeB)) continue;
        const uint16_t previous = value.count;
        auto pulse = [&](int8_t side) {
            if (value.count == 0) { value.entrance = side; value.count = 1; }
            else if (side == value.entrance) { if (value.count < 65535) ++value.count; }
            else if (--value.count == 0) value.entrance = -1;
        };
        if (edgeA) pulse(0);
        if (edgeB) pulse(1);
        value.occupied = value.count > 0;
        if (value.count != previous) publish(i);
    }
}
