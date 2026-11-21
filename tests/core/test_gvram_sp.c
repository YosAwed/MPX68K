/*
 * Grp_DrawLine16SP(): the 65536-colour base page for special priority
 * (R2 bit12 + bit10). Links the real gvram.c.
 *
 * The 65536-colour palette is laid out in pairs (Inside X68000 P.216 as
 * corrected by the errata): for palette code PH<<8|PL the colour is
 *   CLPL = [$E82000 + ((PL & $FE) << 1) + (PL & 1)]
 *   CHPH = [$E82002 + ((PH & $FE) << 1) + (PH & 1)]
 * With B/P, an odd code marks the special area and shows the colour of the
 * code made even; an even code is an ordinary pixel, transparent when 0.
 */
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "gvram.h"

/* Globals gvram.c expects from the rest of the core. */
BYTE  Pal_Regs[1024];
WORD  Pal16[65536];
WORD  GrphPal[256];
WORD  Ibit;
WORD  Pal_HalfMask;
WORD  Pal_Ix2;
BYTE  CRTC_Regs[48];
WORD  CRTC_FastClrMask;
BYTE  CRTC_VramRowStepActive = 1;
DWORD GrphScrollX[4];
DWORD GrphScrollY[4];
DWORD TextDotX = 8;
DWORD VLINE;
BYTE  TextDirtyLine[1024];

static int failures;

#define CHECK_EQ(actual, expected, name) do { \
    unsigned long a_ = (unsigned long)(actual), e_ = (unsigned long)(expected); \
    if (a_ == e_) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s (got 0x%04lx, expected 0x%04lx)\n", name, a_, e_); failures++; } \
} while (0)

/* Pal16[] stands for the host colour conversion; make it recognisable. */
static WORD host(WORD x68color) { return (WORD)(x68color ^ 0x5a5a); }

/* The low colour byte comes from PL, the high byte from PH, each in the
 * paired layout. */
static void set_cl(BYTE pl, BYTE v) { Pal_Regs[((pl & 0xfe) << 1) + (pl & 1)] = v; }
static void set_ch(BYTE ph, BYTE v) { Pal_Regs[2 + ((ph & 0xfe) << 1) + (ph & 1)] = v; }

/* Puts palette code `code` at pixel x of line 0 (GVRAM word, host order). */
static void set_pixel(int x, WORD code)
{
    GVRAM[x * 2] = code & 0xff;
    GVRAM[x * 2 + 1] = code >> 8;
}

int main(void)
{
    int i;

    GVRAM_Init();
    for (i = 0; i < 65536; i++) Pal16[i] = host((WORD)i);

    /* Fill the palette with a pattern first, so reading a byte the layout
     * does not name gives a value no check expects. */
    for (i = 0; i < 1024; i++) Pal_Regs[i] = (BYTE)(0x40 + (i & 0x3f));
    set_cl(0x34, 0xb2); set_ch(0x12, 0xa1);  /* 0x1234 -> 0xa1b2 */
    set_cl(0x36, 0xd4);                      /* 0x1237 made even -> 0xa1d4 */
    set_cl(0x37, 0xf6);                      /* must not be used for 0x1237 */
    set_cl(0x02, 0x88); set_ch(0x03, 0x77);  /* 0x0303 made even -> 0x7788 */

    set_pixel(0, 0x1234);
    set_pixel(1, 0x1237);
    set_pixel(2, 0x0000);
    set_pixel(3, 0x0303);

    VLINE = 0;
    Grp_DrawLine16SP();

    CHECK_EQ(Grp_LineBufSP2[0], host(0xa1b2), "even code: colour from the paired palette layout");
    CHECK_EQ(Grp_LineBufSP[0], 0, "even code: not in the special area");
    CHECK_EQ(Grp_LineBufSP[1], host(0xa1d4), "odd code: special area, colour of the code made even");
    CHECK_EQ(Grp_LineBufSP2[1], 0, "odd code: not an ordinary pixel");
    CHECK_EQ(Grp_LineBufSP[2], 0, "code 0: transparent (special)");
    CHECK_EQ(Grp_LineBufSP2[2], 0, "code 0: transparent (ordinary)");
    CHECK_EQ(Grp_LineBufSP[3], host(0x7788), "odd code 0x0303: high byte from the $E82002 side");

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
