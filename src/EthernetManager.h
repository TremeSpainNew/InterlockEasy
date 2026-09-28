#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <WiFi.h>

class EthernetManager {
public:
    bool begin();
    void loop();
    bool connected();
    IPAddress ip();
    Client& createClient();
    bool wifi() const;

private:
    EthernetClient ethernetClient;
    WiFiClient wifiClient;
    bool wifiStarted = false;
    unsigned long wifiStartedAt = 0;
    bool accessPoint = false;
    byte mac[6] = {0x02, 0x45, 0x41, 0x53, 0x59, 0x01};
};

extern EthernetManager Connectivity;
