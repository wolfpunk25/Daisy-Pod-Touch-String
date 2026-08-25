#pragma once
// The pattern generator, from upstream's cpattern.h.
//
// Distributes 1..16 onsets as evenly as possible over 16 steps using a
// Christoffel word, which produces both the plain divisions (4/16, 8/16) and the
// euclidean ones (5/16, 7/16) from the same construction. Derived upstream from
// Hollos & Hollos, "Creating Rhythms" (ISBN 9781887187220).
//
// Unchanged from upstream apart from naming — this is the part of the instrument
// the Density and Shift knobs drive and it was already right.
#include <stdint.h>
#include <array>

namespace tspod {

class CPattern
{
  public:
    static constexpr uint8_t kSize = 16;

    void SetOnsets(float frac);

    // 0 .. a whole cycle. Rotating the pattern under a fixed downbeat is the
    // cheapest way to move a groove without changing what is in it.
    void SetShift(float frac)
    {
        shift_ = static_cast<uint8_t>(frac * kSize + 0.5f);
        if(shift_ >= kSize) shift_ = kSize - 1;
    }

    uint8_t Onsets() const { return onsets_; }
    uint8_t Shift() const { return shift_; }

    bool Tick();

    void Reset() { next_ = 0; }

  private:
    std::array<uint8_t, kSize> pattern_ = { 0 };
    uint8_t                    onsets_  = 0;
    uint8_t                    next_    = 0;
    uint8_t                    shift_   = 0;
};

} // namespace tspod
