#include "HardwareConfig.h"
#include <LittleFS.h>

bool HardwareConfig::begin() {
    filesystemReady = LittleFS.begin(false);
    if (!filesystemReady || !LittleFS.exists("/config.json")) {
        defaults(); Serial.println("Sin config.json: perfil Waveshare 8DI/8RO."); return true;
    }
    File file = LittleFS.open("/config.json", "r");
    if (!file || file.size()>16384) {error="No se puede leer config.json o supera 16 KiB."; return false;}
    DynamicJsonDocument doc(24576);
    if (deserializeJson(doc,file)) {error="JSON de hardware no valido.";return false;}
    return parse(doc.as<JsonVariantConst>(),error);
}

bool HardwareConfig::saveJson(JsonVariantConst doc, String& error) const {
    HardwareConfig candidate;
    if (!candidate.parse(doc, error)) return false;
    if (!filesystemReady) { error="LittleFS no disponible."; return false; }
    String value;
    const size_t expected = measureJson(doc);
    if (expected > 16384 || serializeJson(doc, value) != expected || value.length() != expected) {
        error="Configuracion demasiado grande o memoria insuficiente."; return false;
    }
    // Replace only after a complete, verified write. The live pin map is unchanged.
    File temp = LittleFS.open("/config.json.tmp", "w");
    if (!temp) {error="No se pudo crear el archivo temporal."; return false;}
    const size_t written = temp.print(value);
    temp.flush(); temp.close();
    File check = LittleFS.open("/config.json.tmp", "r");
    check.setTimeout(0); // A local file must not wait for more bytes at EOF.
    const bool complete = check && written == expected && check.size() == expected && check.readString() == value;
    check.close();
    if (!complete || !LittleFS.rename("/config.json.tmp", "/config.json")) {
        LittleFS.remove("/config.json.tmp");
        error="No se pudo guardar config.json; se conserva el archivo anterior."; return false;
    }
    error=""; return true;
}
