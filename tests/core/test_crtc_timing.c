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
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "winx68k.h"   /* vline / VLINE_TOTAL externs (vline -> HOGEvline) */
#include "windraw.h"
#include "tvram.h"
#include "bg.h"
#include "crtc.h"
#include "crtc_timing.h"
#include "sysport.h"
#include "prop.h"

/* ---- stubs for crtc.c/sysport.c link dependencies ---- */
BYTE TVRAM[0x80000];
BYTE TextDirtyLine[1024];
BYTE BG_Regs[0x12];
Win68Conf Config;
long BG_HAdjust = 0;
long BG_VLINE = 0;
WORD VLINE_TOTAL = 0;
DWORD vline = 0;

void TVRAM_SetAllDirty(void) {}
void FASTCALL TVRAM_RCUpdate(void) {}
void WinDraw_ChangeSize(void) {}
void Pal_ChangeContrast(int num) { (void)num; }

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

/* Only R20 and R21 are readable on real hardware (bytes 0x28-0x2b); every
 * other CRTC register reads back as 0. A guest that "saves" R04-R07 by
 * reading them therefore restores zeros, which must leave the timing model
 * invalid (so the frame scheduler keeps its previous budget) rather than
 * producing a degenerate frame or a division by zero. */
static void test_readback_zero_restore(void)
{
    BYTE regs[48];
    CrtcTiming t;
    int i;

    CRTC_Init();
    preset_512x512_31k(regs);
    write_regs_to_crtc(regs);

    for (i = 0x00; i < 0x30; i++) {
        BYTE expect = (i >= 0x28 && i <= 0x2b) ? CRTC_Regs[i] : 0x00;
        char name[96];
        snprintf(name, sizeof(name), "readback: byte 0x%02x", i);
        CHECK_EQ(CRTC_Read(0xe80000 + i), expect, name);
    }

    /* What such a guest writes back: R04-R07 zeroed, R20 preserved. */
    set_reg(regs, 4, 0x000); set_reg(regs, 5, 0x000);
    set_reg(regs, 6, 0x000); set_reg(regs, 7, 0x000);
    CrtcTiming_FromRegs(regs, 0, &t);
    CHECK(!t.valid, "zeroed vertical registers: invalid");

    /* crtc.c must survive the same writes (VLINE_TOTAL becomes 0). */
    write_regs_to_crtc(regs);
    CHECK_EQ(VLINE_TOTAL, 0, "zeroed R04: VLINE_TOTAL 0");
    CHECK(HSYNC_CLK > 0, "zeroed R04: HSYNC_CLK still positive");
}

/* HSYNC_CLK must be the real raster period. The old formula divided a
 * fixed frame budget by VLINE_TOTAL, so the vertical registers cancelled
 * themselves out and never reached real time: the 525-line 60Hz setup
 * shares its horizontal timing with plain 512x512/31kHz and must therefore
 * get the same HSYNC_CLK, but the old formula returned 344 instead of 317
 * (8% off) purely because R04 changed. mfp.c derives the GPIP HSYNC
 * position from this value, so the error was guest-visible. */
static void check_hsync_clk(const char *label, const BYTE *regs)
{
    CrtcTiming t;
    unsigned long long num, den;
    char name[128];

    write_regs_to_crtc(regs);
    CrtcTiming_FromRegs(regs, 0, &t);
    CrtcTiming_CyclesPerRaster(&t, 10000000, &num, &den);

    snprintf(name, sizeof(name), "%s: HSYNC_CLK == model cycles/raster", label);
    CHECK_EQ(HSYNC_CLK, (long long)(num / den), name);
}

static void test_hsync_clk_from_registers(void)
{
    BYTE regs[48];

    CRTC_Init();

    preset_768x512_31k(regs);
    check_hsync_clk("768x512/31k", regs);

    preset_512x512_31k(regs);
    check_hsync_clk("512x512/31k", regs);
    {
        long long standard = HSYNC_CLK;

        /* Same horizontal timing, 525-line vertical layout: the raster
         * period must not move when only R04-R07 change. */
        set_reg(regs, 4, 0x20c); set_reg(regs, 5, 0x001);
        set_reg(regs, 6, 0x022); set_reg(regs, 7, 0x202);
        check_hsync_clk("60Hz/525", regs);
        CHECK_EQ(HSYNC_CLK, standard,
                 "60Hz/525: raster period unchanged by vertical registers");
    }

    preset_256x240_15k(regs);
    check_hsync_clk("256/15k", regs);
}

