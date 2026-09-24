#include "SignalManager.h"
#include "ConfigManager.h"
#include "IOManager.h"
#include "JsonPayload.h"

SignalManager Signals;
SignalManager::SignalManager() { reload(); }
void SignalManager::reload() {
    for (auto& value : active) value = -1;
}
String SignalManager::aspect(uint8_t index) const {
    if (index >= Config.signals.size() || active[index] < 0) return "";
    return Config.signals[index].aspects[active[index]].value;
}
bool SignalManager::apply(uint8_t index, uint8_t aspectIndex, bool phase) {
    const auto& signal = Config.signals[index];
    const auto& aspect = signal.aspects[aspectIndex];
    const uint8_t local = aspect.mask | (phase ? aspect.blink : 0);
    uint8_t physical = 0;
    for (size_t i = 0; i < signal.lights.size(); ++i)
        if (local & (1U << i)) physical |= uint8_t(1U << (signal.lights[i].relay - 1));
    return IO.setOutputs(signal.relayMask(), physical);
}
void SignalManager::command(const String& topic, const String& payload) {
    for (size_t i = 0; i < Config.signals.size(); ++i) {
        const auto& signal = Config.signals[i];
        if (!signal.enabled || signal.topic != topic) continue;
        DynamicJsonDocument doc(8192);
        if (deserializeJson(doc, payload.c_str(), payload.length())) continue;
        JsonVariantConst value;
        if (!JsonPayload::select(doc.as<JsonVariantConst>(), signal.jsonPath.c_str(), value) || !value.is<const char*>()) continue;
        const String incoming = value.as<String>();
        for (size_t j = 0; j < signal.aspects.size(); ++j) {
            if (signal.aspects[j].value != incoming) continue;
            if (active[i] == int(j)) break; // Repeated retained command preserves blink phase.
            if (apply(i, j, true)) { active[i] = j; started[i] = millis(); }
            break;
        }
    }
}
void SignalManager::loop() {
    const unsigned long now = millis();
    if (now - lastTick < 20) return;
    lastTick = now;
    for (size_t i = 0; i < Config.signals.size(); ++i) {
        const auto& signal = Config.signals[i];
        if (!signal.enabled || active[i] < 0) continue;
        // Retry failed I2C writes on following ticks; never alter another signal's bits.
        apply(i, active[i], ((now - started[i]) / signal.blinkMs) % 2 == 0);
    }
}

bool SignalManager::testAspect(uint8_t index, const String& value) {
    if (index >= Config.signals.size() || !Config.signals[index].enabled) return false;
    const auto& signal = Config.signals[index];
    for (size_t j = 0; j < signal.aspects.size(); ++j) {
        if (signal.aspects[j].value != value) continue;
        if (!apply(index, j, true)) return false;
        active[index] = j; started[index] = millis(); return true;
    }
    return false;
}
