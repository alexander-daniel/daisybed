#include "daisy_patch_sm.h"
#include "daisysp.h"

using namespace daisy;
using namespace patch_sm;
using namespace daisysp;

static const size_t NUM_VOICES = 8;

// Small Voice abstraction
struct Voice
{
  Oscillator oscillator;
  AdEnv envelope;

  void Init(float sample_rate)
  {
    oscillator.Init(sample_rate);
    oscillator.SetWaveform(oscillator.WAVE_POLYBLEP_SAW);
    oscillator.SetFreq(220);
    oscillator.SetAmp(0.0f); // amplitude will come from envelope
    envelope.Init(sample_rate);
    envelope.SetTime(ADENV_SEG_ATTACK, 0.f);
    envelope.SetTime(ADENV_SEG_DECAY, 0.35f);
    envelope.SetMin(0.0f);
    envelope.SetMax(1.f);
    envelope.SetCurve(0.f); // linear
  }

  // Trigger the envelope and set a new freq
  void Trigger(float freq)
  {
    oscillator.SetFreq(freq);
    envelope.Trigger();
  }

  float Process()
  {
    float env_sig = envelope.Process();
    // Scale oscillator amplitude by envelope.
    oscillator.SetAmp(env_sig * 0.2f);
    return oscillator.Process(); // same levels as your original
  }
};

DaisyPatchSM hw;                      // Hardware layer
static Voice voices[NUM_VOICES];      // Voices
static size_t active_voice_index = 0; // Active voice index
Svf svf;                              // Single filter on the sum of voices
Switch gate;                          // Gate input for triggering voices

// Main audio callback of the program
void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size)
{

  hw.ProcessAllControls();
  gate.Debounce();

  // monitor the gate input and output it to CV_OUT_2
  if (gate.RawState())
  {
    hw.WriteCvOut(CV_OUT_2, 5.f);
  }
  else
  {
    hw.WriteCvOut(CV_OUT_2, 0.f);
  }

  // On a gate rising edge, set the frequency of the active voice
  // and then trigger it. then move to the next voice
  if (gate.RisingEdge())
  {
    float coarse_knob = hw.GetAdcValue(CV_1);
    float coarse = fmap(coarse_knob, 0.f, 96.f);
    float voct_cv = hw.GetAdcValue(CV_5); // TODO: This is not accurate v/oct conversion at all!
    float voct = fmap(voct_cv, 0.f, 60.f);
    float midi_nn = fclamp(coarse + voct, 0.f, 127.f);
    float freq = mtof(midi_nn); // Convert note to freq
    voices[active_voice_index].Trigger(freq);

    // naive Round-robin voice steal
    int next_voice = (active_voice_index + 1) % NUM_VOICES;
    active_voice_index = next_voice;
  }

  float filterCutKnob = hw.GetAdcValue(CV_3);
  float filterCutoff = fmap(filterCutKnob, 0.f, 3000.f);
  float rel_knob = hw.GetAdcValue(CV_4);
  float releaseTime = fmap(rel_knob, 0.01f, 2.f);
  float att_knob = hw.GetAdcValue(CV_2);
  float attackTime = fmap(att_knob, 0.01f, 1.f);

  // Apply envelope times to *all* voices
  for (size_t v = 0; v < NUM_VOICES; v++)
  {
    voices[v].envelope.SetTime(ADENV_SEG_ATTACK, attackTime);
    voices[v].envelope.SetTime(ADENV_SEG_DECAY, releaseTime);
  }

  // Update filter freq
  svf.SetFreq(filterCutoff);

  for (size_t i = 0; i < size; i++)
  {
    float mix = 0.f;
    // Sum all voices
    for (size_t v = 0; v < NUM_VOICES; v++)
    {
      mix += voices[v].Process();
    }

    // Process sum through the single filter
    svf.Process(mix);

    OUT_L[i] = svf.Low();
    OUT_R[i] = svf.Low();
  }
}

int main(void)
{
  hw.Init();

  // Initialize all voices
  for (size_t v = 0; v < NUM_VOICES; v++)
  {
    voices[v].Init(hw.AudioSampleRate());
  }

  svf.Init(hw.AudioSampleRate());
  svf.SetFreq(1000.f);
  svf.SetRes(0.7f);

  gate.Init(hw.B10, hw.AudioCallbackRate());

  // Start audio engine
  hw.StartAudio(AudioCallback);

  while (1)
  {
  }
}
