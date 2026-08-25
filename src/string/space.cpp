#include "space.h"
#include "../common/config.h"
#include <math.h>

namespace tspod {

void Space::Init(float sample_rate)
{
    sr_    = sample_rate;
    ap_[0] = { ap_buf0_, kAp0, 0 };
    ap_[1] = { ap_buf1_, kAp1, 0 };
    ap_[2] = { ap_buf2_, kAp2, 0 };
    ap_[3] = { ap_buf3_, kAp3, 0 };
    ap_[4] = { ap_buf4_, kAp4, 0 };
    ap_[5] = { ap_buf5_, kAp5, 0 };

    line_[0] = line0_; base_[0] = kLen0; size_[0] = kLen0 + kModMax;
    line_[1] = line1_; base_[1] = kLen1; size_[1] = kLen1 + kModMax;
    line_[2] = line2_; base_[2] = kLen2; size_[2] = kLen2 + kModMax;
    line_[3] = line3_; base_[3] = kLen3; size_[3] = kLen3 + kModMax;

    // Two slow, unrelated rates: fast enough that a held tail never freezes,
    // slow enough that nothing reads as vibrato.
    mod_inc_[0] = 0.09f / sr_;
    mod_inc_[1] = 0.13f / sr_;

    // Upstream's fixed 10 kHz low pass in the loop, as a one-pole coefficient.
    const float cutoff = 700.0f + 11000.0f * (1.0f - kReverbDamping) * (1.0f - kReverbDamping);
    damp_a_            = 1.0f - expf(-6.2831853f * cutoff / sr_);

    Reset();
    SetKnob(0.0f);
}

void Space::Reset()
{
    for(auto& v : ap_buf0_) v = 0.0f;
    for(auto& v : ap_buf1_) v = 0.0f;
    for(auto& v : ap_buf2_) v = 0.0f;
    for(auto& v : ap_buf3_) v = 0.0f;
    for(auto& v : ap_buf4_) v = 0.0f;
    for(auto& v : ap_buf5_) v = 0.0f;
    for(auto& v : line0_) v = 0.0f;
    for(auto& v : line1_) v = 0.0f;
    for(auto& v : line2_) v = 0.0f;
    for(auto& v : line3_) v = 0.0f;
    for(auto& a : ap_) a.idx = 0;
    for(int i = 0; i < kLines; ++i)
    {
        w_[i]  = 0;
        lp_[i] = 0.0f;
    }
}

void Space::SetKnob(float k)
{
    k = k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k);
    // Squared, because the top of the feedback range is where all the audible
    // change in decay time lives — the difference between 0.90 and 0.96 is
    // several seconds, the difference between 0.62 and 0.68 is barely there.
    fb_ = kMinFb + k * k * (kMaxFb - kMinFb);
}

float Space::ReadDelay(const float* buf, int size, int wpos, float delay)
{
    float r = static_cast<float>(wpos) - delay;
    while(r < 0.0f) r += static_cast<float>(size);
    const int   i = static_cast<int>(r);
    const float f = r - static_cast<float>(i);
    const int   j = (i + 1 >= size) ? 0 : i + 1;
    return buf[i] + (buf[j] - buf[i]) * f;
}

void Space::Process(float in, float& out_l, float& out_r)
{
    float x = in;
    for(auto& a : ap_) x = a.Process(x, g_ap_);

    for(int m = 0; m < 2; ++m)
    {
        mod_phase_[m] += mod_inc_[m];
        if(mod_phase_[m] >= 1.0f) mod_phase_[m] -= 1.0f;
    }
    // Only two of the four move. Modulating all of them is a chorus.
    const float mod0 = 30.0f + 24.0f * sinf(6.2831853f * mod_phase_[0]);
    const float mod3 = 30.0f + 24.0f * sinf(6.2831853f * mod_phase_[1]);

    float d[kLines];
    d[0] = ReadDelay(line_[0], size_[0], w_[0], static_cast<float>(base_[0]) + mod0);
    d[1] = ReadDelay(line_[1], size_[1], w_[1], static_cast<float>(base_[1]) + 30.0f);
    d[2] = ReadDelay(line_[2], size_[2], w_[2], static_cast<float>(base_[2]) + 30.0f);
    d[3] = ReadDelay(line_[3], size_[3], w_[3], static_cast<float>(base_[3]) + mod3);

    // Damping inside the loop, not on the output: a room loses a little more of
    // its top end on every bounce, which is the difference between a room and
    // an EQ.
    float v[kLines];
    for(int i = 0; i < kLines; ++i)
    {
        lp_[i] += damp_a_ * (d[i] - lp_[i]);
        v[i] = lp_[i];
    }

    // Householder: y_i = (2/N)·Σv − v_i. Orthogonal, so the feedback gain alone
    // sets the decay and the energy spreads across all four lines instead of
    // pooling in whichever one is loudest.
    const float sum = 0.5f * (v[0] + v[1] + v[2] + v[3]);
    for(int i = 0; i < kLines; ++i)
    {
        line_[i][w_[i]] = x + fb_ * (sum - v[i]);
        if(++w_[i] >= size_[i]) w_[i] = 0;
    }

    // Two lines to each side, one of each pair inverted, so the outputs are
    // decorrelated without either being an inverted copy of the other.
    out_l = 0.5f * (d[0] + d[2]);
    out_r = 0.5f * (d[1] - d[3]);
}

} // namespace tspod
