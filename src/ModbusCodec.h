#pragma once
#include <stdint.h>
#include <stddef.h>
namespace ModbusCodec {
inline uint16_t crc(const uint8_t* bytes,size_t length){
    uint16_t value=0xffff;
    for(size_t i=0;i<length;++i){value^=bytes[i];for(uint8_t bit=0;bit<8;++bit)value=(value>>1)^((value&1)?0xa001:0);}
    return value;
}
inline uint16_t word(const uint8_t* bytes){return uint16_t(bytes[0])<<8|bytes[1];}
inline uint64_t mask(unsigned count){return count==64?UINT64_MAX:(uint64_t(1)<<count)-1;}
inline bool valid(const uint8_t* bytes,size_t length){return length>=4 && crc(bytes,length)==0;}
}
