#include "WebManager.h"
#include "ConfigManager.h"
#include "EthernetManager.h"
#include "IOManager.h"
#include "MqttManager.h"
#include "SignalManager.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "HardwareConfig.h"

WebManager Web;

void WebManager::begin() {
    filesystemReady = Hardware.filesystemReady;
    if (!filesystemReady)
        Serial.println("LittleFS no disponible: cargar con pio run -t uploadfs.");

    server.begin();
    Serial.println("HTTP disponible en puerto 80.");
}

void WebManager::close() {
    if (file)
        file.close();

    client.stop();

    request = "";
    pending = "";
    offset = 0;

    responding = false;
    readingBody = false;
    testRequest = false;
    hardwareRequest = false;

    contentLength = 0;
    body = "";
}

void WebManager::headers(
    int code,
    const char* reason,
    const char* type,
    size_t length
) {
    pending = "HTTP/1.1 " + String(code) + " " + reason +
              "\r\nContent-Type: " + type;

    pending += "\r\nContent-Length: " + String(length);
    pending += "\r\nCache-Control: no-store";
    pending += "\r\nX-Content-Type-Options: nosniff";
    pending += "\r\nConnection: close\r\n";

    if (code == 405)
        pending += "Allow: GET, POST\r\n";

    pending += "\r\n";

    offset = 0;
    responding = true;
}

void WebManager::respond(
    int code,
    const char* reason,
    const char* type,
    const String& body
) {
    headers(code, reason, type, body.length());
    pending += body;
}

void WebManager::status() {
    DynamicJsonDocument doc(32768);

    doc["inputCount"] = NUM_INPUTS;
    doc["outputCount"] = NUM_OUTPUTS;
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

        if (IO.inputReady(i))
            item["state"] = IO.getInput(i);
        else
            item["state"] = nullptr;

        item["simulated"] = IO.inputSimulated(i);
        item["raw"] = IO.getRawInput(i);
        item["filtering"] = IO.inputFiltering(i);
        item["publishPending"] = MQTT.inputPending(i);
        item["inverted"] = Config.inputs[i].inverted;
        item["debounceMs"] = Config.inputs[i].debounceMs;
    }

    for (uint8_t i = 0; i < NUM_OUTPUTS; ++i) {
        JsonObject item = outputs.createNestedObject();

        item["channel"] = i + 1;
        item["name"] = Config.outputs[i].name;
        item["enabled"] = Config.outputs[i].enabled;

        for (const auto& signal : Config.signals) {
            if (signal.enabled &&
                (signal.relayMask() & (1U << i))) {
                item["signal"] = signal.name;
            }
        }

        if (IO.outputsReady())
            item["state"] = IO.getOutput(i);
        else
            item["state"] = nullptr;
    }

    JsonArray signals = doc.createNestedArray("signals");

    for (size_t i = 0; i < Config.signals.size(); ++i) {
        const auto& signal = Config.signals[i];

        JsonObject item = signals.createNestedObject();

        item["name"] = signal.name;
        item["enabled"] = signal.enabled;
        item["aspect"] = Signals.aspect(i);
        item["channel"] = i + 1;

        JsonArray aspects = item.createNestedArray("aspects");

        for (const auto& aspect : signal.aspects)
            aspects.add(aspect.value);

        JsonArray lights = item.createNestedArray("lights");

        for (const auto& light : signal.lights) {
            JsonObject entry = lights.createNestedObject();

            entry["name"] = light.name;
            entry["relay"] = light.relay;

            if (IO.outputsReady())
                entry["state"] =
                    IO.getOutput(light.relay - 1);
            else
                entry["state"] = nullptr;
        }
    }

    if (doc.overflowed()) {
        respond(
            500,
            "Internal Server Error",
            "application/json",
            "{\"error\":\"status capacity exceeded\"}"
        );
        return;
    }

    String body;
    serializeJson(doc, body);

    respond(
        200,
        "OK",
        "application/json; charset=utf-8",
        body
    );
}

