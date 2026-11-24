/*
 * FLOPPY Ukun register relay (fuk.c) against a scripted fake unit.
 * Checks the bytes sent on port 1, the predicted status values, a READ DATA
 * transfer delivered on port 2 (memory, DMAC channel 0, FDC interrupt and the
 * result phase), the SENSE DEVICE STATUS cache, forced ready, late or stray
 * read packets and link loss.
 */
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "fuk.h"
#include "dmac.h"

/* ---- emulator stubs ---- */

BYTE IOC_IntStat;
BYTE IOC_IntVect;
BYTE IRQH_IRQ[8];
void* IRQH_CallBack[8];
dmac_ch DMA[4];

static BYTE g_mem[0x20000];
static int g_dmaAbortWrites;

DWORD FASTCALL FDC_Int(BYTE irq) { (void)irq; return 0; }
DWORD FASTCALL FDD_Int(BYTE irq) { (void)irq; return 0; }
void IRQH_Int(BYTE irq, void* handler) { IRQH_IRQ[irq & 7] = 1; IRQH_CallBack[irq & 7] = handler; }
void IRQH_IRQCallBack(BYTE irq) { IRQH_IRQ[irq & 7] = 0; }
void FASTCALL DMA_Write(DWORD adr, BYTE data)
{
	if (adr == 0xe84007 && data == 0x10) {
		g_dmaAbortWrites++;
		DMA[0].CSR = (BYTE)((DMA[0].CSR | 0x10) & ~0x08);
	}
}
void dma_writemem24(DWORD adr, BYTE data) { if (adr < sizeof(g_mem)) g_mem[adr] = data; }
BYTE dma_readmem24(DWORD adr) { return adr < sizeof(g_mem) ? g_mem[adr] : 0xff; }

/* ---- fake unit ---- */

static BYTE g_out1[4096]; static int g_out1Len;
static BYTE g_out2[4096]; static int g_out2Len;
static BYTE g_in2[4096];  static int g_in2Len, g_in2Pos;
static int g_answers[64]; static int g_answerCount, g_answerPos;
static int g_armed;              /* a register read is waiting for its answer */
static long long g_clock;
static int g_lostCalls;
static const char* g_lostReason;
static int g_failWrites;           /* every write fails */
static int g_failPort2Reads;       /* port 2 reads fail */

static int fake_read(int port, BYTE* buf, int max)
{
	if (port == 1) {
		if (g_armed && g_answerPos < g_answerCount && max > 0) {
			g_armed = 0;
			buf[0] = (BYTE)g_answers[g_answerPos++];
			return 1;
		}
		return 0;
	}
	if (g_failPort2Reads) return -1;
	{
		int n = g_in2Len - g_in2Pos;
		if (n > max) n = max;
		memcpy(buf, g_in2 + g_in2Pos, (size_t)n);
		g_in2Pos += n;
		return n;
	}
}

static int fake_write(int port, const BYTE* buf, int len)
{
	if (g_failWrites) return -1;
	if (port == 1) {
		int i;
		memcpy(g_out1 + g_out1Len, buf, (size_t)len);
		g_out1Len += len;
		/* [0x80|reg, data] pairs; a lone byte below 0x80 is a read */
		for (i = 0; i < len; i++) {
			if (buf[i] & 0x80) i++;
			else g_armed = 1;
		}
	} else {
		memcpy(g_out2 + g_out2Len, buf, (size_t)len);
		g_out2Len += len;
	}
	return 0;
}

static void fake_wait(void) { g_clock += 10; }
static long long fake_clock(void) { return g_clock; }
static void fake_lost(const char* reason) { g_lostCalls++; g_lostReason = reason; }

static const FUKTransport kFake = { fake_read, fake_write, fake_wait, fake_clock, fake_lost };

