#include "daisy_pod.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

const int NUM_VOICES = 10;

DaisyPod hw;
Svf filter;

// Mode tracking
enum Mode {
    MODE_DEFAULT,
    MODE_FILTER,
    MODE_AD
};
Mode currentMode = MODE_DEFAULT;

// Knob class to handle value catching and mapping
class Knob {
public:
    Knob() : caught(false) {}
    
    void Init(float initValue, float minVal, float maxVal) {
        value = initValue;
        min = minVal;
        max = maxVal;
        caught = false;
    }
    
    bool Update(float knobValue) {
        // Check if knob has caught up to stored value
        if (!caught) {
            float normalizedStored = (value - min) / (max - min);
            caught = hasKnobCaught(knobValue, normalizedStored);
        }
        
        // Only update value if caught
        if (caught) {
            value = min + knobValue * (max - min);
            return true;
        }
        return false;
    }
    
    float GetValue() const { return value; }
    void Reset() { caught = false; }
    
private:
    bool hasKnobCaught(float knobValue, float storedValue) {
        const float threshold = 0.02f;
        return fabs(knobValue - storedValue) < threshold;
    }
    
    float value;
    float min;
    float max;
    bool caught;
};

// Waveform selection
int currentWaveform = 0;
const int NUM_WAVEFORMS = 4;
const int WAVEFORMS[NUM_WAVEFORMS] = {
    Oscillator::WAVE_POLYBLEP_SAW,
    Oscillator::WAVE_POLYBLEP_SQUARE,
    Oscillator::WAVE_POLYBLEP_TRI,
    Oscillator::WAVE_SIN
};

// MIDI note tracking
class Voice {
public:
    Voice() : midiNote(-1), active(false), releasing(false), velocity(0.0f), age(0) {}
    
    void Init(float sampleRate) {
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
    
    void SetNote(int note, float vel) {
        midiNote = note;
        active = true;
        velocity = vel;
        age = 0;
        osc.SetFreq(mtof(note));
        env.Trigger();
    }
    
    void Release() {
        active = false;
        releasing = true;
    }
    
    void Clear() {
        releasing = false;
        midiNote = -1;
    }
    
    bool IsActive() const { return active || releasing; }
    int GetNote() const { return midiNote; }
    uint32_t GetAge() const { return age; }
    void IncrementAge() { age++; }
    
    Oscillator osc;
    AdEnv env;

private:
    int midiNote;
    bool active;
    bool releasing;
    float velocity;
    uint32_t age;

} voices[NUM_VOICES];

// Control parameters with knobs
struct Controls {
    Knob attackKnob;
    Knob releaseKnob;
    Knob cutoffKnob;
    Knob resonanceKnob;
    
    void Init() {
        attackKnob.Init(0.005f, 0.001f, 1.0f);
        releaseKnob.Init(0.15f, 0.1f, 1.0f);
        cutoffKnob.Init(2000.0f, 200.0f, 10000.0f);
        resonanceKnob.Init(0.4f, 0.1f, 0.95f);
    }
    
