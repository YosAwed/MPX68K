# Attribution

MPX68K builds on the work of many authors. This file records the
provenance of the third-party code vendored in this repository.

## px68k

- **Location**: `X68000 Shared/px68k/` (hardware emulation: FDC, SASI/SCSI,
  CRTC, ADPCM, MFP, etc.)
- **Upstream**: <https://github.com/hissorii/px68k>
- **Author**: hissorii
- **Lineage**: px68k is a portable port of **WinX68k (WinX68k高速版 /
  "Keropi")** by Kenjo (けろぴー). The Japanese comments throughout the
  hardware emulation sources originate from WinX68k.

## C68K — M68000 CPU emulator

- **Location**: `X68000 Shared/px68k/m68000/c68k/` (built as a static
  library via `c68k/c68k.xcodeproj`)
- **Author**: Copyright 2003–2004 Stephane Dallongeville
- **License**: GNU General Public License, version 2 or later. The license
  notice in the source files states the files are part of **Yabause**, the
  Sega Saturn emulator through which this copy of C68K was distributed.
  Full license text: `LICENSES/GPL-2.0.txt`.

## fmgen — FM Sound Generator

- **Location**: `X68000 Shared/px68k/fmgen/`
- **Author**: Copyright (C) cisc 1998, 2003
- **Notes**: FM synthesis core used for OPM (YM2151) emulation. The source
  headers credit Tatsuyuki Satoh (fm.c for M.A.M.E.), Hiromitsu Shioya
  (ADPCM-A), DMP-SOFT (OPNB), and KAJA (test program). fmgen is distributed
  under cisc's terms as stated in the upstream distribution.

## MPX68K application layer

- **Location**: `X68000 Shared/*.swift`, `X68000 macOS/`
- **Authors**: GOROman (original iOS port of px68k), YosAwed (macOS port
  and ongoing development), and contributors.
- **Upstream**: <https://github.com/YosAwed/MPX68K>
