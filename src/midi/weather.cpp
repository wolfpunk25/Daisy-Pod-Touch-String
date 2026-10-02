#include "weather.h"
#include <math.h>

namespace tspod {

void WeatherLink::Init(Engine* engine, ControlModel* model)
{
    engine_ = engine;
    model_  = model;
}

void WeatherLink::NoteOn(uint8_t channel, uint8_t note, uint8_t velocity)
{
    since_msg_ = 0.0f;

    // Running-status note-on with zero velocity is a note-off. Plenty of senders
    // do this; the Weather Station does not, but a synth that gets it wrong
    // hangs every note it is ever sent, so it is not worth being fussy about.
    if(velocity == 0)
    {
        NoteOff(channel, note);
        return;
    }

    if(channel == kChThunder)
    {
        thunder_ = 1.0f;
        ApplyThunder();
        return;
    }
    if(channel <= 16 && kNoteChannels[channel]) engine_->NoteOn(note);
}

void WeatherLink::NoteOff(uint8_t channel, uint8_t note)
{
    since_msg_ = 0.0f;
    if(channel == kChThunder) return;
    if(channel <= 16 && kNoteChannels[channel]) engine_->NoteOff(note);
}

void WeatherLink::ControlChange(uint8_t channel, uint8_t number, uint8_t value)
{
    since_msg_ = 0.0f;

    // All notes off / all sound off. The Weather Station's panic sends both on
    // every channel, so this arrives five times over — AllNotesOff is idempotent.
    if(number == 120 || number == 123)
    {
        engine_->AllNotesOff();
        return;
    }

    // ── The Weather Station's own controllers ───────────────────────────────
    // These are matched on CONTROLLER NUMBER, not on channel.
    //
    // Getting that wrong was a real bug, caught by a hardware capture rather
    // than by reading the docs: the Weather Station's README describes CC74 as
    // the sun layer and CC91 as the rain layer, so both were originally handled
    // only on channels 2 and 3. Its code sends them to CH_MAIN as well —
    // `for c in (CH_MAIN, CH_SUN): cc(c, 74, bright)` — and its panic sends both
    // to all five channels. So every time the sun came out, the copy on channel 1
    // fell through to upstream's generic map below and landed on CHANCE, which
    // starts throwing wrong notes. Sunshine randomising the melody is not the
    // metaphor anyone wanted.
    //
    // Matching on number is also the more defensible reading: 74 and 91 are the
    // standard brightness and reverb-send controllers, so anything else on the
    // socket means the same thing by them.
    // The eight-note pad's scale button. Raw index, not a normalised value.
    if(number == kScaleSelectCC)
    {
        model_->SetScaleIndex(value);
        return;
    }

    if(number == 74)
    {
        model_->SetMod(Param::Brightness, Bipolar(value, 64) * kSunBrightnessDepth);
        return;
    }
    if(number == 91)
    {
        model_->SetMod(Param::Reverb, Bipolar(value, 40) * kRainReverbDepth);
        return;
    }
    if(number == 1)
    {
        // Wind's gusts. It sends CC1 on the main channel too, at a lower depth,
        // and either should make the string wander — so this is by number as well.
        model_->SetMod(Param::Chance, Bipolar(value, 0) * kWindChanceDepth);
        return;
    }

    // ── Upstream's map, kept for anything else on the socket ────────────────
    // These SET rather than modulate, because a controller sending them means to
    // be in charge. They go through the control model, not straight at the
    // engine, so the panel's own value moves with them and the next turn of the
    // knob carries on from where MIDI left it.
    //
    const float v = Norm(value);
    switch(number)
    {
        case 70: model_->SetNorm(Param::Brightness, v); break;
        // Transposition is off the panel now, so it goes straight at the engine.
        case 71: engine_->SetTranspose(static_cast<int8_t>(v * (kTransMax - kTransMin)) + kTransMin); break;
        case 72: model_->SetNorm(Param::Timbre, v); break;
        case 73: model_->SetNorm(Param::Density, v); break;
        // Upstream spends 74 and 75 on its two randomisation knobs. 74 is
        // handled above as brightness, which is what the rest of the world means
        // by it, so only 75 reaches chance here.
        case 75: model_->SetNorm(Param::Chance, v); break;
        case 76: model_->SetNorm(Param::Damping, v); break;
        case 77: model_->SetNorm(Param::Reverb, v); break;
        case 78: engine_->SetShift(v); break;
        case 79: model_->SetNorm(Param::Drive, v); break;
        default: break;
    }
}

void WeatherLink::PitchBend(uint8_t channel, int16_t value)
{
    since_msg_ = 0.0f;
    // Gusts arrive on 1, 2 and 4. Thunder and rain do not bend.
    if(channel != kChMain && channel != kChSun && channel != kChWind) return;
    engine_->SetBend(static_cast<float>(value) / 8192.0f * kBendSemitones);
}

void WeatherLink::Clock()
{
    since_msg_ = 0.0f;
    engine_->MidiClockPulse();
}

void WeatherLink::Tick(float dt)
{
    if(since_msg_ < kLinkTimeoutSec) since_msg_ += dt;

    if(thunder_ > 0.0f)
    {
        // Exponential fall to a floor, then hard off, so the modulation actually
        // reaches zero and stops re-pushing two parameters forever.
        thunder_ -= thunder_ * dt / (kThunderDecaySec * 0.35f);
        if(thunder_ < 0.004f) thunder_ = 0.0f;
        ApplyThunder();
    }
}

// Thunder hits the string harder and chokes it at the same time. Both fall back
// together, so what you hear is a strike and a swallow rather than a level jump.
void WeatherLink::ApplyThunder()
{
    model_->SetMod(Param::Drive, thunder_ * kThunderDriveKick);
    model_->SetMod(Param::Damping, thunder_ * 0.25f);
}

} // namespace tspod
