#include "daisy_pod.h"
#include "daisysp.h"
#include "knob.h"
#include "Voice.h"

using namespace daisy;
using namespace daisysp;

const int NUM_VOICES = 10;
// Mode tracking
enum Mode {
    MODE_DEFAULT,
    MODE_FILTER,
    MODE_AD,
    MODE_REVERB
};
Mode currentMode = MODE_DEFAULT;

DaisyPod hw;
Svf filter;
Voice voices[NUM_VOICES];

// Simple Schroeder Reverb implementation
class SimpleReverb {
private:
    static const int NUM_COMBS = 6;  // Increased from 4
    static const int NUM_ALLPASS = 3;  // Increased from 2
    
    // Delay lines
    DelayLine<float, 4096> combDelays[NUM_COMBS];  // Increased buffer size
    DelayLine<float, 2048> allpassDelays[NUM_ALLPASS];  // Increased buffer size
    
    // Feedback coefficients - higher values for more reverb
    float combFeedback[NUM_COMBS] = {0.88f, 0.87f, 0.86f, 0.85f, 0.84f, 0.83f};
    float allpassFeedback = 0.7f;  // Increased from 0.5
    
    // Delay lengths (in samples) - longer delays for more spacious reverb
    int combLengths[NUM_COMBS] = {2557, 2617, 2491, 2422, 2687, 2791};
    int allpassLengths[NUM_ALLPASS] = {525, 756, 889};
    
    float mix = 0.5f;
    float feedback = 0.85f;  // Higher default feedback

public:
    void Init(float sampleRate) {
        for(int i = 0; i < NUM_COMBS; i++) {
            combDelays[i].Init();
            combDelays[i].SetDelay(static_cast<size_t>(combLengths[i]));
        }
        for(int i = 0; i < NUM_ALLPASS; i++) {
            allpassDelays[i].Init();
            allpassDelays[i].SetDelay(static_cast<size_t>(allpassLengths[i]));
        }
    }
    
    void SetMix(float newMix) { mix = newMix; }
    void SetFeedback(float newFeedback) { 
        feedback = newFeedback;
        for(int i = 0; i < NUM_COMBS; i++) {
            combFeedback[i] = feedback * (0.88f - i * 0.01f);  // Higher base feedback
        }
    }
    
    float Process(float in) {
        float combOut = 0.0f;
        
        // Parallel comb filters
        for(int i = 0; i < NUM_COMBS; i++) {
            float delay = combDelays[i].Read();
            combDelays[i].Write(in + delay * combFeedback[i]);
            combOut += delay;
        }
        combOut *= 0.16f;  // Adjusted scaling for 6 combs
        
        // Series allpass filters
        float allpassOut = combOut;
        for(int i = 0; i < NUM_ALLPASS; i++) {
            float delay = allpassDelays[i].Read();
            float temp = allpassOut + delay * allpassFeedback;
            allpassDelays[i].Write(temp);
            allpassOut = delay - temp * allpassFeedback;
        }
        
        // Mix dry and wet with slight emphasis on wet signal
        return in * (1.0f - mix) + allpassOut * (mix * 1.2f);
    }
};

static SimpleReverb reverb;

// Waveform selection
int currentWaveform = 0;
const int NUM_WAVEFORMS = 4;
const int WAVEFORMS[NUM_WAVEFORMS] = {
    Oscillator::WAVE_POLYBLEP_SAW,
    Oscillator::WAVE_POLYBLEP_SQUARE,
    Oscillator::WAVE_POLYBLEP_TRI,
    Oscillator::WAVE_SIN
};

// Control parameters with knobs
struct Controls {
    Knob attackKnob;
    Knob releaseKnob;
    Knob cutoffKnob;
    Knob resonanceKnob;
    Knob reverbFeedbackKnob;
    Knob reverbMixKnob;
    
    // Add constructor with initialization list
    Controls() : attackKnob(), releaseKnob(), cutoffKnob(), resonanceKnob(), 
                reverbFeedbackKnob(), reverbMixKnob() {}
    
    void Init() {
        attackKnob.Init(0.005f, 0.001f, 1.0f);
        releaseKnob.Init(0.15f, 0.1f, 1.0f);
        cutoffKnob.Init(2000.0f, 200.0f, 10000.0f);
        resonanceKnob.Init(0.4f, 0.1f, 0.95f);
        reverbFeedbackKnob.Init(0.7f, 0.4f, 0.95f);  // Higher default and min feedback
        reverbMixKnob.Init(0.4f, 0.1f, 0.9f);  // Higher default mix and range
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
            case MODE_REVERB:
                reverbFeedbackKnob.Reset();
                reverbMixKnob.Reset();
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
            currentMode = MODE_REVERB;
        } else if(currentMode == MODE_REVERB) {
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
        } else if(currentMode == MODE_REVERB) {
            hw.led2.Set(0.0f, 1.0f, 0.0f);
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

        case MODE_REVERB:
            if (controls.reverbFeedbackKnob.Update(knob1)) {
                reverb.SetFeedback(controls.reverbFeedbackKnob.GetValue());
            }
            
            if (controls.reverbMixKnob.Update(knob2)) {
                reverb.SetMix(controls.reverbMixKnob.GetValue());
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
        signal *= (0.8f / NUM_VOICES);  // Reduced slightly to allow for more reverb headroom
        
        filter.Process(signal);
        float filtered = filter.Low();
        
        // Add safety clipping
        filtered = fclamp(filtered, -1.0f, 1.0f);
        
        // Process reverb
        float processed = reverb.Process(filtered);
        
        out[0][i] = processed;
        out[1][i] = processed;
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
    
    // Initialize reverb
    reverb.Init(sampleRate);
    reverb.SetFeedback(0.7f);  // Higher initial feedback
    reverb.SetMix(0.4f);  // Higher initial mix
    
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