static void test_hrl_write_updates_hsync_clock(void)
{
    BYTE regs[48];
    CrtcTiming t;
    unsigned long long num, den;
    int before;

    SysPort_Init();
    CRTC_Init();
    preset_512x512_31k(regs);
    write_regs_to_crtc(regs);
    before = HSYNC_CLK;

    SysPort_Write(0xe8e007, 0x02);
    CrtcTiming_FromRegs(CRTC_Regs, 1, &t);
    CrtcTiming_CyclesPerRaster(&t, 10000000, &num, &den);

    CHECK(HSYNC_CLK != before, "HRL write: raster period changed");
    CHECK_EQ(HSYNC_CLK, (long long)(num / den),
             "HRL write: HSYNC_CLK follows new divider");
    SysPort_Write(0xe8e007, 0x00);
}

static void test_field_clock_preserves_last_valid_mode(void)
{
    BYTE regs[48];
    CrtcTiming t;
    CrtcFieldClock clock;
    int active_lines;
    int valid_cycles, invalid_cycles;

    CrtcFieldClock_Init(&clock, VSYNC_HIGH, 567);
    memset(regs, 0, sizeof(regs));
    CrtcTiming_FromRegs(regs, 0, &t);
    CHECK_EQ(CrtcFieldClock_Next(&clock, &t, 10000000, 0,
                                &active_lines),
             VSYNC_HIGH, "field clock: initial fallback budget");
    CHECK_EQ(active_lines, 567, "field clock: initial fallback rasters");

    preset_512x512_31k(regs);
    set_reg(regs, 4, 0x20c); set_reg(regs, 5, 0x001);
    set_reg(regs, 6, 0x022); set_reg(regs, 7, 0x202);
    CrtcTiming_FromRegs(regs, 0, &t);
    valid_cycles = CrtcFieldClock_Next(&clock, &t, 10000000, 0x20c,
                                       &active_lines);
    CHECK_EQ(active_lines, 0x20c, "field clock: latches valid rasters");

    set_reg(regs, 4, 0); set_reg(regs, 5, 0);
    set_reg(regs, 6, 0); set_reg(regs, 7, 0);
    CrtcTiming_FromRegs(regs, 0, &t);
    invalid_cycles = CrtcFieldClock_Next(&clock, &t, 10000000, 0,
                                         &active_lines);
    CHECK(!t.valid, "field clock: zero restore is invalid");
    CHECK_EQ(active_lines, 0x20c,
             "field clock: invalid restore keeps nonzero rasters");
    CHECK(abs(invalid_cycles - valid_cycles) <= 1,
          "field clock: invalid restore keeps prior cycle budget");
}

/* The legacy vertical scan decode looks only at (R20 & 0x14) -- HF and the
 * low VRES bit -- so it cannot see VRES bit 3. Walk all eight HF/VRES
 * combinations and pin both what the emulator does today and where that
 * disagrees with the hardware model, so stage 6/7 work has a baseline that
 * fails loudly if either side moves.
 *
 * The renderer covers 1024-line modes through a second, independent path
 * ((R20 & 0x1c) == 0x1c inside the draw routines) rather than through
 * CRTC_VStep, which is why VRES=3 legitimately decodes as "normal" here. */
static void test_vertical_scan_decode_all_vres(void)
{
    static const struct {
        int hf, vres;
        int want_vstep;
        int want_height;        /* legacy TextDotY for a 512-raster window */
        CrtcScanMode want_scan; /* what the hardware model reports */
        const char *note;
    } cases[] = {
        { 0, 0, 2,  512, CRTC_SCAN_SLIT,      "15k slit: drawn like normal" },
        { 0, 1, 4, 1024, CRTC_SCAN_INTERLACE, "15k interlace" },
        { 0, 2, 2,  512, CRTC_SCAN_INTERLACE, "15k VRES=2: legacy normal" },
        { 0, 3, 4, 1024, CRTC_SCAN_INTERLACE, "15k VRES=3" },
        { 1, 0, 1,  256, CRTC_SCAN_DOUBLE,    "31k double-read" },
        { 1, 1, 2,  512, CRTC_SCAN_NORMAL,    "31k normal" },
        { 1, 2, 1,  256, CRTC_SCAN_INTERLACE, "31k VRES=2: legacy halves, hw doubles" },
        { 1, 3, 2,  512, CRTC_SCAN_INTERLACE, "31k VRES=3: 1024 lines via draw path" },
    };
    size_t i;

    CRTC_Init();

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        BYTE regs[48];
        CrtcTiming t;
        char name[160];
        BYTE r20;

        /* A 512-raster display window keeps the expected heights readable. */
        preset_512x512_31k(regs);
        r20 = (BYTE)((cases[i].hf << 4) | (cases[i].vres << 2) | 0x01);
        set_reg(regs, 20, r20);
        write_regs_to_crtc(regs);
        CrtcTiming_FromRegs(regs, 0, &t);

        snprintf(name, sizeof(name), "R20=$%02x %s: CRTC_VStep", r20, cases[i].note);
        CHECK_EQ(CRTC_VStep, cases[i].want_vstep, name);

        snprintf(name, sizeof(name), "R20=$%02x %s: TextDotY", r20, cases[i].note);
        CHECK_EQ(TextDotY, cases[i].want_height, name);

        snprintf(name, sizeof(name), "R20=$%02x %s: model scan_mode", r20, cases[i].note);
        CHECK_EQ(t.scan_mode, cases[i].want_scan, name);

        /* The model's v_step field must keep mirroring the legacy decode,
         * since that is its whole purpose. */
        snprintf(name, sizeof(name), "R20=$%02x %s: model mirrors legacy v_step",
                 r20, cases[i].note);
        CHECK_EQ(t.v_step, CRTC_VStep, name);
    }
}

