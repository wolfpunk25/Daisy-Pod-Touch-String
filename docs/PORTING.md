# What changed, and what was measured

Upstream is in [`upstream/`](../upstream/), verbatim and MIT. This is the diff in
prose: what the Pod cost, what was fixed on the way, and which of it is measured
rather than reasoned.

---

## The structural change: notes are MIDI note numbers

Upstream identifies a note by its **scale degree, 0..7**. `NoteOn` takes an index
into an eight-entry table of frequencies; the arpeggiator sorts those indices;
the latch is a `bitset<8>` over them.

That works when the only thing that can play the instrument is eight pads wired
to the same table. It does not survive contact with a MIDI socket, and upstream
knows it — Rosa's re-port has a `TODO` where note forwarding should be, saying in
so many words that a scale degree is not something a keyboard can send. So
upstream's MIDI handler listens for ten controller numbers and **forwards no note
messages at all**.

Here a note is a **MIDI note number, 0..127**, all the way through. The encoder
note pad turns a degree into a note number and hands that over; the MIDI socket
hands over what it received. Both end up in the same held set and the arpeggiator
cannot tell them apart.

**This is allowed only because it is the same instrument afterwards.** Upstream's
three frequency tables are exact 12-TET pitches, and its 25-entry transposition
table is exactly `2^(n/12)` for n = −12..+12 — semitones in a costume. So the
tables became MIDI note numbers and transposition became an integer semitone
offset, and `tests/main.cpp` compares the result against upstream's own numbers
across all three scales, all eight degrees and all 25 transpositions:

> **worst error across 600 combinations: 0.041 cents**

which is upstream's tables being rounded to two decimal places, not the rewrite
drifting.

Consequences worth knowing:

* `Latch` is a `bitset<128>` and the arpeggiator holds **16** notes rather than 8.
  The Weather Station can hold five keys and stack a sun chord and a wind voice
  on top; eight would overflow in ordinary playing.
* The arpeggiator now rejects duplicate notes. The same note can arrive from the
  encoder and from two MIDI channels at once, which upstream had no way to do,
  and a doubled note would take two slots and be arpeggiated twice.
* **All eight degrees are reachable.** Upstream's pad handler guards with
  `pad >= kFirstNotePad + kNotesCount - 1` on touch but `+ kNotesCount` on
  release, so the eighth note of every scale can be turned off but never on. Both
  the sketch and the re-port have this. On the Pod the encoder reaches all eight.

## The panel

Eleven parameters, two knobs, two buttons and an encoder. The full mapping is in
[`src/ui/controlmodel.h`](../src/ui/controlmodel.h) and the README; the decisions
behind it:

**The encoder had to be the note pad.** Seven of the Simple Touch's twelve pads
are note pads and they are the entire performance surface. Spend the encoder on a
parameter and the Pod is a box that cannot be played unless something else is
plugged into it. Everything else could be folded; this could not.

**Pattern shift is on a knob, not a button.** It is a 16-step rotation, and a
momentary press can only ever bump a value like that, never place it.

**Reverb is on a knob, not the encoder.** It is the smoothest parameter on the
instrument and the encoder is the only detented control; the pairing was the
wrong way round.

**Tempo is in the hold layer, not on a knob.** It is the least-touched control
here — the Weather Station sends no clock, so tempo is set once and left — and a
permanently assigned knob is the most expensive thing the panel has.

**Chance is one control where upstream has two.** Upstream spends a knob each on
note randomisation and string randomisation. Driving both from one control in
step would be wrong: by halfway up you would already be throwing wrong notes,
which is a far louder effect than a wobbling timbre. So it is staged — the first
half of the travel is string variation alone, the second half brings the notes in
on top. That collapse is what makes it four clean pages instead of five.

**The knobs are relative.** One pot serves five parameters. Same approach as the
ZenTouch, Spotykach, Terrarium, Audrey and Wrangler Pod ports, and it brings the
same two fixes with it: a 0.002 noise floor on ongoing movement, and a 200 ms
spin of `ProcessAllControls()` before anchoring — the Pod's `AnalogControl`
filter starts at zero, and anchoring before it has reached the real pot position
turns its own ramp into an apparent deliberate turn. Audrey booted with its
reverb at 96% because of exactly this.

## The reverb is not upstream's

Upstream uses `daisysp::ReverbSc`, which **no longer exists** — it went with
DaisySP's "Remove LGPL Modules" commit. Two ways out: pin DaisySP to a pre-purge
commit and ship LGPL code in the binary, which is what the Audrey II Pod port had
to do, or write one.

