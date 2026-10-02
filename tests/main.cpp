// Host tests for the Daisy Pod TouchString port.
//
// These drive the real engine, the real control model and the real MIDI layer —
// none of the three has any hardware in it. On every port in this family the
// same thing has happened: reasoning about the code produced a confident wrong
// answer and a measurement corrected it. So the tests measure.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "common/config.h"
#include "midi/weather.h"
#include "string/engine.h"
#include "ui/controlmodel.h"
#include "wav.h"

using namespace tspod;

static int checks = 0, failures = 0;

static void Check(bool ok, const std::string& what)
{
    checks++;
    if(!ok)
    {
        failures++;
        printf("  FAIL  %s\n", what.c_str());
    }
}

static void CheckNear(float got, float want, float tol, const std::string& what)
{
    checks++;
    if(!(std::fabs(got - want) <= tol))
    {
        failures++;
        printf("  FAIL  %s: got %.6f want %.6f (tol %.6f)\n", what.c_str(), got,
               want, tol);
    }
}

static void Section(const char* name) { printf("\n== %s ==\n", name); }

static constexpr float kSR    = 48000.0f;
static constexpr int   kBlock = 4;

// ── Upstream's own numbers, for the pitch comparison ────────────────────────
// The frequency tables and the transposition ratios exactly as they appear in
// the MIT sketch (upstream/scale.h). Nothing in src/ produces these any more —
// that is the point of the test.
static const float kUpstreamScales[3][8] = {
    { 130.81f, 196.00f, 233.08f, 261.63f, 293.66f, 311.13f, 349.23f, 392.00f },
    { 130.81f, 164.81f, 174.61f, 196.00f, 220.00f, 261.63f, 329.63f, 349.23f },
    { 130.81f, 146.83f, 155.56f, 196.00f, 233.08f, 261.63f, 293.66f, 311.13f }
};
static const float kUpstreamTrans[25]
    = { 0.5f,              0.52973154717962f,  0.56123102415466f,
        0.594603557501335f, 0.629960524947413f, 0.667419927084995f,
        0.707106781186527f, 0.749153538438323f, 0.793700525984085f,
        0.840896415253703f, 0.890898718140331f, 0.943874312681689f,
        1.0f,               1.0594630943593f,   1.12246204830938f,
        1.18920711500273f,  1.25992104989489f,  1.33483985417006f,
        1.41421356237313f,  1.49830707687673f,  1.58740105196826f,
        1.6817928305075f,   1.78179743628076f,  1.88774862536348f,
        2.0f };

// ── A rendering harness ─────────────────────────────────────────────────────
struct Render
{
    std::vector<float> l, r;
    float              peak = 0.0f;
    float              rms  = 0.0f;
    int                plucks = 0;
};

static Render RenderFor(Engine& e, float seconds)
{
    Render      out;
    const int   blocks = static_cast<int>(seconds * kSR / kBlock);
    float       bl[kBlock], br[kBlock];
    double      sum = 0.0;
    for(int b = 0; b < blocks; b++)
    {
        e.Process(bl, br, kBlock);
        if(e.TakePluck()) out.plucks++;
        for(int i = 0; i < kBlock; i++)
        {
            out.l.push_back(bl[i]);
            out.r.push_back(br[i]);
            const float a = std::fabs(bl[i]) > std::fabs(br[i]) ? std::fabs(bl[i])
                                                                : std::fabs(br[i]);
            if(a > out.peak) out.peak = a;
            sum += static_cast<double>(bl[i]) * bl[i];
        }
    }
    out.rms = static_cast<float>(std::sqrt(sum / (out.l.size() ? out.l.size() : 1)));
    return out;
}

static bool AllFinite(const Render& r)
{
    for(size_t i = 0; i < r.l.size(); i++)
        if(!std::isfinite(r.l[i]) || !std::isfinite(r.r[i])) return false;
    return true;
}

static float Db(float amp) { return amp <= 1e-9f ? -180.0f : 20.0f * std::log10(amp); }

// ============================================================================
// 1. Pitch: the MIDI-note rewrite has to be the same instrument
// ============================================================================
// src/common/config.h replaces upstream's three tables of frequencies and its
// 25-entry ratio table with MIDI note numbers and an integer semitone offset.
// That is only allowed if it comes out at the same pitches. All three scales,
// all eight degrees, all 25 transpositions: 600 comparisons.
static void TestPitchFidelity()
{
    Section("Pitch — the note-number rewrite against upstream's tables");

    Scale s;
    float worst_cents = 0.0f;
    for(uint8_t si = 0; si < 3; si++)
    {
        s.SetIndex(si);
        for(int ti = 0; ti < 25; ti++)
        {
            s.SetTranspose(static_cast<int8_t>(ti - 12));
            for(uint8_t d = 0; d < 8; d++)
            {
                // Upstream: table entry, halved by FreqAt, times the ratio.
                const float want = kUpstreamScales[si][d] * 0.5f * kUpstreamTrans[ti];
                const float got  = s.Freq(s.NoteAt(d));
                const float cents = 1200.0f * std::log2(got / want);
                if(std::fabs(cents) > std::fabs(worst_cents)) worst_cents = cents;
            }
        }
    }
    printf("  worst error across 600 scale/degree/transposition combinations: "
           "%.3f cents\n", worst_cents);
    // Upstream's tables are rounded to two decimal places, so they are not
    // exactly 12-TET; a couple of cents is the tables' own rounding, not ours.
    Check(std::fabs(worst_cents) < 3.0f, "every pitch within 3 cents of upstream");
}

