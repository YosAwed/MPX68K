/* Host-side tests for 68000 instructions the c68k core used to get wrong,
 * found with the SingleStepTests/680x0 vectors (see test_c68k_singlestep.c).
 * Expected values follow those vectors. */
#include <stdio.h>
#include <string.h>

#include "c68k/c68k.h"

static unsigned char mem[0x10000];     /* bytes swapped in pairs, as the emulator keeps RAM */
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

static u32 FASTCALL read_byte(const u32 adr) { return mem[(adr & 0xffff) ^ 1]; }
static u32 FASTCALL read_word(const u32 adr) { return peek16(adr & 0xffff); }
static void FASTCALL write_byte(const u32 adr, u32 data) { mem[(adr & 0xffff) ^ 1] = (unsigned char)data; }
static void FASTCALL write_word(const u32 adr, u32 data) { poke16(adr & 0xffff, data); }
static s32 FASTCALL int_ack(s32 level) { (void)level; return C68K_INT_ACK_AUTOVECTOR; }

#define CODE 0x0400
#define CHK_HANDLER 0x0600
#define DIVZ_HANDLER 0x0680

/* Supervisor mode, SSP $8000, the given CCR; `op` (and an optional
 * extension word) at CODE. */
static void setup(u32 op, u32 ccr)
{
    memset(mem, 0, sizeof(mem));
    poke32(5 * 4, DIVZ_HANDLER);
    poke32(6 * 4, CHK_HANDLER);
    poke16(CODE, op);
    C68k_Init(&C68K, int_ack);
    C68k_Set_ReadB(&C68K, read_byte);
    C68k_Set_ReadW(&C68K, read_word);
    C68k_Set_WriteB(&C68K, write_byte);
    C68k_Set_WriteW(&C68K, write_word);
    C68k_Set_Fetch(&C68K, 0x000000, 0x00ffff, (pointer)mem);
    C68k_Set_SR(&C68K, 0x2700 | ccr);
    C68k_Set_AReg(&C68K, 7, 0x8000);
    C68k_Set_PC(&C68K, CODE);
}

static void step(void) { C68k_Exec(&C68K, 1); }
static u32 ccr(void) { return C68k_Get_SR(&C68K) & 0x1f; }

enum { C = 1, V = 2, Z = 4, N = 8, X = 0x10 };

