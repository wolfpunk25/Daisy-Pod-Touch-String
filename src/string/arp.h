#pragma once
// The arpeggiator, from upstream's arp.h.
//
// Held notes live in a fixed array linked into a sorted doubly-linked list, with
// slot 0 a sentinel holding 0xff so an insertion walk always terminates. A
// separate `order_` array remembers the sequence they arrived in, which is what
// "as played" mode walks. That design is upstream's and it is a good one — no
// allocation, and both orderings come out of the same structure.
//
// Two changes from upstream:
//   * `num` is a MIDI note number rather than a scale degree 0..7, so notes off
//     the MIDI socket and notes off the encoder are the same kind of thing. The
//     sorted list needs `num` to be orderable and MIDI note numbers are, so the
//     algorithm is untouched by this.
//   * The note-length / gate machinery is gone. It exists to release a held note
//     early, and every voice here is a pluck that decays on its own — upstream
//     wires its note-off callback to an empty function. Rosa's libDaisy re-port
//     dropped it for the same reason.
#include <stdint.h>
#include <string.h>
#include <functional>
#include <random>

namespace tspod {

enum class ArpDirection
{
    Fwd,
    Rev
};

template <uint8_t note_count, uint8_t ppqn>
class Arp
{
  public:
    Arp() : dice_(0, 100), rng_(0xA55E) { Clear(); }

    void SetOnNoteOn(std::function<void(uint8_t, uint8_t)> f) { on_note_on_ = f; }
    void SetOnNoteOff(std::function<void(uint8_t)> f) { on_note_off_ = f; }
    void SetDirection(ArpDirection d) { dir_ = d; }
    void SetRandChance(uint8_t c) { rand_chance_ = c; }
    void SetAsPlayed(bool v) { as_played_ = v; }

    void NoteOn(uint8_t num, uint8_t vel)
    {
        // Duplicates are tolerated upstream; here they are not, because the same
        // note can now arrive from the encoder and from two MIDI channels at
        // once and a doubled note would take two slots and arpeggiate twice.
        if(Contains(num)) return;

        uint8_t slot = 1;
        while(slot < note_count + 1)
        {
            if(notes_[slot].num == kEmpty) break;
            slot++;
        }
        // No free slot: drop the least recent note. With the Weather Station
        // stacking sun chords on top of held keys this is reachable in ordinary
        // playing, not just in abuse.
        if(slot == note_count + 1)
        {
            slot = order_[0];
            RemoveNote(slot);
        }

        uint8_t idx = bottom_idx_;
        while(notes_[idx].num < num) idx = notes_[idx].next;
        if(idx == bottom_idx_) bottom_idx_ = slot;

        notes_[slot].num  = num;
        notes_[slot].vel  = vel;
        notes_[slot].next = idx;
        notes_[slot].prev = notes_[idx].prev;

        notes_[notes_[idx].prev].next = slot;
        notes_[idx].prev              = slot;

        order_[size_ - 1] = slot;
        size_++;
    }

    void NoteOff(uint8_t num)
    {
        for(uint8_t i = 1; i < note_count + 1; i++)
            if(notes_[i].num == num)
            {
                RemoveNote(i);
                return;
            }
    }

    bool Contains(uint8_t num) const
    {
        for(uint8_t i = 1; i < note_count + 1; i++)
            if(notes_[i].num == num) return true;
        return false;
    }

    // One pulse of the internal clock. Fires a note every 1/16th.
    void Trigger()
    {
        if(!HasNote()) return;
        if(++pulse_counter_ < ppqn / 4) return;
        pulse_counter_ = 0;

        uint8_t note_idx = (dir_ == ArpDirection::Fwd) ? NextNoteIdx() : PrevNoteIdx();

        if(rand_chance_ > 5 && rand_chance_ < 95 && size_ > 2)
        {
            if(dice_(rng_) <= rand_chance_)
            {
                std::uniform_int_distribution<uint8_t> pick(0, size_ - 2);
                note_idx = order_[pick(rng_)];
            }
        }

        current_idx_ = note_idx;
        if(on_note_on_) on_note_on_(notes_[note_idx].num, notes_[note_idx].vel);
    }

