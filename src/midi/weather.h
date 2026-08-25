#pragma once
// ============================================================================
// MIDI in — and in particular the Wolfpunk Weather Station on the TRS socket.
//
// Upstream forwards no note messages at all. There is a TODO in its MIDI handler
// saying why: the engine's NoteOn takes a scale degree 0..7, not a note number,
// so there is nothing sensible to hand it. This port changed the engine's note
// identity to MIDI note numbers precisely so that gap could close, and closing it
// is what lets the Weather Station play this instrument.
//
// The Weather Station is not a keyboard. It is three weather modifiers held in
// the left hand and five keys in the right, and the modifiers stack into eight
// combinations spread across five MIDI channels. So the mapping is semantic —
// what each layer MEANS — rather than a table of controller numbers. Same
// approach the Terrarium Pod port took with the same box.
//
//   ch 1  Main      the five keys. Plucks the string.
//   ch 2  Sun       shimmering chord an octave up. Plucks, and CC74 opens the
//                   brightness — sun opening the filter is the Weather Station's
//                   own metaphor and it lands exactly on this instrument's
//                   brightest control.
//   ch 3  Rain      droplets down the scale. Plucks — short plucks are precisely
//                   what a Karplus string is for — and CC91 wets the reverb.
//   ch 4  Wind      the drifting companion voice. Plucks; CC1 drives the chance
//                   control, so a gust makes the string wander; pitch bend bends.
//   ch 5  Thunder   a sub-bass boom. Does NOT pluck: a plucked string has nothing
//                   to say about a boom. It shoves drive and damping instead and
//                   lets them fall back — the string is hit harder and chokes.
//
// Controllers ADD to where the knobs are sitting rather than replacing them (see
// ControlModel::SetMod), so nothing on the panel goes dead when the cable is in.
// Each one has its own neutral point, because the Weather Station's panic resets
// them to different values — CC74 to 64, CC91 to 40, CC1 to 0 — and neutral has
// to mean "no modulation" or the sound jumps every time it resets.
//
// Set DIN_CHANNEL = None on the Weather Station. Its default folds every layer
// onto channel 1, which would make the sun chords and rain droplets
// indistinguishable from the keys and the modifiers appear dead.
//
// Anything that is not the Weather Station still works: upstream's CC70-79 map is
// kept, on any channel, and MIDI Timing Clock drives the arp when the tempo
// control is at the bottom.
// ============================================================================
#include <stdint.h>
#include "../string/engine.h"
#include "../ui/controlmodel.h"

namespace tspod {

class WeatherLink
{
  public:
    void Init(Engine* engine, ControlModel* model);

    // One parsed MIDI message. `channel` is 1-based.
    void NoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void NoteOff(uint8_t channel, uint8_t note);
    void ControlChange(uint8_t channel, uint8_t number, uint8_t value);
    void PitchBend(uint8_t channel, int16_t value);   // -8192..8191
    void Clock();

    // Main loop. Runs the thunder decay and the "have we heard anything lately"
    // timer the LEDs use.
    void Tick(float dt);

    bool  Linked() const { return since_msg_ < kLinkTimeoutSec; }
    float Thunder() const { return thunder_; }

  private:
    static constexpr float kLinkTimeoutSec = 2.0f;

    static float Norm(uint8_t v) { return static_cast<float>(v) / 127.0f; }
    // Bipolar around a controller's own resting value.
    static float Bipolar(uint8_t v, uint8_t neutral)
    {
        const float d = static_cast<float>(v) - static_cast<float>(neutral);
        return d / (d >= 0.0f ? static_cast<float>(127 - neutral)
                              : static_cast<float>(neutral));
    }

    void ApplyThunder();

    Engine*       engine_ = nullptr;
    ControlModel* model_  = nullptr;

    float thunder_   = 0.0f;
    float since_msg_ = 1e9f;
};

} // namespace tspod
