#pragma once
// The Pod BSP drives its two RGB LEDs through daisy::RgbLed, whose software PWM
// is fixed at a 1 kHz update rate — about eight usable brightness steps. That is
// not enough here: the LEDs are the only display this instrument has, and the
// left one spends its whole life somewhere on a green-to-orange ramp with the
// output level on top of it. So we drive the same six pins directly at the
// sample rate, which gives roughly 400 steps per PWM cycle.
//
// Pin assignments are the Pod's, taken from libDaisy's daisy_pod.cpp. Carried
// over from the Terrarium, Audrey II and Wrangler Pod ports, which all needed
// the same thing for the same reason.
#include <cmath>
#include "daisy_seed.h"

namespace tspod {

class PodLeds
{
  public:
    // Update() advances a 120 Hz PWM sawtooth by 120/rate per call, so the
    // brightness resolution is rate/120 steps. We call it once per audio sample
    // from the audio callback, which needs no extra timer and gives 400 steps.
    void Init(float rate)
    {
        using namespace daisy::seed;
        led_[0][0].Init(D20, true, rate);  // LED 1 red
        led_[0][1].Init(D19, true, rate);  // LED 1 green
        led_[0][2].Init(D18, true, rate);  // LED 1 blue
        led_[1][0].Init(D17, true, rate);  // LED 2 red
        led_[1][1].Init(D24, true, rate);  // LED 2 green
        led_[1][2].Init(D23, true, rate);  // LED 2 blue
        Set(0, 0.0f, 0.0f, 0.0f);
        Set(1, 0.0f, 0.0f, 0.0f);
    }

    // Values are DUTY CYCLE — 0.2 means lit a fifth of the time.
    // daisy::Led::Set() cubes what it is given for gamma correction, so the cube
    // root undoes that and leaves the caller saying what it means. Without this
    // every dim state lands under 1% duty and reads as simply off.
    void Set(uint8_t idx, float r, float g, float b)
    {
        led_[idx][0].Set(Gamma(r));
        led_[idx][1].Set(Gamma(g));
        led_[idx][2].Set(Gamma(b));
    }

    // Boot self-test: red, green, blue, white on both LEDs. Confirms the pins
    // and the PWM in one glance.
    //
    // It spins Update() rather than sleeping through the step. daisy::Led::Set()
    // only stores a PWM threshold — Update() is what touches the pin — so a
    // self-test built on System::Delay() sets four colours and shows none of
    // them. This runs before StartAudio(), which is where Update() gets called
    // from afterwards, so nothing else is going to drive it.
    void SelfTest()
    {
        static constexpr float kSteps[4][3]
            = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 1, 1 } };
        for(auto& s : kSteps)
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

    // Call at the rate passed to Init().
    void Update()
    {
        for(auto& l : led_)
            for(auto& c : l) c.Update();
    }

  private:
    static float Gamma(float duty) { return duty <= 0.0f ? 0.0f : std::cbrt(duty); }

    daisy::Led led_[2][3];
};

} // namespace tspod
