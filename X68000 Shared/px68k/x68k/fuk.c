// ---------------------------------------------------------------------------------------
//  FUK.C - FLOPPY Ukun (ふろっぴU君, TNB製作所) USB FDC unit: register relay
//
//  A C port of FUK.java from XEiJ+t (Copyright (C) 2025-2026 TNB製作所),
//  made with the permission of TNB製作所. The unit sits in place of the
//  emulated uPD72065:
//    port 1  host -> unit  [0x80|reg, data] writes a register,
//                          [reg] reads one, answered by one byte on port 1.
//            reg 0x01/0x03/0x05/0x07 = $E94001-$E94007, 0x1x = $E9C00x,
//            0x23 = OPM CT2 (forced ready), 0x30/0x31 = DMAC ch0 MTC low/high.
//    port 2  unit -> host  'C'/'c' FDC interrupt on/off, 'D'/'d' FDD interrupt
//                          on/off, 'R' len16 data+result (read transfer),
//                          'W' request write data (answered with 0x4D + data),
//                          'w' write data taken, 't' transfer cancelled.
//  A round trip over USB is slow next to a CPU polling the status register,
//  so the status register is predicted for most command phases and
//  SENSE DEVICE STATUS and $E94005 answers are cached, as XEiJ+t does.
//
//  Times are kept in 10 MHz units (0.1 us), the unit of usedclk.
// ---------------------------------------------------------------------------------------

#include "fuk.h"
#include "fdc.h"
#include "fdd.h"
#include "ioc.h"
#include "irqh.h"
#include "dmac.h"
#include "x68kmemory.h"

#if defined(HAVE_C68K)
#include "../m68000/c68k/c68k.h"
extern c68k_struc C68K;
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

extern void* IRQH_CallBack[8];

// XEiJ+t measures in TTT = 40 us steps.
#define FUK_TTT              400LL
#define FUK_POOL_SPAN        2000LL        // 0.2 ms: flush the send pool
#define FUK_FDD_SPAN         10000LL       // 1 ms: FDD interrupt delay
#define FUK_RECV_TIMEOUT_MS  2000         // wall clock limit for a read transfer
#define FUK_ANSWER_TIMEOUT_MS 1000         // wall clock wait for a port 1 answer

#define FUK_POOL_SIZE  0x1000
#define FUK_DATA_SIZE  0x10000

typedef long long fuk_time;

typedef enum {
	Mode_None,

	Mode_SenseDeviceStatus_WaitStatus90,   // status: 0x90
	Mode_SenseDeviceStatus_WaitStatusD0,   // status: 0xD0
	Mode_SenseDeviceStatus_CheckStatus,    // remember the answer
	Mode_SenseDeviceStatus_ReturnStatus,   // answer from the cache
	Mode_SenseDeviceStatus_WaitStatus80,   // status: 0x80

	Mode_SenseInterruptStatus_StatusD0,    // status: 0xD0
	Mode_SenseInterruptStatus_NoWait,

	Mode_Seek_StatusD0,
	Mode_Seek_NoWait,

	Mode_Recalibrate_Status90,
	Mode_Recalibrate_NoWait,

	Mode_ReadData_Command,                 // status: 0x90
	Mode_ReadData_Receive,                 // status: 0x10
	Mode_ReadData_SoftReceive,             // status: 0xF0 (CPU reads the data)
	Mode_ReadData_Response,                // status: 0xD0
	Mode_ReadData_After,                   // status: 0x80

	Mode_WriteData_Command,                // status: 0x90
	Mode_WriteData_Send,
	Mode_WriteData_Response,               // status: 0xD0

	Mode_ReadId_Command,                   // status: 0x90
	Mode_ReadId_Wait,
	Mode_ReadId_Result                     // status: 0xD0
} FUKMode;

static const char* const kModeName[] = {
	"None",
	"SDS_Wait90", "SDS_WaitD0", "SDS_Check", "SDS_Cached", "SDS_Wait80",
	"SIS_D0", "SIS_NoWait",
	"Seek_D0", "Seek_NoWait",
	"Recal_90", "Recal_NoWait",
	"Read_Command", "Read_Receive", "Read_SoftReceive", "Read_Response", "Read_After",
	"Write_Command", "Write_Send", "Write_Response",
	"ReadId_Command", "ReadId_Wait", "ReadId_Result",
};

// Command lengths (command byte included), indexed by command & 0x1F.
static const BYTE s_commandLength[32] = {
	1, 1, 9, 3, 2, 9, 9, 2, 1, 9, 2, 1, 9, 6, 1, 3,
	1, 9, 1, 1, 1, 1, 1, 1, 1, 9, 1, 1, 1, 9, 1, 1,
};

static const FUKTransport* s_io = NULL;
static int s_active = 0;
static int s_needInit = 0;     // first reset after attaching sends the full init
static fuk_time s_now = 0;

// send pool
static BYTE s_pool[FUK_POOL_SIZE];
static int s_poolLength = 0;
static fuk_time s_poolDeadline = -1;

// register state machine
static FUKMode s_mode = Mode_None;
static int s_modeCounter = 0;
static int s_readDataCounter = 0;
static int s_writeDataCounter = 0;
static int s_cmdCount = 0;
static int s_lastCmd = 0;
static fuk_time s_tickCounter = 0;
static BYTE s_lastStatus = 0;
static BYTE s_activeCommandList[16];
static int s_activeCommandCount = 0;
static int s_isForceReadyStatus = 0;
static int s_isNotifyForceReady = 0;
static int s_equalStatusCounter = 0;

