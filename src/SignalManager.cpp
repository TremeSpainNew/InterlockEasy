#include "SignalManager.h"
#include "ConfigManager.h"
#include "IOManager.h"
#include "JsonPayload.h"
#include "HardwareConfig.h"
#include <Adafruit_INA219.h>
#include <math.h>
#include <Wire.h>

SignalManager Signals;
namespace {
bool sensorBusReady = false;
Adafruit_INA219 ina219[] = {
    Adafruit_INA219(0x40), Adafruit_INA219(0x41),
    Adafruit_INA219(0x42), Adafruit_INA219(0x43),
    Adafruit_INA219(0x44), Adafruit_INA219(0x45),
    Adafruit_INA219(0x46), Adafruit_INA219(0x47),
    Adafruit_INA219(0x48), Adafruit_INA219(0x49),
    Adafruit_INA219(0x4A), Adafruit_INA219(0x4B),
    Adafruit_INA219(0x4C), Adafruit_INA219(0x4D),
    Adafruit_INA219(0x4E), Adafruit_INA219(0x4F)
};
}
SignalManager::SignalManager() { reload(); }
void SignalManager::reload() {
    for (auto& value : active) value = -1;
    sensorCount = 0;
    sensorCursor = 0;
    for (auto& value : sampleValid) value = false;
    for (size_t i = 0; i < Config.signals.size(); ++i) {
        for (size_t j = 0; j < Config.signals[i].lights.size(); ++j) {
            const auto& light = Config.signals[i].lights[j];
            if (!light.currentMonitor || sensorCount >= 64) continue;
            const uint8_t addressIndex = light.ina219Address - 0x40;
            sensorSignals[sensorCount] = i;
            sensorLights[sensorCount] = j;
            ++sensorCount;
            if (!sensorInitialized[addressIndex])
                initializeSensor(light.ina219Address);
        }
    }
}
bool SignalManager::initializeSensor(uint8_t address) {
    const uint8_t index = address - 0x40;
    sensorInitAttempt[index] = millis();
    if (!sensorBusReady) {
        bool initializedByIo = false;
        for (const auto& module : Hardware.modules)
            if (module.type != ModuleType::GPIO &&
                module.type != ModuleType::MODBUS) {
                initializedByIo = true;
                break;
            }
        if (!initializedByIo) {
            if (!Wire.begin(Hardware.sda, Hardware.scl, 100000)) {
                Serial.println("ERROR: no se pudo inicializar el bus I2C para INA219.");
                return false;
            }
            Wire.setTimeOut(50);
        }
        sensorBusReady = true;
    }
    sensorInitialized[index] = ina219[index].begin(&Wire);
    if (sensorInitialized[index])
        ina219[index].setCalibration_32V_2A();
    else
        Serial.printf("ERROR: INA219 0x%02X no disponible.\n", address);
    return sensorInitialized[index];
}
int8_t SignalManager::sensorSlot(uint8_t signal, uint8_t light) const {
    for (size_t i = 0; i < sensorCount; ++i)
        if (sensorSignals[i] == signal && sensorLights[i] == light)
            return i;
    return -1;
}
bool SignalManager::currentMonitored(uint8_t signal, uint8_t light) const {
    return sensorSlot(signal, light) >= 0;
}
bool SignalManager::currentSensorReady(uint8_t signal, uint8_t light) const {
    const int8_t slot = sensorSlot(signal, light);
    if (slot < 0) return false;
    const auto& config = Config.signals[signal].lights[light];
    return sensorInitialized[config.ina219Address - 0x40];
}
bool SignalManager::currentReady(uint8_t signal, uint8_t light) const {
    const int8_t slot = sensorSlot(signal, light);
    if (slot < 0) return false;
    const auto& config = Config.signals[signal].lights[light];
    return sensorInitialized[config.ina219Address - 0x40] && sampleValid[slot];
}
float SignalManager::currentMa(uint8_t signal, uint8_t light) const {
    const int8_t slot = sensorSlot(signal, light);
    return slot < 0 ? 0 : currentSamples[slot];
}
bool SignalManager::currentOk(uint8_t signal, uint8_t light) const {
    const int8_t slot = sensorSlot(signal, light);
    return slot >= 0 && sampleValid[slot] && sampleOk[slot];
}
String SignalManager::aspect(uint8_t index) const {
    if (index >= Config.signals.size() || active[index] < 0) return "";
    return Config.signals[index].aspects[active[index]].value;
}
bool SignalManager::apply(uint8_t index, uint8_t aspectIndex, bool phase) {
    const auto& signal = Config.signals[index];
    const auto& aspect = signal.aspects[aspectIndex];
    const uint8_t local = aspect.mask | (phase ? aspect.blink : 0);
    uint64_t group = signal.relayMask();
    uint64_t physical = 0;
    for (size_t i = 0; i < signal.lights.size(); ++i)
        if (local & (1U << i)) physical |= (uint64_t(1) << (signal.lights[i].relay - 1));
    for (const auto& beacon : Config.beacons) {
        if (!beacon.enabled || beacon.signalId != signal.id) continue;
        group |= beacon.relayMask();
        for (const auto& beaconAspect : beacon.aspects) {
            if (beaconAspect.value != aspect.value) continue;
            const uint8_t beaconLocal =
                beaconAspect.mask | (phase ? beaconAspect.blink : 0);
            for (size_t i = 0; i < beacon.relays.size(); ++i)
                if (beaconLocal & (1U << i))
                    physical |= uint64_t(1) << (beacon.relays[i] - 1);
            break;
        }
    }
    return IO.setOutputs(group, physical);
}
void SignalManager::command(const String& topic, const String& payload) {
    for (size_t i = 0; i < Config.signals.size(); ++i) {
        const auto& signal = Config.signals[i];
        if (!signal.enabled || signal.topic != topic) continue;
        String incoming;
        if (signal.numeric) {
            incoming = payload;
        } else {
            DynamicJsonDocument doc(8192);
            if (deserializeJson(doc, payload.c_str(), payload.length())) continue;
            JsonVariantConst value;
            if (!JsonPayload::select(doc.as<JsonVariantConst>(), signal.jsonPath.c_str(), value) ||
                !value.is<const char*>()) continue;
            incoming = value.as<String>();
        }
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
    if (!sensorCount || now - lastSensorTick < 20) return;
    lastSensorTick = now;
    const size_t slot = sensorCursor;
    sensorCursor = (sensorCursor + 1) % sensorCount;
    const uint8_t signalIndex = sensorSignals[slot];
    const uint8_t lightIndex = sensorLights[slot];
    const auto& light = Config.signals[signalIndex].lights[lightIndex];
    const uint8_t addressIndex = light.ina219Address - 0x40;
    if (!sensorInitialized[addressIndex]) {
        if (now - sensorInitAttempt[addressIndex] >= 5000)
            initializeSensor(light.ina219Address);
        return;
    }
    if (!IO.outputReady(light.relay - 1) ||
        !IO.getOutput(light.relay - 1)) {
        sampleValid[slot] = false;
        return;
    }
    const float current = ina219[addressIndex].getCurrent_mA();
    if (!isfinite(current)) {
        sampleValid[slot] = false;
        return;
    }
    currentSamples[slot] = current;
    sampleOk[slot] = current >= light.minCurrentMa &&
                     current <= light.maxCurrentMa;
    sampleValid[slot] = true;
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

void SignalManager::disconnected(const String& topic){
    for(size_t i=0;i<Config.signals.size();++i){
        const auto& signal=Config.signals[i];
        if(!signal.enabled||signal.topic!=topic)continue;
        active[i]=-1;
        uint64_t group = signal.relayMask();
        for (const auto& beacon : Config.beacons)
            if (beacon.enabled && beacon.signalId == signal.id)
                group |= beacon.relayMask();
        IO.setOutputs(group,0);
    }
}
