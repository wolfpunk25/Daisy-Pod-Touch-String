#pragma once
// One string. Plucked, or bowed.
//
// daisysp::StringVoice is an extended Karplus-Strong of the Rings lineage: an
// excitation burst into a tuned delay with a damping filter in the loop. Its
// `SetSustain(true)` swaps that burst for continuous dust — which is a bow, and
// is why bowing cost almost nothing to add. The dust density tracks brightness
// squared inside DaisySP, so the Brightness control reaches the bow as well,
// which is the behaviour you would have chosen anyway.
//
// Monophonic for now. Polyphony is the next piece of work, and "Pluck + Bow"
// (a bowed chord underneath a plucked arpeggio) needs it — one string cannot
// both sustain and be struck.
#include "daisysp.h"

namespace tspod {

class Vox
{
  public:
    void Init(float sample_rate)
    {
        osc_.Init(sample_rate);
        // Continuous excitation can walk a DC offset into the loop, which eats
        // headroom and makes the overdrive flutter. A pluck decays and never
        // accumulates enough to matter; a bow does.
        dc_.Init(sample_rate);
    }

    // Upstream halves this, with the comment "With high brightness and pitch the
    // osc crashes. Limiting value to 0.5 until further investigation." The limit
    // is kept because it is load-bearing on hardware; a 143-point sweep with it
    // off reproduces nothing on the host, which does not clear the board.
    void SetBrightness(float v) { osc_.SetBrightness(v * 0.5f); }

    void SetStructure(float v) { osc_.SetStructure(daisysp::fmap(v, 0.0f, 0.8f)); }
    void SetDamping(float v) { osc_.SetDamping(v); }

    void SetFreq(float freq) { osc_.SetFreq(freq); }

    // Strike it. Does nothing audible while bowing — the bow is already sounding
    // and a burst on top of it is a scratch, not an attack.
    void Trig() { osc_.Trig(); }

    // The bow: on while something is held, off when nothing is, or it drones for
    // ever.
    void SetSustain(bool on) { osc_.SetSustain(on); }

    float Process() { return dc_.Process(osc_.Process()); }

  private:
    daisysp::StringVoice osc_;
    daisysp::DcBlock     dc_;
};

} // namespace tspod
