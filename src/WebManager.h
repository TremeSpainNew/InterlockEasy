#pragma once

#include <Arduino.h>
#include <Ethernet.h>
#include <WiFi.h>
#include <FS.h>

class WebManager {
public:
    void begin();
    void loop();

private:
    EthernetServer ethernetServer{80};
    WiFiServer wifiServer{80};
    EthernetClient ethernetClient;
    WiFiClient wifiClient;
    Client* client = nullptr;
    File file;

    bool filesystemReady = false;
    bool responding = false;

    unsigned long started = 0;

    String request;
    String pending;
    size_t offset = 0;

    bool readingBody = false;
    size_t contentLength = 0;
    String body;

    bool testRequest = false;
    bool hardwareRequest = false;

    // Reinicio automático después de guardar
    // la configuración de hardware.
    bool restartPending = false;
    unsigned long restartAt = 0;

    void close();
    void dispatch();

    void respond(
        int status,
        const char* reason,
        const char* type,
        const String& body
    );

    void headers(
        int status,
        const char* reason,
        const char* type,
        size_t length
    );

    void status();
    void config();
    void saveConfig();
    void testControl();

    void hardwareConfig();
    void saveHardwareConfig();
};

extern WebManager Web;
