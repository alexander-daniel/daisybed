#include "daisy_pod.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

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

// Three-part harmony with moody minor progressions
const int HARMONY_NOTES[32][3] = {
    {62, 65, 69}, {57, 60, 65}, {53, 57, 60}, {50, 53, 57},  // Dm, Am, Em, Bm
    {60, 63, 67}, {55, 58, 63}, {51, 55, 58}, {50, 53, 58},  // Cm, Gm, Dm, Bm
    {58, 62, 65}, {53, 57, 62}, {50, 53, 57}, {55, 58, 62},  // Am, Em, Bm, Gm
    {57, 60, 64}, {52, 55, 60}, {50, 54, 57}, {53, 57, 60},  // Gm, Dm, Bm, Em
    {60, 63, 67}, {55, 58, 63}, {51, 55, 58}, {50, 53, 58},  // Cm, Gm, Dm, Bm
    {58, 62, 65}, {53, 57, 62}, {50, 53, 57}, {55, 58, 62},  // Am, Em, Bm, Gm
    {62, 65, 69}, {57, 60, 65}, {53, 57, 60}, {50, 53, 57},  // Dm, Am, Em, Bm
    {60, 63, 67}, {55, 58, 63}, {51, 55, 58}, {50, 53, 58}   // Cm, Gm, Dm, Bm
};

const uint32_t NOTE_DURATIONS[32] = {
    300, 300, 300, 300, 300, 300, 300, 300,  // Doubled the duration from 150 to 300ms
    300, 300, 300, 300, 300, 300, 300, 300,
    300, 300, 300, 300, 300, 300, 300, 300,
    300, 300, 300, 300, 300, 300, 300, 600   // Final note also doubled from 300 to 600ms
};

// Note sequence
const int MELODY_LENGTH = 32;
int currentNote = 0;
uint32_t noteTimer = 0;
uint32_t currentDuration = NOTE_DURATIONS[0];

// Waveform selection
int currentWaveform = 0;
const int NUM_WAVEFORMS = 4;
const int WAVEFORMS[NUM_WAVEFORMS] = {
    Oscillator::WAVE_POLYBLEP_SAW,
    Oscillator::WAVE_POLYBLEP_SQUARE,
    Oscillator::WAVE_POLYBLEP_TRI,
    Oscillator::WAVE_SIN
};

// Strum control (changed from const to variable)
float STRUM_DELAY_MS = 30.0f;
uint32_t voice2DelayTimer = 0;
uint32_t voice3DelayTimer = 0;
bool voice2Pending = false;
bool voice3Pending = false;

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
        osc1.SetWaveform(WAVEFORMS[currentWaveform]);
        osc2.SetWaveform(WAVEFORMS[currentWaveform]);
        osc3.SetWaveform(WAVEFORMS[currentWaveform]);
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
                env1.SetTime(ADENV_SEG_ATTACK, settings.attackTime);
                env2.SetTime(ADENV_SEG_ATTACK, settings.attackTime);
                env3.SetTime(ADENV_SEG_ATTACK, settings.attackTime);
            }
            
            // Update release only if knob has caught up
            if (hasKnobCaught(knob2, settings.releaseTime, knobStates[MODE_ENVELOPE].caught2)) {
                knobStates[MODE_ENVELOPE].caught2 = true;
                settings.releaseTime = fmap(knob2, 0.1f, 2.0f);
                env1.SetTime(ADENV_SEG_DECAY, settings.releaseTime);
                env2.SetTime(ADENV_SEG_DECAY, settings.releaseTime);
                env3.SetTime(ADENV_SEG_DECAY, settings.releaseTime);
            }
            break;

        case MODE_FILTER_STRUM:
            // Update tempo only if knob has caught up (0.25x to 4x speed)
            if (hasKnobCaught(knob1, settings.tempo/4.0f, knobStates[MODE_FILTER_STRUM].caught1)) {
                knobStates[MODE_FILTER_STRUM].caught1 = true;
                settings.tempo = fmap(knob1, 0.25f, 4.0f);
                // Current duration will be updated in the timing section
            }
            
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

    // Get timing
    static uint32_t lastTime = System::GetNow();
    uint32_t currentTime = System::GetNow();
    
    // Check if it's time for next note
    if(currentTime - lastTime >= (currentDuration / settings.tempo)) {
        lastTime = currentTime;
        currentNote = (currentNote + 1) % MELODY_LENGTH;
        currentDuration = NOTE_DURATIONS[currentNote];
        
        // Set new frequencies for three-part harmony with slight detuning
        float freq1 = mtof(HARMONY_NOTES[currentNote][0]);
        float freq2 = mtof(HARMONY_NOTES[currentNote][1]) * 1.002f; // +0.2% detuning
        float freq3 = mtof(HARMONY_NOTES[currentNote][2]) * 0.998f; // -0.2% detuning
        
        osc1.SetFreq(freq1);
        osc2.SetFreq(freq2);
        osc3.SetFreq(freq3);
        
        // Trigger first voice immediately
        env1.Trigger();
        
        // Set up delayed triggers for other voices
        voice2Pending = true;
        voice3Pending = true;
        voice2DelayTimer = currentTime;
        voice3DelayTimer = currentTime;
    }

    // Handle delayed voice triggers
    if(voice2Pending && (currentTime - voice2DelayTimer >= STRUM_DELAY_MS)) {
        env2.Trigger();
        voice2Pending = false;
    }
    if(voice3Pending && (currentTime - voice3DelayTimer >= STRUM_DELAY_MS * 2)) {
        env3.Trigger();
        voice3Pending = false;
    }

    for(size_t i = 0; i < size; i++)
    {
        // Process each voice with its own envelope
        float voice1 = osc1.Process() * env1.Process();
        float voice2 = osc2.Process() * env2.Process();
        float voice3 = osc3.Process() * env3.Process();
        
        // Mix voices with slightly reduced amplitude to prevent clipping
        float signal = (voice1 + voice2 + voice3) * 0.2f;
        
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
    
    // Initialize all oscillators
    float sampleRate = hw.AudioSampleRate();
    osc1.Init(sampleRate);
    osc2.Init(sampleRate);
    osc3.Init(sampleRate);
    filter.Init(sampleRate);
    
    // Initialize envelopes
    env1.Init(sampleRate);
    env2.Init(sampleRate);
    env3.Init(sampleRate);
    
    // Set envelope parameters for each voice
    for(AdEnv* env : {&env1, &env2, &env3}) {
        env->SetTime(ADENV_SEG_ATTACK, 0.005);  // Faster attack
        env->SetTime(ADENV_SEG_DECAY, 0.15);    // Shorter decay
        env->SetMin(0.0);
        env->SetMax(0.8);                       // Slightly reduced maximum
        env->SetCurve(0);                       // Linear curve
    }
    
    // Set same initial waveform for all oscillators
    osc1.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
    osc2.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
    osc3.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
    
    // Initialize with first chord
    osc1.SetFreq(mtof(HARMONY_NOTES[0][0]));
    osc2.SetFreq(mtof(HARMONY_NOTES[0][1]));
    osc3.SetFreq(mtof(HARMONY_NOTES[0][2]));

    // Set up filter
    filter.SetRes(0.4f);  // Set a moderate fixed resonance
    filter.SetDrive(0.3f);

    hw.StartAudio(AudioCallback);

    while(1) {}
}
