#pragma once
// Minimal 16-bit stereo WAV writer, so `make audio` can produce something to
// judge by ear without a board in the room.
#include <cstdint>
#include <cstdio>
#include <vector>

inline void WriteWav(const char* path, const std::vector<float>& l,
                     const std::vector<float>& r, int sample_rate)
{
    const uint32_t frames    = static_cast<uint32_t>(l.size() < r.size() ? l.size() : r.size());
    const uint32_t data_size = frames * 4;
    FILE*          f         = fopen(path, "wb");
    if(!f) return;

    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };

    fwrite("RIFF", 1, 4, f);
    u32(36 + data_size);
    fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(2);
    u32(static_cast<uint32_t>(sample_rate));
    u32(static_cast<uint32_t>(sample_rate) * 4);
    u16(4);
    u16(16);
    fwrite("data", 1, 4, f);
    u32(data_size);

    for(uint32_t i = 0; i < frames; i++)
        for(float v : { l[i], r[i] })
        {
            if(v > 1.0f) v = 1.0f;
            if(v < -1.0f) v = -1.0f;
            u16(static_cast<uint16_t>(static_cast<int16_t>(v * 32767.0f)));
        }
    fclose(f);
}