/* The VRAM row stride replaces an open-coded (R20 & 0x1c) == 0x1c test that
 * was repeated in sixteen places across gvram.c and tvram.c. Only the 31kHz
 * 1024-line mode takes every second VRAM row; the 15kHz interlace mode does
 * not (it weaves both fields into a double-height buffer from the exec loop
 * instead), so the two must stay distinguishable.
 *
 * The value R20 implies is private to crtc.c, so everything here goes through
 * what the draw routines actually read -- CRTC_VramRowStepActive -- plus the
 * latch. That is the contract worth testing anyway: a register write must not
 * move the active stride, and the latch must adopt it. */
static BYTE latched_row_step(void)
{
    CRTC_LatchVramRowStep();
    return CRTC_VramRowStepActive;
}

static void test_vram_row_step(void)
{
    BYTE regs[48];

    CRTC_Init();

    preset_512x512_31k(regs);           /* HF=1, VRES=1 */
    write_regs_to_crtc(regs);
    CHECK_EQ(latched_row_step(), 1, "31k normal: one VRAM row per rendered row");

    set_reg(regs, 20, 0x1d);            /* HF=1, VRES=3: 1024-line */
    write_regs_to_crtc(regs);
    CHECK_EQ(latched_row_step(), 2, "31k 1024-line: every second VRAM row");
    CHECK_EQ(CRTC_VStep, 2, "31k 1024-line: still normal v_step");

    preset_interlace_15k(regs);         /* HF=0, VRES=1 */
    write_regs_to_crtc(regs);
    CHECK_EQ(latched_row_step(), 1, "15k interlace: rows are sequential");
    CHECK_EQ(CRTC_VStep, 4, "15k interlace: v_step 4 doubles from the loop");

    preset_256x240_15k(regs);           /* HF=0, VRES=0: slit */
    write_regs_to_crtc(regs);
    CHECK_EQ(latched_row_step(), 1, "15k slit: rows are sequential");

    /* A register write must leave the stride the current raster is drawn with
     * alone; only the latch adopts it. Otherwise a guest writing R20 partway
     * through a raster moves the source row under a mapping that already
     * assumed the other stride. */
    preset_512x512_31k(regs);
    write_regs_to_crtc(regs);
    CHECK_EQ(latched_row_step(), 1, "start from stride 1");
    set_reg(regs, 20, 0x1d);
    write_regs_to_crtc(regs);
    CHECK_EQ(CRTC_VramRowStepActive, 1,
             "R20 write alone: active stride unchanged");
    CHECK_EQ(latched_row_step(), 2, "latch: active stride adopts R20");

    /* Derived state has to follow the registers back to zero on reset. The
     * draw routines used to read R20 directly, so zeroing the registers reset
     * the stride implicitly; a cached value can go stale instead and keep
     * reading every second VRAM row after a reset out of 1024-line. Latching
     * again after the reset also proves the private pending value was reset,
     * since a stale 2 there would come straight back. */
    CRTC_Init();
    CHECK_EQ(CRTC_VramRowStepActive, 1, "after reset: active stride is 1");
    CHECK_EQ(latched_row_step(), 1, "after reset: pending stride was reset too");
}