static void reset_fake(void)
{
	g_out1Len = g_out2Len = g_in2Len = g_in2Pos = 0;
	g_answerCount = g_answerPos = 0;
	g_armed = 0;
	g_failWrites = 0;
	g_failPort2Reads = 0;
}

static void answer(int v) { g_answers[g_answerCount++] = v; }
static void unit_sends(const BYTE* p, int n) { memcpy(g_in2 + g_in2Len, p, (size_t)n); g_in2Len += n; }

/* ---- checks ---- */

static int failures;

#define CHECK_EQ(actual, expected, name) do { \
	long a_ = (long)(actual), e_ = (long)(expected); \
	if (a_ == e_) printf("PASS: %s\n", name); \
	else { printf("FAIL: %s (got $%lX, expected $%lX)\n", name, a_, e_); failures++; } \
} while (0)

static void check_out1(const BYTE* expected, int n, const char* name)
{
	if (g_out1Len == n && memcmp(g_out1, expected, (size_t)n) == 0) {
		printf("PASS: %s\n", name);
	} else {
		int i;
		printf("FAIL: %s (sent", name);
		for (i = 0; i < g_out1Len; i++) printf(" %02X", g_out1[i]);
		printf(")\n");
		failures++;
	}
	g_out1Len = 0;
}

static void attach_and_reset(void)
{
	memset(DMA, 0, sizeof(DMA));
	memset(IRQH_IRQ, 0, sizeof(IRQH_IRQ));
	IOC_IntStat = 0x04;              /* FDC interrupt enabled */
	reset_fake();
	FUK_Attach(&kFake);
	FUK_Reset();
	g_armed = 0;                     /* the init bytes are not a register read */
}

/* READ DATA programmed for 'size' bytes into $1000; the unit answers with
   'sent' data bytes and the seven result bytes. */
static void read_sector_short(int size, int sent)
{
	static const BYTE cmd[9] = { 0x46, 0x00, 0x00, 0x00, 0x01, 0x03, 0x01, 0x1b, 0xff };
	BYTE packet[3 + 0x400 + 7];
	int i, n = 0, len = sent + 7;

	DMA[0].MAR = 0x1000;
	DMA[0].MTC = (WORD)size;
	DMA[0].CSR = 0x08;               /* channel active */
	DMA[0].CCR = 0x80;
	for (i = 0; i < 9; i++) FUK_Write(0x03, cmd[i]);
	CHECK_EQ(DMA[0].MTC, 0, "READ DATA: DMAC count taken over");
	CHECK_EQ(g_out1Len, 18, "READ DATA: command sent at once with the last byte");
	CHECK_EQ(g_out1[0], 0x83, "READ DATA: command byte goes to $E94003");
	CHECK_EQ(g_out1[1], 0x46, "READ DATA: command byte");
	g_out1Len = 0;
	CHECK_EQ(FUK_Read(0x01), 0x10, "READ DATA: status 0x10 while waiting");
	CHECK_EQ(FUK_FilterDmaCsr(DMA[0].CSR) & 0x80, 0x00, "READ DATA: DMAC COC clear while waiting");

	packet[n++] = 'R';
	packet[n++] = (BYTE)(len & 0xff);
	packet[n++] = (BYTE)(len >> 8);
	for (i = 0; i < sent; i++) packet[n++] = (BYTE)(i ^ 0x5a);
	for (i = 0; i < 7; i++) packet[n++] = (BYTE)(0xa0 + i);
	unit_sends(packet, n);
	FUK_Tick(10);
}

/* READ DATA of one sector of 'size' bytes into $1000. */
static void read_sector(int size) { read_sector_short(size, size); }

/* Issues READ DATA for 'size' bytes into $1000; the unit sends nothing yet. */
static void start_read(int size)
{
	static const BYTE cmd[9] = { 0x46, 0x00, 0x00, 0x00, 0x01, 0x03, 0x01, 0x1b, 0xff };
	int i;
	DMA[0].MAR = 0x1000;
	DMA[0].MTC = (WORD)size;
	DMA[0].CSR = 0x08;
	DMA[0].CCR = 0x80;
	for (i = 0; i < 9; i++) FUK_Write(0x03, cmd[i]);
	g_out1Len = 0;
}

