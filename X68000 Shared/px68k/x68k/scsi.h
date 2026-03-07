#ifndef _winx68k_scsi
#define _winx68k_scsi

#include "common.h"

extern	BYTE	SCSIIPL[0x2000];
extern	BYTE	SCSIROM_DAT[0x2000];

#define SCSI_SYNTH_BOOT_ENTRY 0x00fc0030
#define SCSI_SYNTH_INIT_ENTRY 0x00fc0070
#define SCSI_SYNTH_IOCS_ENTRY 0x00fc0080
#define SCSI_SYNTH_TRAP15_ENTRY 0x00fc0088
#define SCSI_SYNTH_IOCS_DEFAULT 0x00fc00d2
#define SCSI_SYNTH_IOCS_FN04_OK 0x00fc00d6
#define SCSI_SYNTH_IOCS_FN34_OK 0x00fc00da
#define SCSI_SYNTH_IOCS_FN35_OK 0x00fc00de
#define SCSI_SYNTH_IOCS_FN00_CR 0x00fc00fa
#define SCSI_SYNTH_IOCS_FN33_OK 0x00fc00e2
#define SCSI_SYNTH_IOCS_FN32_OK 0x00fc00e6
#define SCSI_SYNTH_IOCS_FN10_OK 0x00fc00ea
#define SCSI_SYNTH_IOCS_FNAF_OK 0x00fc00ee
#define SCSI_SYNTH_IOCS_DIRECT 0x00fc00f2
#define SCSI_SYNTH_IOCS_FF_FALLBACK 0x00ff06a2
#define SCSI_SYNTH_DEVHDR_ADDR  0x00fc0100

int SCSI_IsROMPresent(void);
void SCSI_Init(void);
void SCSI_Cleanup(void);
void SCSI_InvalidateTransferCache(void);

BYTE FASTCALL SCSI_Read(DWORD adr);
void FASTCALL SCSI_Write(DWORD adr, BYTE data);

void SCSI_LinkDeviceDriver(void);
int SCSI_IsDeviceLinked(void);

#endif
