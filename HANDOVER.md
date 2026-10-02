# Handover

TouchString ported from the Synthux Simple Touch to the **Daisy Pod**. Working,
flashed, pushed. Start here, then read `docs/PORTING.md` for the reasoning and
the measurements.

## The rig

- **Daisy Pod** — the instrument. One plucked Karplus string, arpeggiated.
- **RP2040 pad, physically attached** — eight buttons = the eight degrees of the
  current scale. Firmware in `controller/code.py`, which is the only copy; it
  used to live on the CIRCUITPY volume alone.
- **No Weather Station.** Its mapping is still in `src/midi/weather.*` and is
  host-tested, but the controller that ran it now runs the pad firmware, so none
  of it has been heard since. Darren's call (2026-09-06): leave it.

## Build and flash

```sh
make libs -j8 && make -j8        # DEBUG=1 for the USB serial log
make program-dfu                 # hold BOOT, tap RESET first
make test                        # 147 host checks, no board needed
```

The engine, control model and MIDI layer have no hardware in them, so `make test`
drives the real instrument. **Reach for it first** — on this port, reasoning about
the code produced a confident wrong answer more than once and a measurement
corrected it every time.

## Four things that cost real time

1. **Never copy `libdaisy.a` / `libdaisysp.a` from another Daisy project.** Same
   commit, clean trees, and still wrong: objects built elsewhere carry that
   build's struct layouts, so DSP setters land at the wrong offsets. It does not
   fail to link — it produces an instrument that screams at boot.
2. **`dfu-util`'s leave request is unreliable here.** It often writes perfectly
   and then sits in DFU. Check `dfu-util -l` before assuming a bad flash; a plain
   RESET tap fixes it. `dfu-util -e` is not a substitute.
3. **Only `DEBUG=1` builds enumerate on USB.** Silence on the port is expected
   otherwise. Two `/dev/cu.usbmodem*` appear when the pad is plugged in — the
   Daisy is the long-serial one.
4. **Read the source, not the prose.** Two bugs came from believing documentation
   while the code sat on the same disk.

## Still open

- **MIDI clock sync** — nothing in the rig sends clock.
- **The Weather Station mapping** — supported, unheard since the controller changed.
- **Pitch bend** — the pad sends none.
- **Upstream's brightness ceiling** — halved to work around a crash it never
  explained; a 143-point host sweep reproduces nothing, which does not clear the
  hardware.

## Links

- Repo: https://github.com/wolfpunk25/Daisy-Pod-Touch-String
- Field guide: https://claude.ai/code/artifact/a1dc158a-afd1-4df5-bf6b-3cb45410e746
- Test pass (35/36, results stored in the page): https://claude.ai/code/artifact/403210ea-ffa2-4958-81e4-42baf2691898
