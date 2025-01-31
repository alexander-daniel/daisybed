#include "daisy_pod.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

const int NUM_VOICES = 6;

DaisyPod hw;
Oscillator osc1, osc2, osc3;
Svf filter;
AdEnv env1, env2, env3; // Multiple envelopes for polyphony

// Mode tracking
enum Mode {
    MODE_DEFAULT,
    MODE_ENVELOPE,
    MODE_FILTER_STRUM
};
Mode currentMode = MODE_DEFAULT;

// Waveform selection
int currentWaveform = 0;
const int NUM_WAVEFORMS = 4;
const int WAVEFORMS[NUM_WAVEFORMS] = {
    Oscillator::WAVE_POLYBLEP_SAW,
    Oscillator::WAVE_POLYBLEP_SQUARE,
    Oscillator::WAVE_POLYBLEP_TRI,
    Oscillator::WAVE_SIN
};

// Strum control
float STRUM_DELAY_MS = 30.0f;
uint32_t voice2DelayTimer = 0;
uint32_t voice3DelayTimer = 0;
bool voice2Pending = false;
bool voice3Pending = false;

// MIDI note tracking
struct Voice {
    int midiNote = -1;
    bool active = false;
    float velocity = 0.0f;
    uint32_t age = 0; // Track how long this voice has been active

    Oscillator osc;
    AdEnv env;

} voices[NUM_VOICES];

// Global settings state
struct Settings {
    // Envelope settings
    float attackTime = 0.005f;    // Default 5ms
    float releaseTime = 0.15f;    // Default 150ms
    
    // Filter settings
    float filterCutoff = 2000.0f; // Default 2kHz
    float filterRes = 0.4f;       // Default resonance
    
    // Strum settings
    float strumDelay = 30.0f;     // Default 30ms
    float tempo = 1.0f;           // Default tempo multiplier (1.0 = normal speed)
} settings;

// Add these after the Settings struct
struct KnobState {
    bool caught1 = false;  // Whether knob1 has caught up to its stored value
    bool caught2 = false;  // Whether knob2 has caught up to its stored value
} knobStates[3];  // One state for each mode

// Helper function to determine if a knob has "caught" the stored value
bool hasKnobCaught(float knobValue, float storedValue, bool wasCaught) {
    if (wasCaught) return true;
    // Consider the value "caught" when the knob is within 2% of the stored value
    const float threshold = 0.02f;
    return fabs(knobValue - storedValue) < threshold;
}

// Find oldest voice to steal
int findOldestVoice() {
    int oldestIdx = 0;
    uint32_t oldestAge = voices[0].age;
    
    for(int i = 1; i < NUM_VOICES; i++) {
        if(voices[i].age > oldestAge) {
            oldestAge = voices[i].age;
            oldestIdx = i;
        }
    }
    return oldestIdx;
}

