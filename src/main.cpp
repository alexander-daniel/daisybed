#include "daisy_patch_sm.h"
#include "daisysp.h"

using namespace daisy;
using namespace patch_sm;
using namespace daisysp;

DaisyPatchSM hw; // Hardware class for the Daisy Patch Submodule
Oscillator osc;  // Basic oscillator
AdEnv envelope;  // Simple Attack/Decay envelope
Svf svf;         // State Variable Filter
Switch button;   // Switch for triggering the envelope
GateIn gate;     // Gate input

void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size)
{
  hw.ProcessAllControls();
  button.Debounce();

  float env_sig, osc_sig, out_sig;

  float coarse_knob = hw.GetAdcValue(CV_1);
  float voct_cv = hw.GetAdcValue(CV_5);
  float filter_cut_knob = hw.GetAdcValue(CV_3);
  float att_knob = hw.GetAdcValue(CV_2);
  float rel_knob = hw.GetAdcValue(CV_4);

  float coarse = fmap(coarse_knob, -20.f, 96.f);
  float voct = fmap(voct_cv, 0.f, 60.f);
  float midi_nn = fclamp(coarse + voct, 0.f, 127.f);
  float freq = mtof(midi_nn);
  float filter_cutoff_freq = fmap(filter_cut_knob, 0.f, 10000.f);
  float attack_time = fmap(att_knob, 0.01f, 3.f);
  float release_time = fmap(rel_knob, 0.01f, 3.f);

  envelope.SetTime(ADENV_SEG_ATTACK, attack_time);
  envelope.SetTime(ADENV_SEG_DECAY, release_time);
  svf.SetFreq(filter_cutoff_freq);
  osc.SetFreq(freq);

  if (button.Pressed())
  {
    envelope.Trigger();
  }

  if (gate.State())
  {
    envelope.Trigger();
  }

  for (size_t i = 0; i < size; i++)
  {
    env_sig = envelope.Process();
    osc.SetAmp(env_sig * 0.2f); // for headphones , it's a hot signal
    osc_sig = osc.Process();
    osc_sig *= 0.08f; // for headphones
    osc_sig *= 0.1f;  // for headphones again the signal is SO HOT from submodule (for eurorack which is good!)

    svf.Process(osc_sig);
    OUT_L[i] = svf.Low();
    OUT_R[i] = svf.Low();
    hw.WriteCvOut(CV_OUT_2, 5.f * env_sig);
  }
}

int main(void)
{
  hw.Init();

  envelope.Init(hw.AudioSampleRate());
  osc.Init(hw.AudioSampleRate());
  button.Init(hw.B7);
  gate.Init(hw.B10);

  osc.SetWaveform(osc.WAVE_POLYBLEP_SQUARE);
  osc.SetFreq(220);
  osc.SetAmp(0.25);

  svf.Init(hw.AudioSampleRate());
  svf.SetFreq(1000.f);
  svf.SetRes(0.7f);

  // Set envelope parameters
  envelope.SetTime(ADENV_SEG_ATTACK, 0.01);
  envelope.SetTime(ADENV_SEG_DECAY, 1.35);
  envelope.SetMin(0.0f);
  envelope.SetMax(1.f);
  envelope.SetCurve(0.f); // linear

  /** Start the Audio engine, and call the "AudioCallback" function to fill new data */
  hw.StartAudio(AudioCallback);

  while (1)
  {
  }
}
