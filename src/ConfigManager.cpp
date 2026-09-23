#include "ConfigManager.h"
#include "JsonPayload.h"

ConfigManager Config;

void ConfigManager::begin() {
    prefs.begin("interlock", false);
    load();
    String stored = prefs.getString("config_v1", "");
    if (!stored.isEmpty()) {
        DynamicJsonDocument doc(32768);
        String error;
        if (deserializeJson(doc, stored) || !applyJson(doc.as<JsonVariantConst>(), error, false))
            Serial.println("ERROR: configuracion NVS no valida; se conserva la configuracion anterior.");
    }
}

void ConfigManager::load() {
    mqtt.host = prefs.getString("mqtt_host", "");
    mqtt.port = prefs.getUShort("mqtt_port", 1883);
    mqtt.clientId = prefs.getString("mqtt_client", "InterlockEasy-IO");
    mqtt.username = prefs.getString("mqtt_user", "");
    mqtt.password = prefs.getString("mqtt_pass", "");
    mqtt.keepAlive = prefs.getUShort("mqtt_keep", 30);

    for (int i = 0; i < NUM_INPUTS; i++) {
        String p = "i" + String(i);
        inputs[i].enabled = prefs.getBool((p + "e").c_str(), false);
        inputs[i].name = prefs.getString((p + "n").c_str(), "");
        inputs[i].topic = prefs.getString((p + "t").c_str(), "");
        inputs[i].payloadOn = prefs.getString((p + "on").c_str(), "1");
        inputs[i].payloadOff = prefs.getString((p + "off").c_str(), "0");
        inputs[i].inverted = prefs.getBool((p + "inv").c_str(), true);
        inputs[i].retain = prefs.getBool((p + "ret").c_str(), true);
    }

    for (int i = 0; i < NUM_OUTPUTS; i++) {
        String p = "o" + String(i);
        outputs[i].enabled = prefs.getBool((p + "e").c_str(), false);
        outputs[i].name = prefs.getString((p + "n").c_str(), "");
        outputs[i].commandTopic = prefs.getString((p + "ct").c_str(), "");
        outputs[i].payloadOn = prefs.getString((p + "on").c_str(), "1");
        outputs[i].payloadOff = prefs.getString((p + "off").c_str(), "0");
        outputs[i].publishState = prefs.getBool((p + "ps").c_str(), true);
        outputs[i].stateTopic = prefs.getString((p + "st").c_str(), "");
        outputs[i].stateOn = prefs.getString((p + "son").c_str(), "1");
        outputs[i].stateOff = prefs.getString((p + "sof").c_str(), "0");
        outputs[i].retain = prefs.getBool((p + "ret").c_str(), true);
    }
}