[`src/string/space.*`](../src/string/space.cpp) is a four-line feedback delay
network through a Householder matrix, six allpasses of input diffusion in front,
one-pole damping inside each line, two of the four slowly modulated. Carried over
from the Wrangler Pod port, where its shape was settled by measurement rather
than taste — a two-line cross-coupled tank is not a reverb, it is a pair of slap
echoes with a ring on top.

One deliberate difference from upstream: the knob buys **decay as well as mix**.
Upstream's feedback is fixed at 0.8 and only the send moves. A single reverb knob
is expected to make the room bigger, so this one stretches feedback from 0.62 to
0.965 across its travel, squared, because all the audible change in decay time
lives at the top of that range.

## Two upstream bugs fixed

**The humanizer can fall off the end of a function.** In `humanized_note`, the
middle branch (`human_note_chance` between 33 and 66): if the chance dice hits and
the octave dice lands between 25 and 75, no branch returns. That is undefined
behaviour on the hot path of the instrument. Rosa's re-port found and fixed the
same thing, independently.

**The as-played arpeggiator starts on the second note.** `_played_idx` is
incremented *before* it is read and is not reset by `Clear()`, so a chord that
arrives all at once starts on its second note. Upstream never notices, because
its pads arrive one at a time and the clock starts on the first — at which point
the step happens to wrap to zero. The Weather Station's sun layer sends three or
four notes in a single burst, which is exactly the case that exposes it. Fixed
with a primed flag; the test that caught it is in `TestArp`.

## Things kept bug-for-bug

**The reverb "mix" is a send.** Upstream's crossfade is called with a zero dry
input and its output is added to the dry signal at full level, so the dry
coefficient it computes is never used. It is what the instrument sounds like, so
it stays.

**Brightness is halved before it reaches the voice**, with upstream's comment:
"With high brightness and pitch the osc crashes. Limiting value to 0.5 until
further investigation." That is a report from real hardware and the limit is
kept. `TestBrightnessCeiling` sweeps the whole brightness×pitch plane with the
limit taken *off*:

> **0 of 143 combinations misbehaved, worst peak 0.85**

The host reproduces nothing. That does not clear the hardware — it is a different
FPU and a different compiler — but it does establish that the halving is not
hiding a plain arithmetic blow-up in DaisySP's `StringVoice`. Worth another look
on the board with a `DEBUG=1` build before anyone raises the ceiling.

## Things dropped

**Swing.** Upstream's `Trigger` carries a swing amount and **nothing ever sets
it** — `SetSwing` has no caller in either the sketch or the re-port. There is no
control slot free to expose it on, and swing that only exists in a header is
worse than swing that does not exist.

**The arpeggiator's gate machinery.** It releases a held note early, and every
voice here is a pluck that decays on its own; upstream wires its own note-off
callback to an empty function. The re-port dropped it for the same reason.

**External clock on a GPIO pin.** Upstream reads a +5 V clock into S31 behind
`#define EXTERNAL_SYNC`. The Pod has no CV input and does have a MIDI socket, so
external sync is MIDI Timing Clock — same 24 ppqn, same resync arithmetic. The
switch between internal and external is upstream's own mechanism, untouched: any
tempo under 40 BPM means "follow whatever is coming in", which is why the bottom
of the tempo control is the sync position.

## What the tests measure

`make test` — 96 checks, no board.

| | |
|---|---|
| Pitch | 600-way comparison against upstream's own frequency and ratio tables |
| Pattern | onset counts 1..16, the two-gap-lengths property that defines a euclidean rhythm, shift as a true rotation |
| Arp | as-played and sorted order, duplicate rejection, overflow dropping the least recent |
| Latch | keyboard semantics with and without latch, pad toggle semantics, unlatch dropping only untouched notes |
| Clock | tempo mapping, internal tick rate to within 1%, external sync waiting for a pulse |
| Control model | tap vs hold, paging, the relative knob not dragging a parameter on arrival, chord building, panic firing in the hand |
| Weather link | which channels pluck, thunder not plucking and its swell reaching zero, controllers modulating without moving the panel, per-controller neutral points |
| Audio | pluck rate against tempo and density, density never adding notes, nothing clipping, the reverb staying finite with everything at maximum |

Measured figures worth keeping:

* Internal tick rate **176 ticks in 2 s at 110 BPM**, against 176 expected.
* Arp **29 plucks in 4 s** at 110 BPM and full density, against 29 expected.
* Plucks by density: **29 / 22 / 17 / 10 / 2** across the knob. The bottom is not
  silence — it is one onset in sixteen, because Density is not a volume control.
