#pragma once
#include <Arduino.h>
#include <vector>

static constexpr uint32_t CE_MAX_WHEELSET_INTERVAL_MS = 2000;
static constexpr uint32_t CE_MAX_TRAIN_INTERVAL_MS = 10000;
static constexpr uint32_t CE_CONNECTION_INTERVAL_MS = 30000;

struct CvState {
    bool valid = false;
    bool occupied = false;
};

struct AxleSensorEvent {
    bool isB = false;
    uint32_t timestamp = 0;
};

struct AxleCounterState {
    bool initialized = false;
    bool lastA = false;
    bool lastB = false;
    std::vector<AxleSensorEvent> events;
    int8_t trainDirection = -1;
    int8_t missedLast = -1;
    uint32_t lastActivation = 0;
    uint32_t lastConnection = 0;
    String lastEvent;
    bool error = false;
};

class DetectionManager {
public:
    void reload();
    void loop();
    void publishAll();
    const CvState* cvState(uint8_t index) const;
    const AxleCounterState* axleState(uint8_t index) const;

private:
    std::vector<CvState> cvStates;
    std::vector<AxleCounterState> axleStates;
    void processCv(uint8_t index);
    void publishCv(uint8_t index);
    void processAxleCounter(uint8_t index);
    void addAxleEvent(uint8_t index, bool isB, uint32_t now);
    int8_t getDirection(const AxleCounterState& state, bool strict) const;
    uint16_t countAxles(AxleCounterState& state, int8_t direction);
    void processAxleEvents(uint8_t index);
    void publishAxleEvent(uint8_t index, const String& payload);
};

extern DetectionManager Detections;
