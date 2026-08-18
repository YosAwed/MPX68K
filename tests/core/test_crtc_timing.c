/*
 * Unit tests for the pure CRTC timing model (x68k/crtc_timing.c).
 *
 * Two goals:
 *  1. The model derives correct hardware timing (dot clock, sync
 *     frequencies, display size) from raw register values.
 *  2. The model agrees with the legacy globals that crtc.c computes today
 *     (TextDotX/TextDotY/CRTC_VStep/HSYNC_CLK/...), so it can later replace
 *     them without changing behavior. This links the real crtc.c and
 *     drives it through CRTC_Write.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "winx68k.h"   /* vline / VLINE_TOTAL externs (vline -> HOGEvline) */
#include "windraw.h"
#include "tvram.h"
#include "bg.h"
#include "crtc.h"
#include "crtc_timing.h"

/* ---- stubs for crtc.c link dependencies ---- */
BYTE TVRAM[0x80000];
BYTE TextDirtyLine[1024];
BYTE BG_Regs[0x12];
long BG_HAdjust = 0;
long BG_VLINE = 0;
WORD VLINE_TOTAL = 0;
DWORD vline = 0;

void TVRAM_SetAllDirty(void) {}
void FASTCALL TVRAM_RCUpdate(void) {}
void WinDraw_ChangeSize(void) {}

static int g_failures = 0;

