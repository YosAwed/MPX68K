// ---------------------------------------------------------------------------------------
//  CRTC_TIMING.H - Pure CRTC timing model (no side effects)
// ---------------------------------------------------------------------------------------
//
// Derives the scan geometry and clock timing that the real CRTC would
// produce from the raw register file (CRTC_Regs layout: register N is
// stored big-endian at bytes [2N]=high, [2N+1]=low) plus the HRL bit of
// system port $E8E007.
//
// This model has no side effects and touches no emulator globals, so it can
// be computed speculatively (pending timing), compared against the legacy
// fixed-frame values (VSYNC_HIGH / VSYNC_NORM split by VLINE_TOTAL), shown
// in a debug monitor, and unit-tested on the host.
//
// It is the first step of the CRTC rework:
//   raw registers -> pending timing -> active timing snapshot -> scheduler.
// Nothing in the emulator behaves differently until a caller starts feeding
// these values into the frame loop.

#ifndef PX68K_CRTC_TIMING_H
#define PX68K_CRTC_TIMING_H

#include "common.h"

// Oscillators on the X68000 mainboard (Hz).
// The dot clock is one of these divided by a small ratio selected by
// R20 bit4 (HF), R20 bits1-0 (HRES) and the HRL bit ($E8E007 bit1).
// Frequencies and the selection table follow XEiJ's CRTC analysis
// (https://stdkmd.net/xeij/, CRT_DEFAULT_FREQS = {38863632, 69551900,
// 50349800}), independently re-implemented here.
#define CRTC_OSC_15K_HZ     38863632UL  // 15.98kHz-group modes
#define CRTC_OSC_31K_HZ     69551900UL  // 31.5kHz-group modes
#define CRTC_OSC_VGA_HZ     50349800UL  // VGA-timing modes (later models)

// Scan structure, decided by HF (R20 bit4) and VRES (R20 bits3-2):
//   HF=1, VRES=1 -> NORMAL      one raster per display line
//   HF=1, VRES=0 -> DOUBLE      raster duplication: each display line is
//                               scanned twice (512 rasters show 256 lines)
//   HF=0, VRES=0 -> SLIT        15kHz 256-line scan; on the CRT each raster
//                               leaves an unlit slit line below it
//   VRES > HF    -> INTERLACE   two alternating fields per frame
// DOUBLE_EXCEPT_SP is the hardware quirk where raster duplication applies
// to text/graphics but the sprite unit keeps 512-line addressing. Deciding
// it needs sprite-controller state, which this register-only model does not
// see: the model reports DOUBLE, and a caller that knows the sprite setup
// may refine it. (XEiJ: crtNormal / crtSlit / crtDuplication /
// crtDupExceptSp / crtInterlace.)
typedef enum {
    CRTC_SCAN_NORMAL = 0,
    CRTC_SCAN_SLIT,
    CRTC_SCAN_DOUBLE,
    CRTC_SCAN_DOUBLE_EXCEPT_SP,
    CRTC_SCAN_INTERLACE
} CrtcScanMode;

typedef struct {
    // Horizontal geometry, in character columns (1 column = 8 dots).
    // The displayed columns are [h_disp_start, h_disp_end).
    int h_total;        // columns per raster incl. blanking: R00 + 1
    int h_sync_end;     // end of horizontal sync pulse:      R01 + 1
    int h_disp_start;   // first displayed column:            R02
    int h_disp_end;     // one past last displayed column:    R03

    // Vertical geometry, in rasters. Note the hardware convention differs
    // from horizontal: R06 is the last back-porch raster, so the displayed
    // rasters are [v_disp_first, v_disp_end] == [R06+1, R07] inclusive.
    // v_disp_start keeps the raw R06 value because the legacy emulator
    // (CRTC_VSTART and the vline loop in winx68k.cpp) treats [R06, R07) as
    // the display window -- one raster early. Schedulers must use
    // v_disp_first/v_disp_end, not the legacy window.
    int v_total;        // rasters per field incl. blanking:  R04 + 1
    int v_sync_end;     // end of vertical sync pulse:        R05 + 1
    int v_disp_start;   // raw R06 (legacy display-window start)
    int v_disp_first;   // first displayed raster:            R06 + 1
    int v_disp_end;     // last displayed raster:             R07

    // Mode bits
    int hf;             // R20 bit4:    0 = 15.98kHz group, 1 = 31.5kHz group
    int hres;           // R20 bits1-0: horizontal resolution select
    int vres;           // R20 bits3-2: vertical resolution select
    int hrl;            // $E8E007 bit1
    CrtcScanMode scan_mode;

    // Legacy CRTC_VStep value as crtc.c computes it from (R20 & 0x14):
    // 1 = double-read, 2 = normal, 4 = interlace. Kept for comparison with
    // the current implementation. For HF=1 with VRES>=2 the legacy decode
    // says double-read but the hardware actually interlaces (see
    // scan_mode); the legacy renderer is known-wrong there.
    int v_step;

    // Dot clock as an exact rational: osc_hz / clock_div
    DWORD osc_hz;
    int clock_div;

    // Logical display size in dots (what the renderer must produce).
    // height follows the hardware scan structure: raster count halved for
    // DOUBLE, doubled for INTERLACE, unchanged for NORMAL/SLIT.
    int width;
    int height;

    // Convenience frequencies for monitors and tests. v_freq_hz is the
    // field rate (equal to the frame rate except in interlaced modes).
    // The scheduler must use the exact rationals, not these doubles.
    double dot_clock_hz;
    double h_freq_hz;
    double v_freq_hz;

    // Nonzero when the register combination describes a scannable frame:
    //   - displayed width is 1..128 columns and fits inside h_total
    //   - the vertical registers keep XEiJ's strict ordering
    //     R05 < R06 < R07 < R04 (sync + back porch end before the
    //     display, non-empty display window, front porch remains)
    // When zero, the CRTC should stop scanning; the CPU keeps running.
    int valid;
} CrtcTiming;

// Compute the timing implied by a 48-byte CRTC register file and the HRL
// bit. Pure function: never reads or writes emulator state.
void CrtcTiming_FromRegs(const BYTE regs[48], int hrl, CrtcTiming *out);

// CPU cycles per raster / per field for a cpu_hz-clocked CPU, as an exact
// reduced fraction num/den. Accumulating num and carrying at den avoids
// long-run clock drift; callers wanting a plain number can divide.
void CrtcTiming_CyclesPerRaster(const CrtcTiming *t, DWORD cpu_hz,
    unsigned long long *num, unsigned long long *den);
void CrtcTiming_CyclesPerField(const CrtcTiming *t, DWORD cpu_hz,
    unsigned long long *num, unsigned long long *den);

#endif
