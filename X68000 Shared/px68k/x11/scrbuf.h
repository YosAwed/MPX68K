// ---------------------------------------------------------------------------------------
//  SCRBUF.H - Guest frame buffer and frame-geometry snapshot API
// ---------------------------------------------------------------------------------------
//
// Owns the 16bpp (RGB565) buffer the line renderer draws into, and exposes
// an atomic "everything the host needs to present one frame" snapshot.
// Until now the host pieced the geometry together from TextDotX/TextDotY
// around the emulation step, so a mid-step CRTC mode change could pair a
// stale size with new pixels; X68000_GetFrameInfo returns the whole set in
// one call made after the step.

#ifndef PX68K_SCRBUF_H
#define PX68K_SCRBUF_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Row stride and row count of ScrBuf. Sized for the largest scannable
// screen (128 columns = 1024 dots wide; 1024 lines in interlaced modes)
// plus guard rows so a renderer overrun of one row can never leave the
// allocation. The previous layout (768-word stride inside an 800x600
// allocation) could not hold 1024-dot modes at all.
#define SCRBUF_STRIDE       1024
#define SCRBUF_LINES        1024
#define SCRBUF_GUARD_LINES  2
#define SCRBUF_ALLOC_WORDS  (SCRBUF_STRIDE * (SCRBUF_LINES + SCRBUF_GUARD_LINES))

// Guest frame buffer: SCRBUF_LINES rows of SCRBUF_STRIDE RGB565 words.
// Allocated by Scrbuf_Init (called from WinDraw_Init).
extern WORD *ScrBuf;

int  Scrbuf_Init(void);
void Scrbuf_Cleanup(void);
void Scrbuf_Clear(void);

// Record the current TextDotX/TextDotY; bumps the geometry generation
// when they changed. Called whenever the CRTC recomputes the screen size.
void Scrbuf_NoteGeometry(void);

// One consistent frame description for the host presentation layer.
//
// Contract: buffer/width/height/stride/scan_mode all describe the frame
// as the current renderer produced it. In interlace modes the buffer is a
// persistent weave: rows of field_parity are from the latest field and the
// opposite rows remain from the preceding field. refresh_hz and timing_valid
// report the hardware timing model used for frame pacing and scannability.
typedef struct {
    const WORD  *buffer;        // SCRBUF_STRIDE-word rows, RGB565
    int          width;         // rendered dots per row (<= SCRBUF_STRIDE)
    int          height;        // rendered rows (<= SCRBUF_LINES)
    int          stride_words;  // words from one row to the next
    int          scan_mode;     // CrtcScanMode the renderer applied
    int          field_parity;  // parity of the most recently rendered field
    double       refresh_hz;    // field rate implied by the CRTC registers
    int          timing_valid;  // CrtcTiming.valid for the registers
    unsigned int generation;    // bumped on every geometry change
} X68FrameInfo;

// Fill *out from the current emulator state. Call after the emulation
// step returns, from the same thread; the values are then mutually
// consistent (the emulator only mutates them inside the step).
void X68000_GetFrameInfo(X68FrameInfo *out);

#ifdef __cplusplus
}
#endif

#endif
