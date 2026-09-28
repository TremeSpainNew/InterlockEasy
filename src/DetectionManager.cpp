#include "DetectionManager.h"
#include "ConfigManager.h"
#include "IOManager.h"
#include "MqttManager.h"
#include <limits.h>

DetectionManager Detections;

void DetectionManager::reload() {
    cvStates.assign(Config.cvs.size(), CvState{});
    axleStates.assign(Config.axleCounters.size(), AxleCounterState{});
    const uint32_t now = uint32_t(millis());

    for (size_t i=0; i<Config.cvs.size(); ++i) {
        const auto& cfg=Config.cvs[i];
        if (!cfg.enabled || cfg.input<1 || cfg.input>NUM_INPUTS) continue;
        const uint8_t input=cfg.input-1;
        if (!IO.inputReady(input)) continue;
        cvStates[i].valid=true;
        cvStates[i].occupied=IO.getInput(input);
    }

    for (size_t i=0; i<Config.axleCounters.size(); ++i) {
        const auto& cfg=Config.axleCounters[i];
        auto& state=axleStates[i];
        state.lastConnection=now;
        state.lastActivation=now;
        if (!cfg.enabled || cfg.inputA<1 || cfg.inputA>NUM_INPUTS ||
            cfg.inputB<1 || cfg.inputB>NUM_INPUTS) continue;
        const uint8_t a=cfg.inputA-1, b=cfg.inputB-1;
        if (!IO.inputReady(a) || !IO.inputReady(b)) continue;
        state.lastA=IO.getInput(a);
        state.lastB=IO.getInput(b);
        state.initialized=true;
    }
}

const CvState* DetectionManager::cvState(uint8_t i) const {
    return i<cvStates.size()?&cvStates[i]:nullptr;
}
const AxleCounterState* DetectionManager::axleState(uint8_t i) const {
    return i<axleStates.size()?&axleStates[i]:nullptr;
}

void DetectionManager::publishCv(uint8_t i) {
    if (i>=Config.cvs.size() || i>=cvStates.size()) return;
    const auto& cfg=Config.cvs[i];
    const auto& state=cvStates[i];
    if (!cfg.enabled || !state.valid) return;
    MQTT.publishValue(cfg.topic(),
        state.occupied?"{\"Estado\":\"Ocupado\"}":"{\"Estado\":\"Libre\"}", true);
}

void DetectionManager::processCv(uint8_t i) {
    if (i>=Config.cvs.size() || i>=cvStates.size()) return;
    const auto& cfg=Config.cvs[i];
    auto& state=cvStates[i];
    if (!cfg.enabled || cfg.input<1 || cfg.input>NUM_INPUTS) return;
    const uint8_t input=cfg.input-1;
    if (!IO.inputReady(input)) { state.valid=false; return; }
    const bool occupied=IO.getInput(input);
    if (!state.valid || occupied!=state.occupied) {
        state.valid=true; state.occupied=occupied; publishCv(i);
    }
}

void DetectionManager::publishAxleEvent(uint8_t i,const String& payload) {
    if (i>=Config.axleCounters.size() || i>=axleStates.size()) return;
    if (!Config.axleCounters[i].enabled) return;
    const String encoded="\""+payload+"\"";
    if (MQTT.publishValue(Config.axleCounters[i].topic(),encoded,false))
        axleStates[i].lastEvent=payload;
}

void DetectionManager::addAxleEvent(uint8_t i,bool isB,uint32_t now) {
    if (i>=axleStates.size()) return;
    auto& s=axleStates[i];
    s.events.push_back({isB,now});
    s.lastActivation=now;
    if (s.events.size()>256) {
        s.events.clear(); s.trainDirection=-1; s.missedLast=-1; s.error=true;
        publishAxleEvent(i,"Error");
    }
}

