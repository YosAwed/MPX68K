#ifndef _winx68k_crtc
#define _winx68k_crtc

#include "common.h"
#include "crtc_timing.h"

#define	VSYNC_HIGH	180310L
#define	VSYNC_NORM	162707L

extern	BYTE	CRTC_Regs[48];
extern	BYTE	CRTC_Mode;
extern	WORD	CRTC_VSTART, CRTC_VEND;
extern	WORD	CRTC_HSTART, CRTC_HEND;
extern	DWORD	TextDotX, TextDotY;
extern	DWORD	TextScrollX, TextScrollY;
extern	BYTE	VCReg0[2];
extern	BYTE	VCReg1[2];
extern	BYTE	VCReg2[2];
extern	WORD	CRTC_IntLine;
extern	BYTE	CRTC_FastClr;
extern	BYTE	CRTC_DispScan;
extern	DWORD	CRTC_FastClrLine;
extern	WORD	CRTC_FastClrMask;
extern	BYTE	CRTC_VStep;
extern BYTE CRTC_FieldParity;
// VRAM rows consumed per woven-buffer row. Interlace advances VLINE itself
// by two, so this remains one for all currently supported scan modes.
//
// Split like the field clock. The value R20 implies is private to crtc.c;
// CRTC_LatchScanState() adopts it for the raster about to be drawn, and the
// frame loop calls that at hsync. A mid-raster R20 write therefore cannot
// move the source row under a row mapping that already assumed the other
// stride -- and because the pending value is not declared here, that is
// enforced by the compiler rather than left to callers to remember.
extern	BYTE	CRTC_VramRowStepActive;
extern  int		HSYNC_CLK;

// Recompute HSYNC_CLK (CPU cycles per raster, nominal 10MHz units) from the
// current CRTC registers. Call after changing R00, R04, R20 or the HRL bit.
void CRTC_UpdateHSyncClock(void);

// Recompute TextDotY, CRTC_VStep and pending scan state from R06/R07/R20.
// Call after changing any of them.
void CRTC_UpdateVerticalScan(void);

// Adopt the register-derived scan state for the raster about to be drawn and
// return its scan mode. The frame loop separately latches VSTART/VEND.
CrtcScanMode CRTC_LatchScanState(void);

// Start/end one hardware field. Begin returns nonzero when entering or
// leaving interlace so the caller can discard incompatible woven pixels.
int CRTC_BeginField(void);
void CRTC_EndField(void);

extern	DWORD	GrphScrollX[];
extern	DWORD	GrphScrollY[];

void CRTC_Init(void);

// Run horizontal-front-porch work for the raster that has just been drawn.
void CRTC_HorizontalFrontPorch(void);

BYTE FASTCALL CRTC_Read(DWORD adr);
void FASTCALL CRTC_Write(DWORD adr, BYTE data);

BYTE FASTCALL VCtrl_Read(DWORD adr);
void FASTCALL VCtrl_Write(DWORD adr, BYTE data);

#endif
