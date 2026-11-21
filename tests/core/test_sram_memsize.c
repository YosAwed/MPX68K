/* Human68k uses the main memory size in SRAM ($ED0008) as its memory. The
 * IPL ROM writes 1MB into a blank SRAM, so at power-on the size must be set
 * to the memory fitted: directly for an initialized SRAM, and by replacing
 * the IPL ROM's writes while it initializes a blank one.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "prop.h"
#include "sram.h"

BYTE SysPort[7];
Win68Conf Config;
void SCSI_LogText(const char *text) { (void)text; }
DWORD cpu_readmem24_dword(DWORD adr) { (void)adr; return 0; }

static const char kMagic[8] = { (char)0x82, 0x77, '6', '8', '0', '0', '0', 'W' };

static DWORD rd32(DWORD adr)
{
    return ((DWORD)SRAM_Read(0xed0000 + adr) << 24) | ((DWORD)SRAM_Read(0xed0001 + adr) << 16) |
           ((DWORD)SRAM_Read(0xed0002 + adr) << 8) | (DWORD)SRAM_Read(0xed0003 + adr);
}

static void wr32(DWORD adr, DWORD v)
{
    SRAM_Write(0xed0000 + adr, (BYTE)(v >> 24));
    SRAM_Write(0xed0001 + adr, (BYTE)(v >> 16));
    SRAM_Write(0xed0002 + adr, (BYTE)(v >> 8));
    SRAM_Write(0xed0003 + adr, (BYTE)v);
}

static void write_magic(void)
{
    int i;
    for (i = 0; i < 8; i++) SRAM_Write(0xed0000 + i, (BYTE)kMagic[i]);
}

static void blank_sram(void)
{
    memset(SRAM, 0xff, sizeof SRAM);
}

int main(void)
{
    SysPort[5] = 0x31; /* SRAM writable, as the IPL ROM sets it */

    /* An initialized SRAM saved with 1MB gets the fitted 12MB at power-on. */
    blank_sram();
    write_magic();
    wr32(0x08, 0x00100000);
    assert(SRAM_IsInitialized());
    SRAM_PowerOn(0x00c00000);
    assert(rd32(0x08) == 0x00c00000);
    printf("PASS: initialized SRAM: size set to the memory fitted\n");

    /* Afterwards the size can be changed (SWITCH.X) until the next power-on. */
    wr32(0x08, 0x00400000);
    assert(rd32(0x08) == 0x00400000);
    SRAM_PowerOn(0x00c00000);
    assert(rd32(0x08) == 0x00c00000);
    printf("PASS: a size written while running lasts until the next power-on\n");

    /* Blank SRAM: nothing to set yet; the IPL ROM's 1MB default is replaced,
     * whether it writes the size before or after the magic. */
    blank_sram();
    assert(!SRAM_IsInitialized());
    SRAM_PowerOn(0x00c00000);
    assert(rd32(0x08) == 0xffffffff);
    wr32(0x08, 0x00100000);
    assert(rd32(0x08) == 0x00c00000);
    write_magic();
    wr32(0x08, 0x00100000);
    assert(SRAM_IsInitialized() && rd32(0x08) == 0x00c00000);
    printf("PASS: blank SRAM: the IPL ROM's default size is replaced\n");

    /* Once all four bytes were replaced after the magic, writes are kept. */
    wr32(0x08, 0x00200000);
    assert(rd32(0x08) == 0x00200000);
    printf("PASS: replacement ends after the IPL ROM's initialization\n");

    /* The replacement does not touch the bytes around the size. */
    blank_sram();
    SRAM_PowerOn(0x00c00000);
    write_magic();
    wr32(0x04, 0x30303057);
    wr32(0x0c, 0x00fc0000);
    assert(rd32(0x04) == 0x30303057 && rd32(0x0c) == 0x00fc0000);
    printf("PASS: neighbouring SRAM bytes are written as is\n");

    /* 0 leaves the SRAM alone. */
    blank_sram();
    SRAM_PowerOn(0);
    write_magic();
    wr32(0x08, 0x00100000);
    SRAM_PowerOn(0);
    assert(rd32(0x08) == 0x00100000);
    blank_sram();
    SRAM_PowerOn(0);
    wr32(0x08, 0x00100000);
    assert(rd32(0x08) == 0x00100000);
    printf("PASS: memory size 0 leaves the SRAM alone\n");

    /* Write-protected SRAM is not changed by the replacement either. */
    blank_sram();
    SRAM_PowerOn(0x00c00000);
    SysPort[5] = 0x00;
    wr32(0x08, 0x00100000);
    assert(rd32(0x08) == 0xffffffff);
    printf("PASS: write protection still applies\n");

    /* The default SRAM ($ED0010) holds the SRAM boot address $00ED0100. */
    SRAM_Init();
    assert(rd32(0x10) == 0x00ed0100);
    printf("PASS: default SRAM boot address is $00ED0100\n");
    return 0;
}
