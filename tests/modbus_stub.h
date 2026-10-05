#pragma once
#include "ModbusRtu.h"
ModbusRtu Modbus;
bool ModbusRtu::input(uint8_t,uint8_t,bool&)const{return false;}
bool ModbusRtu::outputReady(uint8_t)const{return false;}
uint64_t ModbusRtu::outputs(uint8_t)const{return 0;}
bool ModbusRtu::requestOutput(uint8_t,uint8_t,bool){return false;}