static fuk_time s_driveOptionTick = 0;
static int s_driveOptionActualDrive = 0;
static int s_driveOptionDrive = 0;
static int s_driveOptionCtrl[16];
static int s_driveOptionResult[16];
static int s_driveOptionIsExec = 0;

static fuk_time s_tickSenseDeviceStatus = 0;
static int s_senseDeviceStatus[8];

// transfers
static BYTE s_recvData[FUK_DATA_SIZE];
static int s_recvDataSize = 0;
static int s_recvDataPos = 0;
static int s_isReadCommandR = 0;
static int s_rcCount = 0;
static DWORD s_mar = 0;
static int s_mtc = 0;
static long long s_recvDeadlineMs = -1;  // wall clock, from s_io->clock_ms()
static BYTE s_writeData[FUK_DATA_SIZE + 1];

// An 'R' packet nobody is waiting for (a late one after a timeout) is read
// to its end by its length and dropped, so its data is not taken for events.
static int s_skipState = 0;    // 0 none, 1 length low, 2 length high, 3 data
static int s_skipRemaining = 0;

// interrupt lines
static BYTE s_signal = 0;      // 0x80 FDC, 0x40 FDD (as in $E9C001)
static fuk_time s_fddRiseAt = -1;
static fuk_time s_fddFallAt = -1;

static void FUK_LinkLost(const char* reason);
static void FUK_PumpEvents(void);
static int FUK_IsWriteIdTransCommand(BYTE cmd);

// -----------------------------------------------------------------------
//   trace: MPX68K_FUK_TRACE=1 writes every register access to stderr
// -----------------------------------------------------------------------

static int s_traceOn = -1;
static char s_traceLast[192];
static int s_traceRepeat = 0;

static int FUK_TraceEnabled(void)
{
	if (s_traceOn < 0) {
		const char* e = getenv("MPX68K_FUK_TRACE");
		s_traceOn = (e != NULL && e[0] != '\0' && e[0] != '0');
	}
	return s_traceOn;
}

static void FUK_Trace(const char* fmt, ...)
{
	char line[192];
	va_list ap;
	if (!FUK_TraceEnabled()) return;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	// a polling loop repeats the same line thousands of times
	if (strcmp(line, s_traceLast) == 0) {
		s_traceRepeat++;
		return;
	}
	if (s_traceRepeat > 0) {
		fprintf(stderr, "FUK   ... %d more\n", s_traceRepeat);
		s_traceRepeat = 0;
	}
	fprintf(stderr, "FUK %s\n", line);
	strncpy(s_traceLast, line, sizeof(s_traceLast) - 1);
	s_traceLast[sizeof(s_traceLast) - 1] = '\0';
}

// FNV-1a; only used to match sectors in the trace
static unsigned FUK_Hash(const BYTE* p, int n)
{
	unsigned h = 2166136261u;
	while (n-- > 0) {
		h ^= *p++;
		h *= 16777619u;
	}
	return h;
}

// One line per sector, keyed by C/H/R, so that a sector written can be
// matched with the same sector read back later. The command bytes are
// cmd, HD/US, C, H, R, N, EOT, ...; with MT set, R wraps to 1 on the
// other head after EOT.
static void FUK_TraceSectors(const char* dir, const BYTE* data, int len)
{
	const BYTE* c = s_activeCommandList;
	int size, h, r, off;
	if (!FUK_TraceEnabled() || s_activeCommandCount < 7) return;
	size = 128 << (c[5] & 7);
	h = c[3];
	r = c[4];
	for (off = 0; off < len; off += size) {
		int n = (len - off < size) ? len - off : size;
		FUK_Trace("sector %s C=%02X H=%02X R=%02X: %d bytes, hash %08X",
		          dir, c[2], h, r, n, FUK_Hash(data + off, n));
		if (r >= c[6] && (c[0] & 0x80)) {
			h ^= 1;
			r = 1;
		} else {
			r++;
		}
	}
}

// -----------------------------------------------------------------------
//   transport helpers
// -----------------------------------------------------------------------

static int FUK_Send(int port, const BYTE* buf, int len)
{
	if (!s_active) return -1;
	if (s_io->write(port, buf, len) < 0) {
		FUK_LinkLost("write failed");
		return -1;
	}
	return 0;
}

static void FUK_PoolFlush(void)
{
	s_poolDeadline = -1;
	if (s_poolLength > 0) {
		int n = s_poolLength;
		s_poolLength = 0;
		FUK_Send(1, s_pool, n);
	}
}

static void FUK_PoolAdd1(int d1)
{
	if (s_poolLength > FUK_POOL_SIZE - 1) FUK_PoolFlush();
	s_pool[s_poolLength++] = (BYTE)d1;
	s_poolDeadline = s_now + FUK_POOL_SPAN;
}

static void FUK_PoolAdd2(int d1, int d2)
{
	if (s_poolLength > FUK_POOL_SIZE - 2) FUK_PoolFlush();
	s_pool[s_poolLength++] = (BYTE)d1;
	s_pool[s_poolLength++] = (BYTE)d2;
	s_poolDeadline = s_now + FUK_POOL_SPAN;
}

// Adds two bytes and sends the pool at once.
static void FUK_PoolAdd2a(int d1, int d2)
{
	if (s_poolLength > FUK_POOL_SIZE - 2) FUK_PoolFlush();
	s_pool[s_poolLength++] = (BYTE)d1;
	s_pool[s_poolLength++] = (BYTE)d2;
	FUK_PoolFlush();
}

