#include "WebManager.h"
#include "ConfigManager.h"
#include "EthernetManager.h"
#include "IOManager.h"
#include "MqttManager.h"
#include "SignalManager.h"
#include "DetectionManager.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "HardwareConfig.h"


WebManager Web;


// ============================================================
// INICIO
// ============================================================

void WebManager::begin() {

    filesystemReady = Hardware.filesystemReady;

    if (!filesystemReady) {
        Serial.println(
            "LittleFS no disponible: "
            "cargar con pio run -t uploadfs."
        );
    }

    if (Connectivity.wifi())
        wifiServer.begin();
    else
        ethernetServer.begin();

    Serial.println(
        "HTTP disponible en puerto 80."
    );
}


// ============================================================
// CERRAR CLIENTE
// ============================================================

void WebManager::close() {

    if (file)
        file.close();

    if (client) client->stop();
    client = nullptr;

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


// ============================================================
// CABECERAS HTTP
// ============================================================

void WebManager::headers(
    int code,
    const char* reason,
    const char* type,
    size_t length
) {

    pending =
        "HTTP/1.1 " +
        String(code) +
        " " +
        reason +
        "\r\nContent-Type: " +
        type;

    pending +=
        "\r\nContent-Length: " +
        String(length);

    pending +=
        "\r\nCache-Control: no-store";

    pending +=
        "\r\nX-Content-Type-Options: nosniff";

    pending +=
        "\r\nConnection: close\r\n";

    if (code == 405)
        pending +=
            "Allow: GET, POST\r\n";

    pending += "\r\n";

    offset = 0;
    responding = true;
}


// ============================================================
// RESPUESTA HTTP
// ============================================================

void WebManager::respond(
    int code,
    const char* reason,
    const char* type,
    const String& body
) {

    headers(
        code,
        reason,
        type,
        body.length()
    );

    pending += body;
}


// ============================================================
// STATUS
// ============================================================

void WebManager::status() {

    DynamicJsonDocument doc(32768);

    doc["inputCount"] = NUM_INPUTS;
    doc["outputCount"] = NUM_OUTPUTS;
    doc["uptimeMs"] = millis();

    doc["ethernet"]["connected"] =
        Connectivity.connected();

    doc["ethernet"]["ip"] =
        Connectivity.ip().toString();

    doc["network"]["type"] =
        Connectivity.wifi() ? "wifi" : "ethernet";

    doc["mqtt"]["connected"] =
        MQTT.connected();

    doc["mqtt"]["configured"] =
        !Config.mqtt.host.isEmpty();

    doc["filesystemReady"] =
        filesystemReady;

    doc["outputsReady"] =
        IO.outputsReady();


    // --------------------------------------------------------
    // ENTRADAS
    // --------------------------------------------------------

    JsonArray inputs =
        doc.createNestedArray("inputs");

    JsonArray outputs =
        doc.createNestedArray("outputs");


    for (
        uint8_t i = 0;
        i < NUM_INPUTS;
        ++i
    ) {

        JsonObject item =
            inputs.createNestedObject();

        item["channel"] = i + 1;
        item["name"] =
            Config.inputs[i].name;

        item["enabled"] =
            Config.inputs[i].enabled;

        if (IO.inputReady(i))
            item["state"] =
                IO.getInput(i);
        else
            item["state"] =
                nullptr;

        item["simulated"] =
            IO.inputSimulated(i);

        item["raw"] =
            IO.getRawInput(i);

        item["filtering"] =
            IO.inputFiltering(i);

        item["publishPending"] =
            MQTT.inputPending(i);

        item["inverted"] =
            Config.inputs[i].inverted;

        item["debounceMs"] =
            Config.inputs[i].debounceMs;
    }


    // --------------------------------------------------------
    // SALIDAS
    // --------------------------------------------------------

    for (
        uint8_t i = 0;
        i < NUM_OUTPUTS;
        ++i
    ) {

        JsonObject item =
            outputs.createNestedObject();

        item["channel"] = i + 1;

        item["name"] =
            Config.outputs[i].name;

        item["enabled"] =
            Config.outputs[i].enabled;


        for (
            const auto& signal :
            Config.signals
        ) {

            if (
                signal.enabled &&
                (
                    signal.relayMask() &
                    (1U << i)
                )
            ) {
                item["signal"] =
                    signal.name;
            }
        }


        if (IO.outputsReady())
            item["state"] =
                IO.getOutput(i);
        else
            item["state"] =
                nullptr;
    }


    // --------------------------------------------------------
    // SEÑALES
    // --------------------------------------------------------

    JsonArray signals =
        doc.createNestedArray("signals");


    for (
        size_t i = 0;
        i < Config.signals.size();
        ++i
    ) {

        const auto& signal =
            Config.signals[i];

        JsonObject item =
            signals.createNestedObject();

        item["name"] =
            signal.name;

        item["enabled"] =
            signal.enabled;

        item["aspect"] =
            Signals.aspect(i);

        item["channel"] =
            i + 1;


        JsonArray aspects =
            item.createNestedArray(
                "aspects"
            );

        for (
            const auto& aspect :
            signal.aspects
        ) {
            aspects.add(
                aspect.value
            );
        }


        JsonArray lights =
            item.createNestedArray(
                "lights"
            );

        for (
            const auto& light :
            signal.lights
        ) {

            JsonObject entry =
                lights.createNestedObject();

            entry["name"] =
                light.name;

            entry["relay"] =
                light.relay;


            if (IO.outputsReady()) {

                entry["state"] =
                    IO.getOutput(
                        light.relay - 1
                    );

            } else {

                entry["state"] =
                    nullptr;
            }
        }
    }


    JsonArray tracks = doc.createNestedArray("trackSections");
    for (size_t i = 0; i < Config.trackSections.size(); ++i) {
        const auto& cfg = Config.trackSections[i];
        const auto* state = Detections.state(i);
        JsonObject item = tracks.createNestedObject();
        item["channel"] = i + 1;
        item["name"] = cfg.name;
        item["enabled"] = cfg.enabled;
        item["type"] = cfg.type == TrackSectionType::AXLE_COUNTER ? "axleCounter" : "linear";
        item["inputA"] = cfg.inputA;
        item["inputB"] = cfg.inputB;
        item["occupied"] = state ? state->occupied : true;
        item["uncertain"] = state ? state->uncertain : true;
        item["count"] = state ? state->count : 0;
        item["direction"] = !state || state->entrance < 0 ? "" : state->entrance == 0 ? "A" : "B";
    }


    if (doc.overflowed()) {

        respond(
            500,
            "Internal Server Error",
            "application/json",
            "{\"error\":"
            "\"status capacity exceeded\"}"
        );

        return;
    }


    String body;

    serializeJson(
        doc,
        body
    );


    respond(
        200,
        "OK",
        "application/json; charset=utf-8",
        body
    );
}


// ============================================================
// DISPATCH
// ============================================================

void WebManager::dispatch() {

    const int end =
        request.indexOf("\r\n");

    const String line =
        request.substring(
            0,
            end
        );


    const int first =
        line.indexOf(' ');

    const int second =
        line.indexOf(
            ' ',
            first + 1
        );


    if (
        end < 0 ||
        first <= 0 ||
        second <= first + 1 ||
        (
            line.substring(
                second + 1
            ) != "HTTP/1.1" &&

            line.substring(
                second + 1
            ) != "HTTP/1.0"
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


    const String method =
        line.substring(
            0,
            first
        );


    String path =
        line.substring(
            first + 1,
            second
        );


    const int query =
        path.indexOf('?');


    if (query >= 0)
        path.remove(query);


    // ========================================================
    // POST
    // ========================================================

    if (
        method == "POST" &&
        (
            path == "/api/config" ||
            path == "/api/test" ||
            path == "/api/hardware"
        )
    ) {

        testRequest =
            path == "/api/test";

        hardwareRequest =
            path == "/api/hardware";


        bool hasLength = false;
        bool json = false;
        bool token = false;


        for (
            int pos = end + 2;
            pos <
                int(request.length()) - 2;
        ) {

            const int next =
                request.indexOf(
                    "\r\n",
                    pos
                );


            if (next < 0)
                break;


            String header =
                request.substring(
                    pos,
                    next
                );


            const int colon =
                header.indexOf(':');


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
                header.substring(
                    0,
                    colon
                );

            key.toLowerCase();


            String value =
                header.substring(
                    colon + 1
                );

            value.trim();


            if (
                key ==
                "transfer-encoding"
            ) {

                respond(
                    400,
                    "Bad Request",
                    "text/plain",
                    "Transfer-Encoding "
                    "no admitido."
                );

                return;
            }


            if (
                key ==
                "content-length"
            ) {

                if (
                    hasLength ||
                    value.isEmpty() ||
                    value.length() > 5
                ) {

                    respond(
                        400,
                        "Bad Request",
                        "text/plain",
                        "Content-Length "
                        "no valido."
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
                            "Content-Length "
                            "no valido."
                        );

                        return;
                    }
                }


                contentLength =
                    value.toInt();

                hasLength = true;
            }


            if (
                key ==
                "content-type"
            ) {

                value.toLowerCase();


                const int separator =
                    value.indexOf(';');


                if (separator >= 0)
                    value.remove(
                        separator
                    );


                value.trim();


                json =
                    value ==
                    "application/json";
            }


            if (
                key ==
                "x-interlock"
            ) {
                token =
                    value == "1";
            }


            pos = next + 2;
        }


        if (
            !hasLength ||
            !contentLength
        ) {

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
                "Configuracion "
                "demasiado grande."
            );

            return;
        }


        if (!json) {

            respond(
                415,
                "Unsupported Media Type",
                "text/plain",
                "Se requiere "
                "application/json."
            );

            return;
        }


        // Cabecera personalizada para
        // impedir modificaciones externas.
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

        body.reserve(
            contentLength
        );

        return;
    }


    // ========================================================
    // GET
    // ========================================================

    if (method != "GET") {

        respond(
            405,
            "Method Not Allowed",
            "text/plain",
            "Metodo no admitido."
        );

        return;
    }


    if (
        path ==
        "/api/hardware"
    ) {

        hardwareConfig();
        return;
    }


    if (
        path ==
        "/api/config"
    ) {

        config();
        return;
    }


    if (
        path ==
        "/api/status"
    ) {

        status();
    }


    // ========================================================
    // LITTLEFS
    // ========================================================

    else if (
        path == "/" ||
        path == "/index.html" ||
        path == "/mqtt.html" ||
        path == "/inputs.html" ||
        path == "/outputs.html" ||
        path == "/signals.html" ||
        path == "/detection.html" ||
        path == "/hardware.html" ||
        path == "/hardware.js" ||
        path == "/style.css" ||
        path == "/dashboard.js" ||
        path == "/config.js" ||
        path == "/detection.js"
    ) {

        const String asset =
            path == "/"
                ? String(
                    "/index.html"
                )
                : path;


        if (filesystemReady) {

            file =
                LittleFS.open(
                    asset.c_str(),
                    "r"
                );
        }


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


// ============================================================
// OBTENER CONFIGURACIÓN
// ============================================================

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

    serializeJson(
        doc,
        value
    );


    respond(
        200,
        "OK",
        "application/json; charset=utf-8",
        value
    );
}


// ============================================================
// GUARDAR CONFIGURACIÓN NORMAL
// ============================================================

void WebManager::saveConfig() {

    DynamicJsonDocument doc(65536);

    String error;


    if (
        deserializeJson(
            doc,
            body
        )
    ) {

        respond(
            400,
            "Bad Request",
            "text/plain",
            "JSON no valido o "
            "demasiado grande."
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
    Detections.reload();

    IO.refreshInputs();

    MQTT.reload();


    respond(
        200,
        "OK",
        "application/json",
        "{\"saved\":true}"
    );
}


// ============================================================
// PRUEBAS
// ============================================================

void WebManager::testControl() {

    DynamicJsonDocument doc(1024);


    if (
        deserializeJson(
            doc,
            body
        ) ||

        !doc["type"]
            .is<const char*>() ||

        !doc["channel"]
            .is<unsigned int>() ||

        doc["channel"]
            .as<unsigned int>() < 1 ||

        doc["channel"]
            .as<unsigned int>() > 32
    ) {

        respond(
            400,
            "Bad Request",
            "text/plain",
            "Mando de prueba "
            "no valido."
        );

        return;
    }


    const uint8_t channel =
        doc["channel"]
            .as<unsigned int>() - 1;


    const String type =
        doc["type"]
            .as<String>();


    if (
        (
            type == "input" &&
            channel >= NUM_INPUTS
        ) ||

        (
            type == "relay" &&
            channel >= NUM_OUTPUTS
        ) ||

        (
            type == "signal" &&
            channel >=
                Config.signals.size()
        ) ||

        (
            type == "trackReset" &&
            channel >= Config.trackSections.size()
        )
    ) {

        respond(
            400,
            "Bad Request",
            "text/plain",
            "Canal fuera del "
            "hardware configurado."
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

        ok =
            IO.simulateInput(
                channel,

                doc["state"].isNull()
                    ? -1

                    : doc["state"]
                        .as<bool>()
                        ? 1
                        : 0
            );
    }


    else if (
        type == "relay" &&
        doc["state"].is<bool>()
    ) {

        if (
            Config.relayAssigned(
                channel
            )
        ) {

            respond(
                409,
                "Conflict",
                "text/plain; charset=utf-8",

                "Relé reservado: "
                "prueba un aspecto "
                "de su señal."
            );

            return;
        }


        if (IO.outputsReady()) {

            IO.setOutput(
                channel,
                doc["state"]
                    .as<bool>()
            );


            ok =
                IO.getOutput(
                    channel
                ) ==

                doc["state"]
                    .as<bool>();
        }
    }


    else if (
        type == "signal" &&

        doc["aspect"]
            .is<const char*>()
    ) {

        ok =
            Signals.testAspect(
                channel,

                doc["aspect"]
                    .as<String>()
            );
    }

    else if (type == "trackReset") {
        ok = Detections.reset(channel);
    }


    else {

        respond(
            400,
            "Bad Request",
            "text/plain",
            "Tipo o valor "
            "no valido."
        );

        return;
    }


    if (!ok) {

        respond(
            409,
            "Conflict",
            "text/plain",

            "No se pudo aplicar "
            "la prueba. Comprueba "
            "canal, aspecto y "
            "controlador."
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
// LOOP HTTP
// ============================================================

void WebManager::loop() {

    // ========================================================
    // REINICIO PENDIENTE
    // ========================================================
    //
    // Se programa al guardar /api/hardware.
    //
    // NO reiniciamos directamente dentro de
    // saveHardwareConfig(), porque primero queremos que el
    // navegador reciba correctamente la respuesta HTTP.
    // ========================================================

    if (
        restartPending &&
        (long)(millis() - restartAt) >= 0
    ) {

        restartPending = false;

        Serial.println(
            "Configuracion de hardware guardada."
        );

        Serial.println(
            "Reiniciando Interlock Easy..."
        );

        ESP.restart();

        return;
    }


    // --------------------------------------------------------
    // Buscar petición
    // --------------------------------------------------------

    if (!client) {

        // server.available() devuelve clientes que tienen
        // datos esperando. Esto evita quedarnos bloqueados
        // con conexiones especulativas del navegador.

        if (Connectivity.wifi()) {
            wifiClient = wifiServer.accept();
            if (wifiClient) client = &wifiClient;
        } else {
            ethernetClient = ethernetServer.available();
            if (ethernetClient) client = &ethernetClient;
        }


        if (!client)
            return;


        started = millis();
    }


    // --------------------------------------------------------
    // Cliente cerrado / timeout
    // --------------------------------------------------------

    if (
        (
            !client->connected() &&
            !client->available()
        ) ||

        millis() - started >
            10000
    ) {

        close();
        return;
    }


    // --------------------------------------------------------
    // LEER PETICIÓN
    // --------------------------------------------------------

    if (!responding) {

        // Trabajo limitado para no bloquear
        // MQTT, IO, señales, etc.

        for (
            int budget = 0;

            budget < 256 &&
            client->available();

            ++budget
        ) {

            // ------------------------------------------------
            // BODY POST
            // ------------------------------------------------

            if (readingBody) {

                body +=
                    char(
                        client->read()
                    );


                if (
                    body.length() ==
                    contentLength
                ) {

                    if (testRequest) {

                        testControl();

                    }

                    else if (
                        hardwareRequest
                    ) {

                        saveHardwareConfig();

                    }

                    else {

                        saveConfig();
                    }


                    body = "";

                    break;
                }


                continue;
            }


            // ------------------------------------------------
            // CABECERAS
            // ------------------------------------------------

            request +=
                char(
                    client->read()
                );


            if (
                request.length() >
                2048
            ) {

                respond(
                    431,
                    "Request Header Fields Too Large",
                    "text/plain",
                    "Cabeceras demasiado grandes."
                );

                break;
            }


            if (
                request.endsWith(
                    "\r\n\r\n"
                )
            ) {

                dispatch();

                request = "";

                break;
            }
        }


        return;
    }


    // ========================================================
    // ENVÍO DE RESPUESTA
    // ========================================================
    //
    // En lugar de enviar solamente 512 bytes por llamada a
    // loop(), dejamos un presupuesto total de 4096 bytes.
    //
    // Cada escritura sigue siendo de máximo 512 bytes.
    //
    // Esto permite liberar mucho antes el socket del W5500
    // cuando el navegador solicita:
    //
    //   HTML
    //   style.css
    //   config.js
    //   dashboard.js
    //
    // prácticamente al mismo tiempo.
    // ========================================================

    size_t sendBudget = 4096;


    while (
        sendBudget > 0 &&
        client && *client
    ) {

        const int available =
            client->availableForWrite();


        if (available <= 0)
            return;


        size_t capacity =
            static_cast<size_t>(
                available
            );


        if (capacity > 512)
            capacity = 512;


        if (
            capacity >
            sendBudget
        ) {
            capacity =
                sendBudget;
        }


        // ----------------------------------------------------
        // RESPUESTA EN RAM / CABECERAS
        // ----------------------------------------------------

        if (
            offset <
            pending.length()
        ) {

            const size_t remaining =
                pending.length() -
                offset;


            const size_t wanted =
                remaining < capacity
                    ? remaining
                    : capacity;


            const size_t sent =
                client->write(

                    reinterpret_cast<
                        const uint8_t*
                    >(
                        pending.c_str()
                    ) + offset,

                    wanted
                );


            if (sent == 0)
                return;


            offset += sent;

            sendBudget -= sent;


            continue;
        }


        // ----------------------------------------------------
        // ARCHIVO LITTLEFS
        // ----------------------------------------------------

        if (
            file &&
            file.available()
        ) {

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
                client->write(
                    buffer,
                    count
                );


            // Si Ethernet.write() no pudo aceptar
            // todos los bytes, recuperamos la posición
            // correcta del archivo.

            if (sent < count) {

                file.seek(
                    position + sent
                );
            }


            if (sent == 0)
                return;


            sendBudget -= sent;


            continue;
        }


        // ----------------------------------------------------
        // TERMINADO
        // ----------------------------------------------------

        close();

        return;
    }
}


// ============================================================
// LEER CONFIGURACIÓN HARDWARE
// ============================================================

void WebManager::hardwareConfig() {

    if (
        filesystemReady &&
        LittleFS.exists(
            "/config.json"
        )
    ) {

        file =
            LittleFS.open(
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
                "No se pudo leer "
                "config.json."
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


    serializeJson(
        doc,
        value
    );


    respond(
        200,
        "OK",
        "application/json; charset=utf-8",
        value
    );
}


// ============================================================
// GUARDAR CONFIGURACIÓN HARDWARE
// ============================================================

void WebManager::saveHardwareConfig() {

    DynamicJsonDocument doc(24576);


    if (
        deserializeJson(
            doc,
            body
        )
    ) {

        respond(
            400,
            "Bad Request",
            "text/plain",
            "JSON no valido o "
            "demasiado grande."
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


    // --------------------------------------------------------
    // IMPORTANTE:
    //
    // Respondemos ANTES de reiniciar.
    //
    // El navegador recibe correctamente que el JSON ha sido
    // guardado y después el ESP32 reinicia.
    // --------------------------------------------------------

    respond(
        200,
        "OK",
        "application/json",
        "{\"saved\":true,"
        "\"restarting\":true}"
    );


    // Reinicio dentro de aproximadamente 1 segundo.
    //
    // No hacemos ESP.restart() aquí porque todavía queda
    // pendiente transmitir la respuesta HTTP.

    restartPending = true;

    restartAt =
        millis() + 1000;
}
