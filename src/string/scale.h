#pragma once
// The three scales, transposition, and the one place a note number becomes a
// frequency. Upstream's scale.h, re-expressed in MIDI note numbers — see the
// note in common/config.h for why.
#include <stdint.h>
#include <random>
#include "../common/config.h"

namespace tspod {

class Scale
{
  public:
    Scale() : rng_(0x5EED) {}

    uint8_t ScalesCount() const { return kScalesCount; }
    uint8_t Index() const { return index_; }
    void    SetIndex(uint8_t i)
    {
        index_ = i < kScalesCount ? i : kScalesCount - 1;
    }

    // Whole semitones, from the panel. Upstream spends a knob on a 25-entry
    // ratio table; this is the same 25 positions.
    int8_t Transpose() const { return trans_; }
    void   SetTranspose(int8_t semis)
    {
        trans_ = semis < kTransMin ? kTransMin : (semis > kTransMax ? kTransMax : semis);
    }

    // Fractional semitones, from MIDI pitch bend. Kept apart from Transpose so a
    // gust from the Weather Station cannot walk the key away permanently.
    void  SetBend(float semis) { bend_ = semis; }
    float Bend() const { return bend_; }

    // The untransposed MIDI note for one of the eight degrees. This is what the
    // encoder note pad hands out.
    uint8_t NoteAt(uint8_t degree) const
    {
        return kScales[index_][degree < kScaleSize ? degree : kScaleSize - 1];
    }

    // The only place pitch actually happens.
    float Freq(uint8_t note) const
    {
        return Mtof(static_cast<float>(note) + static_cast<float>(trans_) + bend_);
    }

    // Used by the humanizer, which reaches for a note that is not the one asked
    // for. Upstream returns a frequency; a note number composes better with the
    // octave dice that follow it.
    uint8_t RandomNote()
    {
        std::uniform_int_distribution<uint8_t> d(0, kScaleSize - 1);
        return NoteAt(d(rng_));
    }

    // Fold an arbitrary note onto the nearest degree of the current scale, in
    // whatever octave it arrived in. Only used when TS_QUANTIZE_MIDI_NOTES is on.
    uint8_t Quantize(uint8_t note) const;

    static float Mtof(float note)
    {
        return 440.0f * powf(2.0f, (note - 69.0f) * (1.0f / 12.0f));
    }

  private:
    uint8_t                   index_ = 0;
    int8_t                    trans_ = 0;
    float                     bend_  = 0.0f;
    std::default_random_engine rng_;
};

} // namespace tspod
