#pragma once
#include <Arduino.h>

class IOManager {
public:
    void begin();
    void loop();
    void refreshInputs();
    bool getInput(uint8_t channel);
    bool simulateInput(uint8_t channel, int8_t state);
    bool inputSimulated(uint8_t channel) const;
    bool getRawInput(uint8_t channel) const;
    bool inputFiltering(uint8_t channel) const;
    bool getOutput(uint8_t channel);
    bool inputReady(uint8_t channel) const { return channel < 32 && (simulated[channel] || inputValid[channel]); }
    bool outputsReady() const { return relayReady; }
    bool setOutputs(uint32_t mask, uint32_t states);
    void setOutput(uint8_t channel, bool state);

private:
    bool simulated[32] = {};
    bool simulatedState[32] = {};
    uint32_t simulationSince[32] = {};
    bool inputState[32] = {}; // Debounced physical levels; inversion is applied on access.
    bool candidateState[32] = {};
    uint32_t candidateSince[32] = {};
    bool inputValid[32] = {};
    bool sampleValid[32] = {};
    bool relayReady = false;
    uint32_t lastScan = 0;

};

extern IOManager IO;
