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

## Still unproven

Everything about how it feels, because none of it has been on hardware yet:

* **Whether eight hues read apart on LED 2.** The degree indicator is a hue
  around the wheel; if it does not read, it is one line to change to a brightness
  ramp.
* **The gesture timings** — 0.4 s for the setup layer, 1.2 s for panic.
* **Whether the pluck flash is legible** at sixteenths, where notes are 68 ms
  apart at the top of the tempo range.
* **CPU.** No figure exists yet. Get one from a `DEBUG=1` serial capture.
* **The Weather Station link itself.** The mapping is written and tested against
  synthetic messages; no real cable has been in the socket.
* **The brightness ceiling**, above.