// Drops whatever port 1 sent unasked (XEiJ+t's check_ints1 empties its queue
// before every access) and services port 2.
static void FUK_DrainPort1(void)
{
	BYTE buf[256];
	int n;
	while (s_active && (n = s_io->read(1, buf, sizeof(buf))) != 0) {
		if (n < 0) {
			FUK_LinkLost("read failed");
			return;
		}
	}
}

// Waits for the one-byte answer on port 1. Port 2 keeps being serviced
// meanwhile, as XEiJ+t's listener thread does. Returns -1 if the link is lost.
static int FUK_WaitAnswer(void)
{
	long long start;
	BYTE b;
	if (!s_active) return -1;      // the request itself may have lost the link
	start = s_io->clock_ms();
	for (;;) {
		int n;
		if (!s_active) return -1;
		n = s_io->read(1, &b, 1);
		if (n == 1) return b;
		if (n < 0) {
			FUK_LinkLost("read failed");
			return -1;
		}
		FUK_PumpEvents();
		if (!s_active) return -1;
		if (s_io->clock_ms() - start > FUK_ANSWER_TIMEOUT_MS) {
			FUK_LinkLost("no answer from the unit");
			return -1;
		}
		s_io->wait();
	}
}

// -----------------------------------------------------------------------
//   interrupts and DMAC
// -----------------------------------------------------------------------

static void FUK_Lower(DWORD (FASTCALL *handler)(BYTE))
{
	if (IRQH_IRQ[1] && IRQH_CallBack[1] == (void*)handler) {
		IRQH_IRQCallBack(1);
	}
}

static void FUK_FdcInterrupt(int on)
{
	FUK_Trace("FDC interrupt %s (IOC %02X)", on ? "on" : "off", IOC_IntStat);
	if (on) {
		if (s_signal & 0x80) {
			// XEiJ+t drops the line first so every 'C' is a fresh edge.
			s_signal &= ~0x80;
			FUK_Lower(FDC_Int);
		}
		s_signal |= 0x80;
		if (IOC_IntStat & 4) IRQH_Int(1, (void*)&FDC_Int);
	} else {
		s_signal &= ~0x80;
		FUK_Lower(FDC_Int);
	}
}

static void FUK_FddInterrupt(int on)
{
	FUK_Trace("FDD interrupt %s (IOC %02X)", on ? "on" : "off", IOC_IntStat);
	if (on) {
		if (!(s_signal & 0x40)) {
			s_signal |= 0x40;
			if (IOC_IntStat & 2) IRQH_Int(1, (void*)&FDD_Int);
		}
	} else {
		s_signal &= ~0x40;
		FUK_Lower(FDD_Int);
	}
}

// The transfer is over: abort DMAC channel 0 and clear its count, as
// XEiJ+t's clearDmac() does (CCR <- SAB, MTC <- 0).
static void FUK_ClearDmac(void)
{
	DMA_Write(0xe84007, 0x10);
	DMA[0].MTC = 0;
}

static void FUK_SendMTC(void)
{
	int m = DMA[0].MTC;
	FUK_PoolAdd2(0x30 | 0x80, m & 0xFF);
	FUK_PoolAdd2(0x31 | 0x80, (m >> 8) & 0xFF);
}

static void FUK_SendForceReady(int ready)
{
	FUK_PoolAdd2(0x23 | 0x80, ready ? 0x40 : 0x00);
}

// -----------------------------------------------------------------------
//   port 2 events
// -----------------------------------------------------------------------

static void FUK_OnReceivedData(void)
{
	FUK_ClearDmac();
	if (s_recvDataSize >= 7) {
		// data (if any) followed by the 7 result bytes
		s_mode = (s_mtc == 0) ? Mode_ReadData_SoftReceive : Mode_ReadData_Response;
	} else {
		// no result at all: make one up (abnormal termination)
		s_recvDataSize = 7;
		s_recvData[0] = (BYTE)(0x40 | (s_activeCommandList[1] & 0x07));
		s_recvData[1] = 0;
		s_recvData[2] = 0;
		s_recvData[3] = s_activeCommandList[3];
		s_recvData[4] = s_activeCommandList[4];
		s_recvData[5] = s_activeCommandList[5];
		s_recvData[6] = s_activeCommandList[6];
		s_mode = Mode_ReadData_Response;
	}
	FUK_FdcInterrupt(1);
	s_readDataCounter = 0;
}

static void FUK_RecvTimeout(void)
{
	s_recvDeadlineMs = -1;
	if (s_mode != Mode_ReadData_Receive) return;
	FUK_Trace("read data timed out (%d of %d bytes)", s_recvDataPos, s_recvDataSize);
	// Skip whatever is left of this packet when it arrives, rather than
	// emptying port 2 now: that cut a packet in half, and the rest of its
	// data was then read as events.
	if (s_isReadCommandR) {
		if (s_rcCount == 0) {
			s_skipState = 1;
		} else if (s_rcCount == 1) {
			s_skipRemaining = s_recvDataSize;   // the low byte of the length
			s_skipState = 2;
		} else {
			s_skipRemaining = s_recvDataSize - s_recvDataPos;
			s_skipState = (s_skipRemaining > 0) ? 3 : 0;
		}
	}
	s_recvDataSize = 0;
	s_isReadCommandR = 0;
	FUK_OnReceivedData();
}

