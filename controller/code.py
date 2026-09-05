"""Eight-note MIDI controller for Daisy Pod TouchString.

Sends the ACTUAL pitches of the Pod's three scales, not 60-67. It used to send
60-67 on the assumption that the Pod read those as scale-degree selectors; it
does not — incoming notes set the string's pitch directly, so that assumption
played a chromatic run from C4 while this display said C, G, Bb, C, D, Eb, F, G.
Measured on the Pod's own MIDI log: notes 60, 62, 64, 65, 67 arriving on
channel 1.

The tables below are `kScales` from the Pod's `src/common/config.h`, and the
letters in SCALE_NOTES already described them correctly all along. This pad is
the master for scale selection: cycling it here sends CC20 and the Pod follows.

The button GPIO tuple is the only wiring-dependent setting. Positions are
musical order 1..8, read row-major in the controller photo: the two-button row,
then the middle three-button row, then the lower three-button row.
"""

import time

import board
import busio
import digitalio
import neopixel
import usb_midi

from adafruit_ht16k33.matrix import Matrix8x8


# Physical musical positions 1..8. The current order preserves the numbering
# from the previous controller firmware and can be corrected after a button
# diagnostic without changing any other code.
BUTTON_PINS = (
    board.GP26,
    board.GP21,
    board.GP22,
    board.GP20,
    board.GP18,
    board.GP17,
    board.GP19,
    board.GP16,
)

MIDI_CHANNEL = 0
SCALE_CC = 20
DEBOUNCE_SECONDS = 0.018
SCALE_NOTICE_SECONDS = 0.75
SCALE_HOLD_SECONDS = 0.5

# The three green buttons, identified by their Amara pitch labels C, G, and
# B-flat: scale-degree positions 1, 2, and 3.
SCALE_CHORD = (0, 1, 2)

NOTE_COLORS = (
    (255, 35, 15),
    (255, 100, 10),
    (255, 210, 15),
    (40, 230, 50),
    (10, 190, 220),
    (25, 90, 255),
    (120, 35, 255),
    (240, 30, 190),
)

SCALE_COLORS = (
    (255, 150, 8),   # Amara
    (12, 255, 70),   # Oxalis
    (35, 95, 255),   # Pigmy
)

# The Pod treats MIDI notes 60-67 as scale-degree selectors. These are the
# actual pitches in its three TouchString scales, from common/config.h.
# MIDI note numbers, straight out of the Pod's src/common/config.h kScales.
# Keep these in step with that file: both ends have to agree on the tables.
SCALE_MIDI = (
    (36, 43, 46, 48, 50, 51, 53, 55),  # Amara   C2 G2 A#2 C3 D3 D#3 F3 G3
    (36, 40, 41, 43, 45, 48, 52, 53),  # Oxalis  C2 E2 F2  G2 A2 C3  E3 F3
    (36, 38, 39, 43, 46, 48, 50, 51),  # Pigmy   C2 D2 D#2 G2 A#2 C3 D3 D#3
)

SCALE_NOTES = (
    ("C", "G", "Bb", "C", "D", "Eb", "F", "G"),  # Amara
    ("C", "E", "F", "G", "A", "C", "E", "F"),   # Oxalis
    ("C", "D", "Eb", "G", "Bb", "C", "D", "Eb"), # Pigmy
)

# Compact five-column letters leave room for a small flat symbol. Rows use the
# least-significant bit as the leftmost pixel.
LETTER_GLYPHS = {
    "A": (0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11),
    "B": (0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E),
    "C": (0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E),
    "D": (0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E),
    "E": (0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F),
    "F": (0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10),
    "G": (0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E),
}
FLAT_MARK = (0x20, 0x20, 0x20, 0x60, 0xA0, 0xA0, 0x60)
SCALE_GLYPHS = (
    (0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E),  # 1
    (0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F),  # 2
    (0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E),  # 3
)

i2c = busio.I2C(scl=board.GP1, sda=board.GP0, frequency=400000)
matrix = Matrix8x8(i2c, address=0x70)
matrix.auto_write = False
matrix.brightness = 0.32

pixel = neopixel.NeoPixel(board.GP23, 1, brightness=0.40, auto_write=False)

buttons = []
for pin in BUTTON_PINS:
    button = digitalio.DigitalInOut(pin)
    button.direction = digitalio.Direction.INPUT
    button.pull = digitalio.Pull.UP
    buttons.append(button)

