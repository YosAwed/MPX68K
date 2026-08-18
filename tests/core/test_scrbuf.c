/*
 * Unit tests for the guest frame buffer module (x11/scrbuf.c) and the
 * windraw.c integration around it.
 *
 * Links the real windraw.c, crtc.c, crtc_timing.c and scrbuf.c with stubs
 * for the surrounding subsystems, and verifies:
 *   - buffer allocation, the 1024-word stride, and row addressing of the
 *     line renderer and both RGBA conversion paths
 *   - the X68000_GetFrameInfo snapshot (size, scan mode, refresh rate,
 *     geometry generation counter)
 *   - the out-of-range guards that keep renderer writes inside the buffer
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "winx68k.h"   /* vline / VLINE / VLINE_TOTAL externs */
#include "windraw.h"
#include "tvram.h"
#include "gvram.h"
#include "bg.h"
#include "crtc.h"
#include "palette.h"
#include "prop.h"
#include "sysport.h"
#include "crtc_timing.h"
#include "scrbuf.h"

/* ---- stubs for link dependencies of windraw.c / crtc.c ---- */
Win68Conf Config;
BYTE TVRAM[0x80000];
BYTE TextDirtyLine[1024];
BYTE Text_TrFlag[SCRBUF_STRIDE + 16];
WORD TextPal[256];
WORD Ibit, Pal_HalfMask, Pal_Ix2;
WORD Grp_LineBuf[1024];
WORD Grp_LineBufSP[1024];
WORD Grp_LineBufSP2[1024];
WORD *Grp_LineBuf_Active = Grp_LineBuf;
WORD *Grp_LineBuf_Draw = Grp_LineBuf;
WORD *Grp_LineBufSP_Active = Grp_LineBufSP;
WORD *Grp_LineBufSP_Draw = Grp_LineBufSP;
WORD *Grp_LineBufSP2_Active = Grp_LineBufSP2;
WORD *Grp_LineBufSP2_Draw = Grp_LineBufSP2;
int Grp_DoubleBuffer = 0;
WORD BG_LineBuf[1600];
WORD *BG_LineBuf_Active = BG_LineBuf;
WORD *BG_LineBuf_Draw = BG_LineBuf;
int BG_DoubleBuffer = 0;
BYTE BG_Regs[0x12];
long BG_HAdjust = 0;
long BG_VLINE = 0;
BYTE SysPort[7];
WORD VLINE_TOTAL = 0;
DWORD VLINE = 0;
DWORD vline = 0;
DWORD VLINEBG = 0;

void TVRAM_SetAllDirty(void) {}
void FASTCALL TVRAM_RCUpdate(void) {}
void FASTCALL Text_DrawLine_C(int opaq) { (void)opaq; }
void Mouse_ChangePos(void) {}
void FASTCALL BG_DrawLine(int opaq, int gd) { (void)opaq; (void)gd; }
void FASTCALL BG_SwapBuffers(void) {}
void FASTCALL Grp_SwapBuffers(void) {}
void Grp_DrawLine16(void) {}
void FASTCALL Grp_DrawLine8(int page, int opaq) { (void)page; (void)opaq; }
void FASTCALL Grp_DrawLine4(DWORD page, int opaq) { (void)page; (void)opaq; }
void FASTCALL Grp_DrawLine4h(void) {}
void FASTCALL Grp_DrawLine16SP(void) {}
void FASTCALL Grp_DrawLine8SP(int page) { (void)page; }
void FASTCALL Grp_DrawLine4SP(DWORD page) { (void)page; }
void FASTCALL Grp_DrawLine4hSP(void) {}
void FASTCALL Grp_DrawLine8TR(int page, int opaq) { (void)page; (void)opaq; }
void FASTCALL Grp_DrawLine4TR(DWORD page, int opaq) { (void)page; (void)opaq; }
void p6logd(const char *fmt, ...) { (void)fmt; }

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

static void set_reg(BYTE *regs, int n, WORD value)
{
    regs[n * 2] = (BYTE)(value >> 8);
    regs[n * 2 + 1] = (BYTE)(value & 0xff);
}

static void write_regs_to_crtc(const BYTE *regs)
{
    int i;
    for (i = 0; i < 0x30; i++)
        CRTC_Write(0xe80000 + i, regs[i]);
}

