#include "daisy_pod.h"
#include "Voice.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

Voice::Voice() : 
    midiNote(-1),
    active(false), 
    releasing(false),
    velocity(0.0f),
    age(0)
{
}

void Voice::Init(float sampleRate) {
    osc.Init(sampleRate);
    env.Init(sampleRate);
    
    // Set envelope parameters
    env.SetTime(ADENV_SEG_ATTACK, 0.005);  // Faster attack
    env.SetTime(ADENV_SEG_DECAY, 0.35);    // medium decay
    env.SetMin(0.0);
    env.SetMax(0.9);                       // Slightly reduced maximum
    env.SetCurve(0);                       // Linear curve
    
    // Set initial waveform
    osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
}

void Voice::SetNote(int note, float vel) {
    midiNote = note;
    active = true;
    velocity = vel;
    age = 0;
    osc.SetFreq(mtof(note));
    env.Trigger();
}

void Voice::Release() {
    active = false;
    releasing = true;
}

void Voice::Clear() {
    releasing = false;
    midiNote = -1;
}

bool Voice::IsActive() const { return active || releasing; }
int Voice::GetNote() const { return midiNote; }
uint32_t Voice::GetAge() const { return age; }
void Voice::IncrementAge() { age++; }