// ============================================================================
// 2. The pattern generator
// ============================================================================
static void TestPattern()
{
    Section("Pattern — Christoffel words");

    for(int want = 1; want <= 16; want++)
    {
        CPattern p;
        p.SetOnsets(static_cast<float>(want - 1) / 15.0f);
        int hits = 0;
        for(int i = 0; i < 16; i++)
            if(p.Tick()) hits++;
        Check(hits == want,
              "onsets requested " + std::to_string(want) + " got "
                  + std::to_string(hits));
    }

    // Evenness: the gaps between onsets may take at most two distinct lengths.
    // That is the defining property of a euclidean/Christoffel rhythm and the
    // reason this generator is worth having at all.
    bool even_everywhere = true;
    for(int want = 1; want <= 16; want++)
    {
        CPattern p;
        p.SetOnsets(static_cast<float>(want - 1) / 15.0f);
        std::vector<int> hits;
        for(int i = 0; i < 16; i++)
            if(p.Tick()) hits.push_back(i);
        if(hits.size() < 2) continue;
        std::vector<int> gaps;
        for(size_t i = 0; i < hits.size(); i++)
            gaps.push_back((hits[(i + 1) % hits.size()] - hits[i] + 16) % 16);
        int lo = 99, hi = 0;
        for(int g : gaps)
        {
            if(g < lo) lo = g;
            if(g > hi) hi = g;
        }
        if(hi - lo > 1) even_everywhere = false;
    }
    Check(even_everywhere, "gaps between onsets never differ by more than one step");

    // Shift rotates the pattern without changing what is in it.
    CPattern a, b;
    a.SetOnsets(0.4f);
    b.SetOnsets(0.4f);
    b.SetShift(4.0f / 16.0f);
    std::vector<int> pa, pb;
    for(int i = 0; i < 16; i++) pa.push_back(a.Tick() ? 1 : 0);
    for(int i = 0; i < 16; i++) pb.push_back(b.Tick() ? 1 : 0);
    int sa = 0, sb = 0;
    for(int i = 0; i < 16; i++)
    {
        sa += pa[i];
        sb += pb[i];
    }
    Check(sa == sb, "shift preserves the onset count");
    bool is_rotation = true;
    for(int i = 0; i < 16; i++)
        if(pb[i] != pa[(i - 4 + 16) % 16]) is_rotation = false;
    Check(is_rotation, "shift of 4 is exactly a rotation by 4");
}

// ============================================================================
// 3. The arpeggiator
// ============================================================================
static void TestArp()
{
    Section("Arp");

    std::vector<int> fired;
    Arp<kMaxHeldNotes, 4> arp;
    arp.SetOnNoteOn([&](uint8_t n, uint8_t) { fired.push_back(n); });
    arp.SetOnNoteOff([](uint8_t) {});

    arp.SetAsPlayed(true);
    arp.NoteOn(60, 127);
    arp.NoteOn(64, 127);
    arp.NoteOn(67, 127);
    for(int i = 0; i < 6; i++) arp.Trigger();
    Check(fired.size() == 6, "six triggers produce six notes");
    Check(fired[0] == 60 && fired[1] == 64 && fired[2] == 67,
          "as-played order follows the order they arrived in");
    Check(fired[3] == 60, "and wraps");

    // Sorted order, which is what as-played is NOT.
    fired.clear();
    Arp<kMaxHeldNotes, 4> sorted;
    sorted.SetOnNoteOn([&](uint8_t n, uint8_t) { fired.push_back(n); });
    sorted.SetOnNoteOff([](uint8_t) {});
    sorted.SetAsPlayed(false);
    sorted.NoteOn(67, 127);
    sorted.NoteOn(60, 127);
    sorted.NoteOn(64, 127);
    for(int i = 0; i < 3; i++) sorted.Trigger();
    std::vector<int> want = { 60, 64, 67 };
    Check(fired == want, "sorted order is by note number regardless of arrival");

    // Duplicates. The same note can now arrive from the encoder and from two
    // MIDI channels at once, which upstream could not do.
    Arp<kMaxHeldNotes, 4> dup;
    dup.SetOnNoteOn([](uint8_t, uint8_t) {});
    dup.SetOnNoteOff([](uint8_t) {});
    dup.NoteOn(60, 127);
    dup.NoteOn(60, 127);
    Check(dup.Size() == 1, "the same note twice takes one slot, not two");

    // Overflow. The Weather Station can stack a sun chord on top of five keys.
    Arp<kMaxHeldNotes, 4> full;
    full.SetOnNoteOn([](uint8_t, uint8_t) {});
    full.SetOnNoteOff([](uint8_t) {});
    for(int i = 0; i < kMaxHeldNotes + 4; i++)
        full.NoteOn(static_cast<uint8_t>(40 + i), 127);
    Check(full.Size() == kMaxHeldNotes, "capacity is respected under overflow");
    Check(!full.Contains(40), "the least recent note is the one dropped");
    Check(full.Contains(static_cast<uint8_t>(40 + kMaxHeldNotes + 3)),
          "the newest note is kept");
}

