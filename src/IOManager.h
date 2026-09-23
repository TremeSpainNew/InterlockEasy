#pragma once
#include <Arduino.h>

class IOManager {
public:
    void begin();
    void loop();
    void refreshInputs();
    bool getInput(uint8_t channel);
    bool getOutput(uint8_t channel);
    bool outputsReady() const { return relayReady; }
    bool setOutputs(uint8_t mask, uint8_t states);
    void setOutput(uint8_t channel, bool state);

private:
    bool inputState[8] = {}; // Debounced physical levels; inversion is applied on access.
    bool candidateState[8] = {};
    uint32_t candidateSince[8] = {};
    bool outputState[8] = {};
    bool relayReady = false;
    uint8_t relayMask = 0;
    uint32_t lastScan = 0;

    bool readInput(uint8_t channel);
    bool writeRelay(uint8_t channel, bool state);
    bool writeRelayRegister(uint8_t reg, uint8_t value);
};

extern IOManager IO;