// Find available voice or steal oldest if needed
int findAvailableVoice(int noteNumber) {
    // First, check if this note is already playing (prevent retriggering)
    for(int i = 0; i < NUM_VOICES; i++) {
        if(voices[i].midiNote == noteNumber && voices[i].active) {
            return -1;
        }
    }
    
    // Then, look for an inactive voice
    for(int i = 0; i < NUM_VOICES; i++) {
        if(!voices[i].active) {
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
                    if(voices[i].midiNote == p.note && voices[i].active) {
                        voices[i].active = false;
                    }
                }
                return;
            }

            // Flash LED for visual feedback
            hw.led1.Set(1.0f, 1.0f, 1.0f);
            hw.UpdateLeds();
            
            int voice = findAvailableVoice(p.note);
            if(voice == -1) return; // Note is already playing
            
            // Set up the voice
            voices[voice].midiNote = p.note;
            voices[voice].active = true;
            voices[voice].velocity = p.velocity / 127.0f;
            voices[voice].age = 0; // Reset age for new note
            voices[voice].osc.SetFreq(mtof(p.note));
            voices[voice].env.Trigger();
            
            // Increment age of other voices
            for(int i = 0; i < NUM_VOICES; i++) {
                if(i != voice && voices[i].active) {
                    voices[i].age++;
                }
            }
            
            // Turn off LED after brief delay
            System::Delay(10);
            hw.led1.Set(0.0f, 0.0f, 0.0f);
            hw.UpdateLeds();
            break;
        }
        
        case NoteOff: {
            NoteOffEvent p = m.AsNoteOff();
            for(int i = 0; i < NUM_VOICES; i++) {
                if(voices[i].midiNote == p.note && voices[i].active) {
                    voices[i].active = false;
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
        if(currentMode == MODE_ENVELOPE) {
            currentMode = MODE_DEFAULT;
        } else {
            currentMode = MODE_ENVELOPE;
            // Clear other mode if active
            if(currentMode == MODE_FILTER_STRUM) {
                hw.led2.Set(0.0f, 0.0f, 0.0f);
            }
        }
        // Reset catch state for the new mode
        knobStates[currentMode].caught1 = false;
        knobStates[currentMode].caught2 = false;
        hw.led1.Set(currentMode == MODE_ENVELOPE ? 1.0f : 0.0f, 0.0f, 0.0f);
        hw.UpdateLeds();
    }
    
    if(hw.button2.RisingEdge()) {
        if(currentMode == MODE_FILTER_STRUM) {
            currentMode = MODE_DEFAULT;
        } else {
            currentMode = MODE_FILTER_STRUM;
            // Clear other mode if active
            if(currentMode == MODE_ENVELOPE) {
                hw.led1.Set(0.0f, 0.0f, 0.0f);
            }
        }
        // Reset catch state for the new mode
        knobStates[currentMode].caught1 = false;
        knobStates[currentMode].caught2 = false;
        hw.led2.Set(0.0f, currentMode == MODE_FILTER_STRUM ? 1.0f : 0.0f, 0.0f);
        hw.UpdateLeds();
    }

    float knob1 = hw.GetKnobValue(DaisyPod::KNOB_1);
    float knob2 = hw.GetKnobValue(DaisyPod::KNOB_2);

    switch(currentMode) {
        case MODE_ENVELOPE:
            // Update attack only if knob has caught up
            if (hasKnobCaught(knob1, settings.attackTime, knobStates[MODE_ENVELOPE].caught1)) {
                knobStates[MODE_ENVELOPE].caught1 = true;
                settings.attackTime = fmap(knob1, 0.001f, 0.5f);
                for(int v = 0; v < NUM_VOICES; v++) {
                    voices[v].env.SetTime(ADENV_SEG_ATTACK, settings.attackTime);
                }
            }
            
            // Update release only if knob has caught up
            if (hasKnobCaught(knob2, settings.releaseTime, knobStates[MODE_ENVELOPE].caught2)) {
                knobStates[MODE_ENVELOPE].caught2 = true;
                settings.releaseTime = fmap(knob2, 0.1f, 2.0f);
                for(int v = 0; v < NUM_VOICES; v++) {
                    voices[v].env.SetTime(ADENV_SEG_DECAY, settings.releaseTime);
                }
            }
            break;

        case MODE_FILTER_STRUM:
            // Update strum delay only if knob has caught up
            if (hasKnobCaught(knob2, settings.strumDelay/200.0f, knobStates[MODE_FILTER_STRUM].caught2)) {
                knobStates[MODE_FILTER_STRUM].caught2 = true;
                settings.strumDelay = fmap(knob2, 10.0f, 200.0f);
                STRUM_DELAY_MS = settings.strumDelay;
            }
            break;

        case MODE_DEFAULT:
            // Update filter cutoff only if knob has caught up
            if (hasKnobCaught(knob1, settings.filterCutoff/10000.0f, knobStates[MODE_DEFAULT].caught1)) {
                knobStates[MODE_DEFAULT].caught1 = true;
                settings.filterCutoff = fmap(knob1, 200.0f, 10000.0f);
                filter.SetFreq(settings.filterCutoff);
            }
            
            // Update resonance only if knob has caught up
            if (hasKnobCaught(knob2, settings.filterRes, knobStates[MODE_DEFAULT].caught2)) {
                knobStates[MODE_DEFAULT].caught2 = true;
                settings.filterRes = fmap(knob2, 0.1f, 0.95f);
                filter.SetRes(settings.filterRes);
            }
            break;
    }

    for(size_t i = 0; i < size; i++)
    {
        // Process all voices
        float signal = 0.0f;
        for(int v = 0; v < NUM_VOICES; v++) {
            float voiceOutput = voices[v].osc.Process() * voices[v].env.Process();
            signal += voiceOutput * (voices[v].active ? voices[v].velocity : 0.0f);
        }
        
        // Scale final mix to prevent clipping based on number of voices
        signal *= (0.6f / NUM_VOICES);
        
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
        voices[v].osc.Init(sampleRate);
        voices[v].env.Init(sampleRate);
        
        // Set envelope parameters
        voices[v].env.SetTime(ADENV_SEG_ATTACK, 0.005);  // Faster attack
        voices[v].env.SetTime(ADENV_SEG_DECAY, 0.15);    // Shorter decay
        voices[v].env.SetMin(0.0);
        voices[v].env.SetMax(0.8);                       // Slightly reduced maximum
        voices[v].env.SetCurve(0);                       // Linear curve
        
        // Set initial waveform
        voices[v].osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
    }

    // Set up filter
    filter.SetRes(0.4f);  // Set a moderate fixed resonance
    filter.SetDrive(0.3f);

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
