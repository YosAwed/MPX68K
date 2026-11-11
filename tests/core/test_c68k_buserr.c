/* Host-side test for the 68000 core's bus error exception: a program hooks
 * vector 2 and reads an address where nothing answers, the way drivers find
 * out whether an expansion board is fitted. */
#include <stdio.h>
#include <string.h>

#include "c68k/c68k.h"

static unsigned char mem[0x10000];     /* stored the way the emulator keeps RAM: bytes swapped in pairs */
static int failures = 0;

#define CHECK(cond, name) do { \
    if (cond) { \
        printf("PASS: %s\n", name); \
    } else { \
        printf("FAIL: %s (%s:%d)\n", name, __FILE__, __LINE__); \
        failures++; \
    } \
} while (0)

static void poke16(u32 adr, u32 v) { mem[adr ^ 1] = (unsigned char)(v >> 8); mem[(adr + 1) ^ 1] = (unsigned char)v; }
static void poke32(u32 adr, u32 v) { poke16(adr, v >> 16); poke16(adr + 2, v & 0xffff); }
static u32 peek16(u32 adr) { return (u32)(mem[adr ^ 1] << 8) | mem[(adr + 1) ^ 1]; }
static u32 peek32(u32 adr) { return (peek16(adr) << 16) | peek16(adr + 2); }

static u32 FASTCALL read_byte(const u32 adr)
{
    if (adr < sizeof(mem)) return mem[adr ^ 1];
    C68k_Bus_Error(&C68K, adr);        /* nothing there */
    return 0xff;
}
static u32 FASTCALL read_word(const u32 adr)
{
    if (adr < sizeof(mem) - 1) return peek16(adr);
    C68k_Bus_Error(&C68K, adr);
    return 0xffff;
}
static void FASTCALL write_byte(const u32 adr, u32 data)
{
    if (adr < sizeof(mem)) mem[adr ^ 1] = (unsigned char)data;
    else C68k_Bus_Error(&C68K, adr);
}
static void FASTCALL write_word(const u32 adr, u32 data)
{
    if (adr < sizeof(mem) - 1) poke16(adr, data);
    else C68k_Bus_Error(&C68K, adr);
}
static s32 FASTCALL int_ack(s32 level) { (void)level; return C68K_INT_ACK_AUTOVECTOR; }

static void boot(void)
{
    memset(mem, 0, sizeof(mem));
    poke32(0x0000, 0x00008000);        /* initial SSP */
    poke32(0x0004, 0x00000400);        /* initial PC */
    poke32(0x0008, 0x00000500);        /* bus error vector */
    poke16(0x0500, 0x70ff);            /* handler: moveq #-1,d0 */
    poke16(0x0502, 0x60fe);            /*          bra.s *      */
    C68k_Init(&C68K, int_ack);
    C68k_Set_ReadB(&C68K, read_byte);
    C68k_Set_ReadW(&C68K, read_word);
    C68k_Set_WriteB(&C68K, write_byte);
    C68k_Set_WriteW(&C68K, write_word);
    C68k_Set_Fetch(&C68K, 0x000000, 0x00ffff, (pointer)mem);
    C68k_Reset(&C68K);
}

int main(void)
{
    u32 sp;

    /* ---- a read from an absent board ---- */
    boot();
    poke16(0x0400, 0x4a39); poke32(0x0402, 0x00eafa11);    /* tst.b $EAFA11 */
    poke16(0x0406, 0x7001);                                /* moveq #1,d0   */
    poke16(0x0408, 0x60fe);                                /* bra.s *       */
    C68k_Exec(&C68K, 200);
    sp = C68k_Get_AReg(&C68K, 7);
    CHECK(C68k_Get_DReg(&C68K, 0) == 0xffffffff, "read: the bus error handler ran");
    CHECK(C68k_Get_PC(&C68K) == 0x0502, "read: execution continues in the handler");
    CHECK(sp == 0x8000 - 14, "read: a 14-byte frame was pushed");
    CHECK(peek32(sp + 2) == 0x00eafa11, "read: frame holds the access address");
    CHECK(peek16(sp + 6) == 0x4a39, "read: frame holds the instruction");
    CHECK((peek16(sp + 8) & 0x2700) == 0x2700, "read: frame holds the old SR");
    CHECK(peek32(sp + 10) == 0x0406, "read: frame holds the PC after the instruction");

    /* ---- a write ---- */
    boot();
    poke16(0x0400, 0x13fc); poke16(0x0402, 0x0080); poke32(0x0404, 0x00eafa13);   /* move.b #$80,$EAFA13 */
    poke16(0x0408, 0x7001);
    poke16(0x040a, 0x60fe);
    C68k_Exec(&C68K, 200);
    CHECK(C68k_Get_DReg(&C68K, 0) == 0xffffffff, "write: the bus error handler ran");
    CHECK(peek32(C68k_Get_AReg(&C68K, 7) + 2) == 0x00eafa13, "write: frame holds the access address");

    /* ---- from user mode: the frame goes on the supervisor stack ---- */
    boot();
    poke16(0x0400, 0x46fc); poke16(0x0402, 0x0000);            /* move #$0000,sr (to user mode) */
    poke16(0x0404, 0x4a39); poke32(0x0406, 0x00eafa11);                          /* tst.b $EAFA11 */
    poke16(0x040a, 0x7001);
    poke16(0x040c, 0x60fe);
    C68k_Set_USP(&C68K, 0x6000);
    C68k_Exec(&C68K, 200);
    CHECK(C68k_Get_DReg(&C68K, 0) == 0xffffffff, "user mode: the bus error handler ran");
    CHECK(C68k_Get_AReg(&C68K, 7) == 0x8000 - 14, "user mode: frame is on the supervisor stack");
    CHECK((C68k_Get_SR(&C68K) & 0x2000) != 0, "user mode: handler runs in supervisor mode");
    CHECK((peek16(C68k_Get_AReg(&C68K, 7) + 8) & 0x2000) == 0, "user mode: the saved SR is the user one");

    /* ---- ordinary memory does not fault, and nothing faults while the CPU is idle ---- */
    boot();
    poke16(0x0400, 0x4a39); poke32(0x0402, 0x00001000);    /* tst.b $1000 */
    poke16(0x0406, 0x7001);
    poke16(0x0408, 0x60fe);
    C68k_Bus_Error(&C68K, 0x00eafa11);                     /* e.g. a debugger reading the address */
    C68k_Exec(&C68K, 200);
    CHECK(C68k_Get_DReg(&C68K, 0) == 1, "no fault: the program runs on");
    CHECK(C68k_Get_AReg(&C68K, 7) == 0x8000, "no fault: nothing was pushed");

    if (failures) {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
