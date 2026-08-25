#pragma once
// ============================================================================
// Everything worth changing without reading the rest of the port.
// ============================================================================
#include <stdint.h>
#include <array>

namespace tspod {

// ── Scales ──────────────────────────────────────────────────────────────────
// Upstream stores these as three tables of eight FREQUENCIES and then halves
// every lookup (`FreqAt` returns `_scale[idx] * 0.5f`). Every one of those
// frequencies is an exact 12-TET pitch, and upstream's 25-entry transposition
// table is exactly 2^(n/12) for n = -12..+12 — so the whole thing is semitones
// wearing a costume.
//
// Here they are MIDI note numbers, already carrying upstream's octave-down, and
// transposition is an integer semitone offset. Identical pitches, and it means a
// note from the panel and a note from the MIDI socket are the same kind of
// thing — which is what lets the Weather Station play this at all. See
// docs/PORTING.md.
static constexpr uint8_t kScaleSize   = 8;
static constexpr uint8_t kScalesCount = 3;

static constexpr std::array<std::array<uint8_t, kScaleSize>, kScalesCount> kScales = {{
    { 36, 43, 46, 48, 50, 51, 53, 55 },  // Amara   C2 G2 A#2 C3 D3 D#3 F3 G3
    { 36, 40, 41, 43, 45, 48, 52, 53 },  // Oxalis  C2 E2 F2  G2 A2 C3  E3 F3
    { 36, 38, 39, 43, 46, 48, 50, 51 }   // Pigmy   C2 D2 D#2 G2 A#2 C3 D3 D#3
}};

static constexpr const char* kScaleNames[kScalesCount] = { "Amara", "Oxalis", "Pigmy" };

// ── Notes ───────────────────────────────────────────────────────────────────
// How many notes can be held at once. Upstream's Simple Touch has seven reachable
// note pads so eight was generous; the Weather Station can hold five keys and
// stack a sun chord and a wind voice on top, so this needs more headroom. The arp
// drops the least recent note when it runs out.
static constexpr uint8_t kMaxHeldNotes = 16;

// ── Transposition ───────────────────────────────────────────────────────────
static constexpr int8_t kTransMin = -12;   // semitones
static constexpr int8_t kTransMax =  12;

// ── Reverb ──────────────────────────────────────────────────────────────────
// DaisySP's ReverbSc — which upstream uses — was removed in the LGPL purge, so
// the tank in string/space.* is a local MIT one. See docs/PORTING.md.
static constexpr float kReverbDamping = 0.55f;   // 0..1, higher = darker tail

// ── MIDI ────────────────────────────────────────────────────────────────────
// The Wolfpunk Weather Station's five-channel layout. It sends every layer to
// both USB and the DIN socket; set DIN_CHANNEL = None on that end so the layers
// stay on their own channels, or all of this folds onto channel 1 and the
// modifiers appear dead.
//
// Channels are 1-based here and converted on the way in.
static constexpr uint8_t kChMain    = 1;   // the five white keys
static constexpr uint8_t kChSun     = 2;   // shimmering chord an octave up
static constexpr uint8_t kChRain    = 3;   // droplets down the scale
static constexpr uint8_t kChWind    = 4;   // the drifting companion voice
static constexpr uint8_t kChThunder = 5;   // sub bass boom

// Notes on these channels pluck the string. Thunder does not — it is a boom, and
// a Karplus string has nothing to say about a boom; it drives the swell instead.
static constexpr bool kNoteChannels[17] = {
    false,
    /* 1 */ true,  /* 2 */ true,  /* 3 */ true,  /* 4 */ true,
    /* 5 */ false, false, false, false, false,
    false, false, false, false, false, false, false
};

// How far the Weather Station's continuous controllers may push a parameter
// away from where the knob is sitting, as a fraction of full travel. These ADD
// to the panel rather than replacing it, so a knob never goes dead — see
// midi/weather.h for why that matters and what "neutral" means per controller.
static constexpr float kSunBrightnessDepth = 0.45f;   // CC74, neutral 64
static constexpr float kRainReverbDepth    = 0.55f;   // CC91, neutral 40
static constexpr float kWindChanceDepth    = 0.50f;   // CC1,  neutral 0
static constexpr float kBendSemitones      = 2.0f;    // pitch bend range

// Thunder (channel 5) shoves drive and damping for this long, then lets go.
static constexpr float kThunderDecaySec  = 1.6f;
static constexpr float kThunderDriveKick = 0.40f;

// Incoming note numbers set the string's pitch directly, so the Weather
// Station's own six scales and four octaves all mean something. Turn this on to
// fold them into whichever of the three scales above is selected instead — safer,
// but it makes the Weather Station's scale and octave switches inert.
#define TS_QUANTIZE_MIDI_NOTES 0

} // namespace tspod