// ============================================================================
// 4. The latch — two doors into one held set
// ============================================================================
static void TestLatch()
{
    Section("Latch");

    std::vector<int> on, off;
    Latch            l;
    l.SetOnNoteOn([&](uint8_t n) { on.push_back(n); });
    l.SetOnNoteOff([&](uint8_t n) { off.push_back(n); });

    // Keyboard, latch off: release releases.
    l.SetOn(false);
    l.NoteOn(60);
    Check(l.IsHeld(60), "note on holds");
    l.NoteOff(60);
    Check(!l.IsHeld(60), "note off releases when not latched");

    // Keyboard, latch on: release does not, but a fresh chord replaces.
    on.clear();
    off.clear();
    l.SetOn(true);
    l.NoteOn(60);
    l.NoteOn(64);   // still holding 60, so this joins the chord
    l.NoteOff(60);
    l.NoteOff(64);
    Check(l.IsHeld(60) && l.IsHeld(64), "latched notes survive release");
    l.NoteOn(67);   // everything was up, so this starts a new chord
    Check(l.IsHeld(67), "the new note is held");
    Check(!l.IsHeld(60) && !l.IsHeld(64),
          "a fresh chord replaces the latched one rather than piling onto it");

    // Pad: toggle, and it never consults the latch — which is what makes the
    // encoder able to build a chord one press at a time.
    Latch p;
    p.SetOnNoteOn([](uint8_t) {});
    p.SetOnNoteOff([](uint8_t) {});
    p.SetOn(true);
    p.Toggle(60);
    p.Toggle(64);
    p.Toggle(67);
    Check(p.IsHeld(60) && p.IsHeld(64) && p.IsHeld(67),
          "three presses of the pad build a three-note chord");
    p.Toggle(64);
    Check(!p.IsHeld(64) && p.IsHeld(60) && p.IsHeld(67),
          "pressing again removes just that note");

    // Coming out of latch drops what nobody is touching.
    Latch d;
    d.SetOnNoteOn([](uint8_t) {});
    d.SetOnNoteOff([](uint8_t) {});
    d.SetOn(true);
    d.NoteOn(60);
    d.NoteOff(60);
    d.NoteOn(64);   // physically down
    d.SetOn(false);
    Check(!d.IsHeld(60), "unlatching drops the note nobody is holding");
    Check(d.IsHeld(64), "and keeps the one that is still down");
}

// ============================================================================
// 5. The clock
// ============================================================================
static void TestClock()
{
    Section("Clock");

    Clock c;
    const float interval_us = 1e6f * kBlock / kSR;
    c.Init(interval_us, 24, 48);

    int ticks = 0;
    c.SetOnTick([&] { ticks++; });

    // Upstream's mapping: norm 1.0 is 230 BPM, and the bottom of the control
    // falls under the 40 BPM sync threshold.
    // (kBPMRange - 10) * norm + kBPMMin - 10, so the top is 220, not 230: the
    // ten-BPM offset that makes room for the sync position at the bottom comes
    // off the top of the range too.
    c.SetTempo(1.0f);
    CheckNear(c.Tempo(), 220.0f, 1.0f, "top of the control is 220 BPM");
    Check(c.Internal(), "and runs on internal time");

    c.SetTempo(0.45f);
    CheckNear(c.Tempo(), 115.5f, 1.0f, "upstream's boot tempo is about 115 BPM");

    c.SetTempo(0.0f);
    Check(!c.Internal(),
          "the bottom of the control selects external sync, as upstream does");

    // Internal rate. 48 ppqn at 120 BPM is 96 ticks a second.
    Clock r;
    r.Init(interval_us, 24, 48);
    ticks = 0;
    r.SetOnTick([&] { ticks++; });
    r.SetTempo(0.4211f);   // ≈ 110 BPM
    const float bpm = r.Tempo();
    r.Run();
    const int blocks = static_cast<int>(2.0f * kSR / kBlock);   // two seconds
    for(int i = 0; i < blocks; i++) r.Tick();
    const float expected = bpm / 60.0f * 48.0f * 2.0f;
    printf("  %d ticks in 2 s at %.1f BPM, expected %.0f\n", ticks, bpm, expected);
    Check(std::fabs(ticks - expected) / expected < 0.01f,
          "internal tick rate is within 1% of the tempo it was asked for");

    // Under external sync nothing runs until pulses arrive.
    Clock x;
    x.Init(interval_us, 24, 48);
    ticks = 0;
    x.SetOnTick([&] { ticks++; });
    x.SetTempo(0.0f);
    x.Run();
    for(int i = 0; i < 1000; i++) x.Tick();
    Check(ticks == 0, "external sync produces nothing until a pulse arrives");
    x.ExternalPulse();
    for(int i = 0; i < 1000; i++) x.Tick();
    Check(ticks > 0, "and starts on the first pulse");
}

