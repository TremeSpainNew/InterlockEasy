#include <Arduino.h>
#include "ConfigManager.h"
#include "EthernetManager.h"
#include "MqttManager.h"
#include "IOManager.h"
#include "WebManager.h"
#include "SignalManager.h"
#include "HardwareConfig.h"
#include "DetectionManager.h"
#include "TurnoutManager.h"
#include "ModbusRtu.h"

static bool hardwareValid=false;

void setup(){
    Serial.begin(115200);delay(1200);
    Serial.println("\n==============================");
    Serial.println(" Interlock Easy");
    Serial.println(" Hardware configurable");
    Serial.println("==============================");

    hardwareValid=Hardware.begin();
    if(!hardwareValid){Serial.println(Hardware.error);return;}

    Config.inputs.resize(Hardware.inputs.size());
    Config.outputs.resize(Hardware.outputs.size());
    Serial.printf("Hardware: %u entradas, %u reles, %u modulos.\n",
        unsigned(Hardware.inputs.size()),unsigned(Hardware.outputs.size()),unsigned(Hardware.modules.size()));

    Config.begin();
    IO.begin();
    Modbus.begin();
    Detections.reload();
    Turnouts.reload();

    if(Hardware.modbus.role==ModbusRole::SLAVE){
        if(Hardware.networkType!=NetworkType::NONE){Connectivity.begin();Web.begin();}
        return;
    }
    while(!Connectivity.begin()){
        Serial.println("Red no disponible. Reintentando...");
        delay(5000);
    }

    MQTT.begin();
    Web.begin();

    Serial.print("Sistema iniciado en ");
    Serial.println(Connectivity.ip());
}

void loop(){
    if(!hardwareValid){delay(100);return;}
    Modbus.loop();
    IO.loop();
    if(Hardware.modbus.role==ModbusRole::SLAVE){
        if(Hardware.networkType!=NetworkType::NONE){
            if(Hardware.networkType==NetworkType::WIFI&&!Connectivity.connected())Connectivity.begin();
            Connectivity.loop();Web.loop();
        }
        return;
    }
    Detections.loop();
    Turnouts.loop();
    Connectivity.loop();
    MQTT.loop();
    IO.loop();
    Signals.loop();
    Turnouts.loop();
    Web.loop();
}
