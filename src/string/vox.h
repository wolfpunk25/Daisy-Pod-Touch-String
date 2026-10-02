#pragma once
// One string, plucked or bowed.
//
// PLUCK is daisysp::StringVoice, untouched: an excitation burst into a tuned
// delay with a damping filter in the loop. It is what the instrument has always
// sounded like and there is no reason to touch it.
//
// BOW is NOT StringVoice's sustain flag, which was the first attempt and sounded
// gravelly. That flag drives the string from daisysp::Dust, whose density comes
// from brightness to the FOURTH power — `density_ = brightness^2` inside
// StringVoice, squared again inside the dust formula — and `Dust::SetDensity`
// quietly multiplies by another 0.3. Measured:
//
//     osc brightness   impulses/sec   crest
//          0.50              914       12.8    gravel
//          0.75            4,545        5.6    gravel
//          1.00           14,327        3.2    noise-like
//
// Smooth only arrives above about 0.85 — and Vox halves the knob for upstream's
// crash workaround, so a knob at full reaches only 0.50. The bow was structurally
// stuck in the gravel zone, and worse at the bottom of the knob than the top.
//
// The deeper problem is that it ties bow density to BRIGHTNESS. Brightness should
// shape the tone, not decide whether the excitation is noise or a stream of
// clicks. So the bow here is its own exciter — white noise through a low pass,
// into a bare daisysp::String (the same Karplus-Strong resonator StringVoice
// uses internally, so both modes are the same string). Brightness opens the
// filter, which is a timbre control again, and the noise is dense by
// construction.
#include "daisysp.h"
#include "../common/config.h"

namespace tspod {

class Vox
{
  public:
    void Init(float sample_rate)
    {
        sr_       = sample_rate;
        atk_coef_ = 1.0f - expf(-1.0f / (kBowAttackSec * sample_rate));
        rel_coef_ = 1.0f - expf(-1.0f / (kBowReleaseSec * sample_rate));
        osc_.Init(sample_rate);

        bow_string_.Init(sample_rate);
        bow_string_.SetNonLinearity(0.05f);
        noise_.Init();
        bow_filter_.Init(sample_rate);
        bow_filter_.SetRes(0.15f);
        bow_filter_.SetDrive(0.0f);

        // Continuous excitation walks a DC offset into the loop, which eats
        // headroom and makes the overdrive flutter. A pluck decays and never
        // accumulates enough to matter; a bow does.
        dc_.Init(sample_rate);

        SetBrightness(0.5f);
    }

    // Upstream halves this for the pluck, with the comment "With high brightness
    // and pitch the osc crashes. Limiting value to 0.5 until further
    // investigation." The limit is kept there because it is load-bearing on
    // hardware. The bow does not go through StringVoice at all, so it is free to
    // use the whole range — and it uses it on the filter, not on a density.
    void SetBrightness(float v)
    {
        bright_ = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        osc_.SetBrightness(bright_ * 0.5f);
        bow_string_.SetBrightness(bright_);
        // Exponential, because the bottom of a cutoff sweep is where the ear
        // hears the most change.
        const float cut = kBowCutoffLow
                          * powf(kBowCutoffHigh / kBowCutoffLow, bright_);
        bow_filter_.SetFreq(cut);
    }

    void SetStructure(float v)
    {
        osc_.SetStructure(daisysp::fmap(v, 0.0f, 0.8f));
        // The same parameter reaches the bare resonator as its non-linearity,
        // which is what StringVoice derives from structure internally.
        bow_string_.SetNonLinearity(daisysp::fmap(v, 0.0f, 0.35f));
    }

    void SetDamping(float v)
    {
        osc_.SetDamping(v);
        // A bow needs the loop to hold on, or the noise never builds into a
        // note. Upstream's damping range is fine for a pluck and far too lossy
        // here, so the bow gets the top of it.
        bow_string_.SetDamping(kBowDampingLow
                               + (kBowDampingHigh - kBowDampingLow) * v);
    }

    void SetFreq(float freq)
    {
        osc_.SetFreq(freq);
        bow_string_.SetFreq(freq);
    }

    // Strike it. Pluck only — a burst on top of a sounding bow is a scratch.
    void Trig() { osc_.Trig(); }

    // The bow runs while something is held and stops when nothing is.
    void SetSustain(bool on)
    {
        if(on == bowing_) return;
        bowing_ = on;
        if(on && !bow_active_)
        {
            bow_string_.Reset();   // start from silence, not an old tail
            bow_env_ = 0.0f;
        }
        if(on) bow_active_ = true;
    }

    float Process()
    {
        // The pluck voice always runs: StringVoice holds its own state and this
        // is where a plucked note decays.
        float out = osc_.Process();

        // The bow resonator keeps running after the bow lifts, fed zero, so the
        // string RINGS DOWN instead of being cut off. Hard-switching away from
        // it was the bug: the tail was still there, just disconnected.
        if(bow_active_)
        {
            const float target = bowing_ ? 1.0f : 0.0f;
            bow_env_ += (bowing_ ? atk_coef_ : rel_coef_) * (target - bow_env_);

            float ex = 0.0f;
            if(bow_env_ > 1e-5f)
            {
                bow_filter_.Process(noise_.Process());
                ex = bow_filter_.Low() * kBowGain * bow_env_;
            }

            // The envelope scales the OUTPUT as well as the excitation, so the
            // release has a shape of its own. Relying on the string's natural
            // decay is not enough: at the top of the damping range the loop
            // barely loses energy, and a measured ring-down was still only 4 dB
            // down after a whole second.
            const float bowed = bow_string_.Process(ex) * bow_env_;
            out += bowed;

            // Stop running it once the excitation has gone AND the string has
            // actually fallen quiet, rather than after a fixed time — a long
            // damping setting rings for much longer than a short one.
            tail_ += 0.0004f * (fabsf(bowed) - tail_);
            if(!bowing_ && bow_env_ < 1e-5f && tail_ < 2e-5f)
            {
                bow_active_ = false;
                tail_       = 0.0f;
            }
        }

        return dc_.Process(out);
    }

  private:
    float sr_        = 48000.0f;
    float bright_    = 0.5f;
    bool  bowing_    = false;
    bool  bow_active_ = false;
    float bow_env_   = 0.0f;
    float tail_      = 0.0f;
    float atk_coef_  = 0.01f;
    float rel_coef_  = 0.01f;

    daisysp::StringVoice osc_;          // pluck
    daisysp::String      bow_string_;   // bow: the bare resonator
    daisysp::WhiteNoise  noise_;
    daisysp::Svf         bow_filter_;
    daisysp::DcBlock     dc_;
};

} // namespace tspod
