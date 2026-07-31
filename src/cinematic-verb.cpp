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
// Control layout (Option C). Each knob is summed with its CV jack, so any
// parameter can be automated from the modular:
//   K1 + CV_5 -> Size + Tone     (bigger space => longer decay + darker tail)
//   K2 + CV_6 -> Shimmer amount  (how much pitched tail is fed back)
//   K3 + CV_7 -> Shimmer interval, stepped: +7 | +12 | +12&+19 | +24
//   K4 + CV_8 -> Dry / Wet mix (equal-power)
//
// The user LED brightness follows the wet/dry mix.
// ----------------------------------------------------------------------------

DaisyPatchSM hardware;

// The plate (~155 KB) and both pitch-shifters (~128 KB each) all fit in SRAM.
static daisybed::DattorroPlate reverb;
static PitchShifter            shifter_first;
static PitchShifter            shifter_second;

static DcBlock shimmer_dc_blocker;

static float previous_wet_left  = 0.f;
static float previous_wet_right = 0.f;

static float smoothed_decay          = 0.7f;
static float smoothed_brightness     = 0.6f;
static float smoothed_shimmer_amount = 0.f;
static float smoothed_mix            = 0.4f;

// Written at block rate by the audio callback, read by the LED PWM in main().
static volatile float led_duty_cycle = 0.f;

static const float kHalfPi = 1.5707963f;

// Panel knob plus its bipolar CV jack. Unpatched jacks read ~0, so the knob
// alone still spans the full range.
static inline float KnobPlusControlVoltage(int knob_index, int jack_index)
{
  return fclamp(hardware.GetAdcValue(knob_index)
                    + hardware.GetAdcValue(jack_index),
                0.f,
                1.f);
}

static int interval_step = 1; // Default: octave up.

// A little hysteresis around each edge keeps CV noise from flipping the
// interval mid-tail.
static int QuantizeInterval(float control)
{
  static const float kStepEdges[3] = {0.25f, 0.5f, 0.75f};
  const float        kHysteresis   = 0.02f;
  for (int edge = 0; edge < 3; edge++)
  {
    if (interval_step <= edge && control > kStepEdges[edge] + kHysteresis)
      interval_step = edge + 1;
    else if (interval_step > edge && control < kStepEdges[edge] - kHysteresis)
      interval_step = edge;
  }
  return interval_step;
}

static void SetInterval(int    step_index,
                        float &semitones_first,
                        float &gain_first,
                        float &semitones_second,
                        float &gain_second)
{
  switch (step_index)
  {
    case 0: // Fifth up: harmonic, pad-like
      semitones_first  = 7.f;
      gain_first       = 1.f;
      semitones_second = 7.f;
      gain_second      = 0.f;
      break;
    case 2: // Octave + fifth: lush cinematic stack
      semitones_first  = 12.f;
      gain_first       = 0.8f;
      semitones_second = 19.f;
      gain_second      = 0.8f;
      break;
    case 3: // Two octaves up: glassy, airy sparkle
      semitones_first  = 24.f;
      gain_first       = 1.f;
      semitones_second = 24.f;
      gain_second      = 0.f;
      break;
    case 1: // Octave up: classic shimmer
    default:
      semitones_first  = 12.f;
      gain_first       = 1.f;
      semitones_second = 12.f;
      gain_second      = 0.f;
      break;
  }
}

static void AudioCallback(AudioHandle::InputBuffer in,
                          AudioHandle::OutputBuffer out,
                          size_t size)
{
  hardware.ProcessAllControls();

  float size_control           = KnobPlusControlVoltage(CV_1, CV_5);
  float shimmer_amount_control = KnobPlusControlVoltage(CV_2, CV_6);
  float interval_control       = KnobPlusControlVoltage(CV_3, CV_7);
  float mix_control            = KnobPlusControlVoltage(CV_4, CV_8);

  // K1: bigger => longer tail (more decay) and darker (less brightness).
  float target_decay      = fmap(size_control, 0.4f, 0.9f);
  float target_brightness = fmap(1.f - size_control, 0.2f, 0.95f);
  // K2: shimmer feedback amount.
  float target_shimmer_amount = fmap(shimmer_amount_control, 0.f, 0.85f);

  fonepole(smoothed_decay, target_decay, 0.2f);
  fonepole(smoothed_brightness, target_brightness, 0.2f);
  fonepole(smoothed_shimmer_amount, target_shimmer_amount, 0.2f);
  fonepole(smoothed_mix, mix_control, 0.2f);

  // Squared so the LED fade reads as linear to the eye.
  led_duty_cycle = smoothed_mix * smoothed_mix;

  reverb.SetDecay(smoothed_decay);
  reverb.SetBrightness(smoothed_brightness);

  // K3: shimmer interval (stepped).
  float semitones_first, gain_first, semitones_second, gain_second;
  SetInterval(QuantizeInterval(interval_control),
              semitones_first,
              gain_first,
              semitones_second,
              gain_second);
  shifter_first.SetTransposition(semitones_first);
  shifter_second.SetTransposition(semitones_second);

  // K4: equal-power dry/wet gains.
  float dry_gain = cosf(smoothed_mix * kHalfPi);
  float wet_gain = sinf(smoothed_mix * kHalfPi);

  for (size_t sample = 0; sample < size; sample++)
  {
    float dry_left  = IN_L[sample];
    float dry_right = IN_R[sample];

    // Pitch-shift the previous (mono) tail upward for the shimmer feedback.
    float tail = 0.5f * (previous_wet_left + previous_wet_right);
    float shifted_first  = shifter_first.Process(tail);
    float shifted_second = shifter_second.Process(tail);
    float mixed_shift = shifted_first * gain_first
                        + shifted_second * gain_second;
    float shimmer     = smoothed_shimmer_amount * mixed_shift;
    // Soft-limit + DC-block so the feedback loop blooms instead of blowing up.
    shimmer = shimmer_dc_blocker.Process(tanhf(shimmer));

    float wet_left, wet_right;
    reverb.Process(dry_left + shimmer,
                   dry_right + shimmer,
                   wet_left,
                   wet_right);

    previous_wet_left  = wet_left;
    previous_wet_right = wet_right;

    OUT_L[sample] = dry_left * dry_gain + wet_left * wet_gain;
    OUT_R[sample] = dry_right * dry_gain + wet_right * wet_gain;
  }
}

int main(void)
{
  hardware.Init();

  float sample_rate = hardware.AudioSampleRate();

  reverb.Init(sample_rate);

  shifter_first.Init(sample_rate);
  shifter_second.Init(sample_rate);
  // A touch of internal modulation keeps the shimmer voices from sounding static.
  shifter_first.SetFun(0.1f);
  shifter_second.SetFun(0.1f);

  shimmer_dc_blocker.Init(sample_rate);

  hardware.StartAudio(AudioCallback);

  // Software PWM for the user LED, which is a plain on/off GPIO. The duty
  // cycle is sampled once per period so the pulse width never changes
  // mid-period, and unsigned subtraction stays correct across timer wrap.
  const uint32_t kLedPeriodMicroseconds = 1000;
  uint32_t       period_start           = System::GetUs();
  uint32_t       on_time                = 0;

  while (1)
  {
    uint32_t now = System::GetUs();
    if (now - period_start >= kLedPeriodMicroseconds)
    {
      period_start = now;
      on_time      = (uint32_t)(led_duty_cycle * kLedPeriodMicroseconds);
    }
    hardware.SetLed((now - period_start) < on_time);
  }
}
