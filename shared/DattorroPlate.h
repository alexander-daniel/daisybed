#pragma once
#ifndef DAISYBED_DATTORRO_PLATE_H
#define DAISYBED_DATTORRO_PLATE_H

#include <math.h>
#include "daisysp.h"

namespace daisybed
{
// Dattorro-style stereo plate reverb.
//
// Original tap lengths are specified at 29761 Hz; the delay-line buffer sizes
// below are the scaled 48 kHz lengths plus headroom for interpolation and
// allpass modulation. Running well above 48 kHz would clamp the longest taps.
class DattorroPlate
{
  public:
    void Init(float sample_rate)
    {
        scale_ = sample_rate / kRefSr;

        in_ap1_.Init();
        in_ap2_.Init();
        in_ap3_.Init();
        in_ap4_.Init();
        ap_l1_.Init();
        ap_r1_.Init();
        del_l1_.Init();
        del_r1_.Init();
        ap_l2_.Init();
        ap_r2_.Init();
        del_l2_.Init();
        del_r2_.Init();

        lp_l_ = lp_r_ = 0.f;
        fb_   = 0.f;

        // Slow, mutually-detuned tank modulation to avoid metallic ringing.
        lfo_phase_l_ = 0.f;
        lfo_phase_r_ = 1.5f;
        lfo_inc_l_   = kTwoPi * 0.70f / sample_rate;
        lfo_inc_r_   = kTwoPi * 1.10f / sample_rate;
        excursion_   = 16.f * scale_;

        decay_  = 0.7f;
        bright_ = 0.6f;
    }

    // decay: tank feedback / tail length (0..~0.92).
    inline void SetDecay(float d) { decay_ = d; }
    // bright: damping filter brightness, 0 (dark) .. 1 (bright).
    inline void SetBrightness(float b) { bright_ = b; }

    void Process(float in_l, float in_r, float &out_l, float &out_r)
    {
        // Plate is mono-in; sum the stereo input.
        float x = 0.5f * (in_l + in_r);

        // Input diffusion (four fixed allpasses).
        x = in_ap1_.Allpass(x, (size_t)(142.f * scale_), 0.75f);
        x = in_ap2_.Allpass(x, (size_t)(107.f * scale_), 0.75f);
        x = in_ap3_.Allpass(x, (size_t)(379.f * scale_), 0.625f);
        x = in_ap4_.Allpass(x, (size_t)(277.f * scale_), 0.625f);

        // Advance tank modulation LFOs.
        lfo_phase_l_ += lfo_inc_l_;
        if(lfo_phase_l_ > kTwoPi)
            lfo_phase_l_ -= kTwoPi;
        lfo_phase_r_ += lfo_inc_r_;
        if(lfo_phase_r_ > kTwoPi)
            lfo_phase_r_ -= kTwoPi;
        float mod_l = 672.f * scale_ + excursion_ * sinf(lfo_phase_l_);
        float mod_r = 908.f * scale_ + excursion_ * sinf(lfo_phase_r_);

        // Left half of the figure-8 tank.
        float split_l = x + fb_; // fb_ = decay * right-branch output (prev sample)
        float n_l     = ModAllpass(ap_l1_, mod_l, 0.7f, split_l);
        del_l1_.Write(n_l);
        float a_l = del_l1_.Read(4453.f * scale_);
        lp_l_ += bright_ * (a_l - lp_l_);
        float u_l = ap_l2_.Allpass(lp_l_, (size_t)(1800.f * scale_), 0.5f);
        del_l2_.Write(u_l);
        float z_l = del_l2_.Read(3720.f * scale_);

        // Right half of the tank.
        float split_r = x + decay_ * z_l;
        float n_r     = ModAllpass(ap_r1_, mod_r, 0.7f, split_r);
        del_r1_.Write(n_r);
        float a_r = del_r1_.Read(4217.f * scale_);
        lp_r_ += bright_ * (a_r - lp_r_);
        float u_r = ap_r2_.Allpass(lp_r_, (size_t)(2656.f * scale_), 0.5f);
        del_r2_.Write(u_r);
        float z_r = del_r2_.Read(3163.f * scale_);

        fb_ = decay_ * z_r;

        // Stereo output taps (Dattorro's decorrelated node accumulation).
        float yl = del_r1_.Read(266.f * scale_) + del_r1_.Read(2974.f * scale_)
                   - ap_r2_.Read(1913.f * scale_) + del_r2_.Read(1996.f * scale_)
                   - del_l1_.Read(1990.f * scale_) - ap_l2_.Read(187.f * scale_)
                   - del_l2_.Read(1066.f * scale_);
        float yr = del_l1_.Read(353.f * scale_) + del_l1_.Read(3627.f * scale_)
                   - ap_l2_.Read(1228.f * scale_) + del_l2_.Read(2673.f * scale_)
                   - del_r1_.Read(2111.f * scale_) - ap_r2_.Read(335.f * scale_)
                   - del_r2_.Read(121.f * scale_);

        out_l = yl * 0.6f;
        out_r = yr * 0.6f;
    }

  private:
    static constexpr float  kRefSr  = 29761.0f;
    static constexpr float  kTwoPi  = 6.2831853f;
    static constexpr size_t kInAp1  = 384;
    static constexpr size_t kInAp2  = 256;
    static constexpr size_t kInAp3  = 768;
    static constexpr size_t kInAp4  = 640;
    static constexpr size_t kApL1   = 1536;
    static constexpr size_t kApR1   = 1920;
    static constexpr size_t kDelL1  = 7680;
    static constexpr size_t kDelR1  = 7168;
    static constexpr size_t kApL2   = 3072;
    static constexpr size_t kApR2   = 4608;
    static constexpr size_t kDelL2  = 6272;
    static constexpr size_t kDelR2  = 5376;

    // Modulated (fractional-delay) allpass, matching DelayLine::Allpass sign.
    static inline float
    ModAllpass(daisysp::DelayLine<float, kApL1> &buf, float delay, float g, float x)
    {
        float r = buf.Read(delay);
        float w = x + g * r;
        buf.Write(w);
        return -w * g + r;
    }
    static inline float
    ModAllpass(daisysp::DelayLine<float, kApR1> &buf, float delay, float g, float x)
    {
        float r = buf.Read(delay);
        float w = x + g * r;
        buf.Write(w);
        return -w * g + r;
    }

    float scale_;
    float decay_, bright_;
    float lp_l_, lp_r_, fb_;
    float lfo_phase_l_, lfo_phase_r_, lfo_inc_l_, lfo_inc_r_, excursion_;

    daisysp::DelayLine<float, kInAp1> in_ap1_;
    daisysp::DelayLine<float, kInAp2> in_ap2_;
    daisysp::DelayLine<float, kInAp3> in_ap3_;
    daisysp::DelayLine<float, kInAp4> in_ap4_;
    daisysp::DelayLine<float, kApL1>  ap_l1_;
    daisysp::DelayLine<float, kApR1>  ap_r1_;
    daisysp::DelayLine<float, kDelL1> del_l1_;
    daisysp::DelayLine<float, kDelR1> del_r1_;
    daisysp::DelayLine<float, kApL2>  ap_l2_;
    daisysp::DelayLine<float, kApR2>  ap_r2_;
    daisysp::DelayLine<float, kDelL2> del_l2_;
    daisysp::DelayLine<float, kDelR2> del_r2_;
};

} // namespace daisybed

#endif // DAISYBED_DATTORRO_PLATE_H
