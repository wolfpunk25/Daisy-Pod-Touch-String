#pragma once
// ============================================================================
// What the Pod's panel means. No hardware in this file — ui/panel.* reads the
// real controls, and tests/ drives this directly.
//
// REWRITTEN 2026-10-02, because the first version was not enjoyable to play.
// It put eleven parameters on two knobs across four pages that nothing on the
// box could label, so you had to remember which page you were on from one LED's
// colour and what the knobs did there. The knobs were RELATIVE, which avoids
// jumps on a page change but costs you the only readable state a pot has: where
// it is pointing. And a third of the panel was an encoder note pad, built when
// the Pod had no note source — which it now has, in the eight-button pad bolted
// to it.
//
// So: the pad plays, the Pod shapes. Four always-live controls, one job each.
//
//   Knob 1              BRIGHTNESS, absolute, and never anything else
//   Knob 2              DENSITY,    absolute, and never anything else
//   Encoder turn        REVERB
//   Encoder press       CHANCE — steps off / subtle / moving / wild
//   Encoder held 1.2s   panic: every note off, sequence reset
//
//   Button 1 tap        arp: off → on → latched
//   Button 1 + encoder  tempo (the encoder, not a knob — see below)
//   Button 2 tap        exciter: pluck → bow
//
// The knobs are ABSOLUTE and permanently assigned, which is the whole point.
// That also means they can never be borrowed: an absolute pot moved while it
// meant something else would jump its real parameter the moment you let go. So
// tempo lives on the ENCODER under button 1, where relative movement is the
// natural behaviour and nothing can jump. Same reasoning the Wrangler Pod port
// used for going absolute in the first place — one pot, one job.
//
// Timbre, damping, drive, pattern shift and transposition are no longer on the
// panel at all; they sit in common/config.h. They are the set-once ones. Taking
// them off is the point rather than a compromise — see docs/PORTING.md.
// ============================================================================
#include <math.h>
#include <stdint.h>
#include "../string/engine.h"

namespace tspod {

// Everything the engine takes. Only the first five are reachable by hand; the
// rest exist so MIDI can still move them.
enum class Param : uint8_t
{
    Brightness = 0,
    Density,
    Reverb,
    Chance,
    Tempo,
    Timbre,
    Damping,
    Drive,
    kCount
};
static constexpr int kNumParams = static_cast<int>(Param::kCount);

enum class ArpMode : uint8_t
{
    Off = 0,
    On,
    Latched
};

class ControlModel
{
  public:
    // Hold button 1 this long and the encoder becomes the tempo.
    static constexpr float kHoldSec = 0.40f;
    // Hold the encoder this long and it is a panic, not a chance step.
    static constexpr float kPanicSec = 1.20f;
    // One detent of the encoder, as a fraction of a parameter's travel.
    static constexpr float kEncStep      = 1.0f / 32.0f;
    static constexpr float kEncTempoStep = 1.0f / 48.0f;

    void Init(Engine* engine, float knob1, float knob2);

    // Panel rate, ~1 kHz.
    void Read(bool btn1, bool btn2, float knob1, float knob2, int enc_inc,
              bool enc_btn, float dt);

    // A modulation offset added on top of the panel, in the same 0..1 units and
    // clamped with it. This is how a controller's CCs reach the sound without
    // killing the knob under your hand. Note that for the parameters a pot owns
    // outright, only modulation can be heard — a CC that tries to SET brightness
    // is overwritten by knob 1 on the next pass, which is what absolute knobs
    // mean.
    void  SetMod(Param p, float offset);
    float Mod(Param p) const { return mod_[static_cast<int>(p)]; }

    // Write a parameter's stored value. Only meaningful for the parameters no
    // pot owns.
    void SetNorm(Param p, float value);

    // The pad's scale button, over CC20. The pad is the master for scale: it is
    // the end with a display on it.
    void SetScaleIndex(uint8_t index);

    // ── Readout, for the LEDs and the debug log ─────────────────────────────
    ArpMode Mode() const { return mode_; }
    Exciter GetExciter() const { return exciter_; }
    bool    TempoLayer() const { return tempo_layer_; }
    uint8_t ChanceStep() const { return chance_step_; }
    float   Norm(Param p) const { return norm_[static_cast<int>(p)]; }
    uint8_t ScaleIndex() const { return scale_; }

    bool Panicked() const { return panicked_; }
    // True from the panic firing until the encoder is let go. The panic happens
    // 1.2 s into a hold, while the finger is still down, so a confirmation that
    // starts fading immediately is over before the hand moves and the eye looks.
    bool PanicLatched() const { return panic_latched_; }

    // How long since anything moved, for the LEDs and the log rate.
    float TouchLeft() const { return touch_left_; }

  private:
    void PushAll();
    void PushParam(Param p);
    void SetMode(ArpMode m);
    void SetExciterMode(Exciter e);
    void StepChance();
    void Touch() { touch_left_ = 1.0f; }

    static float Clamp(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    Engine* engine_ = nullptr;

    float norm_[kNumParams] = { 0 };
    float mod_[kNumParams]  = { 0 };

    ArpMode mode_        = ArpMode::Off;
    Exciter exciter_     = Exciter::Pluck;
    uint8_t scale_       = 0;
    uint8_t chance_step_ = 0;

    // Button and encoder edge state.
    bool  b1_ = false, b2_ = false, enc_ = false;
    float b1_held_ = 0.0f, enc_held_ = 0.0f;
    bool  b1_consumed_ = false, enc_consumed_ = false;
    bool  tempo_layer_ = false;

    bool panicked_      = false;
    bool panic_latched_ = false;

    float touch_left_ = 0.0f;
};

} // namespace tspod
