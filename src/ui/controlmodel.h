#pragma once
// ============================================================================
// What the Pod's panel means. No hardware in this file — ui/panel.* is the thin
// adapter that reads the real controls and lights the real LEDs, and tests/
// drives this directly.
//
// The problem: TouchString is PLAYED. Seven of the Simple Touch's twelve pads
// are note pads and they are the instrument's whole performance surface. The Pod
// has two knobs, two buttons and an encoder, and no pads at all. Strip the pads
// and there is nothing left to play — which is why the encoder is a note pad
// here and not a parameter control.
//
//   Encoder turn        pick one of the eight degrees of the current scale
//   Encoder press       toggle that note in or out of the held set
//   Encoder held 1.2s   panic: every note off, sequence reset
//
//   Button 1 tap        arp: off → on → latched          (upstream's 3-way switch)
//   Button 1 held       the setup layer, while held:
//     + Knob 1            tempo (fully down: follow incoming MIDI clock)
//     + Knob 2            transpose, ±12 semitones
//     + Encoder           scale — Amara / Oxalis / Pigmy
//
//   Button 2 tap        next page
//     String    Knob 1 brightness   Knob 2 timbre
//     Body      Knob 1 damping      Knob 2 drive
//     Pattern   Knob 1 density      Knob 2 shift
//     Space     Knob 1 reverb       Knob 2 chance
//
// Eleven parameters on two knobs. Upstream's two randomisation knobs are one
// "chance" control here — see Engine::SetChance for why that is staged rather
// than parallel — which is what makes it four clean pages instead of five.
//
// The knobs are RELATIVE, not absolute: one pot serves five destinations, so
// after a page change its physical position means nothing and it contributes
// deltas from wherever it happens to be, only once it has actually moved. Same
// approach as the ZenTouch, Spotykach, Terrarium, Audrey and Wrangler Pod ports.
// ============================================================================
#include <math.h>
#include <stdint.h>
#include "../string/engine.h"

namespace tspod {

enum class Param : uint8_t
{
    Brightness = 0,
    Timbre,
    Damping,
    Drive,
    Density,
    Shift,
    Reverb,
    Chance,
    Tempo,
    Transpose,   // stored 0..1, read out as semitones
    kCount
};
static constexpr int kNumParams = static_cast<int>(Param::kCount);

enum class Page : uint8_t
{
    String = 0,
    Body,
    Pattern,
    Space,
    kCount
};
static constexpr int kNumPages = static_cast<int>(Page::kCount);

enum class ArpMode : uint8_t
{
    Off = 0,
    On,
    Latched
};

struct PageMap
{
    const char* name;
    Param       k1, k2;
};
extern const PageMap kPages[kNumPages];
extern const float   kBoot[kNumParams];

// A pot that contributes deltas rather than absolute position, so one physical
// knob can serve five parameters without jumps.
class RelKnob
{
  public:
    // Movement under this is ADC noise, not a hand. Without it an armed knob
    // random-walks its parameter while nobody is touching it — a hardware log on
    // the Audrey port caught a parked knob bouncing a value for a full minute.
    static constexpr float kQuantum = 0.002f;

    void Rearm(float pot)
    {
        active_ = false;
        anchor_ = pot;
        last_   = pot;
    }

    float Apply(float pot, float value)
    {
        if(!active_)
        {
            // 2% of travel before it takes over: past the noise, under a nudge.
            if(fabsf(pot - anchor_) > 0.02f)
            {
                active_ = true;
                last_   = pot;
            }
            return value;
        }
        const float d = pot - last_;
        // Leave `last_` where it is, so a slow deliberate turn still accumulates
        // past the threshold instead of being thrown away a sliver at a time.
        if(fabsf(d) < kQuantum) return value;
        value += d;
        last_ = pot;
        return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }

  private:
    bool  active_ = false;
    float anchor_ = 0.0f, last_ = 0.0f;
};

class ControlModel
{
  public:
    // Hold button 1 this long and it is the setup layer, not a tap.
    static constexpr float kHoldSec  = 0.40f;
    // Hold the encoder this long and it is a panic, not a note.
    static constexpr float kPanicSec = 1.20f;

