#pragma once
// ONE string, plucked or bowed. Several of these make the voice pool — see
// voices.h.
//
// PLUCK is daisysp::StringVoice, untouched: an excitation burst into a tuned
// delay with a damping filter in the loop. It is what the instrument has always
// sounded like.
//
// BOW is its own exciter — white noise through a low pass into a bare
// daisysp::String, the same Karplus-Strong resonator StringVoice uses inside, so
// both modes are the same string. It is NOT StringVoice's sustain flag, which
// drives the string from Dust at a density derived from brightness to the fourth
// power: measured at 914 impulses a second where the knob could reach, which is
// gravel rather than friction. See docs/PORTING.md.
//
// Only the resonator for the CURRENT exciter is processed. Running both costs
// exactly twice as much — measured — and nothing is gained, since the exciter is
// a global mode and the other resonator is silent. A voice also reports when it
// has fallen quiet, so the pool can skip it entirely.
#include "daisysp.h"
#include "../common/config.h"

namespace tspod {

enum class Exciter : uint8_t
{
    Pluck = 0,
    Bow,
    kCount
};

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
        // headroom and makes the overdrive flutter. A pluck never accumulates
        // enough to matter; a bow does.
        dc_.Init(sample_rate);

        SetBrightness(0.5f);
    }

    // Upstream halves this for the pluck: "With high brightness and pitch the
    // osc crashes. Limiting value to 0.5 until further investigation." Kept,
    // because it is load-bearing on hardware. The bow does not go through
    // StringVoice, so it spends the knob on a filter instead of a density.
    void SetBrightness(float v)
    {
        bright_ = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        osc_.SetBrightness(bright_ * 0.5f);
        // Stops short of the knee where the loop turns metallic — see config.h.
        bow_string_.SetBrightness(
            kBowResBrightLow + (kBowResBrightHigh - kBowResBrightLow) * bright_);
        const float cut
            = kBowCutoffLow * powf(kBowCutoffHigh / kBowCutoffLow, bright_);
        bow_filter_.SetFreq(cut);
        // Opening the loop raises the level as well as the tone; take some back
        // so the knob is not also a volume control.
        bow_gain_ = kBowGain * (1.0f - kBowGainTilt * bright_);
    }

    void SetStructure(float v)
    {
        osc_.SetStructure(daisysp::fmap(v, 0.0f, 0.8f));
        bow_string_.SetNonLinearity(daisysp::fmap(v, 0.0f, 0.35f));
    }

    void SetDamping(float v)
    {
        osc_.SetDamping(v);
        // A bow needs the loop to hold on, or noise never builds into a note.
        // Upstream's range is tuned for a pluck and far too lossy here.
        bow_string_.SetDamping(kBowDampingLow
                               + (kBowDampingHigh - kBowDampingLow) * v);
    }

    void SetFreq(float freq)
    {
        osc_.SetFreq(freq);
        bow_string_.SetFreq(freq);
    }

    void SetExciter(Exciter e)
    {
        if(e == ex_) return;
        ex_ = e;
        Kill();   // the other resonator holds unrelated state
    }

    // Strike it. Pluck mode only.
    void Strike()
    {
        if(ex_ != Exciter::Pluck) return;
        osc_.Trig();
        // Primed high so the idle test below has to watch it decay rather than
        // seeing a level of zero on the very first sample and freeing the voice.
        level_  = 1.0f;
        active_ = true;
    }

    // Draw the bow. Bow mode only.
    void BowOn()
    {
        if(ex_ != Exciter::Bow) return;
        if(!active_)
        {
            bow_string_.Reset();   // start from silence, not an old tail
            bow_env_ = 0.0f;
        }
        bowing_ = true;
        level_  = 1.0f;
        active_ = true;
    }

    // Lift it. The string keeps ringing down — see Process.
    void BowOff() { bowing_ = false; }

    void Kill()
    {
        bowing_ = false;
        active_ = false;
        bow_env_ = 0.0f;
        level_   = 0.0f;
        bow_string_.Reset();
    }

    bool Active() const { return active_; }
    bool Bowing() const { return bowing_; }

    float Process()
    {
        if(!active_) return 0.0f;

        float out;
        if(ex_ == Exciter::Bow)
        {
            const float target = bowing_ ? 1.0f : 0.0f;
            bow_env_ += (bowing_ ? atk_coef_ : rel_coef_) * (target - bow_env_);
            // An exponential never actually arrives. Snap the last 60 dB of the
            // release to zero, or the voice stays nominally alive for five
            // seconds after it stopped being audible and cannot be re-used.
            if(!bowing_ && bow_env_ < 1e-3f) bow_env_ = 0.0f;

            float ex = 0.0f;
            if(bow_env_ > 0.0f)
            {
                bow_filter_.Process(noise_.Process());
                ex = bow_filter_.Low() * bow_gain_ * bow_env_;
            }
            // The envelope scales the OUTPUT as well as the excitation, so the
            // release has a shape of its own. The loop barely loses energy at
            // the top of the damping range — a measured ring-down was still only
            // 4 dB down after a whole second — so its natural decay is not a
            // release.
            out = bow_string_.Process(ex) * bow_env_;
        }
        else
        {
            out = osc_.Process();
        }

        out = dc_.Process(out);

        // Free the voice once it has actually fallen quiet, rather than after a
        // fixed time: a long damping setting rings for far longer than a short
        // one.
        level_ += kVoiceIdleTrack * (fabsf(out) - level_);
        if(!bowing_ && bow_env_ <= 0.0f && level_ < kVoiceIdleFloor) Kill();

        return out;
    }

  private:
    float sr_       = 48000.0f;
    float bright_   = 0.5f;
    float bow_gain_ = kBowGain;
    float bow_env_  = 0.0f;
    float level_    = 0.0f;
    float atk_coef_ = 0.01f;
    float rel_coef_ = 0.01f;

    bool    bowing_ = false;
    bool    active_ = false;
    Exciter ex_     = Exciter::Pluck;

    daisysp::StringVoice osc_;          // pluck
    daisysp::String      bow_string_;   // bow: the bare resonator
    daisysp::WhiteNoise  noise_;
    daisysp::Svf         bow_filter_;
    daisysp::DcBlock     dc_;
};

} // namespace tspod
