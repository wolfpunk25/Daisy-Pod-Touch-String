#pragma once
// The reverb.
//
// Upstream uses daisysp::ReverbSc with fixed feedback and a fixed 10 kHz
// low pass, and spends only the mix on a control. ReverbSc went with DaisySP's
// "Remove LGPL Modules" commit, so it is not available to build against any
// more. The Audrey II Pod port solved that by pinning DaisySP to a pre-purge
// commit and shipping LGPL code in the binary; this one keeps the repo MIT all
// the way down instead, which is worth about a hundred lines.
//
// So: a four-line feedback delay network mixed through a Householder matrix,
// with six allpasses of input diffusion in front and one-pole damping inside each
// line. Carried over from the Wrangler Pod port, where the shape of it was
// settled by measurement — a two-line cross-coupled tank is not a reverb, it is a
// pair of slap echoes with a ring on top.
//
// The knob is not only a mix. It stretches the decay as well, so turning it up
// gives a bigger room rather than more of the same small one — which is what a
// single reverb knob is expected to do, and upstream's fixed 0.8 feedback cannot.
#include <stddef.h>
#include <stdint.h>

namespace tspod {

class Space
{
  public:
    void Init(float sample_rate);
    void Reset();

    // 0..1, straight off the Reverb control.
    void SetKnob(float k);

    // Once per audio block. The two line modulators run at 0.09 and 0.13 Hz,
    // and evaluating a sub-1 Hz sine 48000 times a second is pure waste — the
    // Terrarium Pod port halved its CPU by moving exactly this kind of thing to
    // block rate. A block is 83 us; nothing at 0.1 Hz notices.
    void Tick(size_t block_size);

    // Wet only. The dry/wet balance is the caller's, through XFade.
    void Process(float in, float& out_l, float& out_r);

  private:
    static constexpr int kLines  = 4;
    // Mutually prime, 37 to 67 ms at 48 kHz. Nothing is a multiple of anything
    // else, which is what stops the tail settling on a pitch.
    static constexpr int kLen0   = 1789;
    static constexpr int kLen1   = 2311;
    static constexpr int kLen2   = 2797;
    static constexpr int kLen3   = 3203;
    static constexpr int kModMax = 64;

    static constexpr int kAp0 = 149, kAp1 = 271, kAp2 = 419;
    static constexpr int kAp3 = 653, kAp4 = 911, kAp5 = 1123;

    // Past this the tank stops sounding like it is decaying at all.
    static constexpr float kMaxFb = 0.965f;
    // At the bottom of the knob there is still a room, there is just none of it
    // in the output. A plucked string into a dead tank sounds broken.
    static constexpr float kMinFb = 0.62f;

    struct Allpass
    {
        float* buf;
        int    len;
        int    idx;
        float  Process(float in, float g)
        {
            const float d = buf[idx];
            const float y = d - g * in;
            buf[idx]      = in + g * y;
            if(++idx >= len) idx = 0;
            return y;
        }
    };

    static float ReadDelay(const float* buf, int size, int wpos, float delay);

    float ap_buf0_[kAp0] = { 0 };
    float ap_buf1_[kAp1] = { 0 };
    float ap_buf2_[kAp2] = { 0 };
    float ap_buf3_[kAp3] = { 0 };
    float ap_buf4_[kAp4] = { 0 };
    float ap_buf5_[kAp5] = { 0 };

    float line0_[kLen0 + kModMax] = { 0 };
    float line1_[kLen1 + kModMax] = { 0 };
    float line2_[kLen2 + kModMax] = { 0 };
    float line3_[kLen3 + kModMax] = { 0 };

    Allpass ap_[6];
    float*  line_[kLines] = { nullptr, nullptr, nullptr, nullptr };
    int     size_[kLines] = { 0, 0, 0, 0 };
    int     base_[kLines] = { 0, 0, 0, 0 };
    int     w_[kLines]    = { 0, 0, 0, 0 };
    float   lp_[kLines]   = { 0.0f, 0.0f, 0.0f, 0.0f };

    float sr_     = 48000.0f;
    float g_ap_   = 0.62f;
    float fb_     = 0.75f;
    float damp_a_ = 0.5f;

    float mod_phase_[2] = { 0.0f, 0.37f };
    float mod_inc_[2]   = { 0.0f, 0.0f };
    float mod_[2]       = { 30.0f, 30.0f };   // held across the block
};

} // namespace tspod