void WebManager::dispatch() {
    const int end = request.indexOf("\r\n");
    const String line = request.substring(0, end);

    const int first = line.indexOf(' ');
    const int second = line.indexOf(' ', first + 1);

    if (
        end < 0 ||
        first <= 0 ||
        second <= first + 1 ||
        (
            line.substring(second + 1) != "HTTP/1.1" &&
            line.substring(second + 1) != "HTTP/1.0"
        )
    ) {
        respond(
            400,
            "Bad Request",
            "text/plain",
            "Peticion no valida."
        );
        return;
    }

    const String method = line.substring(0, first);

    String path = line.substring(first + 1, second);

    const int query = path.indexOf('?');

    if (query >= 0)
        path.remove(query);

    // ---------------------------------------------------------
    // POST
    // ---------------------------------------------------------

    if (
        method == "POST" &&
        (
            path == "/api/config" ||
            path == "/api/test" ||
            path == "/api/hardware"
        )
    ) {
        testRequest = path == "/api/test";
        hardwareRequest = path == "/api/hardware";

        bool hasLength = false;
        bool json = false;
        bool token = false;

        for (
            int pos = end + 2;
            pos < int(request.length()) - 2;
        ) {
            const int next =
                request.indexOf("\r\n", pos);

            if (next < 0)
                break;

            String header =
                request.substring(pos, next);

            const int colon = header.indexOf(':');

            if (colon <= 0) {
                respond(
                    400,
                    "Bad Request",
                    "text/plain",
                    "Cabecera no valida."
                );
                return;
            }

            String key =
                header.substring(0, colon);

            key.toLowerCase();

            String value =
                header.substring(colon + 1);

            value.trim();

            if (key == "transfer-encoding") {
                respond(
                    400,
                    "Bad Request",
                    "text/plain",
                    "Transfer-Encoding no admitido."
                );
                return;
            }

            if (key == "content-length") {
                if (
                    hasLength ||
                    value.isEmpty() ||
                    value.length() > 5
                ) {
                    respond(
                        400,
                        "Bad Request",
                        "text/plain",
                        "Content-Length no valido."
                    );
                    return;
                }

                for (
                    size_t i = 0;
                    i < value.length();
                    ++i
                ) {
                    if (
                        value[i] < '0' ||
                        value[i] > '9'
                    ) {
                        respond(
                            400,
                            "Bad Request",
                            "text/plain",
                            "Content-Length no valido."
                        );
                        return;
                    }
                }

                contentLength = value.toInt();
                hasLength = true;
            }

            if (key == "content-type") {
                value.toLowerCase();

                const int separator =
                    value.indexOf(';');

                if (separator >= 0)
                    value.remove(separator);

                value.trim();

                json =
                    value == "application/json";
            }

            if (key == "x-interlock")
                token = value == "1";

            pos = next + 2;
        }

        if (!hasLength || !contentLength) {
            respond(
                411,
                "Length Required",
                "text/plain",
                "Falta Content-Length."
            );
            return;
        }

        if (
            contentLength >
            (
                testRequest
                    ? 512U
                    : hardwareRequest
                        ? 16384U
                        : 57344U
            )
        ) {
            respond(
                413,
                "Content Too Large",
                "text/plain",
                "Configuracion demasiado grande."
            );
            return;
        }

        if (!json) {
            respond(
                415,
                "Unsupported Media Type",
                "text/plain",
                "Se requiere application/json."
            );
            return;
        }

        // Cabecera personalizada para evitar que formularios
        // externos puedan cambiar la configuración.
        if (!token) {
            respond(
                403,
                "Forbidden",
                "text/plain",
                "Falta X-Interlock: 1."
            );
            return;
        }

        readingBody = true;
        body.reserve(contentLength);

        return;
    }

    // ---------------------------------------------------------
    // GET
    // ---------------------------------------------------------

    if (method != "GET") {
        respond(
            405,
            "Method Not Allowed",
            "text/plain",
            "Metodo no admitido."
        );
        return;
    }

    if (path == "/api/hardware") {
        hardwareConfig();
        return;
    }

    if (path == "/api/config") {
        config();
        return;
    }

    if (path == "/api/status") {
        status();
    }

    // ---------------------------------------------------------
    // Archivos LittleFS
    // ---------------------------------------------------------

    else if (
        path == "/" ||
        path == "/index.html" ||
        path == "/mqtt.html" ||
        path == "/inputs.html" ||
        path == "/outputs.html" ||
        path == "/signals.html" ||
        path == "/hardware.html" ||
        path == "/hardware.js" ||
        path == "/style.css" ||
        path == "/dashboard.js" ||
        path == "/config.js"
    ) {
        const String asset =
            path == "/"
                ? String("/index.html")
                : path;

        if (filesystemReady)
            file = LittleFS.open(
                asset.c_str(),
                "r"
            );

        if (!file) {
            respond(
                503,
                "Service Unavailable",
                "text/plain; charset=utf-8",
                "Interfaz no disponible. "
                "Carga LittleFS con: "
                "pio run -t uploadfs. "
                "API: /api/status"
            );
            return;
        }

        const char* type =
            path.endsWith(".css")
                ? "text/css; charset=utf-8"
                : path.endsWith(".js")
                    ? "application/javascript; charset=utf-8"
                    : "text/html; charset=utf-8";

        headers(
            200,
            "OK",
            type,
            file.size()
        );
    }

    else {
        respond(
            404,
            "Not Found",
            "text/plain",
            "Recurso no encontrado."
        );
    }
}

