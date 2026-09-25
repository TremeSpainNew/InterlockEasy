#include <Arduino.h>
#include "ConfigManager.h"
#include "EthernetManager.h"
#include "MqttManager.h"
#include "IOManager.h"
#include "WebManager.h"
#include "SignalManager.h"
#include "HardwareConfig.h"

static bool hardwareValid = false;

void setup() {
    Serial.begin(115200);
    delay(1200);

    Serial.println("\n==============================");
    Serial.println(" Interlock Easy");
    Serial.println(" Hardware configurable");
    Serial.println("==============================");

    hardwareValid = Hardware.begin();
    if (!hardwareValid) {Serial.println(Hardware.error); return;}
    Config.inputs.resize(Hardware.inputs.size());
    Config.outputs.resize(Hardware.outputs.size());
    Serial.printf("Hardware: %u entradas, %u reles, %u modulos.\n", unsigned(Hardware.inputs.size()), unsigned(Hardware.outputs.size()), unsigned(Hardware.modules.size()));
    Config.begin();
    IO.begin();

    while (!Network.begin()) {
        Serial.println("Ethernet/DHCP no disponible. Reintentando...");
        delay(5000);
    }

    MQTT.begin();
    Web.begin();

    Serial.print("Sistema iniciado en ");
    Serial.println(Network.ip());
}

void loop() {
    if (!hardwareValid) {delay(100); return;}
    IO.loop();
    Network.loop();
    MQTT.loop();
    IO.loop();
    Signals.loop();
    Web.loop();
}