* Peak output across the drive range: **−9.2 to −4.6 dBFS**, nothing clipped.
* Reverb and drive at maximum for 12 s: **−1.6 dBFS**, finite throughout.

## The scream, and why the host could not see it

The first hardware build screamed continuously from power-on. It is worth writing
down because the cause was not in this repository's source at all, and because
the shape of the mistake is easy to repeat.

**What the board said.** A `DEBUG=1` build with per-stage peak meters — added for
exactly this — reported, at the first log line after boot:

```
lvl vox 6579 dry 3645 wet 1685 out 999
midi on 0 off 128 cc 0 bend 0 srt 0 oth 0
```

The meters are thousandths of full scale. The string voice was producing **6.5×
full scale** with **zero note-ons** and nothing held; `out 999` is `SoftLimit`
pinned at the ceiling. So it was not the reverb self-oscillating, and it was not
MIDI garbage triggering plucks — the two hypotheses worth having beforehand, and
both wrong.

**What it actually was.** libDaisy and DaisySP had been *copied* as prebuilt
`.a` archives from a sibling Pod project to save a build, and then compiled
against this checkout's headers. Same DaisySP commit, both trees clean — which is
what made it look safe. It is not: objects built elsewhere carry that build's
struct layouts, and linking them against different headers puts `SetFreq`,
`SetDamping` and the rest at the wrong member offsets inside `StringVoice`. A
Karplus-Strong loop whose damping coefficient lands in the wrong field is a string
that rings without being plucked.

`rm -rf libDaisy/build DaisySP/build && make libs` and the meters went flat.

