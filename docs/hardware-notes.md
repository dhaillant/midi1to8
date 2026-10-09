# Hardware notes

Implementation details of the midi1to8 module that are not obvious from the
source alone.

Board: ATmega328P (Arduino Uno / Nano), 16 MHz.

---

## Signal path

- **MIDI IN** is pin 0 (RX), **MIDI OUT** is pin 1 (TX). There is a single
  UART, so the input is retransmitted through the same peripheral it was
  received on.
- **The 8 outputs on D2–D9 carry no MIDI data.** They are gates: each one
  enables or blocks the shared TX line towards its DIN socket. The data on
  every socket is the same bitstream; what differs is which gates are open
  while it is being sent.
- **Gates are active LOW.** `setMidiOutputs()` in the control firmware inverts
  the mask for exactly this reason.

Port mapping, used for fast GPIO instead of `digitalWrite()`:

| Outputs | Port | Bits |
|---|---|---|
| 1–6 | `PORTD` | 2–7 |
| 7–8 | `PORTB` | 0–1 |

LEDs: activity on **A0** (lit on LOW), debug on **D13**.

Because a gate is closed between messages anyway, closing it between the bytes
of a single message is electrically equivalent — the line simply idles. This is
what makes byte-at-a-time gating safe.

---

## Sketches

| Path | What it does |
|---|---|
| `midi_thru/` | Plain thru: every byte goes to all 8 outputs |
| `one_output_per_midi_channel/` | Each MIDI channel → the output set in `midi_outputs[]` |
| `control/midi1to8-firmware/` | Routing matrix, 8 presets in EEPROM, SysEx configuration |
| `control/control.py` | PyQt5 + mido GUI for the matrix |
| `setup/manage.py` | Earlier GUI, superseded by `control.py` |

---

## Wiring for `control.py`

The module answers SysEx on **output 8 only** (`SYSEX_OUTPUT_MASK = 0x80`).
A working configuration session needs both directions:

```
computer MIDI OUT  ->  module MIDI IN
module OUTPUT 8    ->  computer MIDI IN
```

If the return cable is on any other output, the GUI reports
*"No response — Check connection to OUTPUT 8"*.

The matrix is 17 bytes: rows 0–15 are MIDI channels 1–16, row 16 is real-time
messages (clock, start, stop). Each byte is a bitmask over the 8 outputs. Since
SysEx data bytes carry only 7 bits, those 17 bytes are packed into 20 for
transmission: groups of 7 data bytes preceded by a carry byte holding their
MSBs. Both sides implement this identically — `sysex_handling.h` and the
encode/decode functions at the top of `control.py`.

---

## Building without the Arduino IDE

`arduino-cli` is not required; the toolchain shipped inside the IDE is enough.
The one catch is that the IDE auto-generates function prototypes for `.ino`
files and a plain `.cpp` gets none, so they have to be supplied:

```bash
SRC=midi_thru/midi_thru.ino
CORE=~/Library/Arduino15/packages/arduino/hardware/avr/1.8.8
GXX=~/Library/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/avr-g++

{ echo '#include <Arduino.h>'
  echo 'void tick(); void blink_MIDI_LED(void); void setup(); void loop(); void render_MIDI_LED();'
  cat "$SRC"; } > /tmp/test.cpp

"$GXX" -c -std=gnu++11 -Os -Wall -Wextra -mmcu=atmega328p -DF_CPU=16000000L \
  -DARDUINO=10819 -DARDUINO_AVR_UNO -DARDUINO_ARCH_AVR \
  -I"$CORE/cores/arduino" -I"$CORE/variants/standard" /tmp/test.cpp -o /tmp/test.o
```

The other two sketches also need `-I ~/Documents/Arduino/libraries/MIDI_Library/src`
(paths are macOS; adjust for other platforms).

---

## Not a bug — do not "fix" this

`TCCR0A |= (1 << WGM01)` in the setup of all three sketches. The comment says
"CTC mode" and is **wrong**, but the code is correct as written.

The Arduino core has already set `WGM00 = 1`, so the result is WGM = 011,
Fast PWM with TOP = 0xFF — not CTC. That is fortunate: in true CTC the counter
would be cleared at `OCR0A` and never reach 0xFF, so `TOV0` would never fire,
`millis()` would stop advancing and `delay()` would never return. The board
would not finish `setup()`.

As it stands the compare-match interrupt fires at 16 MHz / 64 / 256 ≈ 976 Hz,
close enough to the intended 1 kHz for LED timing.
