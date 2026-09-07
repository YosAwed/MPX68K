# Embedded Nuked SC-55

Upstream: https://github.com/nukeykt/Nuked-SC55
Pinned commit: 9c98ab97b8d7b1af841845bbd65c4d2371f33ad0
License: GPL-2.0-or-later; see LICENSE. Copyright headers are retained.

Only source/header files needed by the emulation core are vendored. No ROMs,
SDL/RtMidi, LCD artwork, or upstream executable are included.

Local changes dated 2026-09-07:

- `core/mcu.cpp`: replace standalone SDL audio/thread/main/ROM file loader with
  the serialized `X68SC55_*` API; retain wave unscrambling and MCU peripheral
  behavior; stub write-only LCD display functions; reset RAM, UART, analog and
  timer state; bound UART writes and render work; silence repeated error traps;
  provide an explicit analog-pin fallback required by the host compiler.
- `core/mcu.h`: replace SDL atomic button type with a worker-owned integer.
- `core/submcu.cpp`: remove an unused SDL audio include.
- `core/mcu_timer.h`: expose the existing timer reset function.
- Other upstream files are unchanged.

The bridge is single-instance and not internally thread-safe: all calls must be
serialized by SC55Synthesizer's worker. It renders stereo float audio at 64 kHz,
matching the upstream original-SC-55 output. Core files use -O2 in Debug as well
as Release because instruction-by-instruction emulation is time-sensitive.

Only SC-55 mk1 ROM sets are offered by this integration. Program ROM2 accepts
256 or 512 KiB as upstream does. Files are checked by name, size and nonempty
contents, not by a known-ROM hash catalogue. Never add firmware to this directory.

Validation: `make -C tests/sc55` tests invalid sizes, reset determinism, rendering
and UART backpressure using synthetic firmware with address/undefined sanitizers.
It cannot establish Roland firmware compatibility or audible correctness.

On macOS, `sh tests/sc55/run-host-tests.sh` additionally checks folder validation,
AVAudioEngine start/stop, pause/resume and reconfiguration with silent synthetic
firmware. This needs an available audio output device.

Real-ROM validation (2026-09-07): user-supplied SC-55 v1.21 passed note-on/off
waveform checks and reset reproducibility. Cold boot needs more than the original
one-second warmup; the host now advances four emulated seconds before accepting
MIDI. The optional check subtracts DC before measuring RMS, so a DC offset cannot
be mistaken for a sounding note:

```
make -C tests/sc55 rom ROM_DIR='/path/to/your/SC-55 ROM folder'
```

The macOS app's folder selection, test-note action and saved-folder restoration
were also exercised with this ROM set. No ROM files are stored in the repository.
