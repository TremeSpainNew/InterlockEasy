#pragma once
#include <Arduino.h>
#include <vector>

struct TrackSectionState {
    uint16_t count = 0;
    int8_t entrance = -1;
    bool occupied = false;
    bool uncertain = false;
    bool lastA = false;
    bool lastB = false;
};

class DetectionManager {
public:
    void reload();
    void loop();
    bool reset(uint8_t index);
    void publishAll();
    const TrackSectionState* state(uint8_t index) const;
private:
    std::vector<TrackSectionState> states;
    void publish(uint8_t index, bool includeCount = true);
};

extern DetectionManager Detections;
