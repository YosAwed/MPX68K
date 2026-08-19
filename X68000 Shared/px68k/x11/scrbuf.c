// ---------------------------------------------------------------------------------------
//  SCRBUF.C - Guest frame buffer and frame-geometry snapshot API
// ---------------------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "crtc.h"
#include "sysport.h"
#include "crtc_timing.h"
#include "scrbuf.h"

WORD *ScrBuf = 0;

static unsigned int s_generation = 0;
static DWORD s_noted_width = 0;
static DWORD s_noted_height = 0;

int Scrbuf_Init(void)
{
    if (!ScrBuf)
        ScrBuf = (WORD *)malloc(SCRBUF_ALLOC_WORDS * sizeof(WORD));
    if (!ScrBuf)
        return FALSE;
    Scrbuf_Clear();
    return TRUE;
}

void Scrbuf_Cleanup(void)
{
    free(ScrBuf);
    ScrBuf = 0;
}

void Scrbuf_Clear(void)
{
    if (ScrBuf)
        memset(ScrBuf, 0, SCRBUF_ALLOC_WORDS * sizeof(WORD));
}

void Scrbuf_NoteGeometry(void)
{
    if (TextDotX != s_noted_width || TextDotY != s_noted_height) {
        // Log the mode change here rather than in the RGBA conversion: the
        // old "TextDotX:" trace lived in WinDraw_Draw and disappeared from
        // hosts that switched to X68000_GetImageInto, which left screen-mode
        // switching with no diagnostic trail. Geometry changes are rare, so
        // this costs nothing per frame.
        p6logd("Scrbuf geometry: %lux%lu (gen %u)\n",
               (unsigned long)TextDotX, (unsigned long)TextDotY,
               s_generation + 1);
        s_noted_width = TextDotX;
        s_noted_height = TextDotY;
        s_generation++;
    }
}

void X68000_GetFrameInfo(X68FrameInfo *out)
{
    CrtcTiming t;

    // Geometry may have changed without passing through WinDraw_ChangeSize
    // (e.g. a stretched window keeps the same WindowX/Y); note it here so
    // the generation is always current when read.
    Scrbuf_NoteGeometry();
    CrtcTiming_FromRegs(CRTC_Regs, SysPort[4] & 2, &t);

    out->buffer = ScrBuf;
    out->width = TextDotX > SCRBUF_STRIDE ? SCRBUF_STRIDE : (int)TextDotX;
    out->height = TextDotY > SCRBUF_LINES ? SCRBUF_LINES : (int)TextDotY;
    out->stride_words = SCRBUF_STRIDE;

    // The snapshot describes the frame the current renderer produced, so
    // scan_mode must come from the same legacy (R20 & 0x14) decode that
    // sized TextDotY (v_step) -- not from the hardware model, which
    // disagrees for HF=1 && VRES>=2 (hardware interlaces, the legacy
    // renderer double-reads). Renderer and snapshot switch to the
    // hardware scan mode together in a later stage.
    switch (t.v_step) {
    case 1:
        out->scan_mode = CRTC_SCAN_DOUBLE;
        break;
    case 4:
        out->scan_mode = CRTC_SCAN_INTERLACE;
        break;
    default:
        out->scan_mode = (t.hf == 0 && t.vres == 0) ? CRTC_SCAN_SLIT
                                                    : CRTC_SCAN_NORMAL;
        break;
    }

    out->field_parity = 0;
    out->refresh_hz = t.v_freq_hz;
    out->timing_valid = t.valid;
    out->generation = s_generation;
}