void ConfigManager::toJson(JsonDocument& doc, bool secrets) const {
    doc.clear();
    JsonObject m = doc.createNestedObject("mqtt");
    m["host"] = mqtt.host;
    m["port"] = mqtt.port;
    m["clientId"] = mqtt.clientId;
    m["username"] = mqtt.username;
    if (secrets) m["password"] = mqtt.password;
    m["passwordSet"] = !mqtt.password.isEmpty();
    m["keepAlive"] = mqtt.keepAlive;
    JsonArray inputsArray = doc.createNestedArray("inputs");
    for (int i = 0; i < 8; ++i) {
        JsonObject item = inputsArray.createNestedObject();
        item["enabled"] = inputs[i].enabled;
        item["name"] = inputs[i].name;
        item["topic"] = inputs[i].topic;
        item["payloadJson"] = inputs[i].payloadJson;
        item["payloadOn"] = inputs[i].payloadOn;
        item["payloadOff"] = inputs[i].payloadOff;
        item["inverted"] = inputs[i].inverted;
        item["debounceMs"] = inputs[i].debounceMs;
        item["retain"] = inputs[i].retain;
    }
    JsonArray outputsArray = doc.createNestedArray("outputs");
    for (int i = 0; i < 8; ++i) {
        JsonObject item = outputsArray.createNestedObject();
        item["enabled"] = outputs[i].enabled;
        item["name"] = outputs[i].name;
        item["commandTopic"] = outputs[i].commandTopic;
        item["payloadJson"] = outputs[i].payloadJson;
        item["payloadOn"] = outputs[i].payloadOn;
        item["payloadOff"] = outputs[i].payloadOff;
        item["publishState"] = outputs[i].publishState;
        item["stateTopic"] = outputs[i].stateTopic;
        item["stateJson"] = outputs[i].stateJson;
        item["jsonPath"] = outputs[i].jsonPath;
        item["stateOn"] = outputs[i].stateOn;
        item["stateOff"] = outputs[i].stateOff;
        item["retain"] = outputs[i].retain;
    }
    JsonArray signalArray = doc.createNestedArray("signals");
    for (const auto& signal : signals) {
        JsonObject item = signalArray.createNestedObject();
        item["enabled"] = signal.enabled;
        item["name"] = signal.name;
        item["topic"] = signal.topic;
        item["jsonPath"] = signal.jsonPath;
        item["blinkMs"] = signal.blinkMs;
        JsonArray lights = item.createNestedArray("lights");
        for (const auto& light : signal.lights) {
            JsonObject entry = lights.createNestedObject();
            entry["name"] = light.name; entry["relay"] = light.relay;
        }
        JsonArray aspects = item.createNestedArray("aspects");
        for (const auto& aspect : signal.aspects) {
            JsonObject entry = aspects.createNestedObject();
            entry["value"] = aspect.value;
            JsonArray blink = entry.createNestedArray("blink");
            for (size_t i = 0; i < signal.lights.size(); ++i)
                if (aspect.blink & (1U << i)) blink.add(i + 1);
            JsonArray on = entry.createNestedArray("on");
            for (size_t i = 0; i < signal.lights.size(); ++i)
                if (aspect.mask & (1U << i)) on.add(i + 1);
        }
    }

}

bool ConfigManager::save() {
    DynamicJsonDocument doc(32768);
    toJson(doc, true);
    if (doc.overflowed()) return false;
    String value;
    serializeJson(doc, value);
    return prefs.putString("config_v1", value) == value.length();
}

namespace {
bool textField(JsonVariantConst value, size_t maxLength, bool multiline = false) {
    if (!value.is<const char*>()) return false;
    JsonString text = value.as<JsonString>();
    if (text.size() > maxLength) return false;
    for (size_t i = 0; i < text.size(); ++i)
        if (static_cast<unsigned char>(text.c_str()[i]) < 32 &&
            !(multiline && (text.c_str()[i] == '\n' || text.c_str()[i] == '\r' || text.c_str()[i] == '\t'))) return false;
    return true;
}
bool topicField(const String& topic) {
    return topic.indexOf('#') < 0 && topic.indexOf('+') < 0;
}
}

