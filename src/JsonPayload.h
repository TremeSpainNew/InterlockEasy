#pragma once
#include <ArduinoJson.h>

namespace JsonPayload {
// Compare structure and values; object key order and whitespace are irrelevant.
inline bool equal(JsonVariantConst a, JsonVariantConst b) {
    if (a.is<JsonObjectConst>() || b.is<JsonObjectConst>()) {
        if (!a.is<JsonObjectConst>() || !b.is<JsonObjectConst>() || a.size() != b.size()) return false;
        for (JsonPairConst pair : a.as<JsonObjectConst>()) {
            if (!b.as<JsonObjectConst>().containsKey(pair.key().c_str()) ||
                !equal(pair.value(), b[pair.key().c_str()])) return false;
        }
        return true;
    }
    if (a.is<JsonArrayConst>() || b.is<JsonArrayConst>()) {
        if (!a.is<JsonArrayConst>() || !b.is<JsonArrayConst>() || a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) if (!equal(a[i], b[i])) return false;
        return true;
    }
    if (a.is<bool>() || b.is<bool>()) return a.is<bool>() && b.is<bool>() && a.as<bool>() == b.as<bool>();
    if (a.is<const char*>() || b.is<const char*>())
        return a.is<const char*>() && b.is<const char*>() && a.as<JsonString>() == b.as<JsonString>();
    if (a.isNull() || b.isNull()) return a.isNull() && b.isNull();
    return a == b;
}

inline bool validPair(const char* on, const char* off) {
    DynamicJsonDocument a(2048), b(2048);
    if (deserializeJson(a, on) || deserializeJson(b, off)) return false;
    return !equal(a.as<JsonVariantConst>(), b.as<JsonVariantConst>());
}

inline bool select(JsonVariantConst root, const char* path, JsonVariantConst& selected) {
    selected = root;
    if (!path[0]) return true;
    char key[65];
    size_t n = 0;
    for (size_t j = 0; ; ++j) {
        if (path[j] == '.' || path[j] == '\0') {
            if (!n) return false;
            key[n] = '\0';
            if (!selected.is<JsonObjectConst>() || !selected.as<JsonObjectConst>().containsKey(key)) return false;
            selected = selected[key]; n = 0;
            if (!path[j]) return true;
        } else {
            if (n == 64) return false;
            key[n++] = path[j];
        }
    }
}

inline bool matches(JsonVariantConst incoming, const char* expected) {
    DynamicJsonDocument doc(2048);
    return !deserializeJson(doc, expected) && equal(incoming, doc.as<JsonVariantConst>());
}
}
