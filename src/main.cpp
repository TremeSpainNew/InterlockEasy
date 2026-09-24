#include <Arduino.h>
#include "ConfigManager.h"
#include "EthernetManager.h"
#include "MqttManager.h"
#include "IOManager.h"
#include "WebManager.h"
#include "SignalManager.h"

void setup() {
    Serial.begin(115200);
    delay(1200);

    Serial.println("\n==============================");
    Serial.println(" Interlock Easy");
    Serial.println(" Waveshare ESP32-S3 8DI 8RO");
    Serial.println("==============================");

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
    IO.loop();
    Network.loop();
    MQTT.loop();
    IO.loop();
    Signals.loop();
    Web.loop();
}
