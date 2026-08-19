// ---------------------------------------------------------------------------------------
//  CRTC_TIMING.C - Pure CRTC timing model (no side effects)
// ---------------------------------------------------------------------------------------

#include "crtc_timing.h"

// Register N is stored big-endian at bytes [2N] / [2N+1] (see CRTC_Write).
static WORD reg_word(const BYTE *regs, int n)
{
    return (WORD)(((WORD)regs[n * 2] << 8) | regs[n * 2 + 1]);
}

CrtcScanMode CrtcTiming_DecodeScanMode(BYTE r20)
{
    int hf = (r20 >> 4) & 1;
    int vres = (r20 >> 2) & 3;

    if (vres > hf)
        return CRTC_SCAN_INTERLACE;
    if (hf && vres == 0)
        return CRTC_SCAN_DOUBLE;
    if (!hf && vres == 0)
        return CRTC_SCAN_SLIT;
    return CRTC_SCAN_NORMAL;
}

void CrtcTiming_MapRaster(CrtcScanMode mode, int raster_offset,
                          int field_parity, CrtcRasterMap *out)
{
    out->draw = 1;
    switch (mode) {
    case CRTC_SCAN_INTERLACE:
        out->line = raster_offset * 2 + (field_parity & 1);
        break;
    case CRTC_SCAN_DOUBLE:
    case CRTC_SCAN_DOUBLE_EXCEPT_SP:
        out->line = raster_offset / 2;
        out->draw = raster_offset & 1;
        break;
    default:
        out->line = raster_offset;
        break;
    }
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
    //   HF=0 (15.98kHz group): 38.863632MHz / {8,4,8,8}, HRL ignored
    //   HF=1 (31.5kHz group):  69.551900MHz / {6,3,2}, HRES=3 -> 50.3498MHz / 2
    //                          HRL=1 changes /6 -> /8 and /3 -> /4
    static const DWORD osc_hz[3] = {
        CRTC_OSC_15K_HZ, CRTC_OSC_31K_HZ, CRTC_OSC_VGA_HZ
    };
    static const BYTE osc_sel[16] = {
        0, 0, 0, 0,  1, 1, 1, 2,    // HRL=0
        0, 0, 0, 0,  1, 1, 1, 2     // HRL=1
    };
    static const BYTE div_sel[16] = {
        8, 4, 8, 8,  6, 3, 2, 2,    // HRL=0
        8, 4, 8, 8,  8, 4, 2, 2     // HRL=1
    };

    BYTE r20 = (BYTE)(reg_word(regs, 20) & 0xff);
    int idx;
    int disp_cols;
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
    out->v_disp_first = out->v_disp_start + 1;
    out->v_disp_end   = reg_word(regs, 7) & 0x3ff;

    out->hf   = (r20 >> 4) & 1;
    out->vres = (r20 >> 2) & 3;
    out->hres = r20 & 3;
    out->hrl  = hrl ? 1 : 0;

    idx = (out->hrl << 3) | (out->hf << 2) | out->hres;
    out->osc_hz    = osc_hz[osc_sel[idx]];
    out->clock_div = div_sel[idx];

    // Scan structure (XEiJ: crtInterlace = HF+1 <= VRES, crtDuplication =
    // HF==1 && VRES==0, crtSlit = HF==0 && VRES==0, otherwise normal).
    // DOUBLE_EXCEPT_SP needs sprite-controller state and is never returned
    // by this register-only model.
    out->scan_mode = CrtcTiming_DecodeScanMode(r20);

    if (out->scan_mode == CRTC_SCAN_DOUBLE)
        out->v_step = 1;
    else if (out->scan_mode == CRTC_SCAN_INTERLACE)
        out->v_step = 4;
    else
        out->v_step = 2;

    disp_cols = out->h_disp_end - out->h_disp_start;
    out->width = disp_cols > 0 ? disp_cols * 8 : 0;

    // Displayed rasters are [R06+1, R07] inclusive: R07 - R06 rasters.
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

    // The CRTC line buffer holds 128 columns (1024 dots); wider settings
    // cannot be scanned. Vertically XEiJ requires the strict ordering
    // R05 < R06 < R07 < R04: sync + back porch end before the display,
    // the display window is non-empty, and a front porch remains.
    out->valid = (disp_cols > 0) && (disp_cols <= 128) &&
                 (out->h_disp_end <= out->h_total) &&
                 (out->v_sync_end - 1 < out->v_disp_start) &&
                 (out->v_disp_end > out->v_disp_start) &&
                 (out->v_disp_end < out->v_total - 1);
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

void CrtcFieldClock_Init(CrtcFieldClock *clock, int fallback_cycles,
    int fallback_vline_total)
{
    ZeroMemory(clock, sizeof(*clock));
    clock->numerator = fallback_cycles > 0 ?
        (unsigned long long)fallback_cycles : 1ULL;
    clock->denominator = 1;
    clock->vline_total = fallback_vline_total > 0 ? fallback_vline_total : 1;
}

int CrtcFieldClock_Next(CrtcFieldClock *clock, const CrtcTiming *timing,
    DWORD cpu_hz, int current_vline_total, int *active_vline_total)
{
    unsigned long long num, den, acc;

    if (clock->denominator == 0)
        CrtcFieldClock_Init(clock, 1, 1);

    if (timing->valid && current_vline_total > 0) {
        CrtcTiming_CyclesPerField(timing, cpu_hz, &num, &den);
        if (den != 0 && num != 0) {
            // A remainder is expressed in units of its denominator. It
            // cannot be carried unchanged into a different video mode.
            if (num != clock->numerator || den != clock->denominator)
                clock->remainder = 0;
            clock->numerator = num;
            clock->denominator = den;
            clock->vline_total = current_vline_total;
        }
    }

    acc = clock->numerator + clock->remainder;
    clock->remainder = acc % clock->denominator;
    *active_vline_total = clock->vline_total;
    return (int)(acc / clock->denominator);
}