static void FUK_SkipByte(BYTE b)
{
	if (s_skipState == 1) {
		s_skipRemaining = b;
		s_skipState = 2;
	} else if (s_skipState == 2) {
		s_skipRemaining |= (int)b << 8;
		s_skipState = (s_skipRemaining > 0) ? 3 : 0;
	} else if (--s_skipRemaining <= 0) {
		s_skipState = 0;
		FUK_Trace("late read data dropped");
	}
}

static void FUK_EventByte(BYTE b)
{
	if (s_skipState) {
		FUK_SkipByte(b);
		return;
	}
	if (s_isReadCommandR) {
		if (s_rcCount == 0) {
			s_recvDataSize = b;
		} else if (s_rcCount == 1) {
			// XEiJ+t combines signed bytes here; the length is unsigned.
			s_recvDataSize |= (int)b << 8;
			s_recvDataPos = 0;
			if (s_recvDataSize == 0) {
				// nothing follows: end the transfer with a made-up result
				s_recvDeadlineMs = -1;
				s_isReadCommandR = 0;
				FUK_OnReceivedData();
				return;
			}
		} else {
			// the last seven bytes are the FDC result, not sector data
			if (s_recvDataPos < s_mtc && s_recvDataPos < s_recvDataSize - 7) {
				dma_writemem24((s_mar + s_recvDataPos) & 0xFFFFFF, b);
			}
			if (s_recvDataPos < FUK_DATA_SIZE) {
				s_recvData[s_recvDataPos] = b;
			}
			s_recvDataPos++;
			if (s_recvDataPos == s_recvDataSize) {
				// all data and the result are in
				s_recvDeadlineMs = -1;
				s_isReadCommandR = 0;
				FUK_Trace("read data in: %d bytes to $%06X (count %d)", s_recvDataSize, (unsigned)s_mar, s_mtc);
				if (s_recvDataSize > 7 && s_recvDataSize <= FUK_DATA_SIZE) {
					const BYTE* st = s_recvData + s_recvDataSize - 7;
					FUK_TraceSectors("in ", s_recvData, s_recvDataSize - 7);
					FUK_Trace("read result %02X %02X %02X  C=%02X H=%02X R=%02X N=%02X",
					          st[0], st[1], st[2], st[3], st[4], st[5], st[6]);
				}
				FUK_OnReceivedData();
			}
		}
		s_rcCount++;
		return;
	}

	FUK_Trace("event '%c' (mode %s)", (b >= 0x20 && b < 0x7f) ? b : '?', kModeName[s_mode]);
	switch (b) {
	case 'C':
		FUK_FdcInterrupt(1);
		break;
	case 'c':
		FUK_FdcInterrupt(0);
		break;
	case 'D':
		s_fddRiseAt = s_now + FUK_FDD_SPAN;
		s_tickSenseDeviceStatus = s_now;
		break;
	case 'd':
		s_fddFallAt = s_now + FUK_FDD_SPAN;
		break;
	case 'R':
		if (s_mode == Mode_ReadData_Receive) {
			s_mar = DMA[0].MAR & 0xFFFFFF;
			s_isReadCommandR = 1;
			s_recvDataSize = 0;
			s_rcCount = 0;
			// Wall clock: in Turbo or No-Wait, emulated time runs ahead of
			// the USB link.
			s_recvDeadlineMs = s_io->clock_ms() + FUK_RECV_TIMEOUT_MS;
		} else {
			// nobody is waiting for it (it came after a timeout): skip it
			s_skipState = 1;
		}
		break;
	case 'W':
		if (s_mode == Mode_WriteData_Send) {
			DWORD mar = DMA[0].MAR & 0xFFFFFF;
			int count = DMA[0].MTC;
			int j = 0;
			int k;
			s_writeData[j++] = 0x4D;      // start of the write data
			for (k = 0; k < count && j <= FUK_DATA_SIZE; k++) {
				s_writeData[j++] = dma_readmem24((mar + k) & 0xFFFFFF);
			}
			s_writeDataCounter = count;
			FUK_Trace("write data out: %d bytes from $%06X (cmd $%02X)", j - 1, (unsigned)mar, s_activeCommandList[0]);
			if (FUK_IsWriteIdTransCommand(s_activeCommandList[0])) {
				if (FUK_TraceEnabled()) {
					FUK_Trace("format ids: %d bytes, hash %08X", j - 1, FUK_Hash(s_writeData + 1, j - 1));
				}
			} else {
				FUK_TraceSectors("out", s_writeData + 1, j - 1);
			}
			FUK_Send(2, s_writeData, j);
		}
		break;
	case 'w':
		if (s_mode == Mode_WriteData_Send) {
			FUK_ClearDmac();
			s_mode = Mode_WriteData_Response;
			s_writeDataCounter = 0;
			FUK_FdcInterrupt(1);
		}
		break;
	case 't':
		FUK_ClearDmac();
		s_mode = Mode_None;
		break;
	default:
		break;
	}
}

static void FUK_PumpEvents(void)
{
	BYTE buf[512];
	int n;
	while (s_active && (n = s_io->read(2, buf, sizeof(buf))) != 0) {
		int i;
		if (n < 0) {
			FUK_LinkLost("read failed");
			return;
		}
		for (i = 0; i < n && s_active; i++) {
			FUK_EventByte(buf[i]);
		}
	}
}

