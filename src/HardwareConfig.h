#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

enum class ModuleType { GPIO, TCA9554, PCF8574, PCF8575, MCP23017 };
struct HardwareModule { ModuleType type; uint8_t address; };
struct HardwareChannel { uint8_t module, pin; bool activeLow = false; bool pullup = true; };
class HardwareConfig {
public:
    int sda = 42, scl = 41;
    int sclk = 15, miso = 14, mosi = 13, cs = 16;
    std::vector<HardwareModule> modules;
    std::vector<HardwareChannel> inputs, outputs;
    bool filesystemReady = false;
    String error;
    bool begin();
    bool parse(JsonVariantConst doc, String& error);
    void defaults();
    void toJson(JsonDocument& doc) const;
    bool saveJson(JsonVariantConst doc, String& error) const;
};
extern HardwareConfig Hardware;
