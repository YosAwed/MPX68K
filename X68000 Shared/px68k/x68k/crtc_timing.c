// ---------------------------------------------------------------------------------------
//  CRTC_TIMING.C - Pure CRTC timing model (no side effects)
// ---------------------------------------------------------------------------------------

#include "crtc_timing.h"

// Register N is stored big-endian at bytes [2N] / [2N+1] (see CRTC_Write).
static WORD reg_word(const BYTE *regs, int n)
{
	return (WORD)(((WORD)regs[n * 2] << 8) | regs[n * 2 + 1]);
}

static unsigned long long gcd_ull(unsigned long long a, unsigned long long b)
{
	while (b) {
		unsigned long long t = a % b;
		a = b;
		b = t;
	}
	return a;
}

void CrtcTiming_FromRegs(const BYTE regs[48], int hrl, CrtcTiming *out)
{
	// Dot clock selection, indexed by HRL<<3 | HF<<2 | HRES.
	// Values follow the measured hardware table (cf. XEiJ CRTC notes):
	//   HF=0 (15.98kHz group): 38.86363MHz / {8,4,8,8}, HRL ignored
	//   HF=1 (31.5kHz group):  69.55199MHz / {6,3,2}, HRES=3 -> 50.3498MHz / 2
	//                          HRL=1 changes /6 -> /8 and /3 -> /4
	static const DWORD osc_hz[3] = {
		CRTC_OSC_15K_HZ, CRTC_OSC_31K_HZ, CRTC_OSC_VGA_HZ
	};
	static const BYTE osc_sel[16] = {
		0, 0, 0, 0,  1, 1, 1, 2,	// HRL=0
		0, 0, 0, 0,  1, 1, 1, 2		// HRL=1
	};
	static const BYTE div_sel[16] = {
		8, 4, 8, 8,  6, 3, 2, 2,	// HRL=0
		8, 4, 8, 8,  8, 4, 2, 2		// HRL=1
	};

	BYTE r20 = (BYTE)(reg_word(regs, 20) & 0xff);
	int idx;
	int disp_rasters;

	ZeroMemory(out, sizeof(*out));

	// R00-R03 are 8-bit counters, R04-R07 are 10-bit counters.
	out->h_total      = (reg_word(regs, 0) & 0xff) + 1;
	out->h_sync_end   = (reg_word(regs, 1) & 0xff) + 1;
	out->h_disp_start = reg_word(regs, 2) & 0xff;
	out->h_disp_end   = reg_word(regs, 3) & 0xff;

	out->v_total      = (reg_word(regs, 4) & 0x3ff) + 1;
	out->v_sync_end   = (reg_word(regs, 5) & 0x3ff) + 1;
	out->v_disp_start = reg_word(regs, 6) & 0x3ff;
	out->v_disp_end   = reg_word(regs, 7) & 0x3ff;

	out->hf   = (r20 >> 4) & 1;
	out->vres = (r20 >> 2) & 3;
	out->hres = r20 & 3;
	out->hrl  = hrl ? 1 : 0;

	idx = (out->hrl << 3) | (out->hf << 2) | out->hres;
	out->osc_hz    = osc_hz[osc_sel[idx]];
	out->clock_div = div_sel[idx];

	// Raster-to-display-line relation, matching the legacy CRTC_VStep
	// decode of (R20 & 0x14) in crtc.c.
	if ((r20 & 0x14) == 0x10) {		// 31kHz scan of a 256-line screen
		out->scan_mode = CRTC_SCAN_DOUBLE;
		out->v_step = 1;
	} else if ((r20 & 0x14) == 0x04) {	// 15kHz scan of a 512-line screen
		out->scan_mode = CRTC_SCAN_INTERLACE;
		out->v_step = 4;
	} else {
		out->scan_mode = CRTC_SCAN_NORMAL;
		out->v_step = 2;
	}

	out->width = (out->h_disp_end - out->h_disp_start) * 8;
	if (out->width < 0)
		out->width = 0;

	disp_rasters = out->v_disp_end - out->v_disp_start;
	if (disp_rasters < 0)
		disp_rasters = 0;
	switch (out->scan_mode) {
	case CRTC_SCAN_DOUBLE:
		out->height = disp_rasters / 2;
		break;
	case CRTC_SCAN_INTERLACE:
		out->height = disp_rasters * 2;
		break;
	default:
		out->height = disp_rasters;
		break;
	}

	out->dot_clock_hz = (double)out->osc_hz / out->clock_div;
	out->h_freq_hz = out->dot_clock_hz / (8.0 * out->h_total);
	out->v_freq_hz = out->h_freq_hz / out->v_total;

	out->valid = (out->h_disp_end > out->h_disp_start) &&
	             (out->h_disp_end <= out->h_total) &&
	             (out->v_disp_end > out->v_disp_start) &&
	             (out->v_disp_end <= out->v_total);
}

// cycles/raster = cpu_hz * (8 * h_total * clock_div / osc_hz) seconds
void CrtcTiming_CyclesPerRaster(const CrtcTiming *t, DWORD cpu_hz,
    unsigned long long *num, unsigned long long *den)
{
	unsigned long long n = (unsigned long long)cpu_hz * 8ULL *
	                       (unsigned long long)t->h_total *
	                       (unsigned long long)t->clock_div;
	unsigned long long d = t->osc_hz;
	unsigned long long g = gcd_ull(n, d);

	if (g == 0)
		g = 1;
	*num = n / g;
	*den = d / g;
}

void CrtcTiming_CyclesPerField(const CrtcTiming *t, DWORD cpu_hz,
    unsigned long long *num, unsigned long long *den)
{
	unsigned long long n, d, g;

	CrtcTiming_CyclesPerRaster(t, cpu_hz, &n, &d);
	n *= (unsigned long long)t->v_total;
	g = gcd_ull(n, d);
	if (g == 0)
		g = 1;
	*num = n / g;
	*den = d / g;
}
