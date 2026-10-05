#include "ModbusRtu.h"
#include "ModbusCodec.h"
#include "HardwareConfig.h"
#include "HardwareIO.h"
#include <HardwareSerial.h>

ModbusRtu Modbus;
static HardwareSerial bus(1);

void ModbusRtu::begin(){
    const auto& c=Hardware.modbus;
    if(c.role==ModbusRole::OFF)return;
    if(c.de>=0){pinMode(c.de,OUTPUT);digitalWrite(c.de,LOW);}
    // 11-bit characters: 8N2 or 8E1/8O1.
    bus.begin(c.baud,c.parity=="E"?SERIAL_8E1:c.parity=="O"?SERIAL_8O1:SERIAL_8N2,c.rx,c.tx);
    gapUs=c.baud>19200?1750:(38500000UL+c.baud-1)/c.baud;
    remotes.resize(Hardware.modules.size());
    for(auto ch:Hardware.inputs)if(Hardware.modules[ch.module].type==ModuleType::MODBUS)
        remotes[ch.module].inputCount=max(remotes[ch.module].inputCount,uint8_t(ch.pin+1));
    for(auto ch:Hardware.outputs)if(Hardware.modules[ch.module].type==ModuleType::MODBUS)
        remotes[ch.module].outputCount=max(remotes[ch.module].outputCount,uint8_t(ch.pin+1));
    lastByte=micros();lastWrite=millis();started=true;
}
bool ModbusRtu::input(uint8_t m,uint8_t pin,bool& value) const {
    if(m>=remotes.size()||!remotes[m].inputValid||uint32_t(millis()-remotes[m].lastInput)>Hardware.modbus.watchdogMs)return false;
    value=(remotes[m].inputs&(uint64_t(1)<<pin))!=0;return true;
}
bool ModbusRtu::outputReady(uint8_t m) const {
    return m<remotes.size()&&remotes[m].outputValid&&uint32_t(millis()-remotes[m].lastOutput)<=Hardware.modbus.watchdogMs;
}
uint64_t ModbusRtu::outputs(uint8_t m) const{return m<remotes.size()?remotes[m].confirmed:0;}
bool ModbusRtu::requestOutput(uint8_t m,uint8_t pin,bool value){
    if(m>=remotes.size())return false;
    auto& r=remotes[m];const uint64_t bit=uint64_t(1)<<pin;
    if(value)r.desired|=bit;else r.desired&=~bit;
    return outputReady(m)&&bool(r.confirmed&bit)==value;
}
void ModbusRtu::send(uint8_t* data,size_t n){
    const uint16_t crc=ModbusCodec::crc(data,n);data[n++]=crc&255;data[n++]=crc>>8;
    if(Hardware.modbus.de>=0)digitalWrite(Hardware.modbus.de,HIGH);
    bus.write(data,n);bus.flush(); // Only the short frame transmit time, never wait for a reply here.
    if(Hardware.modbus.de>=0)digitalWrite(Hardware.modbus.de,LOW);
    lastByte=micros();lastSend=millis();
}
void ModbusRtu::nextRequest(){
    for(size_t tried=0;tried<remotes.size()*2;++tried){
        if(cursor>=remotes.size())cursor=0;
        active=cursor;auto& r=remotes[active];
        const bool writing=writePhase;
        writePhase=!writePhase;if(writePhase)++cursor;
        const uint8_t count=writing?r.outputCount:r.inputCount;
        if(Hardware.modules[active].type!=ModuleType::MODBUS||!count)continue;
        function=writing?15:2;
        tx[0]=Hardware.modules[active].address;tx[1]=function;tx[2]=tx[3]=tx[4]=0;tx[5]=count;
        size_t n=6;
        if(writing){
            sentOutputs=r.desired;tx[6]=(count+7)/8;n=7+tx[6];
            for(uint8_t i=0;i<tx[6];++i)tx[7+i]=sentOutputs>>(8*i);
        }
        waiting=true;send(tx,n);return;
    }
}
void ModbusRtu::masterFrame(){
    if(!waiting||frame[0]!=Hardware.modules[active].address)return;
    auto& r=remotes[active];
    if(frame[1]==(function|0x80)){r.inputValid=r.outputValid=false;waiting=false;return;}
    if(frame[1]!=function)return;
    if(function==15){
        if(length!=8||ModbusCodec::word(frame+2)!=0||ModbusCodec::word(frame+4)!=r.outputCount)return;
        r.confirmed=sentOutputs;r.outputValid=true;r.lastOutput=millis();
    }else{
        if(frame[2]!=(r.inputCount+7)/8||length!=size_t(frame[2])+5)return;
        r.inputs=0;for(uint8_t i=0;i<frame[2];++i)r.inputs|=uint64_t(frame[3+i])<<(8*i);
        r.inputValid=true;r.lastInput=millis();
    }
    waiting=false;
}
void ModbusRtu::slaveFrame(){
    if(frame[0]!=Hardware.modbus.address)return; // Broadcast is deliberately not used.
    const uint8_t fn=frame[1];
    uint8_t exception=1;size_t n=0;tx[0]=frame[0];tx[1]=fn;
    if(length>=8){
        const unsigned start=ModbusCodec::word(frame+2),count=ModbusCodec::word(frame+4);
        if(fn==1||fn==2){
            const size_t available=fn==1?Hardware.outputs.size():Hardware.inputs.size();
            if(length!=8||!count||count>64)exception=3;
            else if(start+count>available)exception=2;
            else {
                n=3+(count+7)/8;tx[2]=(count+7)/8;
                for(size_t i=3;i<n;++i)tx[i]=0;
                for(unsigned i=0;i<count;++i){
                    bool value=false;
                    if(fn==1)value=(HardwarePorts.states&(uint64_t(1)<<(start+i)))!=0;
                    else if(!HardwarePorts.read(start+i,value)){exception=4;n=0;break;}
                    if(value)tx[3+i/8]|=1<<(i%8);
                }
            }
        }else if(fn==15){
            if(!count||count>64||frame[6]!=(count+7)/8||length!=size_t(frame[6])+9)exception=3;
            else if(start+count>Hardware.outputs.size())exception=2;
            else {
                uint64_t value=0;for(unsigned i=0;i<count;++i)if(frame[7+i/8]&(1<<(i%8)))value|=uint64_t(1)<<(start+i);
                if(HardwarePorts.write(ModbusCodec::mask(count)<<start,value)){
                    for(uint8_t i=2;i<6;++i)tx[i]=frame[i];n=6;lastWrite=millis();
                }else exception=4;
            }
        }else if(fn==5){
            if(length!=8||(count!=0 && count!=0xff00))exception=3;
            else if(start>=Hardware.outputs.size())exception=2;
            else if(HardwarePorts.write(uint64_t(1)<<start,count?uint64_t(1)<<start:0)){
                for(uint8_t i=2;i<6;++i)tx[i]=frame[i];n=6;lastWrite=millis();
            }else exception=4;
        }
    }
    if(!n){tx[1]=fn|0x80;tx[2]=exception;n=3;}
    send(tx,n);
}
void ModbusRtu::loop(){
    if(!started)return;
    if(Hardware.modbus.role==ModbusRole::SLAVE && uint32_t(millis()-lastWrite)>Hardware.modbus.watchdogMs)
        HardwarePorts.write(UINT64_MAX,0);
    // Drain a bounded frame. Overflowed frames are discarded at the next silence.
    unsigned budget=256;
    while(budget-- && bus.available()){
        int value=bus.read();if(value<0)break;
        if(length<sizeof(frame))frame[length++]=value;else overflow=true;
        lastByte=micros();
    }
    if((length||overflow)&&uint32_t(micros()-lastByte)>=gapUs){
        if(!overflow&&ModbusCodec::valid(frame,length)){
            if(Hardware.modbus.role==ModbusRole::SLAVE)slaveFrame();else masterFrame();
        }
        length=0;overflow=false;
    }
    if(Hardware.modbus.role!=ModbusRole::MASTER)return;
    if(waiting && uint32_t(millis()-lastSend)>Hardware.modbus.timeoutMs){
        remotes[active].inputValid=remotes[active].outputValid=false;waiting=false;
        // Do not accept a late response as confirmation of the next transaction.
        length=0;lastByte=micros();
    }
    if(!waiting&&!length&&uint32_t(micros()-lastByte)>=gapUs)nextRequest();
}