usb_out = usb_midi.ports[1]
uart_out = busio.UART(tx=board.GP4, baudrate=31250)


def send_raw(data):
    try:
        usb_out.write(data)
    except Exception:
        pass
    uart_out.write(data)


# What was actually sent for each button, so a note-off always matches its
# note-on. Without this a scale change under a held button would send the new
# scale's note number to release the old scale's note, and the Pod would hold
# that note for ever.
sent_note = [None] * 8


def send_note(index, pressed):
    if pressed:
        send_scale_select()
        note = SCALE_MIDI[scale_index][index]
        sent_note[index] = note
        send_raw(bytes((0x90 | MIDI_CHANNEL, note, 110)))
    else:
        note = sent_note[index]
        if note is None:
            return
        sent_note[index] = None
        send_raw(bytes((0x80 | MIDI_CHANNEL, note, 0)))


def all_notes_off():
    for i in range(len(sent_note)):
        sent_note[i] = None
    send_raw(bytes((0xB0 | MIDI_CHANNEL, 123, 0)))


def send_scale_select():
    send_raw(bytes((0xB0 | MIDI_CHANNEL, SCALE_CC, scale_index)))


def pitch_glyph(name):
    rows = LETTER_GLYPHS[name[0]]
    if len(name) == 1:
        return rows
    return tuple(rows[y] | FLAT_MARK[y] for y in range(7))


def draw_glyph(rows):
    """Draw an upright glyph on the matrix as mounted in the enclosure."""
    matrix.fill(0)
    for y, row in enumerate(rows):
        for x in range(8):
            if row & (1 << x):
                # The display PCB is mounted with its axes transposed relative
                # to the viewing direction in the enclosure.
                matrix[y, x] = 1
    matrix.show()


def update_pixel(held):
    active = -1
    for index, pressed in enumerate(held):
        if pressed:
            active = index
    pixel[0] = NOTE_COLORS[active] if active >= 0 else (8, 2, 18)
    pixel.show()


stable = [not button.value for button in buttons]
candidate = list(stable)
changed_at = [time.monotonic()] * len(buttons)
held_order = [index for index, pressed in enumerate(stable) if pressed]
last_played = held_order[-1] if held_order else None
scale_index = 0
scale_notice_until = time.monotonic() + SCALE_NOTICE_SECONDS
scale_chord_started = None
scale_chord_latched = False

all_notes_off()
send_scale_select()
draw_glyph(SCALE_GLYPHS[scale_index])
pixel[0] = SCALE_COLORS[scale_index]
pixel.show()

while True:
    now = time.monotonic()

    if scale_notice_until and now >= scale_notice_until:
        scale_notice_until = 0
        if last_played is not None:
            draw_glyph(pitch_glyph(SCALE_NOTES[scale_index][last_played]))
        update_pixel(stable)

    for index, button in enumerate(buttons):
        raw_pressed = not button.value
        if raw_pressed != candidate[index]:
            candidate[index] = raw_pressed
            changed_at[index] = now
        elif candidate[index] != stable[index] and (
            now - changed_at[index] >= DEBOUNCE_SECONDS
        ):
            stable[index] = candidate[index]
            send_note(index, stable[index])
            if stable[index]:
                if index in held_order:
                    held_order.remove(index)
                held_order.append(index)
                last_played = index
            elif index in held_order:
                held_order.remove(index)

            displayed = held_order[-1] if held_order else last_played
            draw_glyph(pitch_glyph(SCALE_NOTES[scale_index][displayed]))
            scale_notice_until = 0
            update_pixel(stable)

    scale_chord_held = all(stable[index] for index in SCALE_CHORD)
    if scale_chord_held:
        if scale_chord_started is None:
            scale_chord_started = now
        elif not scale_chord_latched and (
            now - scale_chord_started >= SCALE_HOLD_SECONDS
        ):
            scale_chord_latched = True
            all_notes_off()
            scale_index = (scale_index + 1) % len(SCALE_NOTES)
            send_scale_select()
            draw_glyph(SCALE_GLYPHS[scale_index])
            pixel[0] = SCALE_COLORS[scale_index]
            pixel.show()
            scale_notice_until = now + SCALE_NOTICE_SECONDS
    else:
        scale_chord_started = None
        scale_chord_latched = False

    time.sleep(0.002)