#define CHECK(cond, name) do { \
    if (cond) { \
        printf("PASS: %s\n", name); \
    } else { \
        printf("FAIL: %s (%s:%d)\n", name, __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

#define CHECK_EQ(actual, expected, name) do { \
    long long _a = (long long)(actual), _e = (long long)(expected); \
    if (_a == _e) { \
        printf("PASS: %s\n", name); \
    } else { \
        printf("FAIL: %s: got %lld, want %lld (%s:%d)\n", \
               name, _a, _e, __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

#define CHECK_NEAR(actual, expected, tol, name) do { \
    double _a = (actual), _e = (expected); \
    if (fabs(_a - _e) <= (tol)) { \
        printf("PASS: %s\n", name); \
    } else { \
        printf("FAIL: %s: got %f, want %f +-%f (%s:%d)\n", \
               name, _a, _e, (double)(tol), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

/* Store register n into a raw 48-byte register file (big-endian pairs,
 * same layout as CRTC_Regs). */
static void set_reg(BYTE *regs, int n, WORD value)
{
    regs[n * 2] = (BYTE)(value >> 8);
    regs[n * 2 + 1] = (BYTE)(value & 0xff);
}

/* Feed a raw register file through the real CRTC_Write so crtc.c updates
 * its legacy globals. Bytes ascend, so word highs land before lows and
 * R04 lands before R20 (avoids the VLINE_TOTAL=0 division in crtc.c). */
static void write_regs_to_crtc(const BYTE *regs)
{
    int i;
    for (i = 0; i < 0x30; i++)
        CRTC_Write(0xe80000 + i, regs[i]);
}

/* Representative register sets for the standard video modes (values as
 * programmed by IOCS CRT mode switching). */
static void preset_768x512_31k(BYTE *regs)
{
    memset(regs, 0, 48);
    set_reg(regs, 0, 0x89);  set_reg(regs, 1, 0x0e);
    set_reg(regs, 2, 0x1c);  set_reg(regs, 3, 0x7c);
    set_reg(regs, 4, 0x237); set_reg(regs, 5, 0x05);
    set_reg(regs, 6, 0x28);  set_reg(regs, 7, 0x228);
    set_reg(regs, 8, 0x1b);  set_reg(regs, 20, 0x16);
}

static void preset_512x512_31k(BYTE *regs)
{
    memset(regs, 0, 48);
    set_reg(regs, 0, 0x5b);  set_reg(regs, 1, 0x09);
    set_reg(regs, 2, 0x11);  set_reg(regs, 3, 0x51);
    set_reg(regs, 4, 0x237); set_reg(regs, 5, 0x05);
    set_reg(regs, 6, 0x28);  set_reg(regs, 7, 0x228);
    set_reg(regs, 8, 0x1b);  set_reg(regs, 20, 0x15);
}

static void preset_256x256_31k(BYTE *regs)
{
    memset(regs, 0, 48);
    set_reg(regs, 0, 0x2d);  set_reg(regs, 1, 0x04);
    set_reg(regs, 2, 0x06);  set_reg(regs, 3, 0x26);
    set_reg(regs, 4, 0x237); set_reg(regs, 5, 0x05);
    set_reg(regs, 6, 0x28);  set_reg(regs, 7, 0x228);
    set_reg(regs, 8, 0x1b);  set_reg(regs, 20, 0x10);
}

static void preset_256x240_15k(BYTE *regs)
{
    memset(regs, 0, 48);
    set_reg(regs, 0, 0x25);  set_reg(regs, 1, 0x01);
    set_reg(regs, 2, 0x00);  set_reg(regs, 3, 0x20);
    set_reg(regs, 4, 0x103); set_reg(regs, 5, 0x02);
    set_reg(regs, 6, 0x10);  set_reg(regs, 7, 0x100);
    set_reg(regs, 8, 0x24);  set_reg(regs, 20, 0x00);
}

static void preset_interlace_15k(BYTE *regs)
{
    memset(regs, 0, 48);
    set_reg(regs, 0, 0x4b);  set_reg(regs, 1, 0x03);
    set_reg(regs, 2, 0x05);  set_reg(regs, 3, 0x45);
    set_reg(regs, 4, 0x103); set_reg(regs, 5, 0x02);
    set_reg(regs, 6, 0x10);  set_reg(regs, 7, 0x100);
    set_reg(regs, 8, 0x2c);  set_reg(regs, 20, 0x05);
}

/* --------------------------------------------------------------------- */
/* 1. Hardware timing derived from registers                             */
/* --------------------------------------------------------------------- */

static void test_timing_768x512(void)
{
    BYTE regs[48];
    CrtcTiming t;

    preset_768x512_31k(regs);
    CrtcTiming_FromRegs(regs, 0, &t);

    CHECK(t.valid, "768x512: valid");
    CHECK_EQ(t.width, 768, "768x512: width");
    CHECK_EQ(t.height, 512, "768x512: height");
    CHECK_EQ(t.h_total, 138, "768x512: h_total columns");
    CHECK_EQ(t.v_total, 568, "768x512: v_total rasters");
    CHECK_EQ(t.v_disp_first, 0x28 + 1, "768x512: first raster is R06+1");
    CHECK_EQ(t.v_disp_end, 0x228, "768x512: last raster is R07");
    CHECK_EQ(t.osc_hz, CRTC_OSC_31K_HZ, "768x512: 69.5519MHz oscillator");
    CHECK_EQ(t.clock_div, 2, "768x512: divide-by-2 dot clock");
    CHECK_EQ(t.scan_mode, CRTC_SCAN_NORMAL, "768x512: normal scan");
    CHECK_EQ(t.v_step, 2, "768x512: legacy v_step 2");
    CHECK_NEAR(t.h_freq_hz, 31500.0, 20.0, "768x512: ~31.5kHz hsync");
    CHECK_NEAR(t.v_freq_hz, 55.46, 0.01, "768x512: ~55.46Hz vsync");
}

static void test_timing_512x512(void)
{
    BYTE regs[48];
    CrtcTiming t;

    preset_512x512_31k(regs);
    CrtcTiming_FromRegs(regs, 0, &t);

    CHECK(t.valid, "512x512: valid");
    CHECK_EQ(t.width, 512, "512x512: width");
    CHECK_EQ(t.height, 512, "512x512: height");
    CHECK_EQ(t.clock_div, 3, "512x512: divide-by-3 dot clock");
    CHECK_NEAR(t.h_freq_hz, 31500.0, 20.0, "512x512: ~31.5kHz hsync");
    CHECK_NEAR(t.v_freq_hz, 55.46, 0.01, "512x512: ~55.46Hz vsync");
}

static void test_timing_crtc60hz_525line(void)
{
    BYTE regs[48];
    CrtcTiming t;

    /* 512x512/31kHz horizontal timing with a VGA-like 525-line vertical
     * layout (VSYNC 2, back porch 33, display 480, front porch 10). The
     * crtc60hz program measured this at ~60.00Hz on real hardware via
     * V-DISP + IOCS _ONTIME over 600 frames
     * (https://github.com/renatus-novus-x/crtc60hz), so the model must
     * reproduce it once the scheduler follows register timing. */
    preset_512x512_31k(regs);
    set_reg(regs, 4, 0x20c); set_reg(regs, 5, 0x01);
    set_reg(regs, 6, 0x22);  set_reg(regs, 7, 0x202);
    CrtcTiming_FromRegs(regs, 0, &t);

    CHECK(t.valid, "60Hz/525: valid");
    CHECK_EQ(t.v_total, 525, "60Hz/525: v_total rasters");
    CHECK_EQ(t.height, 480, "60Hz/525: 480 displayed lines");
    CHECK_EQ(t.v_disp_first, 0x22 + 1, "60Hz/525: first raster is R06+1");
    CHECK_EQ(t.v_disp_end, 0x202, "60Hz/525: last raster is R07");
    CHECK_NEAR(t.h_freq_hz, 31500.0, 20.0, "60Hz/525: ~31.5kHz hsync");
    CHECK_NEAR(t.v_freq_hz, 60.00, 0.01, "60Hz/525: ~60.00Hz vsync");
}

static void test_timing_256x256_double_read(void)
{
    BYTE regs[48];
    CrtcTiming t;

    preset_256x256_31k(regs);
    CrtcTiming_FromRegs(regs, 0, &t);

    CHECK(t.valid, "256x256/31k: valid");
    CHECK_EQ(t.width, 256, "256x256/31k: width");
    CHECK_EQ(t.height, 256, "256x256/31k: height (512 rasters double-read)");
    CHECK_EQ(t.scan_mode, CRTC_SCAN_DOUBLE, "256x256/31k: double-read scan");
    CHECK_EQ(t.v_step, 1, "256x256/31k: v_step 1");
    CHECK_EQ(t.clock_div, 6, "256x256/31k: divide-by-6 dot clock");
    CHECK_NEAR(t.h_freq_hz, 31500.0, 20.0, "256x256/31k: ~31.5kHz hsync");
}

static void test_timing_15k(void)
{
    BYTE regs[48];
    CrtcTiming t;

    preset_256x240_15k(regs);
    CrtcTiming_FromRegs(regs, 0, &t);

    CHECK(t.valid, "256/15k: valid");
    CHECK_EQ(t.width, 256, "256/15k: width");
    CHECK_EQ(t.osc_hz, CRTC_OSC_15K_HZ, "256/15k: 38.8636MHz oscillator");
    CHECK_EQ(t.clock_div, 8, "256/15k: divide-by-8 dot clock");
    CHECK_EQ(t.scan_mode, CRTC_SCAN_SLIT, "256/15k: slit scan");
    CHECK_NEAR(t.h_freq_hz, 15980.0, 20.0, "256/15k: ~15.98kHz hsync");
    CHECK_NEAR(t.v_freq_hz, 61.46, 0.01, "256/15k: ~61.46Hz vsync");
}

static void test_timing_interlace(void)
{
    BYTE regs[48];
    CrtcTiming t;

    preset_interlace_15k(regs);
    CrtcTiming_FromRegs(regs, 0, &t);

    CHECK(t.valid, "interlace: valid");
    CHECK_EQ(t.width, 512, "interlace: width");
    CHECK_EQ(t.scan_mode, CRTC_SCAN_INTERLACE, "interlace: scan mode");
    CHECK_EQ(t.v_step, 4, "interlace: v_step 4");
    CHECK_EQ(t.height, (0x100 - 0x10) * 2, "interlace: doubled height");
    /* v_freq_hz is the field rate; the full frame takes two fields */
    CHECK_NEAR(t.v_freq_hz, 61.46, 0.01, "interlace: ~61.46Hz field rate");
}

static void test_hrl_and_vga(void)
{
    BYTE regs[48];
    CrtcTiming t;

    /* HRL lowers the 31.5kHz-group dot clocks: /3 becomes /4 */
    preset_512x512_31k(regs);
    CrtcTiming_FromRegs(regs, 1, &t);
    CHECK_EQ(t.clock_div, 4, "HRL=1: 512-dot divide becomes 4");
    CHECK_EQ(t.osc_hz, CRTC_OSC_31K_HZ, "HRL=1: oscillator unchanged");

    /* HRL has no effect on the 15.98kHz group */
    preset_256x240_15k(regs);
    CrtcTiming_FromRegs(regs, 1, &t);
    CHECK_EQ(t.clock_div, 8, "HRL=1: 15k divide unchanged");

    /* HF=1 + HRES=3 selects the 50.3498MHz (VGA-timing) oscillator */
    preset_768x512_31k(regs);
    set_reg(regs, 20, 0x17);
    CrtcTiming_FromRegs(regs, 0, &t);
    CHECK_EQ(t.osc_hz, CRTC_OSC_VGA_HZ, "HRES=3: VGA oscillator");
    CHECK_EQ(t.clock_div, 2, "HRES=3: divide-by-2");
}

static void test_1024line_interlace(void)
{
    BYTE regs[48];
    CrtcTiming t;

    /* HF=1 with VRES=2 (1024-line content) interlaces on hardware; the
     * legacy (R20 & 0x14) decode misreads it as double-read. The model
     * reports the hardware scan mode while pinning the legacy v_step. */
    preset_768x512_31k(regs);
    set_reg(regs, 20, 0x19);   /* HF=1, VRES=2, HRES=1 */
    CrtcTiming_FromRegs(regs, 0, &t);
    CHECK_EQ(t.scan_mode, CRTC_SCAN_INTERLACE, "31k/VRES=2: interlace scan");
    CHECK_EQ(t.v_step, 1, "31k/VRES=2: legacy v_step stays 1 (known-wrong)");
    CHECK_EQ(t.height, 1024, "31k/VRES=2: doubled height");
}

static void test_invalid_window(void)
{
    BYTE regs[48];
    CrtcTiming t;

    preset_768x512_31k(regs);
    set_reg(regs, 3, 0x10);    /* h_disp_end < h_disp_start */
    CrtcTiming_FromRegs(regs, 0, &t);
    CHECK(!t.valid, "reversed horizontal window: invalid");
    CHECK_EQ(t.width, 0, "reversed horizontal window: width clamped to 0");

    preset_768x512_31k(regs);
    set_reg(regs, 0, 0xff);    /* widen h_total so only the column count trips */
    set_reg(regs, 2, 0x00);
    set_reg(regs, 3, 0x90);    /* 144 columns > 128-column line buffer */
    CrtcTiming_FromRegs(regs, 0, &t);
    CHECK(!t.valid, "display wider than 128 columns: invalid");

    preset_768x512_31k(regs);
    set_reg(regs, 5, 0x30);    /* R05 > R06: sync runs into the display */
    CrtcTiming_FromRegs(regs, 0, &t);
    CHECK(!t.valid, "vertical sync overlapping display: invalid");

    /* XEiJ requires strict R05 < R06 and R07 < R04 */
    preset_768x512_31k(regs);
    set_reg(regs, 5, 0x28);    /* R05 == R06: no back porch */
    CrtcTiming_FromRegs(regs, 0, &t);
    CHECK(!t.valid, "R05 == R06: invalid");

    preset_768x512_31k(regs);
    set_reg(regs, 7, 0x236);   /* R07 == R04 - 1: minimal front porch */
    CrtcTiming_FromRegs(regs, 0, &t);
    CHECK(t.valid, "R07 == R04 - 1: still scannable");
    set_reg(regs, 7, 0x237);   /* R07 == R04: no front porch */
    CrtcTiming_FromRegs(regs, 0, &t);
    CHECK(!t.valid, "R07 == R04: invalid");
}

static void test_cycle_rationals(void)
{
    BYTE regs[48];
    CrtcTiming t;
    unsigned long long num, den;
    double per_raster, per_field;

    preset_768x512_31k(regs);
    CrtcTiming_FromRegs(regs, 0, &t);

    /* 10MHz CPU: 10e6 * 8 * 138 * 2 / 69551990 ~= 317.46 cycles/raster */
    CrtcTiming_CyclesPerRaster(&t, 10000000, &num, &den);
    per_raster = (double)num / (double)den;
    CHECK_NEAR(per_raster, 317.46, 0.01, "cycles/raster at 10MHz");

    CrtcTiming_CyclesPerField(&t, 10000000, &num, &den);
    per_field = (double)num / (double)den;
    CHECK_NEAR(per_field, per_raster * 568.0, 0.001, "cycles/field = raster * v_total");
    /* legacy whole-frame budget is VSYNC_HIGH = 180310 cycles */
    CHECK_NEAR(per_field, (double)VSYNC_HIGH, 500.0, "cycles/field near legacy VSYNC_HIGH");
}

/* --------------------------------------------------------------------- */
/* 2. Agreement with the legacy crtc.c globals                           */
/* --------------------------------------------------------------------- */

static void check_against_legacy(const char *label, const BYTE *regs)
{
    CrtcTiming t;
    unsigned long long num, den;
    double model_clk, legacy_clk;
    char name[128];

    write_regs_to_crtc(regs);
    CrtcTiming_FromRegs(regs, 0, &t);

    snprintf(name, sizeof(name), "%s: width == TextDotX", label);
    CHECK_EQ(t.width, (long long)TextDotX, name);
    snprintf(name, sizeof(name), "%s: height == TextDotY", label);
    CHECK_EQ(t.height, (long long)TextDotY, name);
    snprintf(name, sizeof(name), "%s: v_step == CRTC_VStep", label);
    CHECK_EQ(t.v_step, CRTC_VStep, name);
    snprintf(name, sizeof(name), "%s: h window == CRTC_HSTART/HEND", label);
    CHECK(t.h_disp_start == CRTC_HSTART && t.h_disp_end == CRTC_HEND, name);
    snprintf(name, sizeof(name), "%s: v window == CRTC_VSTART/VEND", label);
    CHECK(t.v_disp_start == CRTC_VSTART && t.v_disp_end == CRTC_VEND, name);
    snprintf(name, sizeof(name), "%s: v_total == VLINE_TOTAL + 1", label);
    CHECK_EQ(t.v_total, VLINE_TOTAL + 1, name);

    /* The legacy per-line cycle budget divides a fixed frame budget by
     * VLINE_TOTAL (not +1), so it is only an approximation of the real
     * timing; the model must stay within 1% of it. */
    CrtcTiming_CyclesPerRaster(&t, 10000000, &num, &den);
    model_clk = (double)num / (double)den;
    legacy_clk = (double)HSYNC_CLK;
    snprintf(name, sizeof(name), "%s: cycles/raster within 1%% of HSYNC_CLK", label);
    CHECK(fabs(model_clk - legacy_clk) / legacy_clk < 0.01, name);
}

static void test_legacy_agreement(void)
{
    BYTE regs[48];

    CRTC_Init();

    preset_768x512_31k(regs);
    check_against_legacy("legacy 768x512/31k", regs);

    preset_512x512_31k(regs);
    check_against_legacy("legacy 512x512/31k", regs);

    preset_256x256_31k(regs);
    check_against_legacy("legacy 256x256/31k", regs);

    preset_256x240_15k(regs);
    check_against_legacy("legacy 256/15k", regs);

    preset_interlace_15k(regs);
    check_against_legacy("legacy interlace/15k", regs);
}

int main(void)
{
    test_timing_768x512();
    test_timing_512x512();
    test_timing_crtc60hz_525line();
    test_timing_256x256_double_read();
    test_timing_15k();
    test_timing_interlace();
    test_1024line_interlace();
    test_hrl_and_vga();
    test_invalid_window();
    test_cycle_rationals();
    test_legacy_agreement();

    if (g_failures) {
        printf("%d test(s) FAILED\n", g_failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
