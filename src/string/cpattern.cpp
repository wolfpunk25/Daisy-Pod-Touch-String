#include "cpattern.h"

namespace tspod {

void CPattern::SetOnsets(float frac)
{
    if(frac < 0.0f) frac = 0.0f;
    if(frac > 1.0f) frac = 1.0f;
    // 1..16 — never zero. A pattern with no onsets is silence, and Density is
    // not a volume control; upstream makes the same choice.
    const uint8_t onsets
        = static_cast<uint8_t>(frac * static_cast<float>(kSize - 1) + 0.5f) + 1;
    if(onsets == onsets_) return;
    onsets_ = onsets;

    if(onsets >= kSize)
    {
        pattern_.fill(1);
        return;
    }

    // Christoffel word ------------------------------------------------------
    uint8_t y = onsets, a = y;
    uint8_t x = kSize - onsets, b = x;
    int     i = 0;
    pattern_[i++] = 1;
    while(a != b)
    {
        if(a > b)
        {
            pattern_[i] = 1;
            b += x;
        }
        else
        {
            pattern_[i] = 0;
            a += y;
        }
        i++;
    }
    pattern_[i++] = 0;

    // For onset counts that share a factor with 16 the word is shorter than the
    // bar, so it repeats to fill it.
    if(i >= kSize) return;
    const int offset = i;
    i                = 0;
    while(i + offset < kSize)
    {
        pattern_[i + offset] = pattern_[i];
        i++;
    }
}

bool CPattern::Tick()
{
    int point = static_cast<int>(next_) - static_cast<int>(shift_);
    if(point < 0) point += kSize;

    if(++next_ == kSize) next_ = 0;

    return pattern_[point] != 0;
}

} // namespace tspod