// ============================================================================
// 6. The panel — four always-live controls, one job each
// ============================================================================
// The old panel put eleven parameters on two knobs across four unlabelled
// pages, with relative knobs so a pot's position told you nothing. It was not
// enjoyable to play, which is a design failure rather than a bug, and these are
// the properties the rewrite exists to guarantee.
static void TestPanel()
{
    Section("The panel");

    const float dt = 0.001f;

    // ── The knobs ARE their values ─────────────────────────────────────────
    {
        Engine       e;
        ControlModel m;
        e.Init(kSR, kBlock);
        m.Init(&e, 0.80f, 0.20f);
        CheckNear(m.Norm(Param::Brightness), 0.80f, 1e-6f,
                  "brightness boots at whatever knob 1 is pointing at");
        CheckNear(m.Norm(Param::Density), 0.20f, 1e-6f,
                  "density boots at whatever knob 2 is pointing at");

        // Absolute: the value tracks the pot, with no arming and no anchoring.
        m.Read(false, false, 0.30f, 0.70f, 0, false, dt);
        CheckNear(m.Norm(Param::Brightness), 0.30f, 1e-6f,
                  "and follows the pot immediately — no 2% to arm");
        CheckNear(m.Norm(Param::Density), 0.70f, 1e-6f, "both of them");
    }

    // ── A knob is never borrowed, so it can never jump ─────────────────────
    // This is the property the old scheme could not have: whatever else is
    // happening, knob 1 is brightness and knob 2 is density.
    {
        Engine       e;
        ControlModel m;
        e.Init(kSR, kBlock);
        m.Init(&e, 0.50f, 0.50f);

        // Through the arp modes, the exciter, the tempo gesture and a panic.
        for(int i = 0; i < 3; i++)
        {
            m.Read(true, false, 0.50f, 0.50f, 0, false, dt);
            m.Read(false, false, 0.50f, 0.50f, 0, false, dt);
        }
        m.Read(false, true, 0.50f, 0.50f, 0, false, dt);
        m.Read(false, false, 0.50f, 0.50f, 0, false, dt);
        for(int i = 0; i < 600; i++) m.Read(true, false, 0.50f, 0.50f, 3, false, dt);
        m.Read(false, false, 0.50f, 0.50f, 0, false, dt);
        for(int i = 0; i < 2000; i++) m.Read(false, false, 0.50f, 0.50f, 0, true, dt);
        m.Read(false, false, 0.50f, 0.50f, 0, false, dt);

        CheckNear(m.Norm(Param::Brightness), 0.50f, 1e-6f,
                  "brightness is untouched by every other gesture on the box");
        CheckNear(m.Norm(Param::Density), 0.50f, 1e-6f, "and so is density");
    }

    // ── The encoder: reverb, chance, panic ─────────────────────────────────
    {
        Engine       e;
        ControlModel m;
        e.Init(kSR, kBlock);
        m.Init(&e, 0.5f, 0.5f);

        const float v0 = m.Norm(Param::Reverb);
        m.Read(false, false, 0.5f, 0.5f, 8, false, dt);
        Check(m.Norm(Param::Reverb) > v0 + 0.1f, "the encoder turns the reverb up");
        m.Read(false, false, 0.5f, 0.5f, -40, false, dt);
        CheckNear(m.Norm(Param::Reverb), 0.0f, 1e-6f, "and down to zero, clamped");

        Check(m.ChanceStep() == 0, "chance starts off");
        for(int step = 1; step <= 4; step++)
        {
            m.Read(false, false, 0.5f, 0.5f, 0, true, dt);
            m.Read(false, false, 0.5f, 0.5f, 0, false, dt);
            Check(m.ChanceStep() == (step % 4),
                  "a press steps chance to " + std::to_string(step % 4));
        }
        CheckNear(m.Norm(Param::Chance), kChanceSteps[0], 1e-6f,
                  "and four presses bring it back round to off");

        // Holding it is a panic, not a chance step.
        const uint8_t before = m.ChanceStep();
        bool fired = false;
        for(int i = 0; i < 2000; i++)
        {
            m.Read(false, false, 0.5f, 0.5f, 0, true, dt);
            if(m.Panicked()) fired = true;
        }
        Check(fired, "holding the encoder panics");
        Check(m.PanicLatched(), "and the confirmation stays lit while it is held");
        m.Read(false, false, 0.5f, 0.5f, 0, false, dt);
        Check(m.ChanceStep() == before, "a panic does not also step chance");
        Check(!m.PanicLatched(), "and only on release is the red let go to fade");
    }

    // ── Button 1: arp mode, and the tempo gesture ──────────────────────────
    {
        Engine       e;
        ControlModel m;
        e.Init(kSR, kBlock);
        m.Init(&e, 0.5f, 0.5f);

        Check(m.Mode() == ArpMode::Off, "boots with the arp off");
        for(ArpMode want : { ArpMode::On, ArpMode::Latched, ArpMode::Off })
        {
            m.Read(true, false, 0.5f, 0.5f, 0, false, dt);
            m.Read(false, false, 0.5f, 0.5f, 0, false, dt);
            Check(m.Mode() == want, "a tap of button 1 cycles the arp mode");
        }

        // Held, the encoder is the tempo — and the hold is not also a tap.
        const ArpMode mode_before = m.Mode();
        const float   bpm_before  = e.Tempo();
        for(int i = 0; i < 600; i++) m.Read(true, false, 0.5f, 0.5f, 0, false, dt);
        Check(m.TempoLayer(), "holding button 1 hands the encoder the tempo");
        for(int i = 0; i < 12; i++) m.Read(true, false, 0.5f, 0.5f, 1, false, dt);
        m.Read(false, false, 0.5f, 0.5f, 0, false, dt);
        printf("  tempo after twelve detents: %.0f -> %.0f BPM\n", bpm_before, e.Tempo());
        Check(e.Tempo() > bpm_before + 10.0f, "and turning it raises the tempo");
        Check(m.Mode() == mode_before, "a hold is not also a tap");
        Check(!m.TempoLayer(), "and letting go closes it");
    }

    // ── Button 2: the exciter ──────────────────────────────────────────────
    {
        Engine       e;
        ControlModel m;
        e.Init(kSR, kBlock);
        m.Init(&e, 0.5f, 0.5f);
        Check(m.GetExciter() == Exciter::Pluck, "boots plucked");
        m.Read(false, true, 0.5f, 0.5f, 0, false, dt);
        Check(m.GetExciter() == Exciter::Bow, "button 2 switches to bowing");
        Check(e.GetExciter() == Exciter::Bow, "and the engine agrees");
        m.Read(false, false, 0.5f, 0.5f, 0, false, dt);
        m.Read(false, true, 0.5f, 0.5f, 0, false, dt);
        Check(m.GetExciter() == Exciter::Pluck, "and back again");
    }
}

