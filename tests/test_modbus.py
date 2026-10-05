"""Actual RTU state machine with UART and physical I/O simulated."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
scope={'__file__':str(ROOT/'tests/test_config.py')}
exec((ROOT/'tests/test_config.py').read_text().split('TEST =')[0],scope)
stubs=scope['STUBS'].copy()
stubs['Arduino.h']+='''
#include <algorithm>
using std::max;
inline uint32_t clockUs=0;
inline unsigned long millis(){return clockUs/1000;}
inline unsigned long micros(){return clockUs;}
constexpr int OUTPUT=1,HIGH=1,LOW=0;
inline void pinMode(int,int){} inline void digitalWrite(int,int){}
'''
stubs['HardwareSerial.h']='''#pragma once
#include <vector>
#include <deque>
constexpr int SERIAL_8N2=0,SERIAL_8E1=1,SERIAL_8O1=2;
inline std::deque<uint8_t> rx;
inline std::vector<uint8_t> tx;
struct HardwareSerial {
 HardwareSerial(int){} void begin(unsigned,int,int,int){}
 int available(){return rx.size();} int read(){auto v=rx.front();rx.pop_front();return v;}
 void write(uint8_t* b,size_t n){tx.assign(b,b+n);}void flush(){}
};
'''
test=r'''
#include "ModbusRtu.h"
#include "ModbusCodec.h"
#include "HardwareConfig.h"
#include "HardwareIO.h"
#include <HardwareSerial.h>
#include <cassert>
HardwareConfig Hardware;HardwareIO HardwarePorts;
bool failIo=false;uint64_t inputBits=uint64_t(1)<<63;
bool HardwareIO::write(uint64_t mask,uint64_t value){if(failIo)return false;states=(states&~mask)|(value&mask);return true;}
bool HardwareIO::read(uint8_t ch,bool& v)const{v=(inputBits&(uint64_t(1)<<ch));return !failIo;}
void deliver(std::vector<uint8_t> bytes,bool corrupt=false){
 auto crc=ModbusCodec::crc(bytes.data(),bytes.size());bytes.push_back(crc&255);bytes.push_back(crc>>8);
 if(corrupt)bytes.back()^=1;
 tx.clear();for(auto b:bytes)rx.push_back(b);Modbus.loop();clockUs+=5000;Modbus.loop();
}
int main(){
 const uint8_t known[]={1,3,0,0,0,10};assert(ModbusCodec::crc(known,6)==0xcdc5);
 Hardware.modules={{ModuleType::GPIO,0}};Hardware.modbus.role=ModbusRole::SLAVE;Hardware.outputs.resize(64);Hardware.inputs.resize(64);Modbus.begin();
 deliver({1,15,0,63,0,1,1,1});assert(HardwarePorts.states==(uint64_t(1)<<63));assert(tx.size()==8&&tx[1]==15&&ModbusCodec::valid(tx.data(),tx.size()));
 deliver({1,2,0,63,0,1});assert(tx.size()==6&&tx[3]==1);
 deliver({1,5,0,63,0,0},true);assert(tx.empty()&&HardwarePorts.states);
 deliver({2,5,0,63,0,0});assert(tx.empty()&&HardwarePorts.states);
 deliver({1,15,0,64,0,1,1,1});assert(tx[1]==143&&tx[2]==2);
 deliver({1,15,0,0,0,64,1,1});assert(tx[2]==3);
 failIo=true;deliver({1,5,0,63,0,0});assert(tx[2]==4&&HardwarePorts.states);failIo=false;
 clockUs+=4000000;Modbus.loop();assert(!HardwarePorts.states);
 Modbus=ModbusRtu{};Hardware.modbus.role=ModbusRole::MASTER;Hardware.modules={{ModuleType::MODBUS,7}};
 Hardware.inputs={{0,63,false,false}};Hardware.outputs={{0,63,false,false}};Modbus.begin();
 assert(!Modbus.requestOutput(0,63,true));clockUs+=5000;Modbus.loop();
 assert(tx[0]==7&&tx[1]==15&&tx[5]==64&&tx[14]==128);
 deliver({7,15,0,0,0,64});assert(Modbus.outputReady(0));assert(Modbus.outputs(0)==(uint64_t(1)<<63));
 // The next request is FC02 (independent input and output address spaces).
 clockUs+=5000;Modbus.loop();assert(tx[1]==2&&tx[5]==64);
 deliver({7,2,8,0,0,0,0,0,0,0,128});bool value=false;assert(Modbus.input(0,63,value)&&value);
 clockUs+=200000;Modbus.loop();clockUs+=200000;Modbus.loop();assert(!Modbus.outputReady(0));
}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder)
 for n,s in stubs.items():(p/n).write_text(s)
 (p/'test.cpp').write_text(test)
 subprocess.run(['c++','-std=c++17',f'-I{p}',f'-I{ROOT/"src"}',f'-I{ROOT/".pio/libdeps/esp32-s3-devkitc-1/ArduinoJson/src"}',str(ROOT/'src/ModbusRtu.cpp'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('OK: RTU CRC, slave writes/reads at channel 64, invalid frames, watchdog, master ACK and timeouts')
