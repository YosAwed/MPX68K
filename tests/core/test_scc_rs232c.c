/* Host-side tests for the RS-232C port (Z8530 channel A) and its cross-cable link.
 * The wire bytes the port produces are fed straight back in, which is a
 * loopback plug: TxD-RxD, RTS-CTS, DTR-DSR/DCD. */
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "irqh.h"
#include "scc.h"

/* ---- minimal link dependencies for scc.c ---- */
BYTE IRQH_IRQ[8];
static DWORD (*irq_handler)(BYTE);

void IRQH_IRQCallBack(BYTE irq) { IRQH_IRQ[irq & 7] = 0; }
void IRQH_Int(BYTE irq, void *handler)
{
    IRQH_IRQ[irq & 7] = 1;
    irq_handler = (DWORD (*)(BYTE))handler;
}
void Mouse_SetData(void) { MouseSt = 1; MouseX = 2; MouseY = 3; }
void Mouse_SetCompatMode(int enable) { (void)enable; }

static int failures = 0;

#define CHECK(cond, name) do { \
    if (cond) { \
        printf("PASS: %s\n", name); \
    } else { \
        printf("FAIL: %s (%s:%d)\n", name, __FILE__, __LINE__); \
        failures++; \
    } \
} while (0)

#define A_CMD  0xe98005
#define A_DATA 0xe98007
#define B_CMD  0xe98001
#define B_DATA 0xe98003

static int loopback = 1;

static void pump(void)
{
    unsigned char buf[256];
    int n;
    while ((n = SCC_LinkDrain(buf, sizeof(buf))) > 0) {
        if (loopback) SCC_LinkFeed(buf, n);
    }
}

/* Run emulated time, in 10 MHz clocks. */
static void run(int clk)
{
    while (clk > 0) {
        int n = clk > 100 ? 100 : clk;
        pump();
        SCC_Timer(n);
        clk -= n;
    }
    pump();
    SCC_Timer(1);
}

static void wr(int reg, BYTE data)
{
    if (reg) SCC_Write(A_CMD, (BYTE)reg);   /* registers 8-15: the "point high" command is bit 3 */
    SCC_Write(A_CMD, data);
}

static BYTE rr(int reg)
{
    if (reg) SCC_Write(A_CMD, (BYTE)reg);
    return SCC_Read(A_CMD);
}

static int ack(void)
{
    return (int)irq_handler(5);
}

#define CHAR_CLK 10240   /* 10 bits at 5 MHz / (2 * 16 * 16) = 9765 bps */

static void setup(BYTE wr5)
{
    wr(9, 0x80);            /* channel A reset */
    wr(4, 0x44);            /* x16 clock, 1 stop bit, no parity */
    wr(3, 0xc1);            /* Rx 8 bits, Rx enable */
    wr(12, 14);
    wr(13, 0);
    wr(14, 0x01);           /* baud rate generator on */
    wr(15, 0x00);
    wr(1, 0x00);
    wr(2, 0x50);
    wr(9, 0x09);            /* master interrupt enable, vector includes status */
    wr(5, wr5);
    run(1000);
}

