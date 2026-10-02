#include "engine.h"
#include <math.h>
#include <functional>

namespace tspod {

Engine::Engine() : trigger_(kPPQN), rng_(0xC0FFEE), dice_(0, 100) {}

void Engine::Init(float sample_rate, float block_size)
{
    using namespace std::placeholders;

    clock_.Init(1e6f * block_size / sample_rate, kPPQNExtern, kPPQN);
    clock_.SetOnTick(std::bind(&Engine::OnClockTick, this));

    latch_.SetOnNoteOn(std::bind(&Engine::OnNoteFromLatch, this, _1));
    latch_.SetOnNoteOff(std::bind(&Engine::OffNoteFromLatch, this, _1));

    arp_.SetOnNoteOn(std::bind(&Engine::OnArpNote, this, _1, _2));
    arp_.SetOnNoteOff([](uint8_t) {});   // a pluck decays on its own
    arp_.SetDirection(ArpDirection::Fwd);
    arp_.SetRandChance(0);
    arp_.SetAsPlayed(true);

    voices_.Init(sample_rate);
    drive_.Init();
    space_.Init(sample_rate);

    SetDrive(0.0f);
    SetDensity(1.0f);
    SetShift(0.0f);
    SetReverb(0.0f);
    SetTempo(0.45f);
}

// ── Notes ───────────────────────────────────────────────────────────────────

void Engine::NoteOn(uint8_t note)
{
#if TS_QUANTIZE_MIDI_NOTES
    note = scale_.Quantize(note);
#endif
    latch_.NoteOn(note);
    StartOrStop();
}

void Engine::NoteOff(uint8_t note)
{
#if TS_QUANTIZE_MIDI_NOTES
    note = scale_.Quantize(note);
#endif
    latch_.NoteOff(note);
    if(arp_on_) StartOrStop();
}

// The encoder pad. It ALWAYS maintains the held set, in every mode.
//
// It used to pluck once and hold nothing when the arp was off, which is the
// boot state — so the very first thing anyone tries is pressing the encoder,
// and the answer was one quiet blip and no note. Reported, fairly, as "pressing
// the encoder doesn't add any notes"; the diagnostic log showed three toggles,
// no panics and zero notes held, which is exactly that behaviour working as
// written and being wrong.
//
// Now the chord you build survives turning the arp on, which is the order
// anyone would reach for it in.
void Engine::ToggleNote(uint8_t note)
{
    latch_.Toggle(note);
    StartOrStop();
}

// Silent: the note moves, nothing is struck. Going through ToggleNote instead
// would pluck every note of the chord on a scale change with the arp off, which
// is a machine-gun rather than a transposition.
void Engine::RetuneHeldNote(uint8_t from, uint8_t to)
{
    if(from == to) return;
    if(latch_.IsHeld(from)) latch_.Toggle(from);
    if(!latch_.IsHeld(to)) latch_.Toggle(to);
    if(arp_on_) StartOrStop();
}

// Turning the arp on has to pick up whatever is already held, and turning it off
// has to stop the clock WITHOUT dropping the chord — ResetSequence() clears the
// arp, which would throw away notes the player put there deliberately.
void Engine::SetArpOn(bool on)
{
    if(arp_on_ == on) return;
    arp_on_ = on;
    // Whichever regime we were in, let its voices go: the two sound the held set
    // in completely different ways.
    voices_.ReleaseAll();
    last_arp_note_ = kNoNote;
    if(on)
    {
        StartOrStop();
    }
    else
    {
        StopSequence();
        SoundHeldNotes();
    }
}

void Engine::SetExciter(Exciter e)
{
    if(e >= Exciter::kCount || e == exciter_) return;
    exciter_       = e;
    last_arp_note_ = kNoNote;
    voices_.SetExciter(e);
    // A bow is a state, so switching into it with notes already held has to
    // start them sounding; switching out leaves nothing behind.
    if(!arp_on_) SoundHeldNotes();
}

void Engine::AllNotesOff()
{
    latch_.Clear();
    ResetSequence();
    voices_.Kill();
    last_arp_note_ = kNoNote;
}

void Engine::SetLatch(bool on)
{
    latch_.SetOn(on);
    if(!arp_.HasNote()) ResetSequence();
}

void Engine::OnNoteFromLatch(uint8_t note)
{
    arp_.NoteOn(note, 127);
    // Nothing is sequencing, so this note has to sound now. With the arp running
    // the arp decides when each note speaks.
    if(!arp_on_) StartVoice(note);
}

void Engine::OffNoteFromLatch(uint8_t note)
{
    arp_.NoteOff(note);
    if(!arp_on_) voices_.Release(note);
}

// Start the clock when there is something to play, stop it when there is not.
void Engine::StartOrStop()
{
    // With the arp off there is nothing to run, and running it anyway is not
    // silent: the clock ticks, the arp triggers, and the held note is re-struck
    // every sixteenth. That read as a pluck which never decayed.
    if(!arp_on_)
    {
        StopSequence();
        return;
    }
    if(arp_.HasNote())
    {
        if(!clock_.IsRunning()) clock_.Run();
    }
    else
    {
        ResetSequence();
    }
}

// Stop the clock and rewind the pattern, but keep the held notes.
void Engine::StopSequence()
{
    clock_.Stop();
    trigger_.Reset();
    pattern_.Reset();
}

void Engine::ResetSequence()
{
    clock_.Stop();
    trigger_.Reset();
    pattern_.Reset();
    arp_.Clear();
}

// ── Sequencing ──────────────────────────────────────────────────────────────

void Engine::OnClockTick()
{
    if(trigger_.Tick() && pattern_.Tick()) arp_.Trigger();
}

void Engine::OnArpNote(uint8_t note, uint8_t)
{
    // Bowing, each step lifts the bow on the one before it, so the steps overlap
    // and ring down into each other instead of piling up into a drone.
    if(exciter_ == Exciter::Bow && last_arp_note_ != kNoNote)
        voices_.Release(last_arp_note_);
    const uint8_t sounded = HumanizedNote(note);
    last_arp_note_        = sounded;
    StartVoice(sounded);
}

void Engine::Pluck(uint8_t note, bool humanize_pitch)
{
    StartVoice(humanize_pitch ? HumanizedNote(note) : note);
}

// Give `note` a voice and begin sounding it. The pool strikes it when plucking
// and draws the bow when bowing, so this is the one place a note becomes sound.
void Engine::StartVoice(uint8_t note)
{
    ApplyStringHumanize();
    voices_.Start(note, scale_.Freq(note));
    plucked_ = true;
#if TS_DEBUG
    pluck_count_++;
#endif
}

// Only meaningful while bowing: a pluck is an event rather than a state, so
// turning the arp off must not strum the whole held chord.
void Engine::SoundHeldNotes()
{
    if(exciter_ != Exciter::Bow) return;
    for(int n = 0; n < 128; ++n)
        if(latch_.IsHeld(static_cast<uint8_t>(n)))
            StartVoice(static_cast<uint8_t>(n));
}

// ── The humanizer ───────────────────────────────────────────────────────────

// Upstream's `humanized_note`, on note numbers instead of frequencies — the
// octave dice become ±12 rather than ×2 and ÷2, which is the same thing and
// composes with transposition instead of fighting it.
//
// One fix: upstream's middle branch can fall off the end of the function without
// returning. If the chance dice hits and the octave dice lands between 25 and 75
// there is no return statement on that path at all, which is undefined
// behaviour — in practice whatever was in the return register. Rosa's libDaisy
// re-port found and fixed the same thing.
uint8_t Engine::HumanizedNote(uint8_t note)
{
    if(note_chance_ <= 2) return note;

    const uint8_t chance_dice = dice_(rng_);
    const uint8_t note_dice   = dice_(rng_);
    const uint8_t octave_dice = dice_(rng_);

    auto oct = [](int n, int d) {
        const int v = n + d;
        return static_cast<uint8_t>(v < 0 ? 0 : (v > 127 ? 127 : v));
    };

    if(note_chance_ < 33)
    {
        // Gentle: the right note, sometimes in the wrong octave.
        if(chance_dice < note_chance_) return oct(note, octave_dice < 50 ? -12 : 12);
        return note;
    }
    if(note_chance_ < 66)
    {
        // Half the time a different note of the scale, and an octave either way.
        if(chance_dice < note_chance_)
        {
            uint8_t n = (note_dice < 50) ? scale_.RandomNote() : note;
            if(octave_dice < 25) return oct(n, -12);
            if(octave_dice > 75) return oct(n, 12);
            return n;
        }
        return note;
    }
    // Wide open: mostly a different note, and 40% of them displaced.
    uint8_t n = (note_dice < note_chance_) ? scale_.RandomNote() : note;
    if(octave_dice < 20) return oct(n, 12);
    if(octave_dice > 80) return oct(n, -12);
    return n;
}

// Upstream mutates the live brightness/structure/damping values here and relies
// on the control loop overwriting them 250 times a second to undo it. At its
// control rate the two are the same thing, but only because a pluck can never
// land twice between control passes. This computes an offset instead, so the
// base values are whatever the panel says and nothing can drift.
void Engine::ApplyStringHumanize()
{
    float b = brightness_, s = structure_, d = damping_;

    if(string_chance_ > 2 && dice_(rng_) < string_chance_)
    {
        b += static_cast<float>(dice_(rng_)) * 0.002f;   // 0 .. +0.2
        s += static_cast<float>(dice_(rng_)) * 0.002f;
        d += static_cast<float>(dice_(rng_)) * 0.004f;   // 0 .. +0.4
        if(b > 1.0f) b = 1.0f;
        if(s > 1.0f) s = 1.0f;
        if(d > 1.0f) d = 1.0f;
    }

    voices_.SetBrightness(b);
    voices_.SetStructure(s);
    voices_.SetDamping(d);
}

// ── Controls ────────────────────────────────────────────────────────────────

void Engine::SetDrive(float v)
{
    v = Clamp(v);
    drive_.SetDrive(0.2f + v * 0.4f);
    // Upstream's volume compensation: drive makes it louder, so take it back
    // out, squared. Turning drive up is meant to change the tone, not the level.
    volume_ = 1.0f - v * 0.6f;
    volume_ *= volume_;
}

// Upstream spends two of its eight knobs on randomisation — one for the notes,
// one for the string. There are not eight knobs here, and driving both from one
// control in step is wrong: by halfway up you would already be throwing wrong
// notes, which is a much louder effect than a wobbling timbre.
//
// So the control is staged. The first half brings in the string variation on its
// own, which only ever changes what a pluck sounds like. The second half starts
// the notes moving on top of it.
void Engine::SetChance(float v)
{
    v                = Clamp(v);
    const float str  = v * 2.0f > 1.0f ? 1.0f : v * 2.0f;
    const float note = v * 2.0f - 1.0f < 0.0f ? 0.0f : v * 2.0f - 1.0f;
    string_chance_   = static_cast<uint8_t>(str * 100.0f);
    note_chance_     = static_cast<uint8_t>(note * 100.0f);
}

uint16_t Engine::HeldCount() const
{
    uint16_t n = 0;
    for(int i = 0; i < 128; ++i)
        if(latch_.IsHeld(static_cast<uint8_t>(i))) n++;
    return n;
}

bool Engine::TakePluck()
{
    const bool p = plucked_;
    plucked_     = false;
    return p;
}

// ── Audio ───────────────────────────────────────────────────────────────────

void Engine::Process(float* out_l, float* out_r, size_t size)
{
    clock_.Tick();
    space_.Tick(size);

    float peak = 0.0f;
    for(size_t i = 0; i < size; i++)
    {
        const float raw = voices_.Process();
        const float dry = drive_.Process(raw) * volume_;

        // Upstream feeds the reverb through the crossfade and adds the dry
        // signal back at full level — so the "mix" is really a send, and the
        // crossfade's dry coefficient is never used. Kept as it is: it is what
        // the instrument sounds like.
        float wl = 0.0f, wr = 0.0f;
        space_.Process(dry * xfade_.Wet(), wl, wr);

        const float l = daisysp::SoftLimit((dry + wl) * 0.75f);
        const float r = daisysp::SoftLimit((dry + wr) * 0.75f);

        out_l[i] = l;
        out_r[i] = r;

        const float a = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
        if(a > peak) peak = a;

#if TS_DEBUG
        auto hold = [](float& slot, float v) {
            v = fabsf(v);
            if(!(v <= 1e9f)) v = 1e9f;   // catches NaN too: NaN fails every test
            if(v > slot) slot = v;
        };
        hold(stages_.vox, raw);
        hold(stages_.dry, dry);
        hold(stages_.wet, wl);
        hold(stages_.out, l);
#endif
    }
    peak_ = peak;
}

} // namespace tspod