/* The rest of an 'R' packet after its 'R': length, 'size' data bytes with
   'C' at offset 100, and seven result bytes $A0-$A6. */
static int r_packet_body(BYTE* p, int size)
{
	int i, n = 0, len = size + 7;
	p[n++] = (BYTE)(len & 0xff);
	p[n++] = (BYTE)(len >> 8);
	for (i = 0; i < size; i++) p[n++] = (i == 100) ? 'C' : 0x00;
	for (i = 0; i < 7; i++) p[n++] = (BYTE)(0xa0 + i);
	return n;
}

static int out1_has(BYTE reg, BYTE value)
{
	int i;
	for (i = 0; i + 1 < g_out1Len; i++) {
		if (g_out1[i] == reg && g_out1[i + 1] == value) return 1;
	}
	return 0;
}

int main(void)
{
	int i, ok;

	/* --- connect and reset --- */
	attach_and_reset();
	{
		static const BYTE init[] = { 0x80, 0xfe, 0x40 };
		check_out1(init, 3, "first reset sends the init sequence");
	}
	FUK_Reset();
	{
		static const BYTE again[] = { 0x40 };
		check_out1(again, 1, "later resets send 0x40");
	}

	/* --- SEEK: pooled, then status predicted twice before asking --- */
	attach_and_reset();
	g_out1Len = 0;
	FUK_Write(0x03, 0x0f);
	FUK_Write(0x03, 0x00);
	FUK_Write(0x03, 0x10);
	CHECK_EQ(g_out1Len, 0, "SEEK: bytes wait in the send pool");
	FUK_Tick(2000);
	{
		static const BYTE seek[] = { 0x83, 0x0f, 0x83, 0x00, 0x83, 0x10 };
		check_out1(seek, 6, "SEEK: pool flushed after 0.2 ms");
	}
	CHECK_EQ(FUK_Read(0x01), 0x90, "SEEK: status predicted (1)");
	CHECK_EQ(FUK_Read(0x01), 0x90, "SEEK: status predicted (2)");
	CHECK_EQ(FUK_Read(0x01), 0x90, "SEEK: then the last status for 8 ms");
	CHECK_EQ(g_out1Len, 0, "SEEK: no round trip within 8 ms");
	FUK_Tick(200 * 400);
	answer(0x80);
	CHECK_EQ(FUK_Read(0x01), 0x80, "SEEK: after 8 ms the unit is asked");
	{
		static const BYTE rd[] = { 0x01 };
		check_out1(rd, 1, "SEEK: status read sent as 0x01");
	}
	CHECK_EQ(FUK_Read(0x01), 0x80, "SEEK: repeated status read answered from the last value");
	CHECK_EQ(g_out1Len, 0, "SEEK: no extra round trip for the repeat");

	/* --- READ DATA, 512 bytes --- */
	attach_and_reset();
	g_out1Len = 0;
	memset(g_mem, 0, sizeof(g_mem));
	g_dmaAbortWrites = 0;
	read_sector(512);
	ok = 1;
	for (i = 0; i < 512; i++) if (g_mem[0x1000 + i] != (BYTE)(i ^ 0x5a)) ok = 0;
	CHECK_EQ(ok, 1, "READ DATA: sector written at the DMAC address");
	CHECK_EQ(g_mem[0x1000 + 512], 0, "READ DATA: nothing written past the count");
	CHECK_EQ(g_dmaAbortWrites, 1, "READ DATA: DMAC channel 0 aborted after the transfer");
	CHECK_EQ(IRQH_IRQ[1], 1, "READ DATA: FDC interrupt raised");
	CHECK_EQ(IRQH_CallBack[1] == (void*)&FDC_Int, 1, "READ DATA: interrupt vectored through FDC_Int");
	CHECK_EQ(FUK_FilterDmaCsr(DMA[0].CSR) & 0x80, 0x80, "READ DATA: DMAC COC set afterwards");
	CHECK_EQ(FUK_Read(0x01), 0xd0, "READ DATA: status 0xD0 in the result phase");
	for (i = 0; i < 7; i++) {
		char name[64];
		snprintf(name, sizeof(name), "READ DATA: result byte %d", i);
		CHECK_EQ(FUK_Read(0x03), 0xa0 + i, name);
	}
	CHECK_EQ(IRQH_IRQ[1], 0, "READ DATA: FDC interrupt dropped after the result");
	CHECK_EQ(FUK_Read(0x01), 0x80, "READ DATA: status 0x80 afterwards");
	CHECK_EQ(g_out1Len, 0, "READ DATA: result phase needs no round trip");

	/* --- READ DATA, 128 bytes: length byte above 0x7F --- */
	attach_and_reset();
	g_out1Len = 0;
	memset(g_mem, 0, sizeof(g_mem));
	read_sector(128);
	CHECK_EQ(g_mem[0x1000 + 127], (BYTE)(127 ^ 0x5a), "READ DATA 128: last byte written");
	CHECK_EQ(FUK_Read(0x01), 0xd0, "READ DATA 128: result phase reached");

	/* --- READ DATA that fails: only the result comes back --- */
	attach_and_reset();
	g_out1Len = 0;
	memset(g_mem, 0, sizeof(g_mem));
	read_sector_short(512, 0);
	ok = 1;
	for (i = 0; i < 7; i++) if (g_mem[0x1000 + i] != 0) ok = 0;
	CHECK_EQ(ok, 1, "READ DATA error: result bytes not written to memory");
	CHECK_EQ(FUK_Read(0x01), 0xd0, "READ DATA error: result phase reached");
	CHECK_EQ(FUK_Read(0x03), 0xa0, "READ DATA error: result byte 0 still delivered");

	/* --- READ DATA shorter than the DMAC count --- */
	attach_and_reset();
	g_out1Len = 0;
	memset(g_mem, 0, sizeof(g_mem));
	read_sector_short(512, 100);
	CHECK_EQ(g_mem[0x1000 + 99], (BYTE)(99 ^ 0x5a), "READ DATA short: last data byte written");
	ok = 1;
	for (i = 100; i < 107; i++) if (g_mem[0x1000 + i] != 0) ok = 0;
	CHECK_EQ(ok, 1, "READ DATA short: result bytes not written after the data");

	/* --- SENSE DEVICE STATUS: answered once, then from the cache --- */
	attach_and_reset();
	g_out1Len = 0;
	FUK_Tick(30 * 400);              /* past the reset hold-off */
	FUK_Write(0x03, 0x04);
	CHECK_EQ(g_out1Len, 0, "SDS: command byte held back");
	CHECK_EQ(FUK_Read(0x01), 0x90, "SDS: status 0x90 before the drive byte");
	FUK_Write(0x03, 0x00);
	{
		static const BYTE sds[] = { 0x83, 0x04, 0x83, 0x00 };
		check_out1(sds, 4, "SDS: command sent with the drive byte");
	}
	CHECK_EQ(FUK_Read(0x01), 0xd0, "SDS: status 0xD0");
	answer(0x28);
	CHECK_EQ(FUK_Read(0x03), 0x28, "SDS: ST3 from the unit");
	g_out1Len = 0;
	FUK_Write(0x03, 0x04);
	FUK_Read(0x01);
	FUK_Write(0x03, 0x00);
	CHECK_EQ(FUK_Read(0x03), 0x28, "SDS: repeated soon, answered from the cache");
	CHECK_EQ(g_out1Len, 0, "SDS: no traffic for the cached answer");

	/* --- forced ready (OPM CT2) is announced on RECALIBRATE --- */
	attach_and_reset();
	g_out1Len = 0;
	FUK_SetForceReady(1);
	FUK_Write(0x03, 0x07);
	FUK_Write(0x03, 0x00);
	FUK_Tick(2000);
	{
		static const BYTE recal[] = { 0xa3, 0x40, 0x83, 0x07, 0x83, 0x00 };
		check_out1(recal, 6, "forced ready sent before RECALIBRATE");
	}
	FUK_SetForceReady(0);
	FUK_Tick(2000);
	{
		static const BYTE off[] = { 0xa3, 0x00 };
		check_out1(off, 2, "forced ready cleared");
	}

	/* --- FDD interrupt: 'D' raises it 1 ms later --- */
	attach_and_reset();
	g_out1Len = 0;
	IOC_IntStat = 0x02;
	{
		static const BYTE d[] = { 'D' };
		unit_sends(d, 1);
	}
	FUK_Tick(10);
	CHECK_EQ(IRQH_IRQ[1], 0, "FDD interrupt: not yet");
	FUK_Tick(10000);
	CHECK_EQ(IRQH_IRQ[1], 1, "FDD interrupt: raised after 1 ms");
	answer(0x00);
	CHECK_EQ(FUK_IocRead(0x02) & 0x40, 0x40, "FDD interrupt: shown in $E9C001");
	{
		static const BYTE ioc[] = { 0x11 };
		check_out1(ioc, 1, "FDD interrupt: $E9C001 read relayed as 0x11");
	}

	/* --- link loss: no answer --- */
	attach_and_reset();
	g_lostCalls = 0;
	CHECK_EQ(FUK_Read(0x05), 0x00, "no drive selected: $E94005 answered locally");
	FUK_Write(0x07, 0x80);
	CHECK_EQ(FUK_Read(0x01), 0xff, "no answer: read gives $FF");
	CHECK_EQ(FUK_IsActive(), 0, "no answer: detached");
	CHECK_EQ(g_lostCalls, 1, "no answer: host told once");

	/* --- link loss: the register read cannot be sent --- */
	attach_and_reset();
	g_lostCalls = 0;
	FUK_Write(0x07, 0x80);
	g_failWrites = 1;
	CHECK_EQ(FUK_Read(0x01), 0xff, "write fails: read gives $FF");
	CHECK_EQ(FUK_IsActive(), 0, "write fails: detached");
	CHECK_EQ(g_lostCalls, 1, "write fails: host told once");

	/* --- link loss: port 2 fails while waiting for the answer --- */
	attach_and_reset();
	g_lostCalls = 0;
	FUK_Write(0x07, 0x80);
	g_failPort2Reads = 1;
	CHECK_EQ(FUK_Read(0x01), 0xff, "port 2 fails: read gives $FF");
	CHECK_EQ(FUK_IsActive(), 0, "port 2 fails: detached");
	CHECK_EQ(g_lostCalls, 1, "port 2 fails: host told once");

	/* --- link loss: a pooled write fails while a read waits for its data --- */
	attach_and_reset();
	g_lostCalls = 0;
	start_read(0x400);
	unit_sends((const BYTE*)"R", 1);
	FUK_Tick(10);
	FUK_Write(0x07, 0x80);           /* pooled, sent on a later tick */
	g_failWrites = 1;
	FUK_Tick(1000000);               /* flush fails and detaches */
	CHECK_EQ(FUK_IsActive(), 0, "pooled write fails mid-read: detached");
	CHECK_EQ(g_lostCalls, 1, "pooled write fails mid-read: host told once");
	g_failWrites = 0;

	/* --- forced ready follows CT2 even before a RECALIBRATE --- */
	attach_and_reset();
	g_out1Len = 0;
	FUK_SetForceReady(1);
	FUK_SetForceReady(0);
	FUK_Write(0x03, 0x07);
	FUK_Write(0x03, 0x00);
	FUK_Tick(2000);
	CHECK_EQ(out1_has(0xa3, 0x40), 0, "forced ready: CT2 back off before RECALIBRATE, nothing sent");

	/* --- CT2 written again while on: "off" still reaches the unit --- */
	attach_and_reset();
	g_out1Len = 0;
	FUK_SetForceReady(1);
	FUK_Write(0x03, 0x07);
	FUK_Write(0x03, 0x00);
	FUK_Tick(2000);
	g_out1Len = 0;
	FUK_SetForceReady(1);            /* a sound driver rewrites $1B */
	FUK_Tick(2000);
	CHECK_EQ(g_out1Len, 0, "forced ready: repeated on, nothing sent");
	FUK_SetForceReady(0);
	FUK_Tick(2000);
	CHECK_EQ(out1_has(0xa3, 0x00), 1, "forced ready: off sent after a repeated on");

	/* --- a read that times out, then the rest of its packet arrives --- */
	attach_and_reset();
	{
		BYTE body[2 + 0x400 + 7];
		int n;
		start_read(0x400);
		unit_sends((const BYTE*)"R", 1);
		FUK_Tick(10);
		g_clock += 2001;                 /* 2 s of wall clock */
		FUK_Tick(10);
		CHECK_EQ(IRQH_IRQ[1], 1, "late read: timeout ends the command with an interrupt");
		CHECK_EQ(FUK_Read(0x03) & 0xc0, 0x40, "late read: made-up result is abnormal termination");
		for (i = 1; i < 7; i++) FUK_Read(0x03);
		CHECK_EQ(IRQH_IRQ[1], 0, "late read: interrupt dropped after the result");
		n = r_packet_body(body, 0x400);
		unit_sends(body, n);
		FUK_Tick(10);
		CHECK_EQ(IRQH_IRQ[1], 0, "late read: 'C' inside the late data is not an event");
		unit_sends((const BYTE*)"C", 1);
		FUK_Tick(10);
		CHECK_EQ(IRQH_IRQ[1], 1, "late read: the next real event is seen");
	}

	/* --- an 'R' packet with no read waiting is skipped whole --- */
	attach_and_reset();
	{
		BYTE body[2 + 0x400 + 7];
		int n = r_packet_body(body, 0x400);
		unit_sends((const BYTE*)"R", 1);
		unit_sends(body, n);
		FUK_Tick(10);
		CHECK_EQ(IRQH_IRQ[1], 0, "stray R packet: its data is not taken for events");
	}

	/* --- Turbo / No-Wait: emulated time alone does not time a read out --- */
	attach_and_reset();
	{
		BYTE body[2 + 0x400 + 7];
		int n;
		start_read(0x400);
		unit_sends((const BYTE*)"R", 1);
		FUK_Tick(10);
		FUK_Tick(50000 * 400 + 1);       /* over 2 s of emulated time */
		CHECK_EQ(FUK_Read(0x01), 0x10, "turbo: still waiting for the data");
		n = r_packet_body(body, 0x400);
		unit_sends(body, n);
		FUK_Tick(10);
		CHECK_EQ(FUK_Read(0x03), 0xa0, "turbo: result comes from the unit");
	}

	/* --- an empty 'R' packet ends the read at once --- */
	attach_and_reset();
	{
		static const BYTE empty[] = { 'R', 0x00, 0x00, 'C' };
		start_read(0x400);
		unit_sends(empty, 3);
		FUK_Tick(10);
		CHECK_EQ(IRQH_IRQ[1], 1, "empty R: read ended with an interrupt");
		CHECK_EQ(FUK_Read(0x03) & 0xc0, 0x40, "empty R: abnormal termination result");
	}

	if (failures) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("All FLOPPY Ukun checks passed\n");
	return 0;
}
