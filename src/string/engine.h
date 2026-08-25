#pragma once
// The instrument. Upstream's `String` class / the body of the Daisyduino sketch,
// with no hardware anywhere in it — which is what lets tests/ drive the real
// engine rather than a paraphrase of it.
//
// Signal path, unchanged from upstream:
//
//   Clock 48ppqn ─▶ Trigger (÷16ths) ─▶ CPattern (which 16ths) ─▶ Arp ─▶ Vox
//                                                                        │
//                                            Overdrive ─▶ volume comp ───┤
//                                                                        ├─▶ out
//                                                          Space (wet) ──┘
//
// Everything on the left of Vox decides WHEN a note is plucked; the humanizer
// decides which note and what the string is like at the moment it is hit.
#include <stdint.h>
#include <random>
#include "daisysp.h"

#include "../common/config.h"
#include "arp.h"
#include "clock.h"
#include "cpattern.h"
#include "latch.h"
#include "scale.h"
#include "space.h"
#include "trigger.h"
#include "vox.h"
#include "xfade.h"

namespace tspod {

class Engine
{
  public:
    Engine();

    void Init(float sample_rate, float block_size);

    // Audio callback.
    void Process(float* out_l, float* out_r, size_t size);

    // ── Notes ───────────────────────────────────────────────────────────────
    // Keyboard, from the MIDI socket.
    void NoteOn(uint8_t note);
    void NoteOff(uint8_t note);
    // Pad, from the encoder button.
    void ToggleNote(uint8_t note);
    void AllNotesOff();
    bool IsHeld(uint8_t note) const { return latch_.IsHeld(note); }

    // The MIDI note for one of the eight degrees of the current scale.
    uint8_t NoteForDegree(uint8_t d) const { return scale_.NoteAt(d); }

    // ── Arp ─────────────────────────────────────────────────────────────────
    void SetArpOn(bool on) { arp_on_ = on; }
    bool ArpOn() const { return arp_on_; }
    void SetLatch(bool on);
    bool Latched() const { return latch_.On(); }

    // ── Clock ───────────────────────────────────────────────────────────────
    void  SetTempo(float norm) { clock_.SetTempo(norm); }
    float Tempo() const { return clock_.Tempo(); }
    bool  ClockInternal() const { return clock_.Internal(); }
    bool  ClockRunning() const { return clock_.IsRunning(); }
    void  MidiClockPulse() { clock_.ExternalPulse(); }

    // ── Scale and pitch ─────────────────────────────────────────────────────
    void    SetScaleIndex(uint8_t i) { scale_.SetIndex(i); }
    uint8_t ScaleIndex() const { return scale_.Index(); }
    void    SetTranspose(int8_t s) { scale_.SetTranspose(s); }
    int8_t  Transpose() const { return scale_.Transpose(); }
    void    SetBend(float semis) { scale_.SetBend(semis); }

    // ── Sound, all 0..1 ─────────────────────────────────────────────────────
    void SetBrightness(float v) { brightness_ = Clamp(v); }
    void SetTimbre(float v) { structure_ = Clamp(v); }
    // Upstream scales damping by 0.7 before it reaches the voice; past that the
    // string stops ringing at all.
    void SetDamping(float v) { damping_ = Clamp(v) * 0.7f; }
    void SetDensity(float v) { pattern_.SetOnsets(Clamp(v)); }
    void SetShift(float v) { pattern_.SetShift(Clamp(v)); }
    void SetReverb(float v)
    {
        xfade_.Set(Clamp(v));
        space_.SetKnob(Clamp(v));
    }
    void SetDrive(float v);

    // One control where upstream has two. See the comment on the definition.
    void SetChance(float v);

    // ── Readout, for the LEDs ───────────────────────────────────────────────
    // True once per pluck, and clears. The only way the panel can see the
    // pattern, since everything that decides it happens in the audio callback.
    bool  TakePluck();
    float Peak() const { return peak_; }
    uint8_t Density() const { return pattern_.Onsets(); }

  private:
    static float Clamp(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    void OnClockTick();
    void OnNoteFromLatch(uint8_t note);
    void OffNoteFromLatch(uint8_t note);
    void OnArpNote(uint8_t note, uint8_t vel);
    void Pluck(uint8_t note, bool humanize_pitch);
    void ResetSequence();
    void StartOrStop();

    uint8_t HumanizedNote(uint8_t note);
    void    ApplyStringHumanize();

    static constexpr uint32_t kPPQN       = 48;
    static constexpr uint32_t kPPQNExtern = 24;

    Scale                            scale_;
    Vox                              vox_;
    Clock                            clock_;
    Trigger                          trigger_;
    CPattern                         pattern_;
    Arp<kMaxHeldNotes, 4>            arp_;
    Latch                            latch_;
    daisysp::Overdrive               drive_;
    Space                            space_;
    XFade                            xfade_;

    std::default_random_engine             rng_;
    std::uniform_int_distribution<uint8_t> dice_;

    float   brightness_   = 0.0f;
    float   structure_    = 0.0f;
    float   damping_      = 0.0f;
    float   volume_       = 1.0f;
    uint8_t note_chance_  = 0;    // 0..100
    uint8_t string_chance_ = 0;   // 0..100
    bool    arp_on_       = false;

    bool  plucked_ = false;
    float peak_    = 0.0f;
};

} // namespace tspod
