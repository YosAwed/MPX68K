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

#ifndef _winx68k_crtc_timing
#define _winx68k_crtc_timing

#include "common.h"

// Oscillators on the X68000 mainboard (Hz).
// The dot clock is one of these divided by a small ratio selected by
// R20 bit4 (HF), R20 bits1-0 (HRES) and the HRL bit ($E8E007 bit1).
// Frequencies and the selection table follow XEiJ's CRTC analysis
// (https://stdkmd.net/xeij/), independently re-implemented here.
#define CRTC_OSC_15K_HZ		38863630UL	// 15.98kHz-group modes
#define CRTC_OSC_31K_HZ		69551990UL	// 31.5kHz-group modes
#define CRTC_OSC_VGA_HZ		50349800UL	// VGA-timing modes (later models)

typedef enum {
	CRTC_SCAN_NORMAL = 0,	// one raster per display line
	CRTC_SCAN_DOUBLE,	// 31kHz + 256-line: each display line scanned twice
	CRTC_SCAN_INTERLACE	// 15kHz + 512-line: two interlaced fields
} CrtcScanMode;

typedef struct {
	// Horizontal geometry, in character columns (1 column = 8 dots)
	int	h_total;	// columns per raster incl. blanking: R00 + 1
	int	h_sync_end;	// end of horizontal sync pulse:     R01 + 1
	int	h_disp_start;	// first displayed column:           R02
	int	h_disp_end;	// one past last displayed column:   R03

	// Vertical geometry, in rasters
	int	v_total;	// rasters per field incl. blanking: R04 + 1
	int	v_sync_end;	// end of vertical sync pulse:       R05 + 1
	int	v_disp_start;	// first displayed raster:           R06
	int	v_disp_end;	// one past last displayed raster:   R07

	// Mode bits
	int	hf;		// R20 bit4:    0 = 15.98kHz group, 1 = 31.5kHz group
	int	hres;		// R20 bits1-0: horizontal resolution select
	int	vres;		// R20 bits3-2: vertical resolution select
	int	hrl;		// $E8E007 bit1
	CrtcScanMode scan_mode;
	int	v_step;		// legacy CRTC_VStep: 1 = double-read, 2 = normal, 4 = interlace

	// Dot clock as an exact rational: osc_hz / clock_div
	DWORD	osc_hz;
	int	clock_div;

	// Logical display size in dots (what the renderer must produce;
	// height is already adjusted for double-read / interlace)
	int	width;
	int	height;

	// Convenience frequencies for monitors and tests. v_freq_hz is the
	// field rate (equal to the frame rate except in interlaced modes).
	// The scheduler must use the exact rationals, not these doubles.
	double	dot_clock_hz;
	double	h_freq_hz;
	double	v_freq_hz;

	// Nonzero when the register combination describes a scannable frame
	// (display window non-empty and inside the totals). When zero, the
	// CRTC should stop scanning; the CPU keeps running.
	int	valid;
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
