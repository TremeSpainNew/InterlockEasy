#include "EthernetManager.h"

#include "HardwareConfig.h"

EthernetManager Network;

bool EthernetManager::begin() {
    Serial.println("Inicializando Ethernet W5500...");

    SPI.begin(Hardware.sclk, Hardware.miso, Hardware.mosi, Hardware.cs);
    Ethernet.init(Hardware.cs);

    if (Ethernet.begin(mac) == 0) {
        Serial.println("ERROR: DHCP no disponible.");
        return false;
    }

    delay(500);
    Serial.print("IP: ");
    Serial.println(Ethernet.localIP());
    return true;
}

void EthernetManager::loop() {
    Ethernet.maintain();
}

bool EthernetManager::connected() {
    return Ethernet.linkStatus() == LinkON;
}

IPAddress EthernetManager::ip() {
    return Ethernet.localIP();
}

EthernetClient& EthernetManager::createClient() {
    return client;
}