    void Init(Engine* engine, float knob1, float knob2);

    // A modulation offset added on top of where the knob is sitting, in the
    // same 0..1 units, and clamped with it. This is how the Weather Station's
    // controllers reach the sound: CC74 opens the brightness the Brightness knob
    // set, rather than replacing it. A knob that goes dead the moment another
    // box is plugged in is worse than no MIDI mapping at all.
    void SetMod(Param p, float offset);
    float Mod(Param p) const { return mod_[static_cast<int>(p)]; }

    // Write a parameter's panel value outright. Used by the generic CC map,
    // where a controller means to be in charge — going through here rather than
    // straight at the engine is what lets the knob pick up from where MIDI left
    // it instead of snapping back to a value nobody can see.
    void SetNorm(Param p, float value);

    // Panel rate, ~1 kHz. Raw control state in, everything else follows.
    void Read(bool btn1, bool btn2, float knob1, float knob2, int enc_inc,
              bool enc_btn, float dt);

    // ── Readout, for the LEDs and the debug log ─────────────────────────────
    Page    CurrentPage() const { return page_; }
    ArpMode Mode() const { return mode_; }
    bool    SetupLayer() const { return setup_; }
    uint8_t Degree() const { return degree_; }
    bool    DegreeHeld() const;
    float   Norm(Param p) const { return norm_[static_cast<int>(p)]; }
    int8_t  TransposeSemis() const;
    // True for the one pass on which the panic fires.
    bool    Panicked() const { return panicked_; }
    // True from the panic firing until the encoder is let go. The panic happens
    // 1.2 s into a hold, while the finger is still down — so a confirmation that
    // starts fading immediately is over before the hand moves and the eye looks.
    // Reported from the board as exactly that: everything cleared correctly, and
    // the red flash was never seen.
    bool    PanicLatched() const { return panic_latched_; }

    // Enough to tell, from a serial capture, whether an encoder press is being
    // seen at all, whether it is being read as a press or as a hold, and whether
    // the toggle actually ran.
    bool     EncDown() const { return enc_; }
    float    EncHeld() const { return enc_held_; }
    uint16_t Toggles() const { return toggles_; }
    uint16_t Panics() const { return panics_; }
    uint16_t HeldCount() const;

    // The last control to move, and how long it stays highlighted. With relative
    // knobs there is no pointer to look at, so the LEDs borrow this.
    float TouchLeft() const { return touch_left_; }

  private:
    // Move every note the encoder pad put in over to the new scale's note for
    // the same degree. See the definition for why this cannot live in Engine.
    void RetunePadNotes();

    void PushAll();
    void PushParam(Param p);
    void RearmKnobs(float k1, float k2);
    void SetMode(ArpMode m);

    Engine* engine_ = nullptr;

    float   norm_[kNumParams] = { 0 };
    float   mod_[kNumParams]  = { 0 };
    Page    page_             = Page::String;
    ArpMode mode_             = ArpMode::Off;
    uint8_t degree_           = 0;
    uint8_t scale_            = 0;

    RelKnob k1_, k2_;

    // The actual MIDI note the pad issued for each degree, or kNoPadNote. The
    // pad's notes are degrees wearing note numbers and have to follow the scale;
    // notes off the MIDI socket are absolute and must not be touched. Nothing
    // downstream can tell the two apart once they are in the held set, so the
    // distinction is remembered here, where it is made.
    static constexpr uint8_t kNoPadNote = 0xff;
    uint8_t pad_note_[kScaleSize] = { kNoPadNote, kNoPadNote, kNoPadNote, kNoPadNote,
                                      kNoPadNote, kNoPadNote, kNoPadNote, kNoPadNote };

    // Button and encoder edge state.
    bool  b1_ = false, b2_ = false, enc_ = false;
    float b1_held_ = 0.0f, enc_held_ = 0.0f;
    bool  b1_consumed_ = false, enc_consumed_ = false;
    bool  setup_       = false;
    bool  panicked_      = false;
    bool  panic_latched_ = false;

    float touch_left_ = 0.0f;

    uint16_t toggles_ = 0;
    uint16_t panics_  = 0;
};

} // namespace tspod