// -----------------------------------------------------------------------
//   state
// -----------------------------------------------------------------------

static void FUK_InitDriveOption(void)
{
	int i;
	s_driveOptionTick = s_now;
	for (i = 0; i < 16; i++) {
		s_driveOptionCtrl[i] = -1;
		s_driveOptionResult[i] = -1;
	}
	s_driveOptionIsExec = 1;
}

static void FUK_ResetState(fuk_time senseDelay)
{
	int i;
	s_poolLength = 0;
	s_poolDeadline = -1;
	s_recvDeadlineMs = -1;
	s_isReadCommandR = 0;
	s_skipState = 0;
	s_skipRemaining = 0;
	s_cmdCount = 0;
	s_lastCmd = 0;
	s_tickCounter = s_now;
	s_mode = Mode_None;
	FUK_InitDriveOption();
	s_driveOptionActualDrive = 0;
	s_driveOptionDrive = 0;
	s_tickSenseDeviceStatus = s_now + senseDelay;
	for (i = 0; i < 8; i++) s_senseDeviceStatus[i] = -1;
	s_isForceReadyStatus = 0;
	s_isNotifyForceReady = 0;
	s_signal = 0;
	s_fddRiseAt = -1;
	s_fddFallAt = -1;
}

void FUK_Attach(const FUKTransport* transport)
{
	s_io = transport;
	s_active = (transport != NULL);
	s_needInit = s_active;
	FUK_ResetState(0);
}

void FUK_Detach(void)
{
	if (s_active) {
		s_signal = 0;
		FUK_Lower(FDC_Int);
		FUK_Lower(FDD_Int);
	}
	s_active = 0;
	s_io = NULL;
}

int FUK_IsActive(void)
{
	return s_active;
}

static void FUK_LinkLost(const char* reason)
{
	const FUKTransport* io = s_io;
	if (!s_active) return;
	FUK_Trace("link lost: %s (mode %s)", reason, kModeName[s_mode]);
	FUK_Detach();
	if (io && io->lost) io->lost(reason);
}

void FUK_Reset(void)
{
	static const BYTE kInit[] = { 0x80, 0xFE, 0x40 };
	static const BYTE kReset[] = { 0x40 };
	if (!s_active) return;
	FUK_Trace("reset (%s)", s_needInit ? "init 80 FE 40" : "40");
	if (s_needInit) {
		// first reset after connecting: XEiJ+t's fukConnect() sends
		// 0x80,0xFE (no 0xFF escape) and 0x40
		s_needInit = 0;
		if (FUK_Send(1, kInit, sizeof(kInit)) < 0) return;
		FUK_ResetState(0);
	} else {
		if (FUK_Send(1, kReset, sizeof(kReset)) < 0) return;
		FUK_ResetState(10000 * FUK_TTT);
	}
	{
		BYTE buf[256];
		while (s_active && s_io->read(1, buf, sizeof(buf)) > 0) {
		}
	}
}

static fuk_time s_nextSnapshot = 0;

void FUK_Tick(int clk10m)
{
	if (!s_active) return;
	s_now += clk10m;
	if (s_traceOn > 0 && s_now >= s_nextSnapshot) {
		// twice a second: where the CPU is and what is pending
		s_nextSnapshot = s_now + 5000000;
#if defined(HAVE_C68K)
		FUK_Trace("cpu PC=%06X SR=%04X irq1=%d IOC=%02X signal=%02X mode=%s",
		          (unsigned)(C68k_Get_PC(&C68K) & 0xFFFFFF), (unsigned)(C68k_Get_SR(&C68K) & 0xFFFF),
		          IRQH_IRQ[1], IOC_IntStat, s_signal, kModeName[s_mode]);
#else
		FUK_Trace("time %lld IOC=%02X signal=%02X mode=%s", s_now, IOC_IntStat, s_signal, kModeName[s_mode]);
#endif
	}
	FUK_PumpEvents();
	if (!s_active) return;
	if (s_poolDeadline >= 0 && s_now >= s_poolDeadline) {
		FUK_PoolFlush();
	}
	if (s_fddRiseAt >= 0 && s_fddFallAt >= 0) {
		// both pending: apply them in time order
		if (s_fddRiseAt <= s_fddFallAt) {
			if (s_now >= s_fddRiseAt) { s_fddRiseAt = -1; FUK_FddInterrupt(1); }
			if (s_now >= s_fddFallAt) { s_fddFallAt = -1; FUK_FddInterrupt(0); }
		} else {
			if (s_now >= s_fddFallAt) { s_fddFallAt = -1; FUK_FddInterrupt(0); }
			if (s_now >= s_fddRiseAt) { s_fddRiseAt = -1; FUK_FddInterrupt(1); }
		}
	} else if (s_fddRiseAt >= 0 && s_now >= s_fddRiseAt) {
		s_fddRiseAt = -1;
		FUK_FddInterrupt(1);
	} else if (s_fddFallAt >= 0 && s_now >= s_fddFallAt) {
		s_fddFallAt = -1;
		FUK_FddInterrupt(0);
	}
	if (s_recvDeadlineMs >= 0 && s_io->clock_ms() >= s_recvDeadlineMs) {
		FUK_RecvTimeout();
	}
}

// -----------------------------------------------------------------------
//   register read
// -----------------------------------------------------------------------