static void apply_768x512_31k(void)
{
    BYTE regs[48];
    memset(regs, 0, sizeof(regs));
    set_reg(regs, 0, 0x89);  set_reg(regs, 1, 0x0e);
    set_reg(regs, 2, 0x1c);  set_reg(regs, 3, 0x7c);
    set_reg(regs, 4, 0x237); set_reg(regs, 5, 0x05);
    set_reg(regs, 6, 0x28);  set_reg(regs, 7, 0x228);
    set_reg(regs, 8, 0x1b);  set_reg(regs, 20, 0x16);
    write_regs_to_crtc(regs);
}

static void apply_512x512_31k(void)
{
    BYTE regs[48];
    memset(regs, 0, sizeof(regs));
    set_reg(regs, 0, 0x5b);  set_reg(regs, 1, 0x09);
    set_reg(regs, 2, 0x11);  set_reg(regs, 3, 0x51);
    set_reg(regs, 4, 0x237); set_reg(regs, 5, 0x05);
    set_reg(regs, 6, 0x28);  set_reg(regs, 7, 0x228);
    set_reg(regs, 8, 0x1b);  set_reg(regs, 20, 0x15);
    write_regs_to_crtc(regs);
}

static void test_init_and_alloc(void)
{
    CHECK(WinDraw_Init(), "WinDraw_Init succeeds");
    CHECK(ScrBuf != NULL, "ScrBuf allocated");
    /* touching the last guard word must stay inside the allocation
     * (ASan/valgrind would flag this if the guard rows were missing) */
    ScrBuf[SCRBUF_ALLOC_WORDS - 1] = 0xffff;
    Scrbuf_Clear();
    CHECK_EQ(ScrBuf[SCRBUF_ALLOC_WORDS - 1], 0, "Scrbuf_Clear covers guard rows");
}

static void test_frame_info_and_generation(void)
{
    X68FrameInfo info, info2;

    apply_768x512_31k();
    X68000_GetFrameInfo(&info);
    CHECK(info.buffer == ScrBuf, "frame info: buffer points at ScrBuf");
    CHECK_EQ(info.width, 768, "frame info: width");
    CHECK_EQ(info.height, 512, "frame info: height");
    CHECK_EQ(info.stride_words, SCRBUF_STRIDE, "frame info: stride");
    CHECK_EQ(info.scan_mode, CRTC_SCAN_NORMAL, "frame info: scan mode");
    CHECK(info.timing_valid, "frame info: timing valid");
    CHECK(fabs(info.refresh_hz - 55.46) < 0.01, "frame info: ~55.46Hz refresh");

    X68000_GetFrameInfo(&info2);
    CHECK_EQ(info2.generation, info.generation,
             "generation stable without geometry change");

    apply_512x512_31k();
    X68000_GetFrameInfo(&info2);
    CHECK_EQ(info2.width, 512, "frame info: width after mode change");
    CHECK(info2.generation > info.generation,
          "generation bumps on geometry change");
}

static void test_line_render_stride(void)
{
    int i;

    apply_768x512_31k();
    Scrbuf_Clear();

    /* 65536-color graphics plane, priority set so the graphics line is
     * copied opaquely into ScrBuf */
    VCReg0[1] = 3;
    VCReg1[0] = 0x02;
    VCReg2[1] = 0x01;
    for (i = 0; i < 1024; i++)
        Grp_LineBuf[i] = (WORD)(0x1000 + i);

    VLINE = 5;
    TextDirtyLine[5] = 1;
    WinDraw_DrawLine();

    CHECK_EQ(ScrBuf[5 * SCRBUF_STRIDE], 0x1000, "row 5 col 0 rendered");
    CHECK_EQ(ScrBuf[5 * SCRBUF_STRIDE + 767], 0x1000 + 767,
             "row 5 col 767 rendered");
    CHECK_EQ(ScrBuf[5 * SCRBUF_STRIDE + 768], 0, "no write past TextDotX");
    CHECK_EQ(ScrBuf[4 * SCRBUF_STRIDE], 0, "row above untouched");
    CHECK_EQ(ScrBuf[6 * SCRBUF_STRIDE], 0, "row below untouched");
    CHECK_EQ(TextDirtyLine[5], 0, "dirty flag consumed");
}