**Why every host test passed.** The host build compiles DaisySP from source, so it
could never reproduce a linkage fault in the target build. Three things were
eliminated before that landed, each cheap and each worth eliminating: `-ffast-math`
(the host is silent with the target's exact flags), uninitialised delay-line state
(`KarplusString::Init` calls `Reset()`), and stale coefficient caching.

**The test gap it exposed was real but separate.** Every audio test plucked a note
first, so the state the instrument spends its first seconds in had never been
rendered once. `TestBootSilence` now covers it: a freshly initialised engine, the
same engine after the panel has pushed `kBoot`, an armed and latched arp with an
empty chord, and 128 stray note-offs — all asserted digitally silent. That test
would not have caught this bug. It would catch the DSP version of it.

**Do not copy build artefacts between projects.** `make libs` in the project that
is going to link them, every time.

## The startup MIDI burst

Measured, not theorised: **128 note-off messages arrive on every boot**, on
channel 1, note 0, velocity 0, with nothing plugged into the socket. The count is
identical run to run. It is the UART settling as `StartReceive()` comes up.

Note-offs are harmless — they cannot pluck anything — but a stray note-on would
pluck the string before anyone touched it, and a stray controller would move a
parameter out from under the panel. `Panel` therefore drains and discards
everything off the socket for the first **250 ms**, and counts what it threw away
so the `DEBUG=1` log can show it.

## CPU

Idle cost was **33% mean / 39-41% peak** with nothing playing and the arp off —
high for a monophonic string and a reverb. Two independent causes, both the same
mistake in different hands:

1. **This port's own.** `Space::Process` evaluated two `sinf` per sample for line
   modulators running at **0.09 and 0.13 Hz**. A sub-1 Hz sine sampled 48,000
   times a second is pure waste; `Space::Tick(block_size)` now advances both
   phases once per block and holds the values across it. A block is 83 us and
   nothing at 0.1 Hz notices. The Terrarium Pod port halved its CPU on exactly
   this class of finding.

2. **DaisySP's.** `StringVoice::Process` calls `string_.SetBrightness()` and
   `string_.SetDamping()` **on every sample**, and unpatched `KarplusString`
   recomputes a `powf` and an `atanf` inside each. The ZenTouch Pod port found
   this and carries a patch for it in `patches/daisysp-karplus-string.patch`.
   Not applied here — patching a submodule complicates the build, and it is only
   worth doing if the measured figure still calls for it.

## What the hardware said

A 115-second `DEBUG=1` capture of real playing — 2408 log lines covering all
three arp modes, all four pages, the setup layer and the Weather Station.

**CPU is flat.** 30% mean / 37% peak sitting idle; 31% / 40% with the arp dense,
a chord held, the reverb up and notes arriving over MIDI. Playing it costs
almost nothing over doing nothing, because `StringVoice` and the reverb tank both
run every sample whether or not anything has been triggered — the idle figure IS
the cost. **So DaisySP's per-sample `powf`/`atanf` is not worth patching around**:
40% peak leaves plenty of headroom, and vendoring or patching a submodule to buy
headroom nobody needs is a bad trade. The measurement is what settles it, and it
would have been easy to assume the opposite.

**The Weather Station link works.** 120 note-ons and 120 note-offs, exactly
balanced — no hung notes across two minutes of real playing — plus 6 controllers
and the 128 discarded startup bytes. Over a straight 3.5 mm TRS cable, with
`DIN_CHANNEL = None`.

**No cross-page leaks.** Every one of the eleven parameters was checked for
movement while its own page or layer was not selected: brightness moved 40 times
and timbre 44, both only on the String page; damping and drive only on Body;
tempo, transposition and scale only inside the setup layer. Zero leaks — except
one, on Chance, which turned out to be the CC74 bug below rather than a panel
fault.

**Output stayed in the rails.** Peak 0.588 of full scale, about −4.6 dBFS. The
`SoftLimit` ceiling was never reached.

## The controller collision the capture found

The Weather Station's README documents **CC74** as the sun layer and **CC91** as
the rain layer, so both were originally handled on channels 2 and 3 only. Its
code does something else:

```python
for c in (CH_MAIN, CH_SUN):  cc(c, 74, bright)
for c in (CH_MAIN, CH_RAIN): cc(c, 91, wet)
```

and `midi_panic()` sends both to all five channels. So a second copy of CC74
arrived on channel 1, missed the sun handler, fell through to upstream's generic
CC70-79 map — where 74 is "note randomisation" — and jammed **Chance**. Every
time the sun came out, the instrument started throwing wrong notes.

Both controllers are now matched on **number rather than channel**, which is also
the more defensible reading: 74 and 91 are the standard brightness and
reverb-send controllers, so anything else on the socket means the same thing by
them. CC74 no longer reaches chance at all; upstream's 75 still does.

**The lesson is about sources, not MIDI.** The behaviour was read out of the
Weather Station's documentation when its code was sitting on the same disk. A
capture of the real thing found in one pass what the prose had wrong. There is
now a test that replays a full Weather Station panic — both controllers on all
five channels at their documented resting values — and asserts that nothing on
the panel moves.

## The scale control that worked and could not be heard

Reported from the board as "I'm not sure the scale is changing" — which is
exactly what a control that works but is inaudible feels like. The debug log had
been showing `sc 0`, `sc 1`, `sc 2` all along, and a panel-leak analysis had
cleared it. The number was moving. The sound was not.

**It is a consequence of the note-identity rewrite, and it should have been
followed through at the time.** Upstream identifies a note by its scale DEGREE,
so changing the scale retunes whatever is sounding the instant it changes. Here a
note is a MIDI note number, and the encoder pad resolves a degree to an absolute
note at the moment of the press. Nothing revisited it afterwards — so a scale
change only affected notes pressed *after* it, and a held chord carried on in the
old scale. One degree in three appeared to follow, because degree 0 is MIDI 36 in
all three scales and matched by coincidence.

**The fix has to know where a note came from, and only one place does.** By the
time a note reaches the held set there is nothing left to say whether it was a
degree or arrived off the wire — and notes off the wire must NOT be dragged around
by the scale control, because the Weather Station's own six scales and four
octaves are the entire point of passing its pitches through. So `ControlModel`
remembers the note it issued for each degree and moves exactly those, and
`Engine` stays out of it.

Two tests, and the second matters more than the first: a pad chord follows the
scale with no stale notes left ringing, and three MIDI notes deliberately outside
every scale stay exactly where they are while a pad note alongside them moves.

**The lesson is about the shape of the original claim.** The porting notes said the
note-identity rewrite was safe because the pitches came out identical — and they
did, to 0.041 cents. What that test could not see is that *identical pitches* is
not the same as *identical behaviour*: upstream's degrees are late-bound and this
port's note numbers are early-bound, and everything downstream of that difference
had to be checked, not just the frequencies.

## Still unproven

Everything about how it feels, because none of it has been on hardware yet:

* **Whether eight hues read apart on LED 2.** The degree indicator is a hue
  around the wheel; if it does not read, it is one line to change to a brightness
  ramp.
* **The gesture timings** — 0.4 s for the setup layer, 1.2 s for panic.
* **Whether the pluck flash is legible** at sixteenths, where notes are 68 ms
  apart at the top of the tempo range.
* **Density and Shift.** The Pattern page was on screen for about a second of
  the whole capture, so those two are the only panel parameters never moved on
  hardware.
* **Pitch bend.** The capture recorded none — the Weather Station only bends
  during WIND weather, which was never held.
* **MIDI clock.** Nothing on the socket sends it; the Weather Station does not.
* **The brightness ceiling**, above.