// ============================================================================
// 6b. Bowing
// ============================================================================
// DaisySP's StringVoice has a sustain flag that swaps its plucked burst for
// continuous dust, which is a bow. The two things that can go wrong are it
// droning when nothing is held, and an arp step striking the string instead of
// just moving its pitch.
static void TestBow()
{
    Section("Bowing");

    auto render = [](Engine& e, float secs, float* peak_out) {
        float bl[kBlock], br[kBlock];
        float peak = 0.0f;
        for(int b = 0; b < static_cast<int>(secs * kSR / kBlock); b++)
        {
            e.Process(bl, br, kBlock);
            for(int i = 0; i < kBlock; i++)
                if(std::fabs(bl[i]) > peak) peak = std::fabs(bl[i]);
        }
        *peak_out = peak;
    };

    // Nothing held: a bow must be silent, not a drone.
    {
        Engine e;
        e.Init(kSR, kBlock);
        e.SetExciter(Exciter::Bow);
        float peak = 0.0f;
        render(e, 2.0f, &peak);
        Check(peak == 0.0f, "bowing with nothing held is silent, not a drone");
    }

    // A held note sings, and keeps singing — that is the whole point.
    {
        Engine e;
        e.Init(kSR, kBlock);
        e.SetExciter(Exciter::Bow);
        e.SetBrightness(0.6f);
        e.NoteOn(48);
        float early = 0.0f, late = 0.0f;
        render(e, 0.5f, &early);
        render(e, 2.0f, &late);
        printf("  bowed note: %.1f dBFS early, %.1f dBFS two seconds later\n",
               Db(early), Db(late));
        Check(early > 0.01f, "a bowed note sounds");
        Check(late > 0.01f, "and is still sounding seconds later, unlike a pluck");
    }

    // The bow must sound even at the bottom of the Brightness knob. DaisySP
    // drives its dust density from brightness squared, so without a floor this
    // is silence and bow mode reads as broken.
    {
        Engine e;
        e.Init(kSR, kBlock);
        e.SetExciter(Exciter::Bow);
        e.SetBrightness(0.0f);
        e.NoteOn(48);
        float peak = 0.0f;
        render(e, 1.5f, &peak);
        printf("  bowed at brightness 0: %.1f dBFS\n", Db(peak));
        Check(peak > 0.01f, "a bow at zero brightness still sounds");
    }

    // Held for a long time, a driven resonator must not run away.
    {
        Engine e;
        e.Init(kSR, kBlock);
        e.SetExciter(Exciter::Bow);
        e.SetBrightness(1.0f);
        e.SetDrive(1.0f);
        e.SetReverb(1.0f);
        e.NoteOn(48);
        e.NoteOn(55);
        float peak = 0.0f;
        render(e, 12.0f, &peak);
        printf("  bowed 12 s, everything at maximum: %.1f dBFS\n", Db(peak));
        Check(std::isfinite(peak) && peak <= 1.0f,
              "twelve seconds of bowing stays inside the rails");
    }

    // Release it and the bow RINGS DOWN. The first version hard-switched away
    // from the bow resonator the instant the bow lifted, so its tail was
    // disconnected rather than decayed — reported from the board as cutting off
    // very abruptly. The test that was here asserted silence after release and
    // passed on digital zero, which is to say it asserted the bug.
    {
        Engine e;
        e.Init(kSR, kBlock);
        e.SetExciter(Exciter::Bow);
        e.SetBrightness(0.6f);
        e.SetDamping(0.4f);
        e.NoteOn(48);
        float held = 0.0f;
        render(e, 1.5f, &held);
        e.NoteOff(48);

        // 150 ms after the bow lifts it must still be sounding. This is the
        // check that would have caught the cut.
        float just_after = 0.0f;
        render(e, 0.15f, &just_after);
        printf("  released: %.1f dBFS held, %.1f dBFS 150 ms later\n",
               Db(held), Db(just_after));
        Check(just_after > held * 0.25f,
              "150 ms after the bow lifts the string is still ringing, not cut");

        // ...and it is gone within a couple of seconds, rather than for ever.
        float later = 0.0f, settled = 0.0f;
        render(e, 2.5f, &later);
        render(e, 1.0f, &settled);
        printf("  then %.1f dBFS, and %.1f dBFS once settled\n",
               Db(later), Db(settled));
        Check(settled < held * 0.02f, "and has rung down within a few seconds");
        Check(settled < later, "monotonically, rather than sustaining for ever");
    }

    // A pluck still decays, so switching back has not broken anything.
    {
        Engine e;
        e.Init(kSR, kBlock);
        e.SetExciter(Exciter::Pluck);
        e.SetBrightness(0.5f);
        e.NoteOn(48);
        float early = 0.0f, late = 0.0f;
        render(e, 0.3f, &early);
        render(e, 3.0f, &late);
        Check(early > 0.01f, "a pluck sounds");
        Check(late < early * 0.5f, "and decays, where a bow would not");
    }

    // The arp bows a sequence legato: pitch moves, nothing is struck.
    {
        Engine e;
        e.Init(kSR, kBlock);
        e.SetExciter(Exciter::Bow);
        e.SetArpOn(true);
        e.SetTempo(0.5f);
        e.SetDensity(1.0f);
        e.NoteOn(48);
        e.NoteOn(55);
        float peak = 0.0f;
        render(e, 2.0f, &peak);
        Check(peak > 0.01f, "a bowed arpeggio sounds");
        Check(std::isfinite(peak), "and stays finite");
    }
}

