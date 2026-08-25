# TouchString for Daisy Pod

A port of Synthux Academy's [TouchString](https://github.com/Synthux-Academy/simple-touch-instruments/tree/main/daisyduino/TouchString)
— an arpeggiated plucked string — from the Simple Touch to the **Daisy Pod**,
with the [Wolfpunk Weather Station](https://github.com/wolfpunk25) playing it
over MIDI.

Upstream is a twelve-pad, eight-knob, two-fader instrument. The Pod has two
knobs, two buttons, an encoder and two LEDs. Most of this repository is the
answer to what that costs.

---

## The instrument

One Karplus-Strong string, hit over and over by an arpeggiator. What you hold
decides the notes; a Christoffel-word pattern decides which sixteenths get
played; a humanizer decides how far each pluck strays from what you asked for.
It is monophonic on purpose — a single string being struck repeatedly is the
sound, not a chord ringing.

Three scales (Amara, Oxalis, Pigmy), eight notes each, transposable an octave
either way.

## Controls

```
   ┌─────────────────────────────────────────────────┐
   │   (KNOB 1)      (KNOB 2)         ( ENCODER )    │
   │                                                 │
   │   [LED 1]  [BTN 1]      [BTN 2]  [LED 2]        │
   └─────────────────────────────────────────────────┘
```

### The encoder is the note pad

The Simple Touch has seven note pads and they are how you play it. The Pod has
none, so the encoder becomes one:

| Gesture | What it does |
|---|---|
| **Turn** | Pick one of the eight degrees of the current scale |
| **Press** | Toggle that note in or out of the held set |
| **Hold 1.2 s** | Panic — every note off, sequence reset |

Press-to-toggle is how you build a chord with one button: dial a degree, press,
dial another, press. The arpeggiator picks up whatever is in the set. It is also
upstream's own behaviour — the Daisyduino sketch toggles latched pads exactly
this way.

### Button 1 — the arpeggiator

Tap to cycle **Off → On → Latched**, which is upstream's three-position switch.
LED 1 shows it: dim white off, green on, amber latched, with a white flash on
every pluck.

**Hold** button 1 for the setup layer:

| Held + | |
|---|---|
| Knob 1 | **Tempo** — 40 to 220 BPM. Fully down: follow incoming MIDI clock |
| Knob 2 | **Transpose** — ±12 semitones |
| Encoder | **Scale** — Amara / Oxalis / Pigmy |

### Button 2 — pages

Tap to page. LED 2 shows which.

| Page | Knob 1 | Knob 2 |
|---|---|---|
| **String** (blue) | Brightness | Timbre |
| **Body** (yellow) | Damping | Drive |
| **Pattern** (magenta) | Density | Shift |
| **Space** (cyan) | Reverb | Chance |

**The knobs are relative.** One pot serves five parameters, so after a page
change its physical position means nothing: it contributes movement from wherever
it is, and only once it has actually moved. Nothing jumps.

**Chance** is one control where upstream spends two knobs. The first half of its
travel brings in string variation — how much each pluck's brightness, timbre and
damping wander. The second half starts moving the notes as well. Staged rather
than parallel, because wrong notes are a much louder effect than a wobbling
timbre and driving both together would make the bottom half unusable.

## Playing it from the Weather Station

Straight 3.5 mm TRS cable, Weather Station MIDI out to Pod MIDI in.

**Set `DIN_CHANNEL = None` on the Weather Station.** Its default folds every
layer onto channel 1, which makes the sun chords and rain droplets
indistinguishable from the keys and the weather modifiers appear dead.

The mapping is semantic — what each layer *means* — rather than a table of
controller numbers:

| Ch | Layer | Here |
|---|---|---|
| 1 | Main | The five keys. Plucks the string. |
| 2 | Sun | Plucks. **CC74** opens the brightness — sun opening the filter is the Weather Station's own metaphor and it lands on this instrument's brightest control. |
| 3 | Rain | Droplets pluck it; short plucks are what a Karplus string is *for*. **CC91** wets the reverb. |
| 4 | Wind | Plucks. **CC1** drives Chance, so a gust makes the string wander. Pitch bend bends it. |
| 5 | Thunder | Does **not** pluck — a plucked string has nothing to say about a boom. Shoves drive and damping and lets them fall back: the string is hit harder and chokes. |

Controllers **add** to where the knobs are sitting rather than replacing them, so
nothing on the panel goes dead when the cable is in. Each has its own neutral
point, because the Weather Station's panic resets them to different values
(CC74→64, CC91→40, CC1→0).

The Weather Station's own scales and octaves all work: incoming note numbers set
the pitch directly. TouchString's three scales govern the encoder note pad.

**The Weather Station sends no MIDI clock**, so the arpeggiator runs on its own
tempo alongside it. Anything else on the socket that does send clock will drive
it, with the tempo knob at the bottom.

Anything that is not a Weather Station still works: upstream's CC70–79 map is
kept, on any channel.

## Building

Needs the ARM GNU toolchain and `dfu-util` on `PATH`.

```bash
make libs -j8 && make -j8
```

Flash with `make program-dfu` (hold BOOT, tap RESET), or drop
`build/touchstring_pod.bin` on the [Daisy web programmer](https://electro-smith.github.io/Programmer/).

No bootloader and no custom linker script — it fits internal flash at 87%
(94% with `DEBUG=1`).

```bash
make DEBUG=1      # USB serial log: CPU meter, panel state, MIDI activity
```

Non-DEBUG builds do not enumerate on USB. That is expected, not a bad flash.

### Tests

```bash
make test         # 96 host checks: the real engine, no board needed
make audio        # renders demo WAVs
```

The engine, the control model and the MIDI layer have no hardware in them, so
the tests drive the actual instrument rather than a paraphrase of it. Reach for
them first.

## Provenance

The engine descends from the **MIT-licensed** original sketch in
[`Synthux-Academy/simple-touch-instruments`](https://github.com/Synthux-Academy/simple-touch-instruments),
which is committed verbatim in [`upstream/`](upstream/).

Synthux's separate [`TouchString`](https://github.com/Synthux-Academy/TouchString)
repository — the libDaisy re-port their firmware page ships — carries **no
licence file**. Its siblings are all explicitly MIT, so this looks like an
oversight rather than a decision, but an omission is not a grant. It was read as
a reference; no code was taken from it. See
[`upstream/PROVENANCE.md`](upstream/PROVENANCE.md).

What changed in the port, and what was measured, is in
[`docs/PORTING.md`](docs/PORTING.md).

Licensed MIT. See [LICENSE](LICENSE) and [CREDITS.md](CREDITS.md).