static void test_line_render_1024(void)
{
    BYTE regs[48];
    int i;
    X68FrameInfo info;

    /* full 128-column (1024-dot) mode; under ASan this also proves the
     * Text_TrFlag headroom (the renderer clears TextDotX + 16 bytes) */
    memset(regs, 0, sizeof(regs));
    set_reg(regs, 0, 0x89);  set_reg(regs, 1, 0x0e);
    set_reg(regs, 2, 0x00);  set_reg(regs, 3, 0x80);
    set_reg(regs, 4, 0x237); set_reg(regs, 5, 0x05);
    set_reg(regs, 6, 0x28);  set_reg(regs, 7, 0x228);
    set_reg(regs, 8, 0x1b);  set_reg(regs, 20, 0x16);
    write_regs_to_crtc(regs);
    Scrbuf_Clear();

    X68000_GetFrameInfo(&info);
    CHECK_EQ(info.width, 1024, "1024-dot: frame info width");

    VCReg0[1] = 3;
    VCReg1[0] = 0x02;
    VCReg2[1] = 0x01;
    for (i = 0; i < 1024; i++)
        Grp_LineBuf[i] = (WORD)(0x2000 + i);

    VLINE = 9;
    TextDirtyLine[9] = 1;
    WinDraw_DrawLine();

    CHECK_EQ(ScrBuf[9 * SCRBUF_STRIDE + 1023], 0x2000 + 1023,
             "1024-dot: last column rendered");
    CHECK_EQ(ScrBuf[10 * SCRBUF_STRIDE], 0, "1024-dot: next row untouched");
}

static void test_frame_info_legacy_consistency(void)
{
    X68FrameInfo info;
    CrtcTiming t;

    /* HF=1, VRES=2: hardware interlaces, but the legacy renderer
     * double-reads and halves TextDotY. The snapshot must describe what
     * was actually rendered, so all its fields follow the renderer. */
    apply_768x512_31k();
    CRTC_Write(0xe80029, 0x19);
    X68000_GetFrameInfo(&info);
    CHECK_EQ(info.height, 256, "VRES=2: snapshot height matches renderer");
    CHECK_EQ(info.scan_mode, CRTC_SCAN_DOUBLE,
             "VRES=2: snapshot scan mode matches renderer");
    CrtcTiming_FromRegs(CRTC_Regs, 0, &t);
    CHECK_EQ(t.scan_mode, CRTC_SCAN_INTERLACE,
             "VRES=2: hardware model still reports interlace");
}

static void test_draw_guards(void)
{
    DWORD saved_x = TextDotX;

    /* rows past the buffer are dropped before any array access */
    VLINE = SCRBUF_LINES;
    WinDraw_DrawLine();
    CHECK(1, "VLINE == SCRBUF_LINES does not crash");

    /* over-wide modes are dropped and keep their dirty flag */
    TextDotX = SCRBUF_STRIDE + 8;
    VLINE = 7;
    TextDirtyLine[7] = 1;
    WinDraw_DrawLine();
    CHECK_EQ(ScrBuf[7 * SCRBUF_STRIDE], 0, "over-wide line not rendered");
    CHECK_EQ(TextDirtyLine[7], 1, "over-wide line stays dirty");
    TextDirtyLine[7] = 0;
    TextDotX = saved_x;
}

static void test_image_conversion(void)
{
    unsigned char *buf;
    unsigned long need;
    size_t off;

    apply_768x512_31k();
    Scrbuf_Clear();
    ScrBuf[3 * SCRBUF_STRIDE + 10] = 0xf800;  /* pure red at (10, 3) */

    need = 768UL * 512UL * 4UL;
    buf = calloc(1, need);
    off = (size_t)(3 * 768 + 10) * 4;

    CHECK_EQ(X68000_GetImageInto(buf, need - 1), 0,
             "GetImageInto rejects short buffer");
    CHECK_EQ(X68000_GetImageInto(buf, need), 1,
             "GetImageInto accepts exact buffer");
    CHECK(buf[off] == 0xf8 && buf[off + 1] == 0x00 &&
          buf[off + 2] == 0x00 && buf[off + 3] == 0xff,
          "GetImageInto: RGB565 red -> RGBA");

    /* legacy conversion path must use the same stride */
    memset(buf, 0, need);
    Draw_DrawFlag = 1;
    WinDraw_Draw(buf);
    CHECK(buf[off] == 0xf8 && buf[off + 3] == 0xff,
          "WinDraw_Draw: same pixel via legacy path");

    free(buf);
}

int main(void)
{
    test_init_and_alloc();
    test_frame_info_and_generation();
    test_line_render_stride();
    test_line_render_1024();
    test_frame_info_legacy_consistency();
    test_draw_guards();
    test_image_conversion();

    WinDraw_Cleanup();
    CHECK(ScrBuf == NULL, "cleanup releases ScrBuf");

    if (g_failures) {
        printf("%d test(s) FAILED\n", g_failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
