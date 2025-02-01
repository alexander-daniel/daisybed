#pragma once

#include "daisy_pod.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

class Voice {
public:
    Voice();
    void Init(float sampleRate);
    void SetNote(int note, float vel);
    void Release();
    void Clear();
    bool IsActive() const;
    int GetNote() const;
    uint32_t GetAge() const;
    void IncrementAge();

    Oscillator osc;
    AdEnv env;

private:
    int midiNote;
    bool active;
    bool releasing;
    float velocity;
    uint32_t age;
};
