#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "DattorroPlate.h"

using namespace daisy;
using namespace patch_sm;
using namespace daisysp;

// ----------------------------------------------------------------------------
// Cinematic shimmer reverb for Daisy Patch SM.
//
// A Dattorro-style plate reverb (our own MIT implementation, see
// DattorroPlate.h) whose tail is pitch-shifted upward and fed back into its
// input, producing a rising angelic / cinematic halo.
//
// Knob layout (Option C):
//   CV_1 / K1 -> Size + Tone    (bigger space => longer decay + darker tail)
//   CV_2 / K2 -> Shimmer amount  (how much pitched tail is fed back)
//   CV_3 / K3 -> Shimmer interval, stepped: +7 | +12 | +12&+19 | +24
//   CV_4 / K4 -> Dry / Wet mix (equal-power)
// ----------------------------------------------------------------------------

DaisyPatchSM hw;

// The plate (~155 KB) and both pitch-shifters (~128 KB each) all fit in SRAM.
static daisybed::DattorroPlate reverb;
static PitchShifter            shifter_a;
static PitchShifter            shifter_b;

static DcBlock shimmer_dc;

static float prev_wet_l = 0.f;
static float prev_wet_r = 0.f;

static float sm_decay  = 0.7f;
static float sm_bright = 0.6f;
static float sm_amount = 0.f;
static float sm_mix    = 0.4f;

static const float kHalfPi = 1.5707963f;

static void SetInterval(float knob,
                        float &semi_a,
                        float &gain_a,
                        float &semi_b,
                        float &gain_b)
{
  if (knob < 0.25f) // Fifth up: harmonic, pad-like
  {
    semi_a = 7.f;
    gain_a = 1.f;
    semi_b = 7.f;
    gain_b = 0.f;
  }
  else if (knob < 0.5f) // Octave up: classic shimmer
  {
    semi_a = 12.f;
    gain_a = 1.f;
    semi_b = 12.f;
    gain_b = 0.f;
  }
  else if (knob < 0.75f) // Octave + fifth: lush cinematic stack
  {
    semi_a = 12.f;
    gain_a = 0.8f;
    semi_b = 19.f;
    gain_b = 0.8f;
  }
  else // Two octaves up: glassy, airy sparkle
  {
    semi_a = 24.f;
    gain_a = 1.f;
    semi_b = 24.f;
    gain_b = 0.f;
  }
}

static void AudioCallback(AudioHandle::InputBuffer in,
                          AudioHandle::OutputBuffer out,
                          size_t size)
{
  hw.ProcessAllControls();

  float k_size  = hw.GetAdcValue(CV_1);
  float k_amt   = hw.GetAdcValue(CV_2);
  float k_intvl = hw.GetAdcValue(CV_3);
  float k_mix   = hw.GetAdcValue(CV_4);

  // K1: bigger => longer tail (more decay) and darker (less brightness).
  float target_decay  = fmap(k_size, 0.4f, 0.9f);
  float target_bright = fmap(1.f - k_size, 0.2f, 0.95f);
  // K2: shimmer feedback amount.
  float target_amt = fmap(k_amt, 0.f, 0.85f);

  fonepole(sm_decay, target_decay, 0.2f);
  fonepole(sm_bright, target_bright, 0.2f);
  fonepole(sm_amount, target_amt, 0.2f);
  fonepole(sm_mix, k_mix, 0.2f);

  reverb.SetDecay(sm_decay);
  reverb.SetBrightness(sm_bright);

  // K3: shimmer interval (stepped).
  float semi_a, gain_a, semi_b, gain_b;
  SetInterval(k_intvl, semi_a, gain_a, semi_b, gain_b);
  shifter_a.SetTransposition(semi_a);
  shifter_b.SetTransposition(semi_b);

  // K4: equal-power dry/wet gains.
  float dry_gain = cosf(sm_mix * kHalfPi);
  float wet_gain = sinf(sm_mix * kHalfPi);

  for (size_t i = 0; i < size; i++)
  {
    float dry_l = IN_L[i];
    float dry_r = IN_R[i];

    // Pitch-shift the previous (mono) tail upward for the shimmer feedback.
    float tail = 0.5f * (prev_wet_l + prev_wet_r);
    float sa   = shifter_a.Process(tail);
    float sb   = shifter_b.Process(tail);
    float shimmer = sm_amount * (sa * gain_a + sb * gain_b);
    // Soft-limit + DC-block so the feedback loop blooms instead of blowing up.
    shimmer = shimmer_dc.Process(tanhf(shimmer));

    float wet_l, wet_r;
    reverb.Process(dry_l + shimmer, dry_r + shimmer, wet_l, wet_r);

    prev_wet_l = wet_l;
    prev_wet_r = wet_r;

    OUT_L[i] = dry_l * dry_gain + wet_l * wet_gain;
    OUT_R[i] = dry_r * dry_gain + wet_r * wet_gain;
  }
}

int main(void)
{
  hw.Init();

  float sample_rate = hw.AudioSampleRate();

  reverb.Init(sample_rate);

  shifter_a.Init(sample_rate);
  shifter_b.Init(sample_rate);
  // A touch of internal modulation keeps the shimmer voices from sounding static.
  shifter_a.SetFun(0.1f);
  shifter_b.SetFun(0.1f);

  shimmer_dc.Init(sample_rate);

  hw.StartAudio(AudioCallback);

  while (1)
  {
  }
}
