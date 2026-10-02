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

    if(midi_settle_ > 0.0f) midi_settle_ -= dt;

    // Held at full for as long as the encoder is down, so the red is still there
    // when the finger comes off; the fade below only starts after that.
    if(model_.Panicked() || model_.PanicLatched()) panic_flash_ = 1.0f;
}

void Panel::ProcessMidi()
{
    pod_->midi.Listen();
    while(pod_->midi.HasEvents())
    {
        MidiEvent m = pod_->midi.PopEvent();

        // Still settling: drain the queue but act on none of it.
        if(midi_settle_ > 0.0f)
        {
#if TS_DEBUG
            midi_.discarded++;
#endif
            continue;
        }
        // libDaisy reports the channel 0-based; the Weather Station's own
        // documentation counts from one, and so does everything in config.h.
        const uint8_t ch = static_cast<uint8_t>(m.channel + 1);

#if TS_DEBUG
        midi_.last_type = static_cast<uint8_t>(m.type);
        midi_.last_ch   = ch;
        midi_.last_d0   = m.data[0];
        midi_.last_d1   = m.data[1];
        switch(m.type)
        {
            case NoteOn: midi_.notes_on++; break;
            case NoteOff: midi_.notes_off++; break;
            case ControlChange: midi_.ccs++; break;
            case PitchBend: midi_.bends++; break;
            case SystemRealTime: midi_.clocks++; break;
            default: midi_.other++; break;
        }
#endif

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

void Panel::UpdateLeds(float dt)
{
    // ── LED 1: the sequencer ────────────────────────────────────────────────
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
    // sixteenths at 220 BPM are 68 ms apart.
    if(engine_->TakePluck()) pluck_flash_ = 1.0f;
    pluck_flash_ -= pluck_flash_ * dt * 22.0f;
    if(pluck_flash_ < 0.01f) pluck_flash_ = 0.0f;

    r1 += pluck_flash_ * 0.55f;
    g1 += pluck_flash_ * 0.55f;
    b1 += pluck_flash_ * 0.55f;

    // Something is arriving on the MIDI socket.
    if(link_.Linked()) b1 += 0.10f;

    // ── LED 2: the string ───────────────────────────────────────────────────
    // Colour is the exciter, brightness is the chance step. Both are states you
    // set deliberately and then leave, which is what makes them safe to put in
    // one light — nothing here changes while you are playing unless you change
    // it.
    float r2 = 0.0f, g2 = 0.0f, b2 = 0.0f;

    if(panic_flash_ > 0.0f)
    {
        // Held at full while the encoder is still down — see ProcessControls.
        panic_flash_ -= panic_flash_ * dt * 3.0f;
        if(panic_flash_ < 0.02f) panic_flash_ = 0.0f;
        r2 = panic_flash_;
    }
    else if(model_.PolyFlash() > 0.0f)
    {
        // Poly is one steady white flash, mono is two pulses — the same
        // one-blink / two-blink convention the Simple Touch fork uses, and
        // enough to tell which way you just went.
        const float t = 0.9f - model_.PolyFlash();
        const bool  on = model_.Poly()
                             ? (t < 0.45f)
                             : ((t < 0.12f) || (t > 0.24f && t < 0.36f));
        if(on) r2 = g2 = b2 = 0.55f;
    }
    else if(model_.TempoLayer())
    {
        // White, and its brightness IS the tempo — the only parameter on the
        // box without a pot position to read, so it borrows the light.
        const float t = model_.Norm(Param::Tempo);
        r2 = g2 = b2 = 0.06f + 0.5f * t;
    }
    else
    {
        static constexpr float kChanceLevel[4] = { 0.10f, 0.22f, 0.38f, 0.58f };
        const float v = kChanceLevel[model_.ChanceStep() & 3];
        if(model_.GetExciter() == Exciter::Bow)
        {
            r2 = v;                 // warm amber: bowed
            g2 = v * 0.42f;
        }
        else
        {
            b2 = v;                 // cool blue: plucked
            g2 = v * 0.30f;
        }
    }

    auto clip = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    leds_->Set(0, clip(r1), clip(g1), clip(b1));
    leds_->Set(1, clip(r2), clip(g2), clip(b2));
}

} // namespace tspod