int main(void)
{
    int i;
    BYTE v;

    SCC_Init();
    CHECK((rr(0) & 0x04) != 0, "Tx buffer empty after reset");
    CHECK((rr(0) & 0x28) == 0, "no CTS/DCD with nothing connected");

    SCC_LinkSetConnected(1);
    setup(0x68);            /* Tx 8 bits, Tx enable, RTS and DTR negated */
    CHECK(SCC_LinkGetInfo(SCC_LINK_INFO_ONLINE) == 1, "link online");
    CHECK(SCC_LinkGetInfo(SCC_LINK_INFO_BAUD) == 9765, "baud from the time constant");
    CHECK(SCC_LinkGetInfo(SCC_LINK_INFO_PEER_BAUD) == 9765, "peer baud arrives over the link");
    CHECK((rr(0) & 0x38) == 0, "CTS/DSR/DCD low while the peer's RTS/DTR are low");

    wr(5, 0x6a);            /* RTS */
    run(1000);
    CHECK((rr(0) & 0x38) == 0x20, "RTS reaches CTS");
    wr(5, 0xea);            /* RTS + DTR */
    run(1000);
    CHECK((rr(0) & 0x38) == 0x38, "DTR reaches DSR and DCD");

    /* ---- transmit timing ---- */
    SCC_Write(A_DATA, 'A');
    CHECK((rr(0) & 0x04) != 0, "first character moves to the shifter at once");
    SCC_Write(A_DATA, 'B');
    CHECK((rr(0) & 0x04) == 0, "second character waits in the Tx buffer");
    CHECK((rr(1) & 0x01) == 0, "not all sent while shifting");
    run(CHAR_CLK - 1000);
    CHECK((rr(0) & 0x01) == 0, "nothing received before one character time");
    run(2000);
    CHECK((rr(0) & 0x01) != 0, "character received after one character time");
    CHECK((rr(0) & 0x04) != 0, "Tx buffer empties when the shifter takes the next one");
    CHECK(SCC_Read(A_DATA) == 'A', "received data matches");
    run(CHAR_CLK);
    CHECK(SCC_Read(A_DATA) == 'B', "second character follows");
    CHECK((rr(0) & 0x01) == 0, "receive FIFO empty again");
    CHECK((rr(1) & 0x01) != 0, "all sent");

    /* ---- receive interrupt ---- */
    wr(1, 0x10);            /* Rx interrupt on all characters */
    IRQH_IRQ[5] = 0;
    SCC_Write(A_DATA, 'C');
    run(CHAR_CLK + 1000);
    CHECK(IRQH_IRQ[5] == 1, "Rx character requests level 5");
    CHECK((rr(3) & 0x20) != 0, "RR3 shows channel A Rx pending");
    CHECK(ack() == 0x5c, "vector: channel A receive");
    run(100);
    CHECK(IRQH_IRQ[5] == 1, "request stays while the character is unread");
    CHECK(SCC_Read(A_DATA) == 'C', "handler reads the character");
    CHECK(IRQH_IRQ[5] == 0, "request withdrawn once read");

    /* ---- transmit interrupt ---- */
    wr(1, 0x02);
    SCC_Write(A_DATA, 'D');
    CHECK(IRQH_IRQ[5] == 1, "Tx buffer empty requests level 5");
    CHECK(ack() == 0x58, "vector: channel A transmit");
    SCC_Write(A_CMD, 0x28); /* reset Tx interrupt pending */
    CHECK(IRQH_IRQ[5] == 0, "reset Tx interrupt pending withdraws it");
    wr(1, 0x00);
    run(CHAR_CLK * 2);
    (void)SCC_Read(A_DATA);

    /* ---- external/status interrupt and latch ---- */
    wr(15, 0x20);           /* CTS changes */
    wr(1, 0x01);
    wr(5, 0xe8);            /* RTS off */
    run(1000);
    CHECK(IRQH_IRQ[5] == 1, "CTS change requests level 5");
    CHECK(ack() == 0x5a, "vector: channel A external/status");
    CHECK((rr(0) & 0x20) == 0, "RR0 shows CTS low");
    wr(5, 0xea);            /* RTS on again before the latch is reset */
    run(1000);
    CHECK((rr(0) & 0x20) == 0, "RR0 stays latched");
    SCC_Write(A_CMD, 0x10); /* reset external/status interrupts */
    CHECK((rr(0) & 0x20) != 0, "latch reopens with the new CTS");
    CHECK((rr(3) & 0x08) != 0, "the change during the latch is reported");
    SCC_Write(A_CMD, 0x10);
    CHECK((rr(3) & 0x08) == 0, "external/status pending cleared");
    wr(1, 0x00);
    run(100);
    CHECK(IRQH_IRQ[5] == 0, "no request left");

    /* ---- receive FIFO: the link waits for room ---- */
    for (i = 0; i < 6; i++) {
        SCC_Write(A_DATA, (BYTE)('0' + i));
        run(CHAR_CLK);
    }
    run(CHAR_CLK * 4);
    v = 1;
    for (i = 0; i < 6; i++) {
        if (SCC_Read(A_DATA) != '0' + i) v = 0;
        run(CHAR_CLK * 2);
    }
    CHECK(v, "six characters arrive in order through the 3-deep FIFO");
    CHECK((rr(1) & 0x20) == 0, "no overrun when read within a few characters");

    for (i = 0; i < 5; i++) {
        SCC_Write(A_DATA, (BYTE)('a' + i));
        run(CHAR_CLK);
    }
    run(CHAR_CLK * 40);
    (void)SCC_Read(A_DATA);
    (void)SCC_Read(A_DATA);
    CHECK((rr(1) & 0x20) != 0, "overrun when the program stops reading");
    (void)SCC_Read(A_DATA);
    SCC_Write(A_CMD, 0x30); /* error reset */
    run(CHAR_CLK * 40);
    while (rr(0) & 0x01) (void)SCC_Read(A_DATA);
    SCC_Write(A_CMD, 0x30);
    CHECK((rr(1) & 0x70) == 0, "error reset clears the flags");

    /* ---- RTS holds back characters already on their way ---- */
    wr(5, 0xe8);            /* RTS off: "do not send" */
    SCC_Write(A_DATA, 'H');
    run(CHAR_CLK * 5);
    CHECK((rr(0) & 0x01) == 0, "character held while RTS is negated");
    wr(5, 0xea);
    run(CHAR_CLK);
    CHECK((rr(0) & 0x01) != 0 && SCC_Read(A_DATA) == 'H', "delivered when RTS is asserted");
    wr(5, 0xe8);
    SCC_Write(A_DATA, 'I');
    run(1100000);           /* 110 ms */
    CHECK((rr(0) & 0x01) != 0 && SCC_Read(A_DATA) == 'I', "a program that never raises RTS still receives");
    wr(5, 0xea);
    run(1000);

    /* ---- auto enables: CTS gates the transmitter ---- */
    wr(3, 0xe1);
    wr(5, 0xe8);            /* our RTS off, so (loopback) our CTS is off */
    run(1000);
    SCC_Write(A_DATA, 'J');
    run(CHAR_CLK * 3);
    CHECK((rr(0) & 0x04) == 0, "auto enables: character waits for CTS");
    wr(5, 0xea);
    run(CHAR_CLK * 3);
    CHECK((rr(0) & 0x04) != 0, "auto enables: sent once CTS is asserted");
    CHECK(SCC_Read(A_DATA) == 'J', "auto enables: received");
    wr(3, 0xc1);

    /* ---- different line settings arrive as framing errors ---- */
    loopback = 0;
    {
        static const unsigned char slow[] = {3, 0, 0x04, 0xb0,   /* peer at 1200 bps */
                                             1, 0, 0, 'X'};
        SCC_LinkFeed(slow, sizeof(slow));
    }
    run(CHAR_CLK * 2);
    CHECK((rr(0) & 0x01) != 0, "character from a mismatched peer arrives");
    CHECK((rr(1) & 0x40) != 0, "and carries a framing error");
    (void)SCC_Read(A_DATA);
    CHECK(SCC_LinkGetInfo(SCC_LINK_INFO_MISMATCHES) == 1, "mismatch counted");
    {
        static const unsigned char same[] = {3, 0, 0x26, 0x25};  /* 9765 */
        SCC_LinkFeed(same, sizeof(same));
    }
    loopback = 1;
    run(1000);

    /* ---- reset keeps the cable plugged in but drops RTS/DTR ---- */
    SCC_Init();
    run(1000);
    CHECK(SCC_LinkGetInfo(SCC_LINK_INFO_ONLINE) == 1, "reset keeps the link");
    CHECK(SCC_LinkGetInfo(SCC_LINK_INFO_PEER_LINES) == 0, "reset drops RTS and DTR");
    setup(0xea);
    CHECK((rr(0) & 0x38) == 0x38, "lines come back after reprogramming");

    /* ---- unplugging ---- */
    SCC_LinkSetConnected(0);
    run(1000);
    CHECK((rr(0) & 0x38) == 0, "unplugging drops CTS/DSR/DCD");
    SCC_Write(A_DATA, 'Z');
    run(CHAR_CLK * 2);
    CHECK((rr(0) & 0x05) == 0x04, "unplugged: characters go nowhere, Tx still runs");

    /* ---- the mouse channel is unchanged ---- */
    IRQH_IRQ[5] = 0;
    SCC_Write(B_CMD, 1); SCC_Write(B_CMD, 0x10);    /* Rx interrupt on all characters */
    SCC_Write(B_CMD, 3); SCC_Write(B_CMD, 0xc1);    /* Rx enable */
    SCC_Write(B_CMD, 5); SCC_Write(B_CMD, 0x60);
    SCC_Write(B_CMD, 5); SCC_Write(B_CMD, 0x62);    /* RTS rising: mouse sends a packet */
    CHECK(IRQH_IRQ[5] == 1, "mouse packet requests level 5");
    CHECK(ack() == 0x54, "vector: channel B receive");
    CHECK((SCC_Read(B_CMD) & 1) != 0, "mouse data available");
    CHECK(SCC_Read(B_DATA) == 1 && SCC_Read(B_DATA) == 2 && SCC_Read(B_DATA) == 3, "mouse packet bytes");
    run(100);
    CHECK(IRQH_IRQ[5] == 0, "no stray request after the mouse packet");

    if (failures) {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