static int FUK_IsReadTransCommand(BYTE cmd)
{
	return ((cmd & 0x1F) == 0x06)      // READ DATA
	    || ((cmd & 0x1F) == 0x0C)      // READ DELETED DATA
	    || ((cmd & 0xBF) == 0x02);     // READ DIAGNOSTIC
}

static int FUK_IsWriteTransCommand(BYTE cmd)
{
	return ((cmd & 0x3F) == 0x05)      // WRITE DATA
	    || ((cmd & 0x3F) == 0x09)      // WRITE DELETED DATA
	    || ((cmd & 0x1F) == 0x11)      // SCAN EQUAL
	    || ((cmd & 0x1F) == 0x19)      // SCAN LOW OR EQUAL
	    || ((cmd & 0x1F) == 0x1D);     // SCAN HIGH OR EQUAL
}

static int FUK_IsWriteIdTransCommand(BYTE cmd)
{
	return (cmd & 0xBF) == 0x0D;       // WRITE ID
}

static const char* s_how = "";

static BYTE FUK_ReadRegInner(BYTE cmd)
{
	int r;

	s_how = "unit";

	FUK_DrainPort1();
	FUK_PumpEvents();
	if (!s_active) return 0xFF;

	if (cmd == 0x01) {
		// status register: predict it when the phase is known
		int s = -1;
		fuk_time w = 2000;
		const fuk_time SW = 200;
		switch (s_mode) {
		case Mode_SenseDeviceStatus_WaitStatus90:
			s = 0x90;
			s_mode = Mode_SenseDeviceStatus_WaitStatusD0;
			break;
		case Mode_SenseDeviceStatus_WaitStatusD0:
			s = 0xD0;
			break;
		case Mode_SenseDeviceStatus_CheckStatus:
		case Mode_SenseDeviceStatus_ReturnStatus:
			s = 0xD0;
			break;
		case Mode_SenseDeviceStatus_WaitStatus80:
			s = 0x80;
			s_mode = Mode_None;
			break;
		case Mode_SenseInterruptStatus_StatusD0:
			if (s_modeCounter == 0) {
				s = 0xD0;
			} else {
				w = SW;
				s_mode = Mode_SenseInterruptStatus_NoWait;
			}
			break;
		case Mode_SenseInterruptStatus_NoWait:
			w = SW;
			break;
		case Mode_Seek_StatusD0:
			if (++s_modeCounter < 3) {
				s = 0x90;
			} else {
				s_mode = Mode_Seek_NoWait;
				w = SW;
			}
			break;
		case Mode_Seek_NoWait:
			w = SW;
			break;
		case Mode_Recalibrate_Status90:
			if (++s_modeCounter < 2) {
				s = 0x90;
			} else {
				s_mode = Mode_Recalibrate_NoWait;
				w = SW;
			}
			break;
		case Mode_Recalibrate_NoWait:
			w = SW;
			break;
		case Mode_ReadData_Command:
			s = 0x90;
			break;
		case Mode_ReadData_Receive:
			s = 0x10;
			break;
		case Mode_ReadData_SoftReceive:
			s = 0xF0;
			break;
		case Mode_ReadData_Response:
			s = 0xD0;
			break;
		case Mode_ReadData_After:
			s = (s_modeCounter == 1) ? 0xD0 : 0x80;
			break;
		case Mode_WriteData_Command:
			s = 0x90;
			break;
		case Mode_WriteData_Send:
			s = 0x10;
			break;
		case Mode_WriteData_Response:
			s = 0xD0;
			break;
		case Mode_ReadId_Command:
			if (++s_modeCounter < 2) {
				s = 0x90;
			} else {
				s_mode = Mode_ReadId_Wait;
				w = SW;
			}
			break;
		case Mode_ReadId_Wait:
			w = SW;
			break;
		case Mode_ReadId_Result:
			if (s_modeCounter >= 0) {
				if (--s_modeCounter > 0) {
					s = 0xD0;
				} else {
					s_mode = Mode_None;
				}
			} else {
				s = 0xD0;
			}
			break;
		default:
			break;
		}
		if (s >= 0) {
			s_how = "predicted";
			s_lastCmd = cmd;
			s_lastStatus = (BYTE)s;
			return (BYTE)s;
		}
		if (s_lastCmd == cmd && w != 0 && (s_now - s_tickCounter) / FUK_TTT < w) {
			// polled again soon: give the same answer
			s_how = "repeat";
			return s_lastStatus;
		}
		s_lastCmd = cmd;
		s_tickCounter = s_now;
	} else {
		s_lastCmd = cmd;
	}

	if (cmd == 0x03) {
		s_how = "local";
		if (s_mode == Mode_SenseDeviceStatus_ReturnStatus) {
			s_mode = Mode_None;
			return (BYTE)s_senseDeviceStatus[s_modeCounter & 7];
		} else if (s_mode == Mode_ReadData_SoftReceive) {
			BYTE v = s_recvData[s_readDataCounter++ & (FUK_DATA_SIZE - 1)];
			if (s_readDataCounter == s_recvDataSize - 7) {
				s_mode = Mode_ReadData_Response;
				s_readDataCounter = 0;
			}
			return v;
		} else if (s_mode == Mode_ReadData_Response) {
			BYTE v = s_recvData[(s_recvDataSize - 7 + s_readDataCounter) & (FUK_DATA_SIZE - 1)];
			s_readDataCounter++;
			if (s_readDataCounter >= 7) {
				s_mode = Mode_ReadData_After;
				FUK_FdcInterrupt(0);
			}
			return v;
		} else if (s_mode == Mode_ReadData_After) {
			s_modeCounter++;
			return 0x80;
		}
	} else if (cmd == 0x05) {
		s_how = "local";
		if (s_driveOptionDrive == 0) {
			s_driveOptionDrive = s_driveOptionActualDrive;
			FUK_InitDriveOption();
		}
		if (s_driveOptionDrive == 0) {
			return 0;               // no drive selected
		}
		if (s_driveOptionResult[s_driveOptionDrive] >= 0) {
			return (BYTE)s_driveOptionResult[s_driveOptionDrive];
		}
		if (s_driveOptionActualDrive != s_driveOptionDrive) {
			s_driveOptionActualDrive = s_driveOptionDrive;
			s_driveOptionIsExec = 1;
			FUK_PoolAdd2(0x05 | 0x80, (s_driveOptionCtrl[s_driveOptionDrive] | s_driveOptionDrive) & 0xFF);
		}
	}

	// ask the unit
	s_how = "unit";
	FUK_PoolAdd1(cmd);
	FUK_PoolFlush();
	r = FUK_WaitAnswer();
	if (r < 0) return 0xFF;

	if (cmd == 0x01) {
		if (s_mode == Mode_ReadId_Wait && r == 0xD0) {
			s_mode = Mode_ReadId_Result;
			s_modeCounter = -1;
		}
		if (s_mode == Mode_Seek_NoWait) {
			if (r == 0x90) {
				if (s_equalStatusCounter++ > 5) {
					s_equalStatusCounter = 0;
					r = 0x80;
				}
			} else {
				s_equalStatusCounter = 0;
			}
		}
		s_lastStatus = (BYTE)r;
	} else if (cmd == 0x03) {
		if (s_mode == Mode_ReadId_Result && s_modeCounter < 0) {
			s_modeCounter = 7;
		} else if (s_mode == Mode_SenseDeviceStatus_CheckStatus) {
			s_senseDeviceStatus[s_modeCounter & 7] = r;
			s_mode = Mode_None;
		} else if (s_mode == Mode_SenseInterruptStatus_StatusD0) {
			if (s_modeCounter++ == 0) {
				if ((r & 0x18) == 0x18) r &= 0xEF;
			}
		} else if (s_mode == Mode_WriteData_Response) {
			if (++s_writeDataCounter >= 7) {
				s_mode = Mode_None;
				FUK_FdcInterrupt(0);
			}
		}
	} else if (cmd == 0x05) {
		s_driveOptionResult[s_driveOptionActualDrive & 15] = r;
	}
	return (BYTE)r;
}

