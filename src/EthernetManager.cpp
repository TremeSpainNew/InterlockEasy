#include "EthernetManager.h"

#include "HardwareConfig.h"

EthernetManager Connectivity;

bool EthernetManager::begin() {
    if(Hardware.networkType==NetworkType::NONE)return true;
    if (Hardware.networkType == NetworkType::WIFI) {
        if (WiFi.status() == WL_CONNECTED) return true;
        if (accessPoint) return true;
        if (wifiStarted && millis() - wifiStartedAt >= 30000) {
            Serial.println("WiFi no conectado; iniciando AP InterlockEasy-Setup.");
            WiFi.mode(WIFI_AP_STA);
            accessPoint = WiFi.softAP("InterlockEasy-Setup", "InterlockEasy");
            return accessPoint;
        }
        if (!wifiStarted || millis() - wifiStartedAt >= 15000) {
            Serial.printf("Conectando WiFi a %s...\n", Hardware.wifiSsid.c_str());
            WiFi.mode(WIFI_STA);
            WiFi.setHostname(Hardware.hostname.c_str());
            WiFi.begin(Hardware.wifiSsid.c_str(), Hardware.wifiPassword.c_str());
            if (!wifiStarted) wifiStartedAt = millis();
            wifiStarted = true;
        }
        return false;
    }
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
    if (Hardware.networkType == NetworkType::ETHERNET) Ethernet.maintain();
}

bool EthernetManager::connected() {
    if(Hardware.networkType==NetworkType::NONE)return false;
    return Hardware.networkType == NetworkType::WIFI ? (WiFi.status() == WL_CONNECTED || accessPoint) : Ethernet.linkStatus() == LinkON;
}

IPAddress EthernetManager::ip() {
    if(Hardware.networkType==NetworkType::NONE)return IPAddress();
    if (Hardware.networkType == NetworkType::WIFI)
        return accessPoint && WiFi.status() != WL_CONNECTED ? WiFi.softAPIP() : WiFi.localIP();
    return Ethernet.localIP();
}

Client& EthernetManager::createClient() {
    return Hardware.networkType == NetworkType::WIFI ? static_cast<Client&>(wifiClient) : static_cast<Client&>(ethernetClient);
}

bool EthernetManager::wifi() const { return Hardware.networkType == NetworkType::WIFI; }