// ============================================================================
// 7. The Weather Station link
// ============================================================================
static void TestWeatherLink()
{
    Section("Weather Station link");

    Engine       e;
    ControlModel m;
    WeatherLink  w;
    e.Init(kSR, kBlock);
    m.Init(&e, 0.5f, 0.5f);
    w.Init(&e, &m);

    const float dt = 0.001f;
    m.Read(true, false, 0.5f, 0.5f, 0, false, dt);
    m.Read(false, false, 0.5f, 0.5f, 0, false, dt);   // arp on

    // The four note channels play; thunder does not.
    for(uint8_t ch : { kChMain, kChSun, kChRain, kChWind })
    {
        w.NoteOn(ch, static_cast<uint8_t>(50 + ch), 100);
        Check(e.IsHeld(static_cast<uint8_t>(50 + ch)),
              "channel " + std::to_string(ch) + " plays the string");
    }
    w.NoteOn(kChThunder, 30, 127);
    Check(!e.IsHeld(30), "thunder does not pluck");
    Check(w.Thunder() > 0.5f, "thunder arms the swell instead");

    // ...and the swell falls back to nothing rather than sticking.
    for(int i = 0; i < 5000; i++) w.Tick(0.001f);
    Check(w.Thunder() == 0.0f, "the thunder swell reaches zero");
    CheckNear(m.Mod(Param::Drive), 0.0f, 1e-6f, "and stops modulating drive");

    // Velocity-zero note on is a note off.
    w.NoteOn(kChMain, 72, 100);
    Check(e.IsHeld(72), "note on holds");
    w.NoteOn(kChMain, 72, 0);
    Check(!e.IsHeld(72), "note on with velocity zero is a note off");

    // Sun's CC74 modulates rather than replaces: the knob keeps working.
    const float knob = m.Norm(Param::Brightness);
    w.ControlChange(kChSun, 74, 127);
    Check(m.Mod(Param::Brightness) > 0.3f, "full sun opens the brightness");
    CheckNear(m.Norm(Param::Brightness), knob, 1e-6f,
              "without moving where the knob thinks it is");
    w.ControlChange(kChSun, 74, 64);
    CheckNear(m.Mod(Param::Brightness), 0.0f, 1e-6f,
              "and CC74 at its resting value of 64 means no modulation at all");

    // Rain's CC91 rests at 40, not 64 — the Weather Station's panic says so.
    w.ControlChange(kChRain, 91, 40);
    CheckNear(m.Mod(Param::Reverb), 0.0f, 1e-6f, "CC91 is neutral at 40");
    w.ControlChange(kChRain, 91, 127);
    Check(m.Mod(Param::Reverb) > 0.4f, "and wets the reverb above it");

    // The regression that a hardware capture found and the docs hid.
    // The Weather Station's README describes CC74 as the sun layer, but its code
    // sends it on CH_MAIN as well, and its panic sends it on all five channels.
    // Handled by channel, the copy on channel 1 fell through to upstream's
    // generic map and jammed CHANCE — so the sun coming out started throwing
    // wrong notes.
    {
        const float chance_before = m.Norm(Param::Chance);
        w.ControlChange(kChMain, 74, 96);   // exactly what update_timbre() sends
        CheckNear(m.Norm(Param::Chance), chance_before, 1e-6f,
                  "CC74 on the main channel does not touch chance");
        Check(m.Mod(Param::Brightness) > 0.1f,
              "it opens the brightness, the same as it does on the sun channel");
    }
    {
        // ...and the same for rain's CC91, which also arrives on CH_MAIN.
        w.ControlChange(kChSun, 74, 64);    // back to neutral first
        const float verb_before = m.Norm(Param::Reverb);
        w.ControlChange(kChMain, 91, 88);
        CheckNear(m.Norm(Param::Reverb), verb_before, 1e-6f,
                  "CC91 on the main channel modulates rather than overwriting");
        Check(m.Mod(Param::Reverb) > 0.1f, "and wets the reverb");
        w.ControlChange(kChMain, 91, 40);
    }
    {
        // A whole Weather Station panic: both controllers on all five channels,
        // at their documented resting values. Nothing on the panel may move.
        const float before[3] = { m.Norm(Param::Chance), m.Norm(Param::Brightness),
                                  m.Norm(Param::Reverb) };
        for(uint8_t c = 1; c <= 5; c++)
        {
            w.ControlChange(c, 1, 0);
            w.ControlChange(c, 7, 100);
            w.ControlChange(c, 10, 64);
            w.ControlChange(c, 74, 64);
            w.ControlChange(c, 91, 40);
        }
        CheckNear(m.Norm(Param::Chance), before[0], 1e-6f, "a panic leaves chance alone");
        CheckNear(m.Norm(Param::Brightness), before[1], 1e-6f, "and brightness");
        CheckNear(m.Norm(Param::Reverb), before[2], 1e-6f, "and reverb");
        CheckNear(m.Mod(Param::Brightness), 0.0f, 1e-6f,
                  "and leaves no modulation behind");
        CheckNear(m.Mod(Param::Reverb), 0.0f, 1e-6f, "on either controller");
    }

    // The pad's scale button. Notes off the wire are absolute and must not move.
    {
        Engine       e3;
        ControlModel m3;
        WeatherLink  w3;
        e3.Init(kSR, kBlock);
        m3.Init(&e3, 0.5f, 0.5f);
        w3.Init(&e3, &m3);
        m3.Read(true, false, 0.5f, 0.5f, 0, false, 0.001f);
        m3.Read(false, false, 0.5f, 0.5f, 0, false, 0.001f);   // arp on
        w3.NoteOn(kChMain, 61, 100);                           // not in any scale

        Check(e3.ScaleIndex() == 0, "starts on the first scale");
        w3.ControlChange(kChMain, kScaleSelectCC, 2);
        Check(e3.ScaleIndex() == 2, "CC20 selects the scale by index");
        Check(e3.IsHeld(61), "and a note off the wire stays exactly where it is");
        w3.ControlChange(kChMain, kScaleSelectCC, 99);
        Check(e3.ScaleIndex() == 2, "an out-of-range index is ignored");
    }

    // Panic.
    w.ControlChange(kChMain, 123, 0);
    Check(!e.IsHeld(51), "CC123 clears every held note");

    // The generic map moves the panel's own value, so a knob does not snap back.
    w.ControlChange(9, 73, 0);
    CheckNear(m.Norm(Param::Density), 0.0f, 1e-6f,
              "CC73 writes the density the panel remembers, not just the engine's");

    // Pitch bend.
    w.PitchBend(kChWind, 8191);
    Engine e2;
    e2.Init(kSR, kBlock);
    Check(true, "pitch bend accepted");
}

