#pragma once
#include <stdint.h>

// Keeps the latest confirmed state, not a history of edges while offline.
class InputStateQueue {
public:
    void set(uint8_t channel, bool state) {
        if (channel >= 8) return;
        if (!dirty[channel] || values[channel] != state) attempted[channel] = false;
        values[channel] = state;
        dirty[channel] = true;
    }
    bool pending(uint8_t channel) const { return channel < 8 && dirty[channel]; }
    bool value(uint8_t channel) const { return channel < 8 && values[channel]; }
    void clear(uint8_t channel) { if (channel < 8) dirty[channel] = false; }
    int next(uint32_t now) {
        for (uint8_t n = 0; n < 8; ++n) {
            const uint8_t i = cursor;
            cursor = (cursor + 1) % 8;
            if (!dirty[i] || (attempted[i] && uint32_t(now - lastAttempt[i]) < 1000)) continue;
            attempted[i] = true;
            lastAttempt[i] = now;
            return i;
        }
        return -1;
    }
private:
    bool dirty[8] = {};
    bool values[8] = {};
    bool attempted[8] = {};
    uint32_t lastAttempt[8] = {};
    uint8_t cursor = 0;
};
