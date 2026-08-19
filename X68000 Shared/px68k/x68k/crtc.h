#ifndef _winx68k_crtc
#define _winx68k_crtc

#include "common.h"

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
// VRAM rows consumed per rendered row (1, or 2 in the 31kHz 1024-line mode).
// Split like the field clock: CRTC_VramRowStep follows the registers, and
// CRTC_VramRowStepActive is what the current raster is being drawn with. The
// frame loop latches one from the other at hsync, so a mid-raster R20 write
// cannot leave the row mapping and the VRAM source row on different
// settings. Everything that turns VLINE into a VRAM source row must read the
// active value; only crtc.c itself touches the register-derived one.
extern	BYTE	CRTC_VramRowStep;
extern	BYTE	CRTC_VramRowStepActive;
extern  int		HSYNC_CLK;

// Recompute HSYNC_CLK (CPU cycles per raster, nominal 10MHz units) from the
// current CRTC registers. Call after changing R00, R04, R20 or the HRL bit.
void CRTC_UpdateHSyncClock(void);

// Recompute TextDotY, CRTC_VStep and CRTC_VramRowStep from R06/R07/R20.
// Call after changing any of them.
void CRTC_UpdateVerticalScan(void);

extern	DWORD	GrphScrollX[];
extern	DWORD	GrphScrollY[];

void CRTC_Init(void);

void CRTC_RasterCopy(void);

BYTE FASTCALL CRTC_Read(DWORD adr);
void FASTCALL CRTC_Write(DWORD adr, BYTE data);

BYTE FASTCALL VCtrl_Read(DWORD adr);
void FASTCALL VCtrl_Write(DWORD adr, BYTE data);

#endif