bool ConfigManager::applyJson(JsonVariantConst doc, String& error, bool persist) {
    error = "Configuracion incompleta o valores no validos.";
    if (!doc.is<JsonObjectConst>() || !doc["mqtt"].is<JsonObjectConst>() ||
        !doc["inputs"].is<JsonArrayConst>() || doc["inputs"].size() != 8 ||
        !doc["outputs"].is<JsonArrayConst>() || doc["outputs"].size() != 8) return false;
    MQTTConfig nextMqtt;
    InputConfig nextInputs[8];
    OutputConfig nextOutputs[8];
    error = "Broker MQTT: comprueba tipos, longitudes, puerto, Client ID y keep alive.";
    auto m = doc["mqtt"];
    if (!textField(m["host"], 128) || !textField(m["clientId"], 64) ||
        !textField(m["username"], 128) || !m["port"].is<unsigned int>() ||
        m["port"].as<unsigned int>() < 1 || m["port"].as<unsigned int>() > 65535 ||
        !m["keepAlive"].is<unsigned int>() || m["keepAlive"].as<unsigned int>() < 5 ||
        m["keepAlive"].as<unsigned int>() > 3600) return false;
    if (m.containsKey("password") && !textField(m["password"], 128)) return false;
    nextMqtt.password = m.containsKey("password") ? m["password"].as<String>() : mqtt.password;
    nextMqtt.host = m["host"].as<String>();
    nextMqtt.port = m["port"].as<uint16_t>();
    nextMqtt.clientId = m["clientId"].as<String>();
    nextMqtt.username = m["username"].as<String>();
    nextMqtt.keepAlive = m["keepAlive"].as<uint16_t>();
    if (nextMqtt.clientId.isEmpty() || nextMqtt.host.indexOf(' ') >= 0 ||
        nextMqtt.host.indexOf('/') >= 0) return false;
    for (int i = 0; i < 8; ++i) {
        error = "DI" + String(i + 1) + ": campos no validos, topic vacio o payloads iguales.";
        auto item = doc["inputs"][i];
        if (!item.is<JsonObjectConst>()) return false;
        if (!item["enabled"].is<bool>()) return false;
        nextInputs[i].enabled = item["enabled"].as<bool>();
        if (!textField(item["name"], 64)) return false;
        nextInputs[i].name = item["name"].as<String>();
        if (!textField(item["topic"], 128)) return false;
        nextInputs[i].topic = item["topic"].as<String>();
        if (!textField(item["payloadOn"], 256, true)) return false;
        nextInputs[i].payloadOn = item["payloadOn"].as<String>();
        if (!textField(item["payloadOff"], 256, true)) return false;
        nextInputs[i].payloadOff = item["payloadOff"].as<String>();
        if (!item["inverted"].is<bool>()) return false;
        nextInputs[i].inverted = item["inverted"].as<bool>();
        if (item.containsKey("debounceMs")) {
            if (!item["debounceMs"].is<unsigned int>() || item["debounceMs"].as<unsigned int>() > 5000) {
                error = "Antirrebote: debe estar entre 0 y 5000 ms."; return false;
            }
            nextInputs[i].debounceMs = item["debounceMs"].as<uint16_t>();
        }
        if (!item["retain"].is<bool>()) return false;
        nextInputs[i].retain = item["retain"].as<bool>();
        auto& c = nextInputs[i];
        if (item.containsKey("payloadJson") && !item["payloadJson"].is<bool>()) return false;
        c.payloadJson = item["payloadJson"] | false;
        if (c.payloadJson && !JsonPayload::validPair(c.payloadOn.c_str(), c.payloadOff.c_str())) {
            error = "Payloads JSON ON/OFF no validos, demasiado complejos o equivalentes."; return false;
        }

        if (c.payloadOn == c.payloadOff) return false;
        if (!topicField(c.topic) || (c.enabled && c.topic.isEmpty())) return false;
    }
    for (int i = 0; i < 8; ++i) {
        error = "RO" + String(i + 1) + ": campos no validos, topics vacios o payloads iguales.";
        auto item = doc["outputs"][i];
        if (!item.is<JsonObjectConst>()) return false;
        if (!item["enabled"].is<bool>()) return false;
        nextOutputs[i].enabled = item["enabled"].as<bool>();
        if (!textField(item["name"], 64)) return false;
        nextOutputs[i].name = item["name"].as<String>();
        if (!textField(item["commandTopic"], 128)) return false;
        nextOutputs[i].commandTopic = item["commandTopic"].as<String>();
        if (!textField(item["payloadOn"], 256, true)) return false;
        nextOutputs[i].payloadOn = item["payloadOn"].as<String>();
        if (!textField(item["payloadOff"], 256, true)) return false;
        nextOutputs[i].payloadOff = item["payloadOff"].as<String>();
        if (!item["publishState"].is<bool>()) return false;
        nextOutputs[i].publishState = item["publishState"].as<bool>();
        if (!textField(item["stateTopic"], 128)) return false;
        nextOutputs[i].stateTopic = item["stateTopic"].as<String>();
        if (!textField(item["stateOn"], 256, true)) return false;
        nextOutputs[i].stateOn = item["stateOn"].as<String>();
        if (!textField(item["stateOff"], 256, true)) return false;
        nextOutputs[i].stateOff = item["stateOff"].as<String>();
        if (!item["retain"].is<bool>()) return false;
        nextOutputs[i].retain = item["retain"].as<bool>();
        auto& c = nextOutputs[i];
        if (item.containsKey("payloadJson") && !item["payloadJson"].is<bool>()) return false;
        c.payloadJson = item["payloadJson"] | false;
        if (c.payloadJson && !JsonPayload::validPair(c.payloadOn.c_str(), c.payloadOff.c_str())) {
            error = "Payloads JSON ON/OFF no validos, demasiado complejos o equivalentes."; return false;
        }
        if (item.containsKey("stateJson") && !item["stateJson"].is<bool>()) return false;
        c.stateJson = item["stateJson"] | false;
        if (item.containsKey("jsonPath") && !textField(item["jsonPath"], 64)) return false;
        c.jsonPath = item["jsonPath"] | "";
        if (!c.jsonPath.isEmpty()) {
            const char* path = c.jsonPath.c_str();
            bool segment = false;
            for (size_t j = 0; j < c.jsonPath.length(); ++j) {
                if (path[j] == '.') { if (!segment) return false; segment = false; }
                else segment = true;
            }
            if (!segment) return false;
        }
        if (c.stateJson && !JsonPayload::validPair(c.stateOn.c_str(), c.stateOff.c_str())) {
            error = "Payloads JSON de estado no validos, demasiado complejos o equivalentes."; return false;
        }

        if (c.payloadOn == c.payloadOff) return false;
        if (!topicField(c.commandTopic) || !topicField(c.stateTopic) ||
            (c.enabled && (c.commandTopic.isEmpty() || (c.publishState && c.stateTopic.isEmpty()))) ||
            (c.publishState && c.stateOn == c.stateOff)) return false;
    }
    // Prevent this module's own publications from being interpreted as commands.
    for (const auto& output : nextOutputs) {
        if (!output.enabled) continue;
        for (const auto& input : nextInputs)
            if (input.enabled && input.topic == output.commandTopic) {
                error = "Un topic de entrada coincide con un topic de mando.";
                return false;
            }
        for (const auto& state : nextOutputs)
            if (state.enabled && state.publishState && state.stateTopic == output.commandTopic) {
                error = "Un topic de estado coincide con un topic de mando.";
                return false;
            }
    }
    std::vector<SignalConfig> nextSignals;
    error = "Senales: configuracion no valida (maximo 8 senales, 8 focos y 12 aspectos por senal).";
    if (!doc.containsKey("signals") && !signals.empty()) return false;
    if (doc.containsKey("signals")) {
        if (!doc["signals"].is<JsonArrayConst>() || doc["signals"].size() > 8) return false;
        uint8_t reserved = 0;
        for (JsonObjectConst item : doc["signals"].as<JsonArrayConst>()) {
            if (!item["enabled"].is<bool>() || !textField(item["name"], 64) ||
                !textField(item["topic"], 128) || !textField(item["jsonPath"], 64) ||
                !item["lights"].is<JsonArrayConst>() || item["lights"].size() < 1 || item["lights"].size() > 8 ||
                !item["aspects"].is<JsonArrayConst>() || item["aspects"].size() < 1 || item["aspects"].size() > 12) return false;
            SignalConfig signal;
            signal.enabled = item["enabled"].as<bool>(); signal.name = item["name"].as<String>();
            signal.topic = item["topic"].as<String>(); signal.jsonPath = item["jsonPath"].as<String>();
            if (!topicField(signal.topic) || signal.name.isEmpty() || (signal.enabled && signal.topic.isEmpty())) return false;
            if (!signal.jsonPath.isEmpty()) {
                const char* path = signal.jsonPath.c_str();
                for (size_t j = 0; j < signal.jsonPath.length(); ++j)
                    if (path[j] == '.' && (j == 0 || path[j+1] == '.' || path[j+1] == 0)) return false;
            }
            if (item.containsKey("blinkMs")) {
                if (!item["blinkMs"].is<unsigned int>() || item["blinkMs"].as<unsigned int>() < 250 || item["blinkMs"].as<unsigned int>() > 10000) return false;
                signal.blinkMs = item["blinkMs"].as<uint16_t>();
            }
            uint8_t mask = 0;
            for (JsonObjectConst light : item["lights"].as<JsonArrayConst>()) {
                if (!textField(light["name"], 32) || !light["relay"].is<unsigned int>() ||
                    light["relay"].as<unsigned int>() < 1 || light["relay"].as<unsigned int>() > 8) return false;
                SignalLight entry; entry.name = light["name"].as<String>(); entry.relay = light["relay"].as<uint8_t>();
                uint8_t bit = uint8_t(1U << (entry.relay - 1));
                if (mask & bit) { error = "Una senal no puede repetir un rele entre focos."; return false; }
                mask |= bit; signal.lights.push_back(entry);
            }
            for (JsonObjectConst aspect : item["aspects"].as<JsonArrayConst>()) {
                if (!textField(aspect["value"], 64) || !aspect["on"].is<JsonArrayConst>()) return false;
                SignalAspect entry; entry.value = aspect["value"].as<String>();
                if (entry.value.isEmpty()) return false;
                for (const auto& previous : signal.aspects) if (previous.value == entry.value) return false;
                for (JsonVariantConst light : aspect["on"].as<JsonArrayConst>()) {
                    if (!light.is<unsigned int>() || light.as<unsigned int>() < 1 || light.as<unsigned int>() > signal.lights.size()) return false;
                    const uint8_t bit = uint8_t(1U << (light.as<unsigned int>() - 1));
                    if (entry.mask & bit) return false;
                    entry.mask |= bit;
                }
                if (aspect.containsKey("blink")) {
                    if (!aspect["blink"].is<JsonArrayConst>()) return false;
                    for (JsonVariantConst light : aspect["blink"].as<JsonArrayConst>()) {
                        if (!light.is<unsigned int>() || light.as<unsigned int>() < 1 || light.as<unsigned int>() > signal.lights.size()) return false;
                        const uint8_t bit = uint8_t(1U << (light.as<unsigned int>() - 1));
                        if ((entry.mask | entry.blink) & bit) return false;
                        entry.blink |= bit;
                    }
                }
                signal.aspects.push_back(entry);
            }
            if (signal.enabled) {
                if (mask & reserved) { error = "Dos senales habilitadas comparten reles."; return false; }
                reserved |= mask;
                for (int i = 0; i < 8; ++i) if ((mask & (1U << i)) && nextOutputs[i].enabled) {
                    error = "Deshabilita el mando individual de los reles asignados a una senal."; return false;
                }
                for (const auto& input : nextInputs) if (input.enabled && input.topic == signal.topic) {
                    error = "El topic de senal coincide con una publicacion de entrada."; return false;
                }
                for (const auto& output : nextOutputs) if (output.enabled && output.publishState && output.stateTopic == signal.topic) {
                    error = "El topic de senal coincide con una publicacion de salida."; return false;
                }
            }
            nextSignals.push_back(signal);
        }
    }
    // Persist a complete snapshot before changing the live configuration.
    if (persist) {
        DynamicJsonDocument saved(32768);
        saved.set(doc);
        saved["mqtt"]["password"] = nextMqtt.password;
        saved["mqtt"].remove("passwordSet");
        if (saved.overflowed()) { error = "Configuracion demasiado grande."; return false; }
        String value;
        serializeJson(saved, value);
        if (prefs.putString("config_v1", value) != value.length()) {
            error = "No se pudo guardar en NVS. No se aplicaron los cambios.";
            return false;
        }
    }
    signals = std::move(nextSignals);
    mqtt = nextMqtt;
    for (int i = 0; i < 8; ++i) { inputs[i] = nextInputs[i]; outputs[i] = nextOutputs[i]; }
    error = "";
    return true;
}

bool ConfigManager::relayAssigned(uint8_t channel) const {
    if (channel >= 8) return false;
    for (const auto& signal : signals)
        if (signal.enabled && (signal.relayMask() & (1U << channel))) return true;
    return false;
}
