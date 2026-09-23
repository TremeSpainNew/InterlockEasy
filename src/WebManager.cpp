#include "WebManager.h"
#include "ConfigManager.h"
#include "EthernetManager.h"
#include "IOManager.h"
#include "MqttManager.h"
#include "SignalManager.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

WebManager Web;

void WebManager::begin() {
    filesystemReady = LittleFS.begin(false);
    if (!filesystemReady) Serial.println("LittleFS no disponible: cargar con pio run -t uploadfs.");
    server.begin();
    Serial.println("HTTP disponible en puerto 80.");
}

void WebManager::close() {
    if (file) file.close();
    client.stop();
    request = "";
    pending = "";
    offset = 0;
    responding = false;
    readingBody = false;
    contentLength = 0;
    body = "";

}

void WebManager::headers(int code, const char* reason, const char* type, size_t length) {
    pending = "HTTP/1.1 " + String(code) + " " + reason + "\r\nContent-Type: " + type;
    pending += "\r\nContent-Length: " + String(length);
    pending += "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n";
    if (code == 405) pending += "Allow: GET, POST\r\n";
    pending += "\r\n";
    offset = 0;
    responding = true;
}

void WebManager::respond(int code, const char* reason, const char* type, const String& body) {
    headers(code, reason, type, body.length());
    pending += body;
}

void WebManager::status() {
    DynamicJsonDocument doc(16384);
    doc["uptimeMs"] = millis();
    doc["ethernet"]["connected"] = Network.connected();
    doc["ethernet"]["ip"] = Network.ip().toString();
    doc["mqtt"]["connected"] = MQTT.connected();
    doc["mqtt"]["configured"] = !Config.mqtt.host.isEmpty();
    doc["filesystemReady"] = filesystemReady;
    doc["outputsReady"] = IO.outputsReady();
    JsonArray inputs = doc.createNestedArray("inputs");
    JsonArray outputs = doc.createNestedArray("outputs");
    for (uint8_t i = 0; i < NUM_INPUTS; ++i) {
        JsonObject item = inputs.createNestedObject();
        item["channel"] = i + 1;
        item["name"] = Config.inputs[i].name;
        item["enabled"] = Config.inputs[i].enabled;
        item["state"] = IO.getInput(i);
    }
    for (uint8_t i = 0; i < NUM_OUTPUTS; ++i) {
        JsonObject item = outputs.createNestedObject();
        item["channel"] = i + 1;
        item["name"] = Config.outputs[i].name;
        item["enabled"] = Config.outputs[i].enabled;
        for (const auto& signal : Config.signals)
            if (signal.enabled && (signal.relayMask() & (1U << i))) item["signal"] = signal.name;
        if (IO.outputsReady()) item["state"] = IO.getOutput(i);
        else item["state"] = nullptr;
    }
    JsonArray signals = doc.createNestedArray("signals");
    for (size_t i = 0; i < Config.signals.size(); ++i) {
        const auto& signal = Config.signals[i];
        JsonObject item = signals.createNestedObject();
        item["name"] = signal.name; item["enabled"] = signal.enabled;
        item["aspect"] = Signals.aspect(i);
        JsonArray lights = item.createNestedArray("lights");
        for (const auto& light : signal.lights) {
            JsonObject entry = lights.createNestedObject();
            entry["name"] = light.name; entry["relay"] = light.relay;
            if (IO.outputsReady()) entry["state"] = IO.getOutput(light.relay - 1);
            else entry["state"] = nullptr;
        }
    }
    if (doc.overflowed()) {
        respond(500, "Internal Server Error", "application/json", "{\"error\":\"status capacity exceeded\"}");
        return;
    }
    String body;
    serializeJson(doc, body);
    respond(200, "OK", "application/json; charset=utf-8", body);
}

