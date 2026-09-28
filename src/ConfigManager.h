#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <vector>

#define NUM_INPUTS  (Config.inputs.size())
#define NUM_OUTPUTS (Config.outputs.size())


// ============================================================
// ENTRADAS
// ============================================================

struct InputConfig {
    bool enabled = false;
    String name;
    String topic;

    bool payloadJson = false;

    String payloadOn = "1";
    String payloadOff = "0";

    bool inverted = true;

    uint16_t debounceMs = 50;

    bool retain = true;
};


// ============================================================
// SALIDAS
// ============================================================

struct OutputConfig {
    bool enabled = false;

    String name;

    String commandTopic;
    String jsonPath;

    bool payloadJson = false;

    String payloadOn = "1";
    String payloadOff = "0";

    bool publishState = true;

    String stateTopic;

    bool stateJson = false;

    String stateOn = "1";
    String stateOff = "0";

    bool retain = true;
};


// ============================================================
// MQTT
// ============================================================

struct MQTTConfig {
    String host = "";

    uint16_t port = 1883;

    String clientId = "InterlockEasy-IO";

    String username = "";
    String password = "";

    uint16_t keepAlive = 30;
};


// ============================================================
// CV TRADICIONAL
// ============================================================
//
// Topic:
//   cv/<station>/<id>/field_state
//
// Payload:
//   {"Estado":"Libre"}
//   {"Estado":"Ocupado"}
//
// El CV tradicional representa un estado.
// ============================================================

struct CvConfig {
    bool enabled = false;

    String station;
    String id;

    // Entrada logica 1..NUM_INPUTS.
    uint8_t input = 1;

    String topic() const {
        return
            "cv/" +
            station +
            "/" +
            id +
            "/field_state";
    }
};


// ============================================================
// CONTADOR DE EJES
// ============================================================
//
// Cada punto contador dispone de dos detectores fisicos:
//
//       A ---- B
//
// El orden temporal A/B determina el sentido.
//
// Topic:
//   cejes/<station>/<id>/event
//
// Eventos:
//   Nominal:N
//   Reverse:N
//   Error
//   conexion
//
// El contador NO calcula LIBRE/OCUPADO.
// Ese calculo corresponde a Enclavamiento.
// ============================================================

struct AxleCounterConfig {
    bool enabled = false;

    String station;
    String id;

    // Entradas logicas 1..NUM_INPUTS.
    uint8_t inputA = 1;
    uint8_t inputB = 2;

    String topic() const {
        return
            "cejes/" +
            station +
            "/" +
            id +
            "/event";
    }
};



enum class TurnoutDrive : uint8_t {
    PULSE = 0,
    MAINTAINED = 1
};

struct TurnoutConfig {
    bool enabled = false;
    String station;
    String id;

    uint8_t outputNormal = 1;
    uint8_t outputReverse = 2;

    // 0 = sin comprobacion. Si se usa comprobacion deben configurarse ambas.
    uint8_t inputNormal = 0;
    uint8_t inputReverse = 0;

    TurnoutDrive drive = TurnoutDrive::PULSE;
    uint16_t pulseMs = 500;

    String commandTopic() const {
        return "aguja/" + station + "/" + id + "/mando";
    }

    String feedbackTopic() const {
        return "aguja/" + station + "/" + id + "/comprobacion";
    }

    bool hasFeedback() const {
        return inputNormal > 0 && inputReverse > 0;
    }
};

// ============================================================
// SEÑALES
// ============================================================

struct SignalLight {
    String name;

    // Numero logico de RO: 1..32.
    uint8_t relay = 1;
};


struct SignalAspect {
    String value;

    // Indices locales de los focos.
    uint8_t mask = 0;

    uint8_t blink = 0;
};


struct SignalConfig {
    bool enabled = false;

    String name;

    String topic;

    String jsonPath = "Aspecto";

    // Duracion de cada fase ON/OFF.
    uint16_t blinkMs = 500;

    std::vector<SignalLight> lights;

    std::vector<SignalAspect> aspects;


    uint32_t relayMask() const {
        uint32_t mask = 0;

        for (const auto& light : lights) {
            if (light.relay >= 1 &&
                light.relay <= 32) {
                mask |=
                    uint32_t(1)
                    << (light.relay - 1);
            }
        }

        return mask;
    }
};


// ============================================================
// CONFIG MANAGER
// ============================================================

class ConfigManager {

public:

    MQTTConfig mqtt;

    std::vector<SignalConfig> signals;

    // Deteccion de via.
    //
    // CV tradicionales y cuenta-ejes son objetos distintos.
    std::vector<CvConfig> cvs;
    std::vector<AxleCounterConfig> axleCounters;
    std::vector<TurnoutConfig> turnouts;


    std::vector<InputConfig> inputs =
        std::vector<InputConfig>(8);

    std::vector<OutputConfig> outputs =
        std::vector<OutputConfig>(8);


    void begin();

    void load();

    bool save();


    void toJson(
        JsonDocument& doc,
        bool secrets = false
    ) const;


    bool applyJson(
        JsonVariantConst doc,
        String& error,
        bool persist = true
    );


    // true si el rele pertenece a una señal habilitada.
    bool relayAssigned(
        uint8_t channel
    ) const;


    // true si la DI pertenece a un CV o CE habilitado.
    //
    // channel es indice interno 0..N-1.
    bool inputAssigned(
        uint8_t channel
    ) const;


private:

    // Namespace NVS "interlock".
    //
    // ConfigManager.cpp utiliza este objeto en:
    //   begin()
    //   load()
    //   save()
    //   applyJson()
    Preferences prefs;
};


extern ConfigManager Config;
