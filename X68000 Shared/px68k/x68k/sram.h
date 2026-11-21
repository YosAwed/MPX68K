#ifndef _winx68k_sram
#define _winx68k_sram

#include "common.h"

extern	BYTE	SRAM[0x4000];

extern unsigned char CGROM_DAT[0xc0000]; // added by Awed 2023/10/7
extern unsigned char IPLROM_DAT[0x20000]; // added by Awed 2023/10/7

void SRAM_Init(void);
void SRAM_Cleanup(void);
void SRAM_VirusCheck(void);

// True once the IPL ROM has initialised the SRAM ($ED0000 = "\x82\x77" "68000W").
int SRAM_IsInitialized(void);

// Call at power-on / reset, after SRAM[] holds what the machine starts with.
// For a blank SRAM (no magic), the IPL ROM initializes it on boot with a
// main memory size of 1MB; its writes of the size are replaced with the size
// already in SRAM[] if it is a whole number of MB up to installedMemorySize,
// else with installedMemorySize (0 = keep 1MB). An initialized SRAM is left
// as it is.
void SRAM_PowerOn(DWORD installedMemorySize);

// Main memory size in SRAM ($ED0008), the size Human68k uses. SWITCH.X
// changes it through SRAM_Write(); the host reads it here, so a menu built
// from it always shows the current setting. Takes effect at the next boot.
DWORD SRAM_GetMemorySize(void);
// Sets it from the host (ignores the SRAM write protection, like editing
// SRAM.DAT). If the SRAM is still blank, the IPL ROM's initialization keeps
// this size too.
void SRAM_SetMemorySize(DWORD memorySize);

// While enabled, reads of the SCSI settings ($ED006F/$ED0070) report a
// configured external SCSI board without modifying SRAM[] itself, so the
// override never leaks into SRAM.DAT or the SASI-mode SRAM snapshot.
void SRAM_SetSCSIBoardOverlay(int enable);

BYTE FASTCALL SRAM_Read(DWORD adr);
void FASTCALL SRAM_Write(DWORD adr, BYTE data);

#endif

