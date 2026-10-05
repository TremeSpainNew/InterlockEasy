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
    bool inputReady(uint8_t channel) const { return channel < 64 && (simulated[channel] || inputValid[channel]); }
    bool outputReady(uint8_t channel) const;
    bool outputsReady() const { return relayReady; }
    bool setOutputs(uint64_t mask, uint64_t states);
    void setOutput(uint8_t channel, bool state);

private:
    bool simulated[64] = {};
    bool simulatedState[64] = {};
    uint32_t simulationSince[64] = {};
    bool inputState[64] = {}; // Debounced physical levels; inversion is applied on access.
    bool candidateState[64] = {};
    uint32_t candidateSince[64] = {};
    bool inputValid[64] = {};
    bool sampleValid[64] = {};
    uint64_t publishedOutputs = 0;
    bool relayReady = false;
    uint32_t lastScan = 0;

};

extern IOManager IO;