void WebManager::dispatch() {
    const int end = request.indexOf("\r\n");
    const String line = request.substring(0, end);
    const int first = line.indexOf(' ');
    const int second = line.indexOf(' ', first + 1);
    if (end < 0 || first <= 0 || second <= first + 1 ||
        (line.substring(second + 1) != "HTTP/1.1" && line.substring(second + 1) != "HTTP/1.0")) {
        respond(400, "Bad Request", "text/plain", "Peticion no valida.");
        return;
    }
    const String method = line.substring(0, first);
    String path = line.substring(first + 1, second);
    const int query = path.indexOf('?');
    if (query >= 0) path.remove(query);
    if (method == "POST" && path == "/api/config") {
        bool hasLength = false, json = false, token = false;
        for (int pos = end + 2; pos < int(request.length()) - 2;) {
            const int next = request.indexOf("\r\n", pos);
            if (next < 0) break;
            String header = request.substring(pos, next);
            const int colon = header.indexOf(':');
            if (colon <= 0) { respond(400, "Bad Request", "text/plain", "Cabecera no valida."); return; }
            String key = header.substring(0, colon); key.toLowerCase();
            String value = header.substring(colon + 1); value.trim();
            if (key == "transfer-encoding") {
                respond(400, "Bad Request", "text/plain", "Transfer-Encoding no admitido."); return;
            }
            if (key == "content-length") {
                if (hasLength || value.isEmpty() || value.length() > 5) {
                    respond(400, "Bad Request", "text/plain", "Content-Length no valido."); return;
                }
                for (size_t i = 0; i < value.length(); ++i)
                    if (value[i] < '0' || value[i] > '9') {
                        respond(400, "Bad Request", "text/plain", "Content-Length no valido."); return;
                    }
                contentLength = value.toInt(); hasLength = true;
            }
            if (key == "content-type") {
                value.toLowerCase();
                const int separator = value.indexOf(';');
                if (separator >= 0) value.remove(separator);
                value.trim(); json = value == "application/json";
            }
            if (key == "x-interlock") token = value == "1";
            pos = next + 2;
        }
        if (!hasLength || !contentLength) { respond(411, "Length Required", "text/plain", "Falta Content-Length."); return; }
        if (contentLength > 24576) { respond(413, "Content Too Large", "text/plain", "Configuracion demasiado grande."); return; }
        if (!json) { respond(415, "Unsupported Media Type", "text/plain", "Se requiere application/json."); return; }
        // A custom header prevents cross-origin HTML forms from changing settings.
        if (!token) { respond(403, "Forbidden", "text/plain", "Falta X-Interlock: 1."); return; }
        readingBody = true;
        body.reserve(contentLength);
        return;
    }
    if (method != "GET") {
        respond(405, "Method Not Allowed", "text/plain", "Metodo no admitido."); return;
    }
    if (path == "/api/config") { config(); return; }
    if (path == "/api/status") {
        status();
    } else if (path == "/" || path == "/index.html") {
        if (filesystemReady) file = LittleFS.open("/index.html", "r");
        if (!file) {
            respond(503, "Service Unavailable", "text/plain; charset=utf-8",
                    "Interfaz no disponible. Carga LittleFS con: pio run -t uploadfs. API: /api/status");
            return;
        }
        headers(200, "OK", "text/html; charset=utf-8", file.size());
    } else {
        respond(404, "Not Found", "text/plain", "Recurso no encontrado.");
    }
}

void WebManager::config() {
    DynamicJsonDocument doc(32768);
    Config.toJson(doc);
    if (doc.overflowed()) { respond(500, "Internal Server Error", "text/plain", "Sin memoria."); return; }
    String value;
    serializeJson(doc, value);
    respond(200, "OK", "application/json; charset=utf-8", value);
}

void WebManager::saveConfig() {
    DynamicJsonDocument doc(32768);
    String error;
    if (deserializeJson(doc, body)) {
        respond(400, "Bad Request", "text/plain", "JSON no valido o demasiado grande."); return;
    }
    if (!Config.applyJson(doc.as<JsonVariantConst>(), error)) {
        respond(400, "Bad Request", "text/plain; charset=utf-8", error); return;
    }
    Signals.reload();
    IO.refreshInputs();
    MQTT.reload();
    respond(200, "OK", "application/json", "{\"saved\":true}");
}

void WebManager::loop() {
    if (!client) {
        client = server.accept();
        if (!client) return;
        client.setConnectionTimeout(100);
        started = millis();
    }
    if ((!client.connected() && !client.available()) || millis() - started > 10000) {
        close();
        return;
    }
    if (!responding) {
        // Bounded work per iteration, even for a slow or oversized request.
        for (int budget = 0; budget < 256 && client.available(); ++budget) {
            if (readingBody) {
                body += char(client.read());
                if (body.length() == contentLength) { saveConfig(); body = ""; break; }
                continue;
            }
            request += char(client.read());
            if (request.length() > 2048) {
                respond(431, "Request Header Fields Too Large", "text/plain", "Cabeceras demasiado grandes.");
                break;
            }
            if (request.endsWith("\r\n\r\n")) {
                dispatch();
                request = "";
                break;
            }
        }
        return;
    }
    const int available = client.availableForWrite();
    if (available <= 0) return;
    const size_t capacity = available < 512 ? available : 512;
    if (offset < pending.length()) {
        const size_t remaining = pending.length() - offset;
        offset += client.write(reinterpret_cast<const uint8_t*>(pending.c_str()) + offset,
                               remaining < capacity ? remaining : capacity);
        return;
    }
    if (file && file.available()) {
        uint8_t buffer[512];
        const size_t position = file.position();
        const size_t count = file.read(buffer, capacity);
        if (count == 0) { close(); return; }
        const size_t sent = client.write(buffer, count);
        if (sent < count) file.seek(position + sent);
        return;
    }
    close();
}
