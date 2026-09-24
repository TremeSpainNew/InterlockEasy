#pragma once
#include <Arduino.h>
#include <Ethernet.h>
#include <FS.h>

class WebManager {
public:
    void begin();
    void loop();

private:
    EthernetServer server{80};
    EthernetClient client;
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

    void close();
    void dispatch();
    void respond(int status, const char* reason, const char* type, const String& body);
    void headers(int status, const char* reason, const char* type, size_t length);
    void status();
    void config();
    void saveConfig();
    void testControl();
};

extern WebManager Web;