int8_t DetectionManager::getDirection(const AxleCounterState& s,bool strict) const {
    if (s.events.size()<2) return -1;
    uint32_t abTotal=0,baTotal=0,abMin=UINT32_MAX,baMin=UINT32_MAX;
    uint16_t ab=0,ba=0;
    for (size_t i=1;i<s.events.size();++i) {
        const auto& p=s.events[i-1]; const auto& c=s.events[i];
        if (p.isB==c.isB) continue;
        const uint32_t dt=c.timestamp-p.timestamp;
        if (dt>CE_MAX_WHEELSET_INTERVAL_MS) continue;
        if (!p.isB && c.isB) { ++ab; abTotal+=dt; if(dt<abMin)abMin=dt; }
        else { ++ba; baTotal+=dt; if(dt<baMin)baMin=dt; }
    }
    if (!ab&&!ba) return -1;
    if (ab&&!ba) return strict&&ab<2?-1:1;
    if (ba&&!ab) return strict&&ba<2?-1:0;
    if (abMin<baMin) return 1;
    if (baMin<abMin) return 0;
    const uint32_t abAvg=abTotal/ab, baAvg=baTotal/ba;
    if (abAvg<baAvg) return 1;
    if (baAvg<abAvg) return 0;
    if (!strict) { if(ab>ba)return 1; if(ba>ab)return 0; }
    return -1;
}

uint16_t DetectionManager::countAxles(AxleCounterState& s,int8_t direction) {
    if (s.events.empty() || direction<0) return 0;
    const int8_t first=direction==1?0:1;
    const int8_t second=direction==1?1:0;
    uint16_t count=0;
    int8_t waiting=s.missedLast;

    for (const auto& e:s.events) {
        const int8_t sensor=e.isB?1:0;
        if (waiting<0) {
            if (sensor==first) waiting=second;
            else { ++count; waiting=-1; }
        } else if (sensor==waiting) {
            ++count; waiting=-1;
        } else if (sensor==first) {
            ++count; waiting=second;
        }
    }
    s.missedLast=waiting;
    return count;
}

void DetectionManager::processAxleEvents(uint8_t i) {
    if (i>=axleStates.size() || i>=Config.axleCounters.size()) return;
    auto& s=axleStates[i];
    if (s.events.empty()) return;
    int8_t dir=getDirection(s,true);
    if (dir<0) dir=s.trainDirection;
    if (dir<0) dir=getDirection(s,false);
    if (dir<0) {
        s.error=true; publishAxleEvent(i,"Error");
        s.events.clear(); s.missedLast=-1; return;
    }
    s.trainDirection=dir;
    const uint16_t count=countAxles(s,dir);
    s.events.clear();
    if (!count) return;
    s.error=false;
    publishAxleEvent(i,String(dir==1?"Nominal:":"Reverse:")+String(count));
}

void DetectionManager::processAxleCounter(uint8_t i) {
    if (i>=Config.axleCounters.size() || i>=axleStates.size()) return;
    const auto& cfg=Config.axleCounters[i]; auto& s=axleStates[i];
    if (!cfg.enabled || cfg.inputA<1 || cfg.inputA>NUM_INPUTS ||
        cfg.inputB<1 || cfg.inputB>NUM_INPUTS) return;
    const uint8_t ia=cfg.inputA-1, ib=cfg.inputB-1;
    if (!IO.inputReady(ia)||!IO.inputReady(ib)) { s.initialized=false; return; }

    const uint32_t now=uint32_t(millis());
    const bool a=IO.getInput(ia), b=IO.getInput(ib);
    if (!s.initialized) {
        s.lastA=a; s.lastB=b; s.initialized=true; s.lastActivation=now; return;
    }
    const bool edgeA=a&&!s.lastA, edgeB=b&&!s.lastB;
    s.lastA=a; s.lastB=b;
    if(edgeA)addAxleEvent(i,false,now);
    if(edgeB)addAxleEvent(i,true,now);

    if(s.events.size()>3) {
        const int8_t dir=getDirection(s,true);
        if(dir>=0){s.trainDirection=dir;processAxleEvents(i);}
    }
    if(!s.events.empty() && uint32_t(now-s.lastActivation)>=CE_MAX_WHEELSET_INTERVAL_MS)
        processAxleEvents(i);
    if(uint32_t(now-s.lastActivation)>=CE_MAX_TRAIN_INTERVAL_MS) {
        if(!s.events.empty())processAxleEvents(i);
        s.trainDirection=-1; s.missedLast=-1;
    }
    if(uint32_t(now-s.lastConnection)>=CE_CONNECTION_INTERVAL_MS) {
        publishAxleEvent(i,"conexion"); s.lastConnection=now;
    }
}

void DetectionManager::publishAll() {
    for(uint8_t i=0;i<cvStates.size();++i)publishCv(i);
}
void DetectionManager::loop() {
    for(uint8_t i=0;i<Config.cvs.size();++i)processCv(i);
    for(uint8_t i=0;i<Config.axleCounters.size();++i)processAxleCounter(i);
}