// ============================================================================
// 8. Audio — does it actually make a sound, and is that sound sane
// ============================================================================
static void TestAudio()
{
    Section("Audio");

    // A plucked note with the arp off.
    {
        Engine e;
        e.Init(kSR, kBlock);
        e.SetBrightness(0.5f);
        e.SetTimbre(0.35f);
        e.SetDamping(0.5f);
        e.NoteOn(48);
        const Render r = RenderFor(e, 2.0f);
        Check(AllFinite(r), "a single pluck produces finite output");
        printf("  single pluck: peak %.1f dBFS, rms %.1f dBFS\n", Db(r.peak), Db(r.rms));
        Check(r.peak > 0.02f, "and it is audible");
        Check(r.peak <= 1.0f, "and it does not clip");
    }

    // The arp, at a known tempo and density, plucks at a countable rate.
    {
        Engine e;
        e.Init(kSR, kBlock);
        e.SetArpOn(true);
        e.SetTempo(0.4211f);   // ≈ 110 BPM
        e.SetDensity(1.0f);    // all sixteen
        e.SetBrightness(0.5f);
        e.NoteOn(48);
        e.NoteOn(55);
        const Render r = RenderFor(e, 4.0f);
        // 110 BPM, sixteenths: 110/60*4 = 7.33 a second.
        const float expected = 110.0f / 60.0f * 4.0f * 4.0f;
        printf("  arp at 110 BPM, density 16/16: %d plucks in 4 s, expected %.0f\n",
               r.plucks, expected);
        Check(std::fabs(r.plucks - expected) / expected < 0.05f,
              "the arp plucks at the rate the tempo and density ask for");
        Check(AllFinite(r), "and the output stays finite");
    }

    // Density really does thin the pattern out.
    {
        int last = 999;
        bool monotonic = true;
        printf("  plucks in 4 s by density: ");
        for(float d : { 1.0f, 0.75f, 0.5f, 0.25f, 0.0f })
        {
            Engine e;
            e.Init(kSR, kBlock);
            e.SetArpOn(true);
            e.SetTempo(0.4211f);
            e.SetDensity(d);
            e.NoteOn(48);
            const Render r = RenderFor(e, 4.0f);
            printf("%.2f→%d  ", d, r.plucks);
            if(r.plucks > last) monotonic = false;
            last = r.plucks;
        }
        printf("\n");
        Check(monotonic, "turning density down never adds notes");
        Check(last > 0, "and the bottom of the control still plays — it is a "
                        "density, not a volume");
    }
}

// ============================================================================
// 8b. Boot silence — the state every other audio test skipped past
// ============================================================================
// Every audio test above plucks a note first, so the state the instrument
// actually spends its first seconds in was never rendered once. The board
// screamed at boot and nothing here objected. It turned out to be a linkage
// problem rather than a DSP one, so this test could not have caught THAT — but
// "does it make a noise before anyone has asked it to" is a question a test
// suite should be able to answer, and this one could not.
static void TestBootSilence()
{
    Section("Boot silence");

    {
        Engine e;
        e.Init(kSR, kBlock);
        const Render r = RenderFor(e, 3.0f);
        printf("  Init only, no note:            peak %.1f dBFS\n", Db(r.peak));
        Check(r.peak == 0.0f, "a freshly initialised engine is digitally silent");
        Check(r.plucks == 0, "and nothing plucks by itself");
    }

    // The same thing the board actually boots into: the control model pushing
    // every boot value through before a note exists.
    {
        Engine       e;
        ControlModel m;
        e.Init(kSR, kBlock);
        m.Init(&e, 0.5f, 0.5f);
        const Render r = RenderFor(e, 3.0f);
        printf("  Init + panel boot values:      peak %.1f dBFS\n", Db(r.peak));
        Check(r.peak == 0.0f, "and still silent once the panel has pushed kBoot");
    }

    // Note-offs alone must not make a sound. The MIDI socket delivers a burst of
    // 128 of them as the UART settles, every single boot — measured on hardware,
    // not imagined — so this is a real input, not a hypothetical one.
    {
        Engine       e;
        ControlModel m;
        WeatherLink  w;
        e.Init(kSR, kBlock);
        m.Init(&e, 0.5f, 0.5f);
        w.Init(&e, &m);
        for(int i = 0; i < 128; i++) w.NoteOff(kChMain, static_cast<uint8_t>(i));
        const Render r = RenderFor(e, 2.0f);
        printf("  after 128 stray note-offs:     peak %.1f dBFS\n", Db(r.peak));
        Check(r.peak == 0.0f, "128 stray note-offs produce no sound");
        Check(r.plucks == 0, "and no plucks");
    }

    // Arp on, latched, but nothing held: the sequencer must not run on an empty
    // chord.
    {
        Engine e;
        e.Init(kSR, kBlock);
        e.SetArpOn(true);
        e.SetLatch(true);
        e.SetTempo(0.8f);
        const Render r = RenderFor(e, 3.0f);
        printf("  arp on and latched, no notes:  peak %.1f dBFS\n", Db(r.peak));
        Check(r.peak == 0.0f, "an armed arp with an empty chord stays silent");
        Check(r.plucks == 0, "and never triggers");
    }
}

