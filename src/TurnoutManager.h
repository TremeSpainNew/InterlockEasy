#pragma once
#include <Arduino.h>
#include <vector>

struct TurnoutState {
    int8_t commanded = -1;     // -1 ninguno, 0 normal, 1 invertida
    int8_t feedback = -2;      // -2 desconocido, -1 sin comprobacion, 0 normal, 1 invertida
    bool pulseActive = false;
    uint32_t pulseUntil = 0;
    String lastFeedbackPayload;
    uint32_t lastFeedbackPublish = 0;
};

class TurnoutManager {
public:
    void reload();
    void stop(const String& topic = "");
    void loop();
    bool command(const String& topic, const String& payload);
    void publishAllFeedback();
    const TurnoutState* state(uint8_t index) const;

private:
    std::vector<TurnoutState> states;
    void drive(uint8_t index, uint8_t position);
    void stopPulse(uint8_t index);
    int8_t readFeedback(uint8_t index) const;
    void publishFeedback(uint8_t index, bool force=false);
};

extern TurnoutManager Turnouts;
