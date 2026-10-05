#pragma once
#include <Arduino.h>
#include <vector>

class ModbusRtu {
public:
    void begin();
    void loop();
    bool input(uint8_t module,uint8_t pin,bool& value) const;
    bool outputReady(uint8_t module) const;
    bool requestOutput(uint8_t module,uint8_t pin,bool value);
    uint64_t outputs(uint8_t module) const;
private:
    struct Remote {
        uint64_t desired=0, confirmed=0, inputs=0;
        uint8_t inputCount=0, outputCount=0;
        bool inputValid=false, outputValid=false;
        uint32_t lastInput=0,lastOutput=0;
    };
    std::vector<Remote> remotes;
    uint8_t frame[256]={}, tx[256]={};
    size_t length=0;
    bool overflow=false, waiting=false, started=false;
    uint8_t cursor=0, active=0, function=0;
    bool writePhase=true;
    uint64_t sentOutputs=0;
    uint32_t lastByte=0, lastSend=0, lastWrite=0, gapUs=1750;
    void send(uint8_t* data,size_t length);
    void masterFrame();
    void slaveFrame();
    void nextRequest();
};
extern ModbusRtu Modbus;
