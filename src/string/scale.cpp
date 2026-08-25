#include "scale.h"
#include <math.h>

namespace tspod {

uint8_t Scale::Quantize(uint8_t note) const
{
    // Compare pitch classes, so a note keeps the octave it was played in and
    // only its degree moves. Distance is measured the short way round the
    // circle: without that, a B against a scale containing C is eleven
    // semitones from it rather than one, and every leading note falls the
    // wrong way.
    const int pc   = note % 12;
    int       best = 0, best_d = 127;
    for(uint8_t i = 0; i < kScaleSize; ++i)
    {
        int d = (static_cast<int>(kScales[index_][i]) % 12) - pc;
        if(d > 6) d -= 12;
        if(d < -6) d += 12;
        const int ad = d < 0 ? -d : d;
        if(ad < best_d)
        {
            best_d = ad;
            best   = d;
        }
    }
    const int out = static_cast<int>(note) + best;
    return static_cast<uint8_t>(out < 0 ? 0 : (out > 127 ? 127 : out));
}

} // namespace tspod
