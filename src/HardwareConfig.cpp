#include "HardwareConfig.h"
#include <driver/gpio.h>

HardwareConfig Hardware;
void HardwareConfig::defaults() {
    modules = {{ModuleType::GPIO,0},{ModuleType::TCA9554,0x20}};
    inputs.clear(); outputs.clear();
    for (uint8_t i=0;i<8;++i) { inputs.push_back({0,uint8_t(i+4),false,true}); outputs.push_back({1,i,false,false}); }
}
bool HardwareConfig::parse(JsonVariantConst doc, String& error) {
    error="config.json: version, modulos, pines o cantidades no validos.";
    HardwareConfig next;
    if (!doc["version"].is<unsigned int>() || doc["version"].as<unsigned int>()!=1 ||
        !doc["modules"].is<JsonArrayConst>() || doc["modules"].size()>16 ||
        !doc["inputs"].is<JsonArrayConst>() || doc["inputs"].size()>32 ||
        !doc["outputs"].is<JsonArrayConst>() || doc["outputs"].size()>32) return false;
    for (const char* key : {"inputCount","outputCount"}) if (!doc[key].is<unsigned int>()) return false;
    if (doc["inputCount"].as<unsigned int>()!=doc["inputs"].size() || doc["outputCount"].as<unsigned int>()!=doc["outputs"].size()) return false;
    auto pin = [](JsonVariantConst v, int& dest, bool output) {
        if (!v.is<int>()) return false;
        int n=v.as<int>();
        if (n<0 || n>=GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(n) || (output && !GPIO_IS_VALID_OUTPUT_GPIO(n))) return false;
        dest=n; return true;
    };
    if (!pin(doc["i2c"]["sda"],next.sda,true) || !pin(doc["i2c"]["scl"],next.scl,true)) return false;
    if (doc.containsKey("ethernet")) {
        if (doc["ethernet"]["type"] != "w5500" || !pin(doc["ethernet"]["sclk"],next.sclk,true) ||
            !pin(doc["ethernet"]["miso"],next.miso,false) || !pin(doc["ethernet"]["mosi"],next.mosi,true) || !pin(doc["ethernet"]["cs"],next.cs,true)) return false;
    }
    bool pins[GPIO_NUM_MAX] = {};
    for (int n : {next.sda,next.scl,next.sclk,next.miso,next.mosi,next.cs}) {if(pins[n])return false; pins[n]=true;}
    bool addresses[128] = {};
    for (JsonObjectConst m : doc["modules"].as<JsonArrayConst>()) {
        HardwareModule module{};
        String type=m["type"] | "";
        if(type=="gpio") module.type=ModuleType::GPIO;
        else {
            if(type=="tca9554")module.type=ModuleType::TCA9554;
            else if(type=="pcf8574")module.type=ModuleType::PCF8574;
            else if(type=="pcf8575")module.type=ModuleType::PCF8575;
            else if(type=="mcp23017")module.type=ModuleType::MCP23017;
            else return false;
            if(!m["address"].is<unsigned int>())return false;
            unsigned a=m["address"].as<unsigned int>();
            if(a<0x20 || a>0x27 || addresses[a])return false;
            addresses[a]=true; module.address=a;
        }
        next.modules.push_back(module);
    }
    uint64_t used[16] = {};
    for (bool output : {false,true}) {
        for (JsonObjectConst c : doc[output?"outputs":"inputs"].as<JsonArrayConst>()) {
            if(!c["module"].is<unsigned int>() || !c["pin"].is<unsigned int>())return false;
            unsigned m=c["module"].as<unsigned int>(), p=c["pin"].as<unsigned int>();
            if(m>=next.modules.size() || p>=GPIO_NUM_MAX)return false;
            auto type=next.modules[m].type;
            if(type==ModuleType::GPIO) {
                int n;
                if(!pin(c["pin"],n,output) || pins[p])return false;
                pins[p]=true;
            } else if(p >= ((type==ModuleType::PCF8574 || type==ModuleType::TCA9554)?8U:16U)) return false;
            if(used[m] & (uint64_t(1)<<p))return false;
            used[m] |= uint64_t(1)<<p;
            HardwareChannel channel{uint8_t(m),uint8_t(p),false,true};
            if(output) {if(!c["activeLow"].is<bool>())return false; channel.activeLow=c["activeLow"].as<bool>();}
            else {if(!c["pullup"].is<bool>())return false;channel.pullup=c["pullup"].as<bool>();}
            if (!output) {
                if (type==ModuleType::MCP23017 && (p==7 || p==15)) {error="MCP23017: GPA7 y GPB7 solo admiten salidas.";return false;}
                if (type==ModuleType::TCA9554 && channel.pullup) {error="TCA9554: pullup debe ser false; usar resistencias externas.";return false;}
                if ((type==ModuleType::PCF8574 || type==ModuleType::PCF8575) && !channel.pullup) {error="PCF857x: entradas cuasi-bidireccionales, pullup debe ser true.";return false;}
            }
            (output?next.outputs:next.inputs).push_back(channel);
        }
    }
    next.filesystemReady=filesystemReady;
    *this=std::move(next); error=""; return true;
}

void HardwareConfig::toJson(JsonDocument& doc) const {
    doc.clear();
    doc["version"] = 1;
    doc["inputCount"] = inputs.size(); doc["outputCount"] = outputs.size();
    doc["i2c"]["sda"] = sda; doc["i2c"]["scl"] = scl;
    auto eth = doc.createNestedObject("ethernet");
    eth["type"] = "w5500"; eth["sclk"] = sclk; eth["miso"] = miso;
    eth["mosi"] = mosi; eth["cs"] = cs;
    auto list = doc.createNestedArray("modules");
    for (const auto& module : modules) {
        auto item = list.createNestedObject();
        const char* type = "gpio";
        switch (module.type) {
            case ModuleType::TCA9554: type="tca9554"; break;
            case ModuleType::PCF8574: type="pcf8574"; break;
            case ModuleType::PCF8575: type="pcf8575"; break;
            case ModuleType::MCP23017: type="mcp23017"; break;
            default: break;
        }
        item["type"] = type;
        if (module.type != ModuleType::GPIO) item["address"] = module.address;
    }
    for (bool output : {false, true}) {
        auto channels = doc.createNestedArray(output ? "outputs" : "inputs");
        for (const auto& channel : output ? outputs : inputs) {
            auto item = channels.createNestedObject();
            item["module"] = channel.module; item["pin"] = channel.pin;
            if (output) item["activeLow"] = channel.activeLow;
            else item["pullup"] = channel.pullup;
        }
    }
}