int main(void)
{
    /* ---- ROXR.l Dx,Dy rotates the 33 bits X:Dy, and X/C take the last bit out ---- */
    setup(0xe8b1, X);                        /* roxr.l d4,d1 */
    C68k_Set_DReg(&C68K, 4, 63);             /* 63 mod 33 = 30 */
    C68k_Set_DReg(&C68K, 1, 0xcd1b56f7);
    step();
    CHECK(C68k_Get_DReg(&C68K, 1) == 0x68dab7bf, "roxr.l by 63: result");
    CHECK(ccr() == 0, "roxr.l by 63: X and C are the last bit out (0)");

    setup(0xe0b4, X);                        /* roxr.l d0,d4 */
    C68k_Set_DReg(&C68K, 0, 32);
    C68k_Set_DReg(&C68K, 4, 0x0e73d42d);
    step();
    CHECK(C68k_Get_DReg(&C68K, 4) == 0x1ce7a85b, "roxr.l by 32: result");
    CHECK(ccr() == 0, "roxr.l by 32: flags");

    /* ---- ROXL.l by 32 ---- */
    setup(0xe3b0, X);                        /* roxl.l d1,d0 */
    C68k_Set_DReg(&C68K, 1, 32);
    C68k_Set_DReg(&C68K, 0, 0x89abcdef);
    step();
    CHECK(C68k_Get_DReg(&C68K, 0) == 0xc4d5e6f7, "roxl.l by 32: result");
    CHECK(ccr() == (X | N | C), "roxl.l by 32: X and C are the last bit out (1)");

    /* ---- ASL.l by 32: the last bit out is bit 0 ---- */
    setup(0xe3a0, 0);                        /* asl.l d1,d0 */
    C68k_Set_DReg(&C68K, 1, 32);
    C68k_Set_DReg(&C68K, 0, 0x00000001);
    step();
    CHECK(C68k_Get_DReg(&C68K, 0) == 0, "asl.l by 32: result is 0");
    CHECK(ccr() == (X | Z | V | C), "asl.l by 32: X and C from bit 0, V for the lost bit");

    /* ---- NEGX.l with X set borrows whenever the operand is not 0 ---- */
    setup(0x4080, X);                        /* negx.l d0 */
    C68k_Set_DReg(&C68K, 0, 0xdb771402);
    step();
    CHECK(C68k_Get_DReg(&C68K, 0) == 0x2488ebfd, "negx.l: result");
    CHECK((ccr() & (X | C)) == (X | C), "negx.l: X and C set");

    setup(0x4080, X);
    C68k_Set_DReg(&C68K, 0, 0);
    step();
    CHECK(C68k_Get_DReg(&C68K, 0) == 0xffffffff, "negx.l of 0 with X: result");
    CHECK((ccr() & (X | C)) == (X | C), "negx.l of 0 with X: X and C set");

    setup(0x4080, Z);
    C68k_Set_DReg(&C68K, 0, 0);
    step();
    CHECK(ccr() == Z, "negx.l of 0 without X: no borrow, Z kept");

    /* ---- UNLK A7 loads A7 from the long at A7 ---- */
    setup(0x4e5f, 0);                        /* unlk a7 */
    poke32(0x8000, 0x2abc74cd);
    step();
    CHECK(C68k_Get_AReg(&C68K, 7) == 0x2abc74cd, "unlk a7: A7 is the long at (A7)");

    /* ---- CHK compares signed words ---- */
    setup(0x4181, 0);                        /* chk d1,d0 */
    C68k_Set_DReg(&C68K, 0, 0x4f0b);
    C68k_Set_DReg(&C68K, 1, 0x9c92);         /* a negative bound: any positive Dn is above it */
    step();
    CHECK(C68k_Get_PC(&C68K) == CHK_HANDLER, "chk above a negative bound: traps");
    CHECK((ccr() & N) == 0, "chk above the bound: N clear");

    setup(0x4181, 0);
    C68k_Set_DReg(&C68K, 0, 0x8000);         /* negative */
    C68k_Set_DReg(&C68K, 1, 0x7fff);
    step();
    CHECK(C68k_Get_PC(&C68K) == CHK_HANDLER, "chk of a negative Dn: traps");
    CHECK((ccr() & N) == N, "chk of a negative Dn: N set");

    setup(0x4181, V | C);
    C68k_Set_DReg(&C68K, 0, 0x0000);
    C68k_Set_DReg(&C68K, 1, 0x0010);
    step();
    CHECK(C68k_Get_PC(&C68K) == CODE + 2, "chk within bounds: no trap");
    CHECK(ccr() == Z, "chk: V and C cleared, Z from Dn");

    /* ---- DIVU/DIVS overflow sets V and clears C, leaving Dn ---- */
    setup(0x80c1, C);                        /* divu d1,d0 */
    C68k_Set_DReg(&C68K, 0, 0x00100000);
    C68k_Set_DReg(&C68K, 1, 1);
    step();
    CHECK(C68k_Get_DReg(&C68K, 0) == 0x00100000, "divu overflow: Dn unchanged");
    CHECK((ccr() & (V | C)) == V, "divu overflow: V set, C clear");

    setup(0x81c1, C);                        /* divs d1,d0 */
    C68k_Set_DReg(&C68K, 0, 0x00100000);
    C68k_Set_DReg(&C68K, 1, 1);
    step();
    CHECK((ccr() & (V | C)) == V, "divs overflow: V set, C clear");

    setup(0x81c1, 0);
    C68k_Set_DReg(&C68K, 0, 0x80000000);
    C68k_Set_DReg(&C68K, 1, 0xffff);         /* -1: the quotient +$80000000 overflows */
    step();
    CHECK(C68k_Get_DReg(&C68K, 0) == 0x80000000, "divs $80000000/-1: Dn unchanged");
    CHECK(ccr() == (N | V), "divs $80000000/-1: N and V set");

    /* The cases below fail XEiJ's instructiontest.x on the old core; expected
     * values follow XEiJ's MC68000 rules, which that test measures on the chip. */

    /* ---- ABCD/SBCD/NBCD on operands that are not valid BCD ---- */
    setup(0xc101, X | Z);                    /* abcd d1,d0 */
    C68k_Set_DReg(&C68K, 0, 0x8f);
    C68k_Set_DReg(&C68K, 1, 0x04);
    step();
    CHECK((C68k_Get_DReg(&C68K, 0) & 0xff) == 0x9a, "abcd $8f+$04+X: result $9a");
    CHECK(ccr() == N, "abcd $8f+$04+X: no decimal carry, Z cleared");

    setup(0x8101, Z);                        /* sbcd d1,d0 */
    C68k_Set_DReg(&C68K, 0, 0x0a);
    C68k_Set_DReg(&C68K, 1, 0x00);
    step();
    CHECK((C68k_Get_DReg(&C68K, 0) & 0xff) == 0x0a, "sbcd $0a-$00: no half borrow, no correction");
    CHECK(ccr() == 0, "sbcd $0a-$00: no borrow, Z cleared");

    setup(0x4800, X | Z);                    /* nbcd d0 */
    C68k_Set_DReg(&C68K, 0, 0x0f);
    step();
    CHECK((C68k_Get_DReg(&C68K, 0) & 0xff) == 0x8a, "nbcd $0f with X: result $8a");
    CHECK(ccr() == (X | N | C), "nbcd $0f with X: borrow");

    /* V is the overflow of adding the decimal correction, N is bit 7 */
    setup(0xc101, 0);                        /* abcd d1,d0 */
    C68k_Set_DReg(&C68K, 0, 0x79);
    C68k_Set_DReg(&C68K, 1, 0x01);
    step();
    CHECK((C68k_Get_DReg(&C68K, 0) & 0xff) == 0x80, "abcd $79+$01: result $80");
    CHECK(ccr() == (N | V), "abcd $79+$01: the correction overflows into bit 7");

    /* ---- ASL.b by 8 and ASL.w by 16: the last bit out is bit 0 ---- */
    setup(0xe320, 0);                        /* asl.b d1,d0 */
    C68k_Set_DReg(&C68K, 1, 8);
    C68k_Set_DReg(&C68K, 0, 0x01);
    step();
    CHECK((C68k_Get_DReg(&C68K, 0) & 0xff) == 0, "asl.b by 8: result is 0");
    CHECK(ccr() == (X | Z | V | C), "asl.b by 8: X and C from bit 0");

    setup(0xe360, 0);                        /* asl.w d1,d0 */
    C68k_Set_DReg(&C68K, 1, 16);
    C68k_Set_DReg(&C68K, 0, 0x0001);
    step();
    CHECK((C68k_Get_DReg(&C68K, 0) & 0xffff) == 0, "asl.w by 16: result is 0");
    CHECK(ccr() == (X | Z | V | C), "asl.w by 16: X and C from bit 0");

    /* ---- DIVU/DIVS by zero set the CCR before trapping ---- */
    setup(0x80c1, X | C);                    /* divu d1,d0 */
    C68k_Set_DReg(&C68K, 0, 0x00001234);
    C68k_Set_DReg(&C68K, 1, 0);
    step();
    CHECK(C68k_Get_PC(&C68K) == DIVZ_HANDLER, "divu by zero: traps");
    CHECK(ccr() == (X | Z | V), "divu by zero: Z from the upper word, V set, C clear");

    setup(0x81c1, N | C);                    /* divs d1,d0 */
    C68k_Set_DReg(&C68K, 0, 0x00001234);
    C68k_Set_DReg(&C68K, 1, 0);
    step();
    CHECK(C68k_Get_PC(&C68K) == DIVZ_HANDLER, "divs by zero: traps");
    CHECK(ccr() == (Z | V), "divs by zero of a positive dividend: Z and V set");

    /* ---- DIVU/DIVS overflow: N and Z as the chip leaves them ---- */
    setup(0x80c1, Z);                        /* divu d1,d0 */
    C68k_Set_DReg(&C68K, 0, 0x80000000);
    C68k_Set_DReg(&C68K, 1, 1);
    step();
    CHECK(ccr() == (N | V), "divu overflow: N from the dividend, Z clear");

    setup(0x81c1, 0);                        /* divs d1,d0 */
    C68k_Set_DReg(&C68K, 0, 0x00008080);
    C68k_Set_DReg(&C68K, 1, 1);
    step();
    CHECK(ccr() == (N | V), "divs overflow to $8080: N set");

    setup(0x81c1, 0);
    C68k_Set_DReg(&C68K, 0, 0x00008000);
    C68k_Set_DReg(&C68K, 1, 1);
    step();
    CHECK(ccr() == (Z | V), "divs overflow to $8000: Z set");

    if (failures) {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
