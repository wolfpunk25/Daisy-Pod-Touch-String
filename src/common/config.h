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

// ── Voices ──────────────────────────────────────────────────────────────────
// Upstream is strictly monophonic. Mono is kept as a mode because it IS the
// original instrument, but with several strings a plucked arpeggio lets each
// note ring on while the next is struck, and several pad buttons at once become
// a chord rather than a last-note-wins race.
//
// Each voice is about 10.7 KB (two Karplus-Strong delay lines) and costs one
// resonator's worth of CPU, since only the current exciter's resonator runs —
// measured, running both costs exactly twice as much. Start here and raise it
// once the figure on hardware says there is room; the pad has eight buttons, so
// eight is the number that would let every button sound at once.
static constexpr int kMaxVoices = 6;

// A voice is freed once it has fallen quiet, so silent ones cost nothing. The
// tracker is slow on purpose: a long damping setting rings for far longer than a
// short one and a fixed timeout would cut it.
static constexpr float kVoiceIdleTrack = 0.0004f;
// -72 dBFS. Low enough to be inaudible, high enough that a voice is handed back
// promptly instead of idling for seconds at a level nobody can hear.
static constexpr float kVoiceIdleFloor = 2.5e-4f;

// ── Notes ───────────────────────────────────────────────────────────────────
// How many notes can be held at once. Upstream's Simple Touch has seven reachable
// note pads so eight was generous; the Weather Station can hold five keys and
// stack a sun chord and a wind voice on top, so this needs more headroom. The arp
// drops the least recent note when it runs out.
static constexpr uint8_t kMaxHeldNotes = 16;

// ── Off the panel ───────────────────────────────────────────────────────────
// Damping, drive, pattern shift and transposition used to be reachable by hand,
// across four pages that nothing on the box could label. The panel is now four
// always-live controls with one job each, and these four are set here instead.
// They are the ones that are set once and left; if any turns out to be wanted
// mid-performance it belongs on the panel, not in a page.
//
// Four steps of the Chance control, which is a stepped encoder press rather than
// a knob — it is a character setting you pick, not something you ride.
static constexpr float kChanceSteps[4] = { 0.00f, 0.30f, 0.60f, 0.90f };

// ── The bow ─────────────────────────────────────────────────────────────────
// The bow is its own exciter — noise through a low pass into the bare resonator
// — rather than StringVoice's sustain flag, which is a stream of clicks at
// anything the knob can reach. See string/vox.h for the measurements.
//
// Brightness sweeps the exciter's cutoff across this range, exponentially.
// It used to reach 7.5 kHz, but measurement showed the balance stops changing
// above about 3 kHz — HF share went 0.277 at 3 kHz to 0.279 at 7.5 — so the top
// third of the knob was travel that did nothing.
static constexpr float kBowCutoffLow  = 280.0f;    // Hz
static constexpr float kBowCutoffHigh = 3000.0f;

// The RESONATOR's own brightness, which is a different thing from the exciter's
// cutoff and the one that was making the bow harsh. In KarplusString it raises
// the loop's damping cutoff, so the string keeps more of its high harmonics on
// every round trip: Q climbs, inharmonic partials ring, and it reads as metallic.
// Measured HF share against resonator brightness, exciter fixed:
//
//     0.20 -> 0.222    0.40 -> 0.208    0.65 -> 0.244
//     0.90 -> 0.338    1.00 -> 0.376
//
// The knee is at about 0.65, and the knob used to run all the way to 1.0. It now
// stops short of the knee, and the exciter's cutoff carries the audible sweep —
// which the same measurement showed is safe and almost level-flat.
static constexpr float kBowResBrightLow  = 0.15f;
static constexpr float kBowResBrightHigh = 0.62f;

// Resonator brightness also adds level (7 dB across the old range), so a little
// compensation keeps Brightness a timbre control rather than a second volume.
static constexpr float kBowGainTilt = 0.40f;
// How hard the bow is drawn. The resonator needs enough energy to build a note
// out of noise, and not so much that it saturates.
static constexpr float kBowGain = 0.28f;
// A bow needs a lossier-than-nothing loop that still holds on; upstream's
// damping range is tuned for a pluck and is far too lossy to sustain.
static constexpr float kBowDampingLow = 0.55f;
// KarplusString crossfades to INFINITE decay above 0.95 damping, so the bow's
// range has to stop short of it: with the loop never losing energy there is
// nothing for a release to decay into, and the string rings for ever.
static constexpr float kBowDampingHigh = 0.90f;
// How quickly the bow takes hold and lets go, in seconds. The release is the
// one that matters: a bow lifting off a string leaves it ringing, and cutting
// the excitation dead sounds like a switch rather than a player.
static constexpr float kBowAttackSec  = 0.030f;
static constexpr float kBowReleaseSec = 0.450f;
static constexpr float kTimbreDefault  = 0.35f;
static constexpr float kDampingDefault = 0.50f;
static constexpr float kDriveDefault   = 0.20f;
static constexpr float kShiftDefault   = 0.00f;
static constexpr int8_t kTransposeDefault = 0;

// ── Transposition ───────────────────────────────────────────────────────────
static constexpr int8_t kTransMin = -12;   // semitones
static constexpr int8_t kTransMax =  12;

// ── Reverb ──────────────────────────────────────────────────────────────────
// DaisySP's ReverbSc — which upstream uses — was removed in the LGPL purge, so
// the tank in string/space.* is a local MIT one. See docs/PORTING.md.
static constexpr float kReverbDamping = 0.55f;   // 0..1, higher = darker tail

// The dedicated eight-note pad selects the scale with this controller, sending
// the index (0..2) as the value. It is the master: both ends have to agree on
// which note each button means, and the pad is the end with the display on it.
static constexpr uint8_t kScaleSelectCC = 20;

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
