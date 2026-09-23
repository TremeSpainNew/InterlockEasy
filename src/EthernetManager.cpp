#include "EthernetManager.h"

#define ETH_INT  12
#define ETH_MOSI 13
#define ETH_MISO 14
#define ETH_SCLK 15
#define ETH_CS   16

EthernetManager Network;

bool EthernetManager::begin() {
    Serial.println("Inicializando Ethernet W5500...");

    SPI.begin(ETH_SCLK, ETH_MISO, ETH_MOSI, ETH_CS);
    Ethernet.init(ETH_CS);

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