    void ResetMode(Mode mode) {
        switch(mode) {
            case MODE_AD:
                attackKnob.Reset();
                releaseKnob.Reset();
                break;
            case MODE_FILTER:
                cutoffKnob.Reset();
                resonanceKnob.Reset();
                break;
            default:
                break;
        }
    }
} controls;

// Find oldest voice to steal
int findOldestVoice() {
    int oldestIdx = 0;
    uint32_t oldestAge = voices[0].GetAge();
    
    for(int i = 1; i < NUM_VOICES; i++) {
        if(voices[i].GetAge() > oldestAge) {
            oldestAge = voices[i].GetAge();
            oldestIdx = i;
        }
    }
    return oldestIdx;
}

// Find available voice or steal oldest if needed
int findAvailableVoice(int noteNumber) {
    // First, check if this note is already playing (prevent retriggering)
    for(int i = 0; i < NUM_VOICES; i++) {
        if(voices[i].GetNote() == noteNumber && voices[i].IsActive()) {
            return -1;
        }
    }
    
    // Then, look for an inactive voice
    for(int i = 0; i < NUM_VOICES; i++) {
        if(!voices[i].IsActive()) {
            return i;
        }
    }
    
    // If all voices are active, steal the oldest one
    return findOldestVoice();
}

void HandleMidiMessage(MidiEvent m) {
    switch(m.type) {
        case NoteOn: {
            NoteOnEvent p = m.AsNoteOn();
            if(p.velocity == 0) {
                // Note-off message in disguise
                for(int i = 0; i < NUM_VOICES; i++) {
                    if(voices[i].GetNote() == p.note && voices[i].IsActive()) {
                        voices[i].Release();
                    }
                }
                return;
            }
            
            int voice = findAvailableVoice(p.note);
            if(voice == -1) return; // Note is already playing
            
            // Set up the voice
            voices[voice].SetNote(p.note, p.velocity / 127.0f);
            
            // Increment age of other voices
            for(int i = 0; i < NUM_VOICES; i++) {
                if(i != voice && voices[i].IsActive()) {
                    voices[i].IncrementAge();
                }
            }
            
            break;
        }
        
        case NoteOff: {
            NoteOffEvent p = m.AsNoteOff();
            for(int i = 0; i < NUM_VOICES; i++) {
                if(voices[i].GetNote() == p.note && voices[i].IsActive()) {
                    voices[i].Release();
                }
            }
            break;
        }
    }
}

void AudioCallback(AudioHandle::InputBuffer in,
                  AudioHandle::OutputBuffer out,
                  size_t size)
{
    hw.ProcessAllControls();

    // Handle encoder for waveform selection
    int32_t inc = hw.encoder.Increment();
    if(inc != 0) {
        currentWaveform = (currentWaveform + inc) % NUM_WAVEFORMS;
        if(currentWaveform < 0) currentWaveform = NUM_WAVEFORMS - 1;
        
        // Set all oscillators to the same waveform
        for(int v = 0; v < NUM_VOICES; v++) {
            voices[v].osc.SetWaveform(WAVEFORMS[currentWaveform]);
        }
    }

    // Handle mode switching
    if(hw.button1.RisingEdge()) {
        if(currentMode == MODE_FILTER) {
            currentMode = MODE_DEFAULT;
        } else {
            currentMode = MODE_FILTER;
        }
        controls.ResetMode(currentMode);
        
        // Turn off both LEDs and then set the active one
        hw.led1.Set(0.0f, 0.0f, 0.0f);
        hw.led2.Set(0.0f, 0.0f, 0.0f);
        if(currentMode == MODE_FILTER) {
            hw.led1.Set(1.0f, 0.0f, 0.0f);
        }
        hw.UpdateLeds();
    }
    
    if(hw.button2.RisingEdge()) {
        if(currentMode == MODE_AD) {
            currentMode = MODE_DEFAULT;
        } else {
            currentMode = MODE_AD;
        }
        controls.ResetMode(currentMode);
        
        // Turn off both LEDs and then set the active one
        hw.led1.Set(0.0f, 0.0f, 0.0f);
        hw.led2.Set(0.0f, 0.0f, 0.0f);
        if(currentMode == MODE_AD) {
            hw.led2.Set(0.0f, 0.0f, 1.0f);
        }
        hw.UpdateLeds();
    }

    float knob1 = hw.GetKnobValue(DaisyPod::KNOB_1);
    float knob2 = hw.GetKnobValue(DaisyPod::KNOB_2);

    switch(currentMode) {
        case MODE_AD:
            if (controls.attackKnob.Update(knob1)) {
                float attackTime = controls.attackKnob.GetValue();
                for(int v = 0; v < NUM_VOICES; v++) {
                    voices[v].env.SetTime(ADENV_SEG_ATTACK, attackTime);
                }
            }
            
            if (controls.releaseKnob.Update(knob2)) {
                float releaseTime = controls.releaseKnob.GetValue();
                for(int v = 0; v < NUM_VOICES; v++) {
                    voices[v].env.SetTime(ADENV_SEG_DECAY, releaseTime);
                }
            }
            break;

        case MODE_FILTER:
            if (controls.cutoffKnob.Update(knob1)) {
                filter.SetFreq(controls.cutoffKnob.GetValue());
            }
            
            if (controls.resonanceKnob.Update(knob2)) {
                filter.SetRes(controls.resonanceKnob.GetValue());
            }
            break;

        case MODE_DEFAULT:
            break;
    }

    for(size_t i = 0; i < size; i++)
    {
        float signal = 0.0f;
        for(int v = 0; v < NUM_VOICES; v++) {
            float envValue = voices[v].env.Process();
            // Check if envelope is done releasing
            if(voices[v].IsActive() && envValue < 0.001f) {
                voices[v].Clear();
            }
            
            float voiceOutput = voices[v].osc.Process() * envValue;
            // Use voice if either active or releasing
            if(voices[v].IsActive()) {
                signal += voiceOutput;
            }
        }
        
        // Scale final mix to prevent clipping based on number of voices
        signal *= (0.9f / NUM_VOICES);
        
        filter.Process(signal);
        float filtered = filter.Low();
        
        // Add safety clipping
        filtered = fclamp(filtered, -1.0f, 1.0f);
        
        out[0][i] = filtered;
        out[1][i] = filtered;
    }
}

int main(void)
{
    hw.Init();
    hw.StartAdc();
    
    // Initialize MIDI for TRS input
    hw.midi.StartReceive();
    
    // Initialize all oscillators
    float sampleRate = hw.AudioSampleRate();
    filter.Init(sampleRate);
    
    // Initialize oscillators and envelopes for each voice
    for(int v = 0; v < NUM_VOICES; v++) {
        voices[v].Init(sampleRate);
    }

    // Initialize controls
    controls.Init();

    // Set up filter
    filter.SetRes(0.4f);  // Set a moderate fixed resonance

    hw.StartAudio(AudioCallback);

    for(;;)
    {
        hw.midi.Listen();    
        // Handle USB MIDI Events
        while(hw.midi.HasEvents())
        {
            HandleMidiMessage(hw.midi.PopEvent());
        }

    }

    while(1) {}
}
