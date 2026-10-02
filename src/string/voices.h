#pragma once
// The voice pool.
//
// Upstream is strictly monophonic: every note retriggers the same string, so a
// plucked arpeggio is a single string being hit over and over and each step cuts
// the one before it dead. That IS the original instrument and it is kept as the
// mono mode — but with several strings the same arpeggio lets each note ring on
// while the next is struck, which is a different and much larger sound, and
// several pad buttons at once become a chord instead of a last-note-wins race.
//
// Allocation is deliberately simple. A note event takes a free voice, or the one
// already serving that note, or failing both the oldest — and a stolen voice is
// retriggered, which cuts it. That is what every polyphonic instrument does when
// it runs out of voices, and the alternative (refusing the note) is worse.
//
// Voices that have fallen quiet are skipped entirely rather than processed and
// added as zero, so the cost tracks how much is actually sounding.
#include <stdint.h>
#include "../common/config.h"
#include "vox.h"

namespace tspod {

class Voices
{
  public:
    static constexpr uint8_t kNone = 0xff;

    void Init(float sample_rate)
    {
        for(auto& v : vox_) v.Init(sample_rate);
        for(auto& n : note_) n = kNone;
        for(auto& s : stamp_) s = 0;
    }

    // Mono is upstream's instrument: one string, last note wins.
    void SetPoly(bool on)
    {
        if(on == poly_) return;
        poly_ = on;
        // Anything above the new limit has to go, or it hangs for ever with
        // nothing able to address it.
        for(int i = Limit(); i < kMaxVoices; ++i) Free(i);
    }
    bool Poly() const { return poly_; }

    void SetExciter(Exciter e)
    {
        if(e == ex_) return;
        ex_ = e;
        for(int i = 0; i < kMaxVoices; ++i)
        {
            vox_[i].SetExciter(e);
            note_[i] = kNone;
        }
    }
    Exciter GetExciter() const { return ex_; }

    void SetBrightness(float v) { for(auto& x : vox_) x.SetBrightness(v); }
    void SetStructure(float v) { for(auto& x : vox_) x.SetStructure(v); }
    void SetDamping(float v) { for(auto& x : vox_) x.SetDamping(v); }

    // Begin sounding `note` at `freq`. Strikes it when plucking, draws the bow
    // when bowing.
    void Start(uint8_t note, float freq)
    {
        const int i = Alloc(note);
        note_[i]    = note;
        stamp_[i]   = ++clock_;
        vox_[i].SetFreq(freq);
        if(ex_ == Exciter::Bow) vox_[i].BowOn();
        else vox_[i].Strike();
    }

    // Lift the bow on `note`. Plucks have nothing to release — they decay.
    void Release(uint8_t note)
    {
        for(int i = 0; i < Limit(); ++i)
            if(note_[i] == note)
            {
                vox_[i].BowOff();
                note_[i] = kNone;   // free to re-use; it rings down meanwhile
            }
    }

    // Lift every bow, leaving the tails to ring down.
    void ReleaseAll()
    {
        for(int i = 0; i < kMaxVoices; ++i)
        {
            vox_[i].BowOff();
            note_[i] = kNone;
        }
    }

    // Stop everything dead. The panic.
    void Kill()
    {
        for(int i = 0; i < kMaxVoices; ++i) Free(i);
    }

    float Process()
    {
        float out = 0.0f;
        for(int i = 0; i < Limit(); ++i)
            if(vox_[i].Active()) out += vox_[i].Process();
        return out;
    }

    // How many are sounding, for the debug log.
    uint8_t ActiveCount() const
    {
        uint8_t n = 0;
        for(int i = 0; i < Limit(); ++i)
            if(vox_[i].Active()) n++;
        return n;
    }

  private:
    int Limit() const { return poly_ ? kMaxVoices : 1; }

    void Free(int i)
    {
        vox_[i].Kill();
        note_[i] = kNone;
    }

    // A free voice, or the one already on this note, or the oldest.
    int Alloc(uint8_t note)
    {
        const int limit = Limit();

        // Re-use the voice already serving this note, so repeating a held note
        // does not consume a second one.
        for(int i = 0; i < limit; ++i)
            if(note_[i] == note) return i;

        for(int i = 0; i < limit; ++i)
            if(!vox_[i].Active()) return i;

        int oldest = 0;
        for(int i = 1; i < limit; ++i)
            if(stamp_[i] < stamp_[oldest]) oldest = i;
        return oldest;
    }

    Vox      vox_[kMaxVoices];
    uint8_t  note_[kMaxVoices]  = { 0 };
    uint32_t stamp_[kMaxVoices] = { 0 };
    uint32_t clock_             = 0;
    bool     poly_              = true;
    Exciter  ex_                = Exciter::Pluck;
};

} // namespace tspod