void WebManager::config() {
    DynamicJsonDocument doc(65536);

    Config.toJson(doc);

    if (doc.overflowed()) {
        respond(
            500,
            "Internal Server Error",
            "text/plain",
            "Sin memoria."
        );
        return;
    }

    String value;
    serializeJson(doc, value);

    respond(
        200,
        "OK",
        "application/json; charset=utf-8",
        value
    );
}

void WebManager::saveConfig() {
    DynamicJsonDocument doc(65536);

    String error;

    if (deserializeJson(doc, body)) {
        respond(
            400,
            "Bad Request",
            "text/plain",
            "JSON no valido o demasiado grande."
        );
        return;
    }

    if (
        !Config.applyJson(
            doc.as<JsonVariantConst>(),
            error
        )
    ) {
        respond(
            400,
            "Bad Request",
            "text/plain; charset=utf-8",
            error
        );
        return;
    }

    Signals.reload();
    IO.refreshInputs();
    MQTT.reload();

    respond(
        200,
        "OK",
        "application/json",
        "{\"saved\":true}"
    );
}

void WebManager::testControl() {
    DynamicJsonDocument doc(1024);

    if (
        deserializeJson(doc, body) ||
        !doc["type"].is<const char*>() ||
        !doc["channel"].is<unsigned int>() ||
        doc["channel"].as<unsigned int>() < 1 ||
        doc["channel"].as<unsigned int>() > 32
    ) {
        respond(
            400,
            "Bad Request",
            "text/plain",
            "Mando de prueba no valido."
        );
        return;
    }

    const uint8_t channel =
        doc["channel"].as<unsigned int>() - 1;

    const String type =
        doc["type"].as<String>();

    if (
        (type == "input" &&
         channel >= NUM_INPUTS) ||

        (type == "relay" &&
         channel >= NUM_OUTPUTS) ||

        (type == "signal" &&
         channel >= Config.signals.size())
    ) {
        respond(
            400,
            "Bad Request",
            "text/plain",
            "Canal fuera del hardware configurado."
        );
        return;
    }

    bool ok = false;

    if (
        type == "input" &&
        doc.containsKey("state") &&
        (
            doc["state"].isNull() ||
            doc["state"].is<bool>()
        )
    ) {
        ok = IO.simulateInput(
            channel,
            doc["state"].isNull()
                ? -1
                : doc["state"].as<bool>()
                    ? 1
                    : 0
        );
    }

    else if (
        type == "relay" &&
        doc["state"].is<bool>()
    ) {
        if (Config.relayAssigned(channel)) {
            respond(
                409,
                "Conflict",
                "text/plain; charset=utf-8",
                "Relé reservado: prueba un "
                "aspecto de su señal."
            );
            return;
        }

        if (IO.outputsReady()) {
            IO.setOutput(
                channel,
                doc["state"].as<bool>()
            );

            ok =
                IO.getOutput(channel) ==
                doc["state"].as<bool>();
        }
    }

    else if (
        type == "signal" &&
        doc["aspect"].is<const char*>()
    ) {
        ok = Signals.testAspect(
            channel,
            doc["aspect"].as<String>()
        );
    }

    else {
        respond(
            400,
            "Bad Request",
            "text/plain",
            "Tipo o valor no valido."
        );
        return;
    }

    if (!ok) {
        respond(
            409,
            "Conflict",
            "text/plain",
            "No se pudo aplicar la prueba. "
            "Comprueba canal, aspecto y controlador."
        );
        return;
    }

    respond(
        200,
        "OK",
        "application/json",
        "{\"applied\":true}"
    );
}


// ============================================================
// BUCLE DEL SERVIDOR HTTP
//
// IMPORTANTE:
// Se permite enviar hasta 4096 bytes por llamada a loop(),
// manteniendo escrituras individuales de un máximo de 512.
//
// Esto permite terminar rápidamente HTML/CSS/JS y liberar
// el socket del W5500 para las siguientes peticiones del
// navegador.
// ============================================================

