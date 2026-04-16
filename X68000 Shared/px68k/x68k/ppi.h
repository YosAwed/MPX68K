//---------------------------------------------------------------------------
//
//	X68000 EMULATOR "MPX68K"
//
//	Copyright (C) 2025 Awed
//	[ PPI(i8255A) ]
//
//---------------------------------------------------------------------------

#ifndef _px68k_ppi
#define _px68k_ppi

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

// PPI registers
#define PPI_PORTA   0xe9a001
#define PPI_PORTB   0xe9a003
#define PPI_PORTC   0xe9a005
#define PPI_CONTROL 0xe9a007

// Function prototypes
void PPI_Init(void);
void PPI_Cleanup(void);
void PPI_Reset(void);
BYTE FASTCALL PPI_Read(DWORD addr);
void FASTCALL PPI_Write(DWORD addr, BYTE data);

// JoyportU configuration
extern int joyport_ukun_mode;
void PPI_SetJoyportUMode(int mode);
int PPI_GetJoyportUMode(void);

#ifdef __cplusplus
}
#endif

#endif // _px68k_ppi

