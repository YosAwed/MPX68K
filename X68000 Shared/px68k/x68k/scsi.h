#ifndef _winx68k_scsi
#define _winx68k_scsi

#include "common.h"

extern	BYTE	SCSIIPL[0x2000];
extern	BYTE	SCSIROM_DAT[0x2000];

#define SCSI_SYNTH_BOOT_ENTRY 0x00ea0030
#define SCSI_SYNTH_INIT_ENTRY 0x00ea0070
#define SCSI_SYNTH_IOCS_ENTRY 0x00ea0080
#define SCSI_SYNTH_TRAP15_ENTRY 0x00ea0088
#define SCSI_SYNTH_IOCS_DEFAULT 0x00ea00d2
#define SCSI_SYNTH_IOCS_FN04_OK 0x00ea00d6
#define SCSI_SYNTH_IOCS_FN34_OK 0x00ea00da
#define SCSI_SYNTH_IOCS_FN35_OK 0x00ea00de
#define SCSI_SYNTH_IOCS_FN00_CR 0x00ea00fa
#define SCSI_SYNTH_IOCS_FN33_OK 0x00ea00e2
#define SCSI_SYNTH_IOCS_FN32_OK 0x00ea00e6
#define SCSI_SYNTH_IOCS_FN10_OK 0x00ea00ea
#define SCSI_SYNTH_IOCS_FNAF_OK 0x00ea00ee
#define SCSI_SYNTH_IOCS_DISPLAY 0x00ea00c2
#define SCSI_SYNTH_IOCS_DIRECT 0x00ea00f2
#define SCSI_SYNTH_IOCS_FF_FALLBACK SCSI_SYNTH_IOCS_FN10_OK
#define SCSI_SYNTH_DEVHDR_ADDR  0x00ea0100

int SCSI_IsROMPresent(void);
void SCSI_Init(void);
void SCSI_Cleanup(void);
void SCSI_InvalidateTransferCache(void);

BYTE FASTCALL SCSI_Read(DWORD adr);
void FASTCALL SCSI_Write(DWORD adr, BYTE data);

void SCSI_InjectBoot(void);
int SCSI_HasDeferredBoot(void);
void SCSI_CommitDeferredBoot(void);
void SCSI_LinkDeviceDriver(void);
int SCSI_IsDeviceLinked(void);
int SCSI_HasBootActivity(void);
int SCSI_HasDriverActivity(void);

#endif