void WebManager::loop() {

    // ---------------------------------------------------------
    // Buscar una petición que ya tenga datos disponibles
    // ---------------------------------------------------------

    if (!client) {
        // server.available() solamente devuelve clientes que
        // tienen datos esperando. Así ignoramos conexiones
        // especulativas abiertas por el navegador.
        client = server.available();

        if (!client)
            return;

        client.setConnectionTimeout(100);
        started = millis();
    }

    // ---------------------------------------------------------
    // Cliente cerrado o timeout general
    // ---------------------------------------------------------

    if (
        (!client.connected() && !client.available()) ||
        millis() - started > 10000
    ) {
        close();
        return;
    }

    // ---------------------------------------------------------
    // Leer petición
    // ---------------------------------------------------------

    if (!responding) {

        // Trabajo limitado para no bloquear MQTT/IO.
        for (
            int budget = 0;
            budget < 256 && client.available();
            ++budget
        ) {

            // -------------------------------------------------
            // BODY POST
            // -------------------------------------------------

            if (readingBody) {
                body += char(client.read());

                if (body.length() == contentLength) {

                    if (testRequest)
                        testControl();

                    else if (hardwareRequest)
                        saveHardwareConfig();

                    else
                        saveConfig();

                    body = "";
                    break;
                }

                continue;
            }

            // -------------------------------------------------
            // CABECERAS
            // -------------------------------------------------

            request += char(client.read());

            if (request.length() > 2048) {
                respond(
                    431,
                    "Request Header Fields Too Large",
                    "text/plain",
                    "Cabeceras demasiado grandes."
                );
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

    // =========================================================
    // ENVÍO DE RESPUESTA
    // =========================================================
    //
    // Antes se enviaba solamente un bloque de hasta 512 bytes
    // por cada ejecución de loop().
    //
    // Ahora permitimos hasta 4096 bytes por ejecución,
    // manteniendo bloques de máximo 512 bytes.
    //
    // Esto es especialmente importante para:
    //
    //   /outputs.html
    //   /inputs.html
    //   /signals.html
    //   /config.js
    //   /style.css
    //
    // porque el navegador los solicita prácticamente
    // simultáneamente.
    // =========================================================

    size_t sendBudget = 4096;

    while (sendBudget > 0 && client) {

        const int available =
            client.availableForWrite();

        if (available <= 0)
            return;

        size_t capacity =
            static_cast<size_t>(available);

        if (capacity > 512)
            capacity = 512;

        if (capacity > sendBudget)
            capacity = sendBudget;

        // -----------------------------------------------------
        // Enviar cabeceras HTTP o respuesta almacenada en RAM
        // -----------------------------------------------------

        if (offset < pending.length()) {

            const size_t remaining =
                pending.length() - offset;

            const size_t wanted =
                remaining < capacity
                    ? remaining
                    : capacity;

            const size_t sent =
                client.write(
                    reinterpret_cast<const uint8_t*>(
                        pending.c_str()
                    ) + offset,
                    wanted
                );

            // El socket no puede aceptar datos ahora.
            // Lo intentaremos en la próxima pasada.
            if (sent == 0)
                return;

            offset += sent;
            sendBudget -= sent;

            continue;
        }

        // -----------------------------------------------------
        // Enviar archivo LittleFS
        // -----------------------------------------------------

        if (file && file.available()) {

            uint8_t buffer[512];

            const size_t position =
                file.position();

            const size_t count =
                file.read(
                    buffer,
                    capacity
                );

            if (count == 0) {
                close();
                return;
            }

            const size_t sent =
                client.write(
                    buffer,
                    count
                );

            // Si Ethernet.write() solo pudo aceptar una parte,
            // retrocedemos LittleFS hasta el primer byte que
            // todavía no ha sido transmitido.
            if (sent < count)
                file.seek(position + sent);

            if (sent == 0)
                return;

            sendBudget -= sent;

            continue;
        }

        // -----------------------------------------------------
        // Respuesta completamente transmitida
        // -----------------------------------------------------

        close();
        return;
    }
}


void WebManager::hardwareConfig() {
    if (
        filesystemReady &&
        LittleFS.exists("/config.json")
    ) {
        file = LittleFS.open(
            "/config.json",
            "r"
        );

        if (
            !file ||
            file.size() > 16384
        ) {
            if (file)
                file.close();

            respond(
                500,
                "Internal Server Error",
                "text/plain",
                "No se pudo leer config.json."
            );
            return;
        }

        headers(
            200,
            "OK",
            "application/json; charset=utf-8",
            file.size()
        );

        return;
    }

    DynamicJsonDocument doc(24576);

    Hardware.toJson(doc);

    if (doc.overflowed()) {
        respond(
            500,
            "Internal Server Error",
            "text/plain",
            "Sin memoria."
        );
        return;
    }

    String value;
    serializeJson(doc, value);

    respond(
        200,
        "OK",
        "application/json; charset=utf-8",
        value
    );
}


void WebManager::saveHardwareConfig() {
    DynamicJsonDocument doc(24576);

    if (deserializeJson(doc, body)) {
        respond(
            400,
            "Bad Request",
            "text/plain",
            "JSON no valido o demasiado grande."
        );
        return;
    }

    String error;

    if (
        !Hardware.saveJson(
            doc.as<JsonVariantConst>(),
            error
        )
    ) {
        respond(
            400,
            "Bad Request",
            "text/plain; charset=utf-8",
            error
        );
        return;
    }

    respond(
        200,
        "OK",
        "application/json",
        "{\"saved\":true,"
        "\"restartRequired\":true}"
    );
}