#pragma once
// The Pod's two RGB LEDs, driven directly.
//
// Not daisy::RgbLed, and — since 2026-09 — not daisy::Led either. Both are
// software PWM, and daisy::Led's carrier is hardcoded:
//
//     pwm_ += 120.f / samplerate_;
//     hw_pin_.Write(bright_ > pwm_ ? on_ : off_);
//
// **120 Hz, whatever rate you call it at.** That is inside the band the eye sees
// as flicker, especially in peripheral vision and across a saccade, and it was
// reported from the board as "the LEDs are flickering constantly, it hurts my
// eyes". The rate argument does not raise the carrier — it only keeps the
// carrier AT 120 Hz as the call rate changes.
//
// It was also worse here than the arithmetic suggests. Update() was being called
// once per audio SAMPLE, four times in a burst inside each block, so the pin
// physically changed only at the block rate and the three intermediate writes
// were invisible. Since the phase advanced by exactly four every block, the
// comparison always landed on the same residues — throwing away most of the
// resolution the fast calls were supposed to buy, and leaving the flicker.
//
// So the PWM is local now: one comparison per audio block, which is the fastest
// the pins can actually change, and a counter sized to put the carrier well
// clear of anything visible.
//
//     12 kHz block rate / kSteps 24 = 500 Hz carrier, 24 brightness levels
//
// 24 levels is more than enough — every value the panel uses lands on a distinct
// step — and 500 Hz is comfortably above the flicker fusion threshold even for
// eye movement.
#include "daisy_seed.h"

namespace tspod {

class PodLeds
{
  public:
    // Called once per audio block. At the Pod's 4-sample block that is 12 kHz.
    static constexpr uint8_t kSteps = 24;

    void Init()
    {
        using namespace daisy::seed;
        // Pin assignments are the Pod's, from libDaisy's daisy_pod.cpp.
        const daisy::Pin pins[2][3] = {
            { D20, D19, D18 },   // LED 1  r, g, b
            { D17, D24, D23 },   // LED 2  r, g, b
        };
        for(int l = 0; l < 2; ++l)
            for(int c = 0; c < 3; ++c)
            {
                gpio_[l][c].Init(pins[l][c], daisy::GPIO::Mode::OUTPUT);
                thresh_[l][c] = 0;
            }
        phase_ = 0;
        WritePins();
    }

    // Values are DUTY CYCLE — 0.2 means lit a fifth of the time. No gamma
    // curve is applied: daisy::Led cubed its argument and the old wrapper took a
    // cube root to cancel it, so the panel's numbers have always been literal
    // duty cycles and they stay that way.
    void Set(uint8_t idx, float r, float g, float b)
    {
        thresh_[idx][0] = Duty(r);
        thresh_[idx][1] = Duty(g);
        thresh_[idx][2] = Duty(b);
    }

    // Boot self-test: red, green, blue, white on both LEDs. Confirms the pins
    // and the PWM in one glance.
    //
    // It spins Update() rather than sleeping through the step, because nothing
    // else drives the PWM until the audio callback starts. A self-test built on
    // System::Delay() sets four colours and shows none of them — a bug inherited
    // from three sibling Pod ports, whose boot self-tests all display nothing.
    void SelfTest()
    {
        static constexpr float kStepsRGB[4][3]
            = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 1, 1 } };
        for(auto& s : kStepsRGB)
        {
            Set(0, s[0], s[1], s[2]);
            Set(1, s[0], s[1], s[2]);
            const uint32_t t0 = daisy::System::GetNow();
            while(daisy::System::GetNow() - t0 < 160) Update();
        }
        Set(0, 0, 0, 0);
        Set(1, 0, 0, 0);
        Update();
    }

    // ONE call per audio block. Calling it per sample buys nothing — see above.
    void Update()
    {
        if(++phase_ >= kSteps) phase_ = 0;
        WritePins();
    }

  private:
    // The Pod's LED pins are active low.
    static constexpr bool kOn  = false;
    static constexpr bool kOff = true;

    static uint8_t Duty(float d)
    {
        if(d <= 0.0f) return 0;
        if(d >= 1.0f) return kSteps;
        return static_cast<uint8_t>(d * static_cast<float>(kSteps) + 0.5f);
    }

    void WritePins()
    {
        for(int l = 0; l < 2; ++l)
            for(int c = 0; c < 3; ++c)
                gpio_[l][c].Write(phase_ < thresh_[l][c] ? kOn : kOff);
    }

    daisy::GPIO gpio_[2][3];
    uint8_t     thresh_[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };
    uint8_t     phase_        = 0;
};

} // namespace tspod