// -----------------------------------------------------------------------
//   register write
// -----------------------------------------------------------------------

static void FUK_WriteRegInner(BYTE cmd, BYTE prm)
{
	FUK_DrainPort1();
	FUK_PumpEvents();
	if (!s_active) return;
	s_lastCmd = 0;

	if (cmd == 0x03) {
		if (--s_cmdCount <= 0) {
			// first byte of a command
			s_cmdCount = s_commandLength[prm & 0x1F];
			if (s_activeCommandList[0] == 0x08 && s_mode == Mode_ReadData_After && prm == 0x08) {
				s_mode = Mode_SenseInterruptStatus_StatusD0;
			} else if (s_mode == Mode_ReadData_After && prm == 0x08) {
				// SENSE INTERRUPT STATUS right after a read is answered here
				s_activeCommandCount = 0;
				s_activeCommandList[s_activeCommandCount++] = prm;
				s_modeCounter++;
				return;
			}
			s_mode = Mode_None;
			s_modeCounter = 0;
			if (prm == 0x04) {                       // SENSE DEVICE STATUS
				s_mode = Mode_SenseDeviceStatus_WaitStatus90;
			} else if (prm == 0x08) {                // SENSE INTERRUPT STATUS
				s_mode = Mode_SenseInterruptStatus_StatusD0;
			} else if (prm == 0x0F) {                // SEEK
				s_mode = Mode_Seek_StatusD0;
			} else if (prm == 0x07) {                // RECALIBRATE
				s_mode = Mode_Recalibrate_Status90;
				if (s_isForceReadyStatus && !s_isNotifyForceReady) {
					s_isNotifyForceReady = 1;
					FUK_SendForceReady(1);
				}
			} else if (FUK_IsReadTransCommand(prm)) {
				FUK_Trace("read command $%02X: DMAC0 CSR=%02X CCR=%02X CER=%02X MTC=%04X MAR=%06X",
				          prm, DMA[0].CSR, DMA[0].CCR, DMA[0].CER, DMA[0].MTC, (unsigned)(DMA[0].MAR & 0xFFFFFF));
				s_mode = Mode_ReadData_Command;
				s_readDataCounter = 9;
				s_mtc = DMA[0].MTC;
				DMA[0].MTC = 0;     // the data arrives in one piece on port 2
			} else if (FUK_IsWriteTransCommand(prm)) {
				FUK_Trace("write command $%02X: DMAC0 CSR=%02X CCR=%02X CER=%02X MTC=%04X MAR=%06X",
				          prm, DMA[0].CSR, DMA[0].CCR, DMA[0].CER, DMA[0].MTC, (unsigned)(DMA[0].MAR & 0xFFFFFF));
				FUK_SendMTC();
				s_mode = Mode_WriteData_Command;
				s_writeDataCounter = 9;
			} else if (FUK_IsWriteIdTransCommand(prm)) {
				FUK_SendMTC();
				s_mode = Mode_WriteData_Command;
				s_writeDataCounter = 6;
			} else if ((prm & 0xBF) == 0x0A) {       // READ ID
				s_mode = Mode_ReadId_Command;
			}
			s_activeCommandCount = 0;
		}
		if (s_activeCommandCount < (int)sizeof(s_activeCommandList)) {
			s_activeCommandList[s_activeCommandCount++] = prm;
		}

		if (s_mode == Mode_SenseDeviceStatus_WaitStatus90) {
			return;                 // sent together with the drive byte
		} else if (s_mode == Mode_SenseDeviceStatus_WaitStatusD0) {
			if (s_activeCommandCount == 2) {
				if ((s_now - s_tickSenseDeviceStatus) / FUK_TTT > 500) {
					int i;
					s_tickSenseDeviceStatus = s_now;
					for (i = 0; i < 8; i++) s_senseDeviceStatus[i] = -1;
				}
				if (s_senseDeviceStatus[prm & 7] < 0) {
					FUK_PoolAdd2(0x03 | 0x80, 0x04);
					FUK_PoolAdd2a(0x03 | 0x80, prm);
					s_modeCounter = prm & 7;
					s_mode = Mode_SenseDeviceStatus_CheckStatus;
				} else {
					s_modeCounter = prm & 7;
					s_mode = Mode_SenseDeviceStatus_ReturnStatus;
				}
			}
			return;
		}
	} else if (cmd == 0x05) {
		if ((s_now - s_driveOptionTick) / FUK_TTT > 500) {
			FUK_InitDriveOption();
		}
		if ((prm & 0x0F) != 0) {
			s_driveOptionDrive = prm & 0x0F;
			if ((prm & 0x20) != 0) {
				s_tickSenseDeviceStatus = s_now;
				FUK_InitDriveOption();
			}
			if (s_driveOptionCtrl[s_driveOptionDrive] != (prm & 0xF0)) {
				s_driveOptionCtrl[s_driveOptionDrive] = prm & 0xF0;
				s_driveOptionIsExec = 1;
				s_driveOptionActualDrive = s_driveOptionDrive;
			} else {
				return;             // same request as before
			}
		} else {
			int wasExec = s_driveOptionIsExec;
			s_driveOptionDrive = 0;
			s_driveOptionActualDrive = 0;
			s_driveOptionIsExec = 0;
			if (!wasExec) return;
		}
	}

	if (cmd == 0x03) {
		if (s_mode == Mode_ReadData_Command) {
			if (--s_readDataCounter == 0) {
				s_mode = Mode_ReadData_Receive;
				s_readDataCounter = 0;
				FUK_PoolAdd2a(0x80 | cmd, prm);
				return;
			}
		} else if (s_mode == Mode_WriteData_Command) {
			if (--s_writeDataCounter == 0) {
				s_mode = Mode_WriteData_Send;
			}
		}
	}
	FUK_PoolAdd2(0x80 | cmd, prm);
}

