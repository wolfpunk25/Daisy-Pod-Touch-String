#pragma once
// Which notes are currently held, and what "held" means when the latch is on.
//
// Two things play this instrument and they are not the same kind of control, so
// they get different doors into the same set:
//
//   NoteOn / NoteOff   a keyboard. The Weather Station's keys go through here.
//                      With the latch off, releasing a key releases the note.
//                      With it on, the note stays until a fresh chord starts —
//                      that is, until every key has been let go and a new one
//                      arrives. Notes added while something is still down join
//                      the chord rather than replacing it.
//
//   Toggle             a pad. The Pod's encoder button goes through here: one
//                      press puts a note in the set, the next takes it out.
//                      A momentary button cannot "hold" anything, so latch
//                      semantics do not apply and it is how you build a chord
//                      one press at a time.
//
// The toggle behaviour is upstream's, from the Daisyduino sketch's pad handler
// (`if (arp_on && latch && hold[n]) arp.NoteOff(n) else arp.NoteOn(n)`). Rosa's
// libDaisy re-port replaced it with replace-the-chord latching, which is right
// for a keyboard and would make the encoder pad useless.
#include <stdint.h>
#include <bitset>
#include <functional>

namespace tspod {

class Latch
{
  public:
    static constexpr int kNotes = 128;

    void SetOnNoteOn(std::function<void(uint8_t)> f) { on_ = f; }
    void SetOnNoteOff(std::function<void(uint8_t)> f) { off_ = f; }

    bool On() const { return latched_; }

    void SetOn(bool on)
    {
        const bool was = latched_;
        latched_       = on;
        // Coming out of latch drops everything nobody is actually touching.
        if(was && !on) ReleaseUntouched();
    }

    void NoteOn(uint8_t num)
    {
        if(num >= kNotes) return;
        // A new chord: the last physical key went up before this one came down,
        // so whatever the latch was holding is finished with.
        if(latched_ && down_.none()) ReleaseUntouched();
        down_.set(num);
        if(!held_.test(num))
        {
            held_.set(num);
            if(on_) on_(num);
        }
    }

    void NoteOff(uint8_t num)
    {
        if(num >= kNotes) return;
        down_.reset(num);
        if(!latched_ && held_.test(num))
        {
            held_.reset(num);
            if(off_) off_(num);
        }
    }

    // The encoder pad. Never consults the latch — see the note at the top.
    void Toggle(uint8_t num)
    {
        if(num >= kNotes) return;
        if(held_.test(num))
        {
            held_.reset(num);
            down_.reset(num);
            if(off_) off_(num);
        }
        else
        {
            held_.set(num);
            if(on_) on_(num);
        }
    }

    bool IsHeld(uint8_t num) const { return num < kNotes && held_.test(num); }
    bool Any() const { return held_.any(); }

    void Clear()
    {
        for(int i = 0; i < kNotes; ++i)
            if(held_.test(i) && off_) off_(static_cast<uint8_t>(i));
        held_.reset();
        down_.reset();
    }

  private:
    void ReleaseUntouched()
    {
        for(int i = 0; i < kNotes; ++i)
            if(held_.test(i) && !down_.test(i))
            {
                held_.reset(i);
                if(off_) off_(static_cast<uint8_t>(i));
            }
    }

    std::function<void(uint8_t)> on_, off_;
    std::bitset<kNotes>          held_;   // in the arp right now
    std::bitset<kNotes>          down_;   // physically down right now
    bool                         latched_ = false;
};

} // namespace tspod
