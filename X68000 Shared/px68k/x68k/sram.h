#ifndef _winx68k_sram
#define _winx68k_sram

#include "common.h"

extern	BYTE	SRAM[0x4000];

extern unsigned char CGROM_DAT[0xc0000]; // added by Awed 2023/10/7
extern unsigned char IPLROM_DAT[0x20000]; // added by Awed 2023/10/7

void SRAM_Init(void);
void SRAM_Cleanup(void);
void SRAM_VirusCheck(void);

// While enabled, reads of the SCSI settings ($ED006F/$ED0070) report a
// configured external SCSI board without modifying SRAM[] itself, so the
// override never leaks into SRAM.DAT or the SASI-mode SRAM snapshot.
void SRAM_SetSCSIBoardOverlay(int enable);

BYTE FASTCALL SRAM_Read(DWORD adr);
void FASTCALL SRAM_Write(DWORD adr, BYTE data);

#endif

