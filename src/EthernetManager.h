#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>

class EthernetManager {
public:
    bool begin();
    void loop();
    bool connected();
    IPAddress ip();
    EthernetClient& createClient();

private:
    EthernetClient client;
    byte mac[6] = {0x02, 0x45, 0x41, 0x53, 0x59, 0x01};
};

extern EthernetManager Network;
