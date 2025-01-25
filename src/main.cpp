#include "daisy_patch_sm.h"
#include "daisysp.h"

using namespace daisy;
using namespace patch_sm;
using namespace daisysp;

DaisyPatchSM hw;
Oscillator osc;
AdEnv env;
Switch button;
GateIn gate;
AnalogControl cvIn;
Svf svf;

float freq;
float filterCutoff;
float attackTime;
float releaseTime;

void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size)
{
  float env_sig, osc_sig, out_sig;
  osc.SetFreq(freq);

  for (size_t i = 0; i < size; i++)
  {
    env_sig = env.Process();
    osc.SetAmp(env_sig * 0.2f);
    osc_sig = osc.Process();
    osc_sig *= 0.08f;
    osc_sig *= 0.1f;

    svf.Process(osc_sig);

    OUT_L[i] = svf.Low();
    OUT_R[i] = svf.Low();

    hw.WriteCvOut(CV_OUT_2, 5.f * env_sig);
  }
}

int main(void)
{
  hw.Init();
  button.Init(hw.B7);

  env.Init(hw.AudioSampleRate());
  gate.Init(hw.B10);
  osc.Init(hw.AudioSampleRate());

  // Set parameters for oscillator
  osc.SetWaveform(osc.WAVE_POLYBLEP_SQUARE);
  osc.SetFreq(220);
  osc.SetAmp(0.25);

  svf.Init(hw.AudioSampleRate());
  svf.SetFreq(1000.f);
  svf.SetRes(0.7f);

  // Set envelope parameters
  env.SetTime(ADENV_SEG_ATTACK, 0.01);
  env.SetTime(ADENV_SEG_DECAY, 1.35);
  env.SetMin(0.0f);
  env.SetMax(1.f);
  env.SetCurve(0.f); // linear

  /** Start the Audio engine, and call the "AudioCallback" function to fill new data */
  hw.StartAudio(AudioCallback);

  /**
   * Forever, read all controls and process, AudioCallback
   * will pick up the changes as it goes.
   */
  while (1)
  {
    hw.ProcessAllControls();
    button.Debounce();

    float coarse_knob = hw.GetAdcValue(CV_1);
    float coarse = fmap(coarse_knob, -20.f, 96.f);

    float voct_cv = hw.GetAdcValue(CV_5);
    float voct = fmap(voct_cv, 0.f, 60.f);
    float midi_nn = fclamp(coarse + voct, 0.f, 127.f);

    float filterCutKnob = hw.GetAdcValue(CV_3);

    float rel_knob = hw.GetAdcValue(CV_4);
    releaseTime = fmap(rel_knob, 0.01f, 3.f);

    float att_knob = hw.GetAdcValue(CV_2);
    attackTime = fmap(att_knob, 0.01f, 3.f);

    env.SetTime(ADENV_SEG_ATTACK, attackTime);
    env.SetTime(ADENV_SEG_DECAY, releaseTime);

    freq = mtof(midi_nn);
    filterCutoff = fmap(filterCutKnob, 0.f, 10000.f);
    svf.SetFreq(filterCutoff);

    if (button.Pressed())
    {
      env.Trigger();
    }

    if (gate.State())
    {
      env.Trigger();
    }
  }
}