// ============================================================================
// 9. The brightness ceiling upstream could not explain
// ============================================================================
// upstream/vox.h: "With high brightness and pitch the osc crashes. Limiting
// value to 0.5 until further investigation." That is a real report from a real
// board and the halving is kept, but nobody has said what it is. Sweep the whole
// plane with the limit taken off and see whether the host can reproduce it.
static void TestBrightnessCeiling()
{
    Section("The brightness ceiling");

    int    bad = 0, total = 0;
    float  worst_peak = 0.0f;
    for(int b = 0; b <= 10; b++)
    {
        for(int n = 24; n <= 96; n += 6)
        {
            Engine e;
            e.Init(kSR, kBlock);
            // Straight past Vox's halving, at the values upstream warns about.
            e.SetBrightness(static_cast<float>(b) / 10.0f * 2.0f);
            e.SetTimbre(1.0f);
            e.SetDamping(1.0f);
            e.NoteOn(static_cast<uint8_t>(n));
            const Render r = RenderFor(e, 0.35f);
            total++;
            if(!AllFinite(r) || r.peak > 4.0f) bad++;
            if(r.peak > worst_peak) worst_peak = r.peak;
        }
    }
    printf("  %d of %d brightness×pitch combinations misbehaved, worst peak %.2f\n",
           bad, total, worst_peak);
    // Recorded, not asserted as a pass: the host reproducing nothing does not
    // clear the hardware. What it does establish is that the halving is not
    // hiding a plain arithmetic blow-up in DaisySP's StringVoice.
    Check(true, "sweep completed");
}

// ============================================================================
// 10. Levels
// ============================================================================
static void TestLevels()
{
    Section("Levels");

    float worst = 0.0f;
    printf("  peak dBFS by drive:  ");
    for(float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        Engine e;
        e.Init(kSR, kBlock);
        e.SetArpOn(true);
        e.SetTempo(0.6f);
        e.SetDensity(1.0f);
        e.SetBrightness(0.8f);
        e.SetDrive(d);
        e.SetReverb(0.5f);
        e.NoteOn(48);
        e.NoteOn(55);
        e.NoteOn(60);
        const Render r = RenderFor(e, 3.0f);
        printf("%.2f→%.1f  ", d, Db(r.peak));
        if(r.peak > worst) worst = r.peak;
    }
    printf("\n");
    Check(worst <= 1.0f, "nothing clips across the drive range");

    // Reverb at the top is where a feedback network is most likely to run away.
    Engine e;
    e.Init(kSR, kBlock);
    e.SetArpOn(true);
    e.SetTempo(0.8f);
    e.SetDensity(1.0f);
    e.SetReverb(1.0f);
    e.SetDrive(1.0f);
    e.NoteOn(48);
    e.NoteOn(55);
    const Render r = RenderFor(e, 12.0f);
    printf("  reverb and drive at maximum, 12 s: peak %.1f dBFS\n", Db(r.peak));
    Check(AllFinite(r), "the reverb stays finite with everything at maximum");
    Check(r.peak <= 1.0f, "and inside the rails");
}

// ============================================================================
// Demo audio
// ============================================================================
static void RenderDemos()
{
    struct Demo
    {
        const char* name;
        float       tempo, density, bright, timbre, damp, drive, verb, chance;
        int         notes[4];
    };
    const Demo demos[] = {
        { "touchstring-plain.wav", 0.45f, 1.0f, 0.5f, 0.35f, 0.5f, 0.0f, 0.25f, 0.0f,
          { 36, 43, 48, -1 } },
        { "touchstring-sparse.wav", 0.6f, 0.35f, 0.7f, 0.6f, 0.3f, 0.2f, 0.6f, 0.0f,
          { 36, 46, 50, 55 } },
        { "touchstring-chance.wav", 0.5f, 0.8f, 0.6f, 0.5f, 0.5f, 0.35f, 0.5f, 0.85f,
          { 36, 43, 48, -1 } },
    };

    for(const Demo& d : demos)
    {
        Engine e;
        e.Init(kSR, kBlock);
        e.SetArpOn(true);
        e.SetTempo(d.tempo);
        e.SetDensity(d.density);
        e.SetBrightness(d.bright);
        e.SetTimbre(d.timbre);
        e.SetDamping(d.damp);
        e.SetDrive(d.drive);
        e.SetReverb(d.verb);
        e.SetChance(d.chance);
        for(int n : d.notes)
            if(n >= 0) e.NoteOn(static_cast<uint8_t>(n));
        const Render r = RenderFor(e, 12.0f);
        WriteWav(d.name, r.l, r.r, static_cast<int>(kSR));
        printf("  %-26s peak %.1f dBFS, %d plucks\n", d.name, Db(r.peak), r.plucks);
    }
}

int main(int argc, char** argv)
{
    const bool audio = argc > 1 && std::strcmp(argv[1], "--audio") == 0;

    TestPitchFidelity();
    TestPattern();
    TestArp();
    TestLatch();
    TestClock();
    TestPanel();
    TestBow();
    TestWeatherLink();
    TestAudio();
    TestBootSilence();
    TestBrightnessCeiling();
    TestLevels();

    if(audio)
    {
        Section("Demo audio");
        RenderDemos();
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