    bool    HasNote() const { return size_ > 1; }
    uint8_t Size() const { return size_ - 1; }

    void Clear()
    {
        memset(order_, 0, sizeof(order_));
        notes_[0].num  = kSentinel;
        notes_[0].next = 0;
        notes_[0].prev = 0;
        size_          = 1;
        bottom_idx_    = 0;
        for(uint8_t i = 1; i < note_count + 1; i++)
        {
            notes_[i].num  = kEmpty;
            notes_[i].next = kUnlinked;
            notes_[i].prev = kUnlinked;
        }
        // Primed so the first Trigger() after a Clear() fires immediately rather
        // than a sixteenth late. Upstream does the same.
        pulse_counter_ = ppqn / 4;
        current_idx_   = 0;
        played_idx_    = 0;
        primed_        = true;
    }

  private:
    struct Note
    {
        uint8_t num, vel, next, prev;
    };

    static constexpr uint8_t kSentinel = 0xff;
    static constexpr uint8_t kEmpty    = 0xfe;
    static constexpr uint8_t kUnlinked = 0xfd;

    void RemoveNote(uint8_t idx)
    {
        if(on_note_off_) on_note_off_(notes_[idx].num);
        if(idx == current_idx_) current_idx_ = PrevNoteIdx();

        notes_[notes_[idx].prev].next = notes_[idx].next;
        notes_[notes_[idx].next].prev = notes_[idx].prev;
        if(idx == bottom_idx_) bottom_idx_ = notes_[idx].next;

        notes_[idx].num  = kEmpty;
        notes_[idx].next = kUnlinked;
        notes_[idx].prev = kUnlinked;

        for(uint8_t i = 0; i + 1 < size_; i++)
            if(order_[i] == idx)
            {
                for(uint8_t j = i; j + 1 < size_ - 1; j++) order_[j] = order_[j + 1];
                break;
            }

        size_--;
        if(size_ <= 1) played_idx_ = 0;
    }

    uint8_t NextNoteIdx()
    {
        if(as_played_)
        {
            // The first trigger after a Clear() starts on the first note that was
            // played, in both directions. Upstream leaves `_played_idx` where it
            // was and steps before reading, so a chord that arrives all at once —
            // which is exactly what the Weather Station's sun layer sends — starts
            // on its SECOND note. Upstream never notices because its pads arrive
            // one at a time and the clock starts on the first, where the step
            // happens to wrap back to zero.
            if(primed_)
            {
                primed_     = false;
                played_idx_ = 0;
                return order_[0];
            }
            played_idx_++;
            if(played_idx_ >= size_ - 1) played_idx_ = 0;
            return order_[played_idx_];
        }
        const uint8_t idx = notes_[current_idx_].next;
        return (idx == 0) ? notes_[idx].next : idx;   // step over the sentinel
    }

    uint8_t PrevNoteIdx()
    {
        if(as_played_)
        {
            if(primed_)
            {
                primed_     = false;
                played_idx_ = 0;
                return order_[0];
            }
            played_idx_ = played_idx_ == 0 ? size_ - 2 : played_idx_ - 1;
            return order_[played_idx_];
        }
        const uint8_t idx = notes_[current_idx_].prev;
        return (idx == 0) ? notes_[idx].prev : idx;
    }

    std::function<void(uint8_t, uint8_t)> on_note_on_;
    std::function<void(uint8_t)>          on_note_off_;

    Note    notes_[note_count + 1];
    uint8_t order_[note_count];

    ArpDirection                           dir_ = ArpDirection::Fwd;
    std::uniform_int_distribution<uint8_t> dice_;
    std::default_random_engine             rng_;

    uint8_t rand_chance_   = 0;
    uint8_t played_idx_    = 0;
    bool    as_played_     = true;
    uint8_t bottom_idx_    = 0;
    uint8_t current_idx_   = 0;
    uint8_t pulse_counter_ = 0;
    uint8_t size_          = 0;
    bool    primed_        = true;
};

} // namespace tspod
