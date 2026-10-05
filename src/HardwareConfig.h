#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

enum class ModuleType { GPIO, TCA9554, PCF8574, PCF8575, MCP23017, MODBUS };
enum class NetworkType { ETHERNET, WIFI, NONE };
enum class ModbusRole { OFF, MASTER, SLAVE };
struct ModbusConfig {
    ModbusRole role = ModbusRole::OFF;
    uint8_t address = 1;
    int tx = 17, rx = 18, de = -1;
    uint32_t baud = 38400;
    uint16_t timeoutMs = 100, watchdogMs = 3000;
    String parity = "N";
};
struct HardwareModule { ModuleType type; uint8_t address; };
struct HardwareChannel { uint8_t module, pin; bool activeLow = false; bool pullup = true; };
class HardwareConfig {
public:
    ModbusConfig modbus;
    NetworkType networkType = NetworkType::ETHERNET;
    String wifiSsid;
    String wifiPassword;
    String hostname = "InterlockEasy";
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