static BYTE FUK_ReadReg(BYTE cmd)
{
	FUKMode before = s_mode;
	BYTE v = FUK_ReadRegInner(cmd);
	FUK_Trace("R %02X -> %02X %s (%s -> %s)", cmd, v, s_how, kModeName[before], kModeName[s_mode]);
	return v;
}

static void FUK_WriteReg(BYTE cmd, BYTE prm)
{
	FUKMode before = s_mode;
	FUK_WriteRegInner(cmd, prm);
	FUK_Trace("W %02X <- %02X (%s -> %s)", cmd, prm, kModeName[before], kModeName[s_mode]);
}

// -----------------------------------------------------------------------
//   emulator hooks
// -----------------------------------------------------------------------

BYTE FUK_Read(BYTE reg)
{
	return FUK_ReadReg(reg & 0x0F);
}

void FUK_Write(BYTE reg, BYTE data)
{
	FUK_WriteReg(reg & 0x0F, data);
}

BYTE FUK_IocRead(BYTE localStatus)
{
	BYTE unit = FUK_ReadReg(0x11);
	if (!s_active) return localStatus;
	// The FDC and FDD bits come from the unit; the rest stays local.
	return (BYTE)((localStatus & 0x3F) | s_signal | (unit & 0xF0));
}

void FUK_IocWrite(BYTE reg, BYTE data)
{
	FUK_WriteReg((BYTE)(0x10 | (reg & 0x0F)), data);
}

void FUK_SetForceReady(int ready)
{
	// Sound drivers set CT2 freely, so following every write would keep the
	// unit busy while music plays. Only remember it here; the unit hears
	// "ready on" when a RECALIBRATE is issued, and "ready off" only if it
	// was told "on" (TNB製作所). XEiJ+t looks at bit 6 of every OPM data
	// write; px68k calls this for register $1B, the one that carries CT2.
	if (!s_active) return;
	// The remembered request always follows CT2. "On" is sent at most once
	// per change (on RECALIBRATE); "off" only when "on" was sent.
	s_isForceReadyStatus = ready ? 1 : 0;
	if (!ready && s_isNotifyForceReady) {
		s_isNotifyForceReady = 0;
		FUK_SendForceReady(0);
	}
}

BYTE FUK_FilterDmaCsr(BYTE csr)
{
	if (s_mode == Mode_ReadData_Command || s_mode == Mode_ReadData_Receive) {
		return csr & ~0x80;         // COC stays clear until the data is in
	}
	return csr | 0x80;
}
