/* The external-SCSI SRAM view must be visible to X68000 code while enabled,
 * but must never be written into SRAM[] (which is saved to SRAM.DAT and
 * copied into the SASI-mode SRAM snapshot).
 */
#include <assert.h>
#include <stdio.h>
#include "common.h"
#include "prop.h"
#include "sram.h"

BYTE SysPort[7];
Win68Conf Config;
void SCSI_LogText(const char *text) { (void)text; }
DWORD cpu_readmem24_dword(DWORD adr) { (void)adr; return 0; }

static BYTE rd(DWORD adr) { return SRAM_Read(0xed0000 + adr); }

int main(void)
{
    SRAM_Init();
    SRAM[0x6f ^ 1] = 0xff;
    SRAM[0x70 ^ 1] = 0x00;

    SRAM_SetSCSIBoardOverlay(0);
    assert(rd(0x6f) == 0xff && rd(0x70) == 0x00);
    printf("PASS: overlay off reports real SRAM\n");

    SRAM_SetSCSIBoardOverlay(1);
    assert(rd(0x6f) == 'V' && rd(0x70) == 0x0f);
    assert(SRAM[0x6f ^ 1] == 0xff && SRAM[0x70 ^ 1] == 0x00);
    assert(rd(0x6e) == SRAM[0x6e ^ 1] && rd(0x71) == SRAM[0x71 ^ 1]);
    printf("PASS: overlay reports external SCSI without touching SRAM[]\n");

    /* A SWITCH.X-configured SCSI ID is kept; only the external bit is forced. */
    SysPort[5] = 0x31;
    SRAM_Write(0xed006f, 'V');
    SRAM_Write(0xed0070, 0x05);
    assert(rd(0x6f) == 'V' && rd(0x70) == 0x0d);
    assert(SRAM[0x70 ^ 1] == 0x05);
    printf("PASS: user SCSI ID kept, external bit forced\n");

    SRAM_SetSCSIBoardOverlay(0);
    assert(rd(0x70) == 0x05);
    printf("PASS: overlay off restores real view\n");
    return 0;
}