static void test_raster_copy_runs_at_front_porch(void)
{
    const int src = 1 << 9;
    const int dst = 2 << 9;
    const int next_dst = 3 << 9;
    int i;

    CRTC_Init();
    CRTC_Write(0xe80481, 0);  /* raster-copy switch off */
    memset(TVRAM, 0, sizeof(TVRAM));
    for (i = 0; i < 512; i++)
        TVRAM[src + i] = (BYTE)(i ^ 0x5a);

    CRTC_Write(0xe8002b, 1);  /* R21: text plane 0 */
    CRTC_Write(0xe8002c, 1);  /* R22 source block */
    CRTC_Write(0xe8002d, 2);  /* R22 destination block */
    CRTC_Write(0xe80481, 8);  /* raster-copy switch on */

    CHECK(memcmp(&TVRAM[src], &TVRAM[dst], 512) != 0,
          "raster copy: enabling switch does not copy immediately");
    CRTC_HorizontalFrontPorch();
    CHECK(memcmp(&TVRAM[src], &TVRAM[dst], 512) == 0,
          "raster copy: first front porch copies selected block");

    TVRAM[src] ^= 0xff;
    CRTC_HorizontalFrontPorch();
    CHECK(TVRAM[dst] == TVRAM[src],
          "raster copy: switch remains active on later porches");

    CRTC_Write(0xe8002d, 3);  /* change destination while enabled */
    CHECK(memcmp(&TVRAM[src], &TVRAM[next_dst], 512) != 0,
          "raster copy: R22 write does not copy immediately");
    CRTC_HorizontalFrontPorch();
    CHECK(memcmp(&TVRAM[src], &TVRAM[next_dst], 512) == 0,
          "raster copy: next porch uses latest R22 value");

    CRTC_Write(0xe80481, 0);  /* switch off */
    TVRAM[src] ^= 0xff;
    CRTC_HorizontalFrontPorch();
    CHECK(TVRAM[next_dst] != TVRAM[src],
          "raster copy: switch off suppresses later porches");
}

/* Write a 16-bit CRTC register so that both byte handlers actually run.
 * CRTC_Write returns early when a byte already holds the value being
 * written, so passing the current value is a no-op; go via a scratch value
 * that differs in both bytes first. */
static void rewrite_word_reg(int n, WORD value)
{
    WORD scratch = (WORD)(value ^ 0x0101);

    CRTC_Write(0xe80000 + n * 2,     (BYTE)(scratch >> 8));
    CRTC_Write(0xe80000 + n * 2 + 1, (BYTE)(scratch & 0xff));
    CRTC_Write(0xe80000 + n * 2,     (BYTE)(value >> 8));
    CRTC_Write(0xe80000 + n * 2 + 1, (BYTE)(value & 0xff));
}

/* All three CRTC registers that feed the vertical scan must produce the same
 * decode; they used to carry three copies of it. */
static void test_vertical_scan_decode_is_single_sourced(void)
{
    BYTE regs[48];
    int via_r20, height_via_r20;

    CRTC_Init();

    /* Arrive at 31k double-read by writing R20 last... */
    preset_512x512_31k(regs);
    set_reg(regs, 20, 0x10);
    write_regs_to_crtc(regs);
    via_r20 = CRTC_VStep;
    height_via_r20 = (int)TextDotY;
    CHECK_EQ(via_r20, 1, "R20 last: double-read");

    /* ...then reach the same state again through each register in turn.
     * CRTC_VStep and TextDotY are clobbered first, so a write that never
     * reaches the decode -- e.g. because CRTC_Write early-returned on an
     * unchanged byte -- fails instead of passing on the leftover value. */
    CRTC_VStep = 0xff;
    TextDotY = 0;
    rewrite_word_reg(6, 0x28);
    CHECK_EQ(CRTC_VStep, via_r20, "R06 rewrite: decode ran, same v_step");
    CHECK_EQ((int)TextDotY, height_via_r20, "R06 rewrite: same TextDotY");

    CRTC_VStep = 0xff;
    TextDotY = 0;
    rewrite_word_reg(7, 0x228);
    CHECK_EQ(CRTC_VStep, via_r20, "R07 rewrite: decode ran, same v_step");
    CHECK_EQ((int)TextDotY, height_via_r20, "R07 rewrite: same TextDotY");

    CRTC_VStep = 0xff;
    TextDotY = 0;
    rewrite_word_reg(20, 0x10);
    CHECK_EQ(CRTC_VStep, via_r20, "R20 rewrite: decode ran, same v_step");
    CHECK_EQ((int)TextDotY, height_via_r20, "R20 rewrite: same TextDotY");
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
    test_readback_zero_restore();
    test_hsync_clk_from_registers();
    test_hrl_write_updates_hsync_clock();
    test_field_clock_preserves_last_valid_mode();
    test_vertical_scan_decode_all_vres();
    test_vram_row_step();
    test_raster_copy_runs_at_front_porch();
    test_vertical_scan_decode_is_single_sourced();
    test_legacy_agreement();

    if (g_failures) {
        printf("%d test(s) FAILED\n", g_failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
