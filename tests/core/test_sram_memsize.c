/* The main memory size in SRAM ($ED0008) is the user's setting: SWITCH.X in
 * Human68k and the host's menu both change it, and power-on keeps it. Only a
 * blank SRAM, which the IPL ROM initializes with 1MB, gets the size the host
 * wants (the one already in SRAM[], or all of the memory installed).
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

#define MB(n) ((DWORD)(n) << 20)
#define INSTALLED MB(12)

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

/* The IPL ROM initializing a blank SRAM: size (1MB) before and after the magic. */
static void ipl_initializes(void)
{
    wr32(0x08, MB(1));
    write_magic();
    wr32(0x08, MB(1));
}

int main(void)
{
    SysPort[5] = 0x31; /* SRAM writable, as the IPL ROM sets it */

    /* An initialized SRAM keeps its size at power-on (SWITCH.X chose it). */
    blank_sram();
    write_magic();
    wr32(0x08, MB(4));
    SRAM_PowerOn(INSTALLED);
    assert(rd32(0x08) == MB(4) && SRAM_GetMemorySize() == MB(4));
    printf("PASS: power-on keeps the size of an initialized SRAM\n");

    /* SWITCH.X writes through the bus; the host reads the same value. */
    wr32(0x08, MB(6));
    assert(SRAM_GetMemorySize() == MB(6));
    printf("PASS: a size written by SWITCH.X is what the host reads\n");

    /* The host's menu writes it; X68000 code reads the same value. */
    SRAM_SetMemorySize(MB(8));
    assert(rd32(0x08) == MB(8));
    SRAM_PowerOn(INSTALLED);
    assert(rd32(0x08) == MB(8));
    printf("PASS: a size set by the host is what X68000 code reads, and survives power-on\n");

    /* The host's setting ignores write protection (like editing SRAM.DAT). */
    SysPort[5] = 0x00;
    SRAM_SetMemorySize(MB(2));
    assert(SRAM_GetMemorySize() == MB(2));
    wr32(0x08, MB(10));
    assert(SRAM_GetMemorySize() == MB(2));
    SysPort[5] = 0x31;
    printf("PASS: host setting ignores, bus writes obey, write protection\n");

    /* Blank SRAM holding no usable size: the IPL ROM's 1MB becomes all memory. */
    blank_sram();
    SRAM_PowerOn(INSTALLED);
    ipl_initializes();
    assert(SRAM_IsInitialized() && rd32(0x08) == INSTALLED);
    printf("PASS: blank SRAM: the IPL ROM's 1MB is replaced with the installed memory\n");

    /* After its initialization, writes are kept (SWITCH.X in the same session). */
    wr32(0x08, MB(2));
    assert(rd32(0x08) == MB(2));
    printf("PASS: replacement ends after the IPL ROM's initialization\n");

    /* Blank SRAM with a size in SRAM[] (the host's defaults): that size is kept. */
    blank_sram();
    SRAM_SetMemorySize(MB(4));
    SRAM_PowerOn(INSTALLED);
    ipl_initializes();
    assert(rd32(0x08) == MB(4));
    printf("PASS: blank SRAM: the size already in SRAM[] is kept through the IPL ROM\n");

    /* Blank SRAM, menu used after power-on but before the IPL ROM ran. */
    blank_sram();
    SRAM_PowerOn(INSTALLED);
    SRAM_SetMemorySize(MB(6));
    ipl_initializes();
    assert(rd32(0x08) == MB(6));
    printf("PASS: blank SRAM: a size chosen before boot survives the IPL ROM\n");

    /* Unusable sizes in a blank SRAM fall back to the installed memory. */
    blank_sram();
    SRAM_SetMemorySize(MB(16));
    SRAM_PowerOn(INSTALLED);
    ipl_initializes();
    assert(rd32(0x08) == INSTALLED);
    blank_sram();
    SRAM_SetMemorySize(MB(4) + 0x8000);
    SRAM_PowerOn(INSTALLED);
    ipl_initializes();
    assert(rd32(0x08) == INSTALLED);
    printf("PASS: blank SRAM: sizes over the installed memory or not whole MB are not used\n");

    /* The replacement does not touch the bytes around the size. */
    blank_sram();
    SRAM_PowerOn(INSTALLED);
    write_magic();
    wr32(0x0c, 0x00fc0000);
    assert(rd32(0x04) == 0x30303057 && rd32(0x0c) == 0x00fc0000);
    printf("PASS: neighbouring SRAM bytes are written as is\n");

    /* 0 keeps the IPL ROM's default. */
    blank_sram();
    SRAM_PowerOn(0);
    ipl_initializes();
    assert(rd32(0x08) == MB(1));
    printf("PASS: installed size 0 leaves the IPL ROM's default\n");

    /* The default SRAM ($ED0010) holds the SRAM boot address $00ED0100. */
    SRAM_Init();
    assert(rd32(0x10) == 0x00ed0100);
    printf("PASS: default SRAM boot address is $00ED0100\n");
    return 0;
}
