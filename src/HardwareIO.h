#pragma once
#include "HardwareConfig.h"
class HardwareIO {
public:
    bool begin();
    void sample();
    bool read(uint8_t channel, bool& value) const;
    bool outputReady(uint8_t channel) const;
    bool write(uint64_t mask, uint64_t states);
    uint64_t states = 0;
private:
    bool initialized = false;
    struct Device {uint16_t latch=0xffff, direction=0xffff, pullup=0, sample=0; bool valid=false;};
    std::vector<Device> devices;
    bool reg(uint8_t module, uint8_t address, uint16_t value, bool wide);
    bool latch(uint8_t module, uint16_t value);
};
extern HardwareIO HardwarePorts;
