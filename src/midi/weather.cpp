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
    if(channel == kChSun && number == 74)
    {
        model_->SetMod(Param::Brightness, Bipolar(value, 64) * kSunBrightnessDepth);
        return;
    }
    if(channel == kChRain && number == 91)
    {
        model_->SetMod(Param::Reverb, Bipolar(value, 40) * kRainReverbDepth);
        return;
    }
    if(channel == kChWind && number == 1)
    {
        model_->SetMod(Param::Chance, Bipolar(value, 0) * kWindChanceDepth);
        return;
    }

    // ── Upstream's map, kept for anything else on the socket ────────────────
    // These SET rather than modulate, because a controller sending them means to
    // be in charge. They go through the control model, not straight at the
    // engine, so the panel's own value moves with them and the next turn of the
    // knob carries on from where MIDI left it.
    //
    // CC74 is the one collision: it is "note randomisation" in upstream's map and
    // "brightness" in the Weather Station's. The channel settles it — the sun
    // layer is handled above and returns before reaching here.
    const float v = Norm(value);
    switch(number)
    {
        case 70: model_->SetNorm(Param::Brightness, v); break;
        case 71: model_->SetNorm(Param::Transpose, v); break;
        case 72: model_->SetNorm(Param::Timbre, v); break;
        case 73: model_->SetNorm(Param::Density, v); break;
        // Upstream spends 74 and 75 on its two randomisation knobs; there is one
        // control here, so either reaches it.
        case 74:
        case 75: model_->SetNorm(Param::Chance, v); break;
        case 76: model_->SetNorm(Param::Damping, v); break;
        case 77: model_->SetNorm(Param::Reverb, v); break;
        case 78: model_->SetNorm(Param::Shift, v); break;
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
