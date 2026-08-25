#pragma once
// One plucked string. Monophonic, exactly as upstream: every note retriggers the
// same voice, and with the arp running that is the whole character of the
// instrument — a single string being hit over and over, not a chord ringing.
//
// daisysp::StringVoice is an extended Karplus-Strong of the Rings lineage: an
// excitation burst into a tuned delay with a damping filter in the loop.
#include "daisysp.h"

namespace tspod {

class Vox
{
  public:
    void Init(float sample_rate) { osc_.Init(sample_rate); }

    // Upstream halves this, with the comment "With high brightness and pitch the
    // osc crashes. Limiting value to 0.5 until further investigation." The limit
    // is kept because it is load-bearing on hardware; what it is working around
    // has not been established here either. tests/ has a sweep that hunts for
    // the non-finite output, and it does not reproduce on the host — see
    // docs/PORTING.md.
    void SetBrightness(float v) { osc_.SetBrightness(v * 0.5f); }

    void SetStructure(float v) { osc_.SetStructure(daisysp::fmap(v, 0.0f, 0.8f)); }

    void SetDamping(float v) { osc_.SetDamping(v); }

    void NoteOn(float freq)
    {
        osc_.SetFreq(freq);
        osc_.Trig();
    }

    float Process() { return osc_.Process(); }

  private:
    daisysp::StringVoice osc_;
};

} // namespace tspod
