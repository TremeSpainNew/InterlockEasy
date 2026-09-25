#include "HardwareIO.h"
#include <Wire.h>
#ifdef ARDUINO_ARCH_ESP32
#include <driver/gpio.h>
#endif
HardwareIO HardwarePorts;
bool HardwareIO::reg(uint8_t m,uint8_t address,uint16_t value,bool wide) {
    Wire.beginTransmission(Hardware.modules[m].address); Wire.write(address); Wire.write(uint8_t(value));
    if(wide)Wire.write(uint8_t(value>>8));
    return Wire.endTransmission()==0;
}
bool HardwareIO::latch(uint8_t m,uint16_t value) {
    auto type=Hardware.modules[m].type;
    if(type==ModuleType::MCP23017)return reg(m,0x14,value,true);
    if(type==ModuleType::TCA9554)return reg(m,1,value,false);
    Wire.beginTransmission(Hardware.modules[m].address); Wire.write(uint8_t(value));
    if(type==ModuleType::PCF8575)Wire.write(uint8_t(value>>8));
    return Wire.endTransmission()==0;
}
bool HardwareIO::begin() {
    devices.assign(Hardware.modules.size(), Device{}); states=0;
    bool needsI2c=false;
    for(auto m:Hardware.modules)if(m.type!=ModuleType::GPIO)needsI2c=true;
    for(auto c:Hardware.inputs) {
        if(Hardware.modules[c.module].type==ModuleType::GPIO)pinMode(c.pin,c.pullup?INPUT_PULLUP:INPUT);
        else if(c.pullup)devices[c.module].pullup |= uint16_t(1U<<c.pin);
    }
    for(auto c:Hardware.outputs) {
        if(Hardware.modules[c.module].type==ModuleType::GPIO) {
            // Arduino-ESP32 3.x digitalWrite ignores pins not attached to GPIO yet.
#ifdef ARDUINO_ARCH_ESP32
            gpio_set_level(gpio_num_t(c.pin), c.activeLow ? 1 : 0);
#else
            digitalWrite(c.pin,c.activeLow?HIGH:LOW);
#endif
            pinMode(c.pin,OUTPUT);
        } else {
            auto& d=devices[c.module]; d.direction &= ~uint16_t(1U<<c.pin);
            if(!c.activeLow)d.latch &= ~uint16_t(1U<<c.pin);
        }
    }
    if(needsI2c && !Wire.begin(Hardware.sda,Hardware.scl,100000))return false;
    if(needsI2c)Wire.setTimeOut(50);
    bool ok=true;
    for(uint8_t m=0;m<devices.size();++m) {
        auto type=Hardware.modules[m].type;auto& d=devices[m];
        if(type==ModuleType::GPIO)continue;
        bool good=true;
        if(type==ModuleType::MCP23017) {
            // Normalize BANK/SEQOP even after an MCU-only reset. Disable interrupts.
            good=reg(m,0x05,0,false) && reg(m,0x0a,0,false) && reg(m,0x00,0xffff,true) &&
                 reg(m,0x04,0,true) && reg(m,0x02,0,true) && reg(m,0x0c,d.pullup,true);
        }
        if(good)good=latch(m,d.latch);
        if(good && type==ModuleType::MCP23017)good=reg(m,0,d.direction,true);
        if(good && type==ModuleType::TCA9554)good=reg(m,2,0,false) && reg(m,3,d.direction,false);
        ok=ok && good;
    }
    sample();return ok;
}
void HardwareIO::sample() {
    for(uint8_t m=0;m<devices.size();++m) {
        auto type=Hardware.modules[m].type;auto& d=devices[m];d.valid=false;
        if(type==ModuleType::GPIO)continue;
        bool hasInput=false;for(auto c:Hardware.inputs)if(c.module==m)hasInput=true;
        if(!hasInput)continue;
        uint8_t address=Hardware.modules[m].address;
        if(type==ModuleType::MCP23017 || type==ModuleType::TCA9554) {
            Wire.beginTransmission(address);Wire.write(type==ModuleType::MCP23017?0x12:0);
            if(Wire.endTransmission(false)!=0)continue;
        }
        uint8_t count=(type==ModuleType::PCF8574 || type==ModuleType::TCA9554)?1:2;
        if(Wire.requestFrom(address,count)!=count)continue;
        d.sample=uint8_t(Wire.read());if(count==2)d.sample |= uint16_t(uint8_t(Wire.read()))<<8;
        d.valid=true;
    }
}
bool HardwareIO::read(uint8_t channel,bool& value) const {
    if(channel>=Hardware.inputs.size())return false;
    auto c=Hardware.inputs[channel];
    if(Hardware.modules[c.module].type==ModuleType::GPIO) {value=digitalRead(c.pin);return true;}
    if(!devices[c.module].valid)return false;
    value=(devices[c.module].sample & (1U<<c.pin))!=0; return true;
}
bool HardwareIO::write(uint32_t mask,uint32_t desired) {
    for(uint8_t m=0;m<devices.size();++m) {
        auto& d=devices[m];uint16_t next=d.latch;uint32_t affected=0;
        for(uint8_t i=0;i<Hardware.outputs.size();++i) {
            auto c=Hardware.outputs[i];uint32_t bit=uint32_t(1)<<i;
            if(c.module!=m || !(mask&bit))continue;
            affected |= bit;
            if(Hardware.modules[m].type!=ModuleType::GPIO) {
                if(bool(desired&bit)!=c.activeLow)next |= uint16_t(1U<<c.pin);
                else next &= ~uint16_t(1U<<c.pin);
            }
        }
        if(!affected)continue;
        if(Hardware.modules[m].type==ModuleType::GPIO) {
            for(uint8_t i=0;i<Hardware.outputs.size();++i)if(affected&(uint32_t(1)<<i)) {
                auto c=Hardware.outputs[i];digitalWrite(c.pin,bool(desired&(uint32_t(1)<<i))!=c.activeLow);
            }
        } else if(next!=d.latch && !latch(m,next))return false;
        d.latch=next; states=(states&~affected)|(desired&affected);
    }
    return true;
}
