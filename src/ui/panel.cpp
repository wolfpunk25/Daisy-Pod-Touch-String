#include "panel.h"
#include <math.h>

using namespace daisy;

namespace tspod {

void Panel::Init(DaisyPod* pod, Engine* engine, PodLeds* leds)
{
    pod_    = pod;
    engine_ = engine;
    leds_   = leds;

    // Spin the controls before anchoring the relative knobs.
    //
    // The Pod's pots come through a smoothed AnalogControl whose filter STARTS AT
    // ZERO and needs a few hundred passes to reach where the pot actually is.
    // Anchor after a single ProcessAllControls and the filter's own ramp up to
    // the real position reads as a deliberate turn, and lands in whatever
    // parameter the boot page is showing. The Audrey port booted with its reverb
    // at 96% because of this; every port in this family carries the same fix.
    const uint32_t t0 = System::GetNow();
    while(System::GetNow() - t0 < 200) pod_->ProcessAllControls();

    model_.Init(engine_, pod_->knob1.Process(), pod_->knob2.Process());
    link_.Init(engine_, &model_);

    pod_->midi.StartReceive();
}

void Panel::ProcessControls(float dt)
{
    pod_->ProcessAllControls();

    model_.Read(pod_->button1.Pressed(),
                pod_->button2.Pressed(),
                pod_->knob1.Process(),
                pod_->knob2.Process(),
                pod_->encoder.Increment(),
                pod_->encoder.Pressed(),
                dt);

    link_.Tick(dt);

    if(model_.Panicked()) panic_flash_ = 1.0f;
}

void Panel::ProcessMidi()
{
    pod_->midi.Listen();
    while(pod_->midi.HasEvents())
    {
        MidiEvent m = pod_->midi.PopEvent();
        // libDaisy reports the channel 0-based; the Weather Station's own
        // documentation counts from one, and so does everything in config.h.
        const uint8_t ch = static_cast<uint8_t>(m.channel + 1);

        switch(m.type)
        {
            case NoteOn:
            {
                const auto n = m.AsNoteOn();
                link_.NoteOn(ch, n.note, n.velocity);
                break;
            }
            case NoteOff:
            {
                const auto n = m.AsNoteOff();
                link_.NoteOff(ch, n.note);
                break;
            }
            case ControlChange:
            {
                const auto c = m.AsControlChange();
                link_.ControlChange(ch, c.control_number, c.value);
                break;
            }
            case PitchBend:
            {
                const auto p = m.AsPitchBend();
                link_.PitchBend(ch, p.value);
                break;
            }
            case SystemRealTime:
                if(m.srt_type == TimingClock) link_.Clock();
                else if(m.srt_type == Stop) engine_->AllNotesOff();
                break;
            default: break;
        }
    }
}

// Hue in turns, 0..1. Cheap and good enough for eight distinguishable steps.
void Panel::Hsv(float h, float s, float v, float& r, float& g, float& b)
{
    h = h - floorf(h);
    const float i = floorf(h * 6.0f);
    const float f = h * 6.0f - i;
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));
    switch(static_cast<int>(i) % 6)
    {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

void Panel::UpdateLeds(float dt)
{
    // ── LED 1: what the arp is doing, and every pluck ───────────────────────
    // Upstream lights the Seed's onboard LED when the latch is on and shows
    // nothing else. There is more to say here and two RGB LEDs to say it with.
    float r1 = 0.0f, g1 = 0.0f, b1 = 0.0f;
    switch(model_.Mode())
    {
        case ArpMode::Off:
            r1 = g1 = b1 = 0.05f;   // alive, but not sequencing
            break;
        case ArpMode::On:
            g1 = 0.45f;
            break;
        case ArpMode::Latched:
            r1 = 0.55f;
            g1 = 0.22f;   // amber
            break;
    }

    // A pluck is the only thing that happens entirely in the audio callback, so
    // the flash is the one window the panel has onto the pattern. Short, because
    // sixteenths at 220 BPM are 68 ms apart and a slower decay would smear into
    // a steady glow.
    if(engine_->TakePluck()) pluck_flash_ = 1.0f;
    pluck_flash_ -= pluck_flash_ * dt * 22.0f;
    if(pluck_flash_ < 0.01f) pluck_flash_ = 0.0f;

    r1 += pluck_flash_ * 0.55f;
    g1 += pluck_flash_ * 0.55f;
    b1 += pluck_flash_ * 0.55f;

    // Something on the MIDI socket is playing: a blue floor, so it is obvious
    // whether the Weather Station is actually reaching the box.
    if(link_.Linked()) b1 += 0.10f;

    // ── LED 2: where the panel is ───────────────────────────────────────────
    float r2 = 0.0f, g2 = 0.0f, b2 = 0.0f;
    if(panic_flash_ > 0.0f)
    {
        panic_flash_ -= panic_flash_ * dt * 6.0f;
        if(panic_flash_ < 0.02f) panic_flash_ = 0.0f;
        r2 = panic_flash_;
    }
    else if(model_.SetupLayer())
    {
        // White, with the scale as a brightness step so the encoder has an
        // answer while you are turning it.
        const float v = 0.25f + 0.35f * static_cast<float>(engine_->ScaleIndex());
        r2 = g2 = b2 = v;
    }
    else if(model_.TouchLeft() > 0.0f && model_.Degree() != 0xff)
    {
        // Recently touched: show which of the eight degrees the encoder is
        // sitting on, as a hue around the wheel. Full brightness if that note is
        // in the held set, dim if it is only selected.
        //
        // Whether eight hues actually read apart in the hand is unproven — see
        // docs/PORTING.md. It is one line to change to a brightness ramp.
        const float h = static_cast<float>(model_.Degree()) / 8.0f;
        Hsv(h, 1.0f, model_.DegreeHeld() ? 0.60f : 0.12f, r2, g2, b2);
    }
    else
    {
        static constexpr float kPageColour[kNumPages][3] = {
            { 0.05f, 0.10f, 0.45f },   // String  — blue
            { 0.40f, 0.30f, 0.00f },   // Body    — yellow
            { 0.40f, 0.00f, 0.35f },   // Pattern — magenta
            { 0.00f, 0.35f, 0.35f },   // Space   — cyan
        };
        const int p = static_cast<int>(model_.CurrentPage());
        r2          = kPageColour[p][0];
        g2          = kPageColour[p][1];
        b2          = kPageColour[p][2];
    }

    auto clip = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    leds_->Set(0, clip(r1), clip(g1), clip(b1));
    leds_->Set(1, clip(r2), clip(g2), clip(b2));
}

} // namespace tspod
