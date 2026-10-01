/* System port: machine-class read at $E8E00B and the guest power-off
 * sequence ($00, $0F, $0F written to $E8E00F).
 */
#include <assert.h>
#include <stdio.h>
#include "common.h"
#include "sysport.h"

/* sysport.c only reaches these through contrast / dot-clock writes. */
void Pal_ChangeContrast(int num) { (void)num; }
void CRTC_UpdateHSyncClock(void) {}

static void poke(BYTE v) { SysPort_Write(0xe8e00f, v); }

int main(void)
{
    SysPort_Init();

    SysPort_SetClockMHz(10);
    assert(SysPort_Read(0xe8e00b) == 0xff);
    SysPort_SetClockMHz(16);
    assert(SysPort_Read(0xe8e00b) == 0xfe);
    SysPort_SetClockMHz(24);
    assert(SysPort_Read(0xe8e00b) == 0xfe);
    SysPort_SetClockMHz(25);
    assert(SysPort_Read(0xe8e00b) == 0xff);
    SysPort_SetClockMHz(50);
    assert(SysPort_Read(0xe8e00b) == 0xff);

    /* Interrupted sequences don't fire. */
    poke(0x00); poke(0x0f); poke(0x05); poke(0x0f);
    assert(!SysPort_TakePowerOffRequest());
    /* Upper nibble is ignored. */
    poke(0xf0); poke(0x0f); poke(0x0f);
    assert(SysPort_TakePowerOffRequest());
    assert(!SysPort_TakePowerOffRequest());
    assert(SysPort_Read(0xe8e00f) == 0x0f);
    /* Latched until reset. */
    poke(0x00); poke(0x0f); poke(0x0f);
    assert(!SysPort_TakePowerOffRequest());
    SysPort_ResetPowerOff();
    poke(0x00); poke(0x0f); poke(0x0f);
    assert(SysPort_TakePowerOffRequest());

    puts("PASS: machine class by clock and guest power-off sequence");
    return 0;
}
