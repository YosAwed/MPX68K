/*	$Id: mem_wrap.c,v 1.2 2003/12/05 18:07:19 nonaka Exp $	*/

#include "common.h"
#include "memory.h"
#include "../m68000/m68000.h"
#include "winx68k.h"

#include "adpcm.h"
#include "bg.h"
#include "crtc.h"
#include "dmac.h"
#include "fdc.h"
#include "gvram.h"
//#include "mercury.h"
#include "mfp.h"
#include "midi.h"
#include "ioc.h"
#include "palette.h"
#include "pia.h"
#include "rtc.h"
#include "sasi.h"
#include "scc.h"
#include "scsi.h"
#include "sram.h"
#include "sysport.h"
#include "tvram.h"

#include "fmg_wrap.h"
#if defined(HAVE_C68K)
#include "../m68000/c68k/c68k.h"
extern c68k_struc C68K;
#endif
#include <stdarg.h>

void AdrError(DWORD, DWORD);
void BusError(DWORD, DWORD);

static void wm_main(DWORD addr, BYTE val);
static void wm_cnt(DWORD addr, BYTE val);
static void wm_buserr(DWORD addr, BYTE val);
static void wm_opm(DWORD addr, BYTE val);
static void wm_e82(DWORD addr, BYTE val);
static void wm_nop(DWORD addr, BYTE val);
static void wm_scsi_dummy(DWORD addr, BYTE val);
static void wm_midi(DWORD addr, BYTE val);

static BYTE rm_main(DWORD addr);
static BYTE rm_font(DWORD addr);
static BYTE rm_ipl(DWORD addr);
static BYTE rm_nop(DWORD addr);
static BYTE rm_opm(DWORD addr);
static BYTE rm_e82(DWORD addr);
static BYTE rm_buserr(DWORD addr);
static BYTE rm_scsi_dummy(DWORD addr);
static BYTE rm_midi(DWORD addr);

void cpu_setOPbase24(DWORD addr);
void Memory_ErrTrace(void);
static int s_adr_error_detail_count = 0;
static unsigned int s_adr_error_count = 0;
static unsigned int s_queue_watch_count = 0;
static unsigned int s_rom_write_ignored_count = 0;
static unsigned int s_sysptr_watch_count = 0;
static unsigned int s_stackslot_watch_count = 0;
static unsigned int s_excvec_watch_count = 0;
static int s_excvec_guard_active = 0;  /* set by Memory_EnableExcVecGuard after kernel init */

BYTE (*MemReadTable[])(DWORD) = {
	TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read,
	TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read,
	TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read,
	TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read,
	TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read,
	TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read,
	TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read,
	TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read, TVRAM_Read,
	CRTC_Read, rm_e82, DMA_Read, rm_nop, MFP_Read, RTC_Read, rm_nop, SysPort_Read,
	rm_opm, ADPCM_Read, FDC_Read, SASI_Read, SCC_Read, PIA_Read, IOC_Read, SCSI_Read,
	SCSI_Read, rm_scsi_dummy, rm_scsi_dummy, rm_scsi_dummy, rm_scsi_dummy, rm_scsi_dummy, rm_scsi_dummy, rm_midi,
	BG_Read, BG_Read, BG_Read, BG_Read, BG_Read, BG_Read, BG_Read, BG_Read,
	rm_buserr, rm_buserr, rm_buserr, rm_buserr, rm_buserr, rm_buserr, rm_buserr, rm_buserr,
	SRAM_Read, SRAM_Read, SRAM_Read, SRAM_Read, SRAM_Read, SRAM_Read, SRAM_Read, SRAM_Read,
	rm_buserr, rm_buserr, rm_buserr, rm_buserr, rm_buserr, rm_buserr, rm_buserr, rm_buserr,
	rm_buserr, rm_buserr, rm_buserr, rm_buserr, rm_buserr, rm_buserr, rm_buserr, rm_buserr,
	rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font,
	rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font,
	rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font,
	rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font,
	rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font,
	rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font,
	rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font,
	rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font,
	rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font,
	rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font,
	rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font,
	rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font, rm_font,
/* SCSI の場合は rm_buserr になる？ */
	rm_ipl, rm_ipl, rm_ipl, rm_ipl, rm_ipl, rm_ipl, rm_ipl, rm_ipl,
	rm_ipl, rm_ipl, rm_ipl, rm_ipl, rm_ipl, rm_ipl, rm_ipl, rm_ipl,
	rm_ipl, rm_ipl, rm_ipl, rm_ipl, rm_ipl, rm_ipl, rm_ipl, rm_ipl,
	rm_ipl, rm_ipl, rm_ipl, rm_ipl, rm_ipl, rm_ipl, rm_ipl, rm_ipl,
};

void (*MemWriteTable[])(DWORD, BYTE) = {
	TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write,
	TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write,
	TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write,
	TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write,
	TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write,
	TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write,
	TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write,
	TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write, TVRAM_Write,
	CRTC_Write, wm_e82, DMA_Write, wm_nop, MFP_Write, RTC_Write, wm_nop, SysPort_Write,
	wm_opm, ADPCM_Write, FDC_Write, SASI_Write, SCC_Write, PIA_Write, IOC_Write, SCSI_Write,
	SCSI_Write, wm_scsi_dummy, wm_scsi_dummy, wm_scsi_dummy, wm_scsi_dummy, wm_scsi_dummy, wm_scsi_dummy, wm_midi,
	BG_Write, BG_Write, BG_Write, BG_Write, BG_Write, BG_Write, BG_Write, BG_Write,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	SRAM_Write, SRAM_Write, SRAM_Write, SRAM_Write, SRAM_Write, SRAM_Write, SRAM_Write, SRAM_Write,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
/* ROMエリアへの書きこみは全てバスエラー */
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
	wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr, wm_buserr,
};

BYTE *IPL;
BYTE *MEM;
BYTE *OP_ROM;
BYTE *FONT;

DWORD BusErrFlag = 0;
DWORD BusErrHandling = 0;
DWORD BusErrAdr;
DWORD MemByteAccess = 0;
static int SCSIModeEnabled = 0;

// Disabled by default — this debug logging was added for SCSI wild-jump
// investigation but does fopen/fprintf/fclose to a sandbox file on every
// call. Even with the per-event 256-line caps, the burst at boot stalls the
// AppKit main thread (sandbox permission churn) and breaks NSOpenPanel.
// Re-enable by defining MPX68K_ENABLE_RUNTIME_FILE_LOGS=1 at compile time.
#ifndef MPX68K_ENABLE_RUNTIME_FILE_LOGS
#define MPX68K_ENABLE_RUNTIME_FILE_LOGS 0
#endif

static void
Memory_LogRuntimeEvent(const char* fmt, ...)
{
#if !MPX68K_ENABLE_RUNTIME_FILE_LOGS
	(void)fmt;
	return;
#else
#ifdef __APPLE__
	const char* home = getenv("HOME");
	char path[512];
	FILE* fp;
	va_list ap;

	if (home && home[0] != '\0') {
		snprintf(path, sizeof(path), "%s/Documents/X68000/_scsi_iocs.txt", home);
	} else {
		snprintf(path, sizeof(path), "X68000/_scsi_iocs.txt");
	}
	fp = fopen(path, "a");
	if (!fp) {
		return;
	}
	va_start(ap, fmt);
	vfprintf(fp, fmt, ap);
	va_end(ap);
	fputc('\n', fp);
	fclose(fp);
#else
	(void)fmt;
#endif
#endif /* MPX68K_ENABLE_RUNTIME_FILE_LOGS */
}

static int
Memory_PeekLogBytes(DWORD base24, BYTE* out, int count)
{
	int i;

	if (out == NULL || count <= 0) {
		return 0;
	}
	base24 &= 0x00ffffff;
	for (i = 0; i < count; i++) {
		DWORD addr = (base24 + (DWORD)i) & 0x00ffffff;
		if (addr < 0x00c00000) {
			if (MEM == NULL) return 0;
			out[i] = MEM[addr ^ 1];
		} else if (addr >= 0x00f00000) {
			if (IPL == NULL) return 0;
			out[i] = IPL[(addr & 0x3ffff) ^ 1];
		} else {
			return 0;
		}
	}
	return 1;
}

static DWORD
Memory_LogReadLong24(DWORD addr)
{
	DWORD a = addr & 0x00ffffffU;
	if (MEM == NULL || a + 3U >= 0x00c00000U) {
		return 0;
	}
	return ((DWORD)MEM[(a + 0U) ^ 1] << 24) |
	       ((DWORD)MEM[(a + 1U) ^ 1] << 16) |
	       ((DWORD)MEM[(a + 2U) ^ 1] << 8) |
	       ((DWORD)MEM[(a + 3U) ^ 1]);
}

static WORD
Memory_LogReadWord24(DWORD addr)
{
	DWORD a = addr & 0x00ffffffU;
	if (MEM == NULL || a + 1U >= 0x00c00000U) {
		return 0;
	}
	return (WORD)(((WORD)MEM[(a + 0U) ^ 1] << 8) |
	              ((WORD)MEM[(a + 1U) ^ 1]));
}

static void
Memory_LogQueueWriteWatch(DWORD addr, BYTE oldVal, BYTE newVal)
{
#if defined(HAVE_C68K)
	DWORD bca;
	DWORD bcc;
	DWORD pc;
	DWORD a0;
	DWORD a1;
	DWORD a7;

	if (s_queue_watch_count >= 256) {
		return;
	}
	if (addr < 0x00000bc6U || addr > 0x00000bcfU) {
		return;
	}
	pc = C68k_Get_PC(&C68K) & 0x00ffffffU;
	a0 = C68k_Get_AReg(&C68K, 0) & 0x00ffffffU;
	a1 = C68k_Get_AReg(&C68K, 1) & 0x00ffffffU;
	a7 = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
	bca = (DWORD)Memory_LogReadWord24(0x00000bca);
	bcc = Memory_LogReadLong24(0x00000bcc) & 0x00ffffffU;
	Memory_LogRuntimeEvent(
	    "QUE_WRITE adr=%06X old=%02X new=%02X pc=%08X bca=$%04X bcc=%08X a0=%08X a1=%08X a7=%08X",
	    (unsigned int)(addr & 0x00ffffffU),
	    (unsigned int)oldVal,
	    (unsigned int)newVal,
	    (unsigned int)pc,
	    (unsigned int)bca,
	    (unsigned int)bcc,
	    (unsigned int)a0,
	    (unsigned int)a1,
	    (unsigned int)a7);
	s_queue_watch_count++;
#else
	(void)addr;
	(void)oldVal;
	(void)newVal;
#endif
}

static void
Memory_LogSysPtrWriteWatch(DWORD addr, BYTE oldVal, BYTE newVal)
{
#if defined(HAVE_C68K)
	DWORD pc;
	DWORD a7;
	DWORD d0;
	DWORD d1;
	DWORD ptr1c18;
	DWORD ptr1c6a;
	DWORD ptr1c98;
	DWORD ptr1c9c;

	if (s_sysptr_watch_count >= 512) {
		return;
	}
	addr &= 0x00ffffffU;
	if (!((addr >= 0x00001c18U && addr <= 0x00001c1bU) ||
	      (addr >= 0x00001c6aU && addr <= 0x00001c6dU) ||
	      (addr >= 0x00001c98U && addr <= 0x00001c9fU))) {
		return;
	}

	pc = C68k_Get_PC(&C68K) & 0x00ffffffU;
	a7 = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
	d0 = C68k_Get_DReg(&C68K, 0);
	d1 = C68k_Get_DReg(&C68K, 1);
	ptr1c18 = Memory_LogReadLong24(0x00001c18U);
	ptr1c6a = Memory_LogReadLong24(0x00001c6aU);
	ptr1c98 = Memory_LogReadLong24(0x00001c98U);
	ptr1c9c = Memory_LogReadLong24(0x00001c9cU);

	Memory_LogRuntimeEvent(
	    "SYS_PTR_WRITE adr=%06X old=%02X new=%02X pc=%08X a7=%08X d0=%08X d1=%08X p1c18=%08X p1c6a=%08X p1c98=%08X p1c9c=%08X",
	    (unsigned int)addr,
	    (unsigned int)oldVal,
	    (unsigned int)newVal,
	    (unsigned int)pc,
	    (unsigned int)a7,
	    (unsigned int)d0,
	    (unsigned int)d1,
	    (unsigned int)ptr1c18,
	    (unsigned int)ptr1c6a,
	    (unsigned int)ptr1c98,
	    (unsigned int)ptr1c9c);
	s_sysptr_watch_count++;
#else
	(void)addr;
	(void)oldVal;
	(void)newVal;
#endif
}

static void
Memory_LogStackSlotWriteWatch(DWORD addr, BYTE oldVal, BYTE newVal)
{
#if defined(HAVE_C68K)
	DWORD pc;
	DWORD d0;
	DWORD d1;
	DWORD d2;
	DWORD a0;
	DWORD a1;
	DWORD a6;
	DWORD a7;
	DWORD m692a;
	DWORD m692e;

	if (s_stackslot_watch_count >= 1024) {
		return;
	}
	addr &= 0x00ffffffU;
	if (addr < 0x00006920U || addr > 0x0000693fU) {
		return;
	}

	pc = C68k_Get_PC(&C68K) & 0x00ffffffU;
	d0 = C68k_Get_DReg(&C68K, 0);
	d1 = C68k_Get_DReg(&C68K, 1);
	d2 = C68k_Get_DReg(&C68K, 2);
	a0 = C68k_Get_AReg(&C68K, 0) & 0x00ffffffU;
	a1 = C68k_Get_AReg(&C68K, 1) & 0x00ffffffU;
	a6 = C68k_Get_AReg(&C68K, 6) & 0x00ffffffU;
	a7 = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
	m692a = Memory_LogReadLong24(0x0000692aU);
	m692e = Memory_LogReadLong24(0x0000692eU);

	Memory_LogRuntimeEvent(
	    "STACK_SLOT_WRITE adr=%06X old=%02X new=%02X pc=%08X d0=%08X d1=%08X d2=%08X a0=%08X a1=%08X a6=%08X a7=%08X m692a=%08X m692e=%08X",
	    (unsigned int)addr,
	    (unsigned int)oldVal,
	    (unsigned int)newVal,
	    (unsigned int)pc,
	    (unsigned int)d0,
	    (unsigned int)d1,
	    (unsigned int)d2,
	    (unsigned int)a0,
	    (unsigned int)a1,
	    (unsigned int)a6,
	    (unsigned int)a7,
	    (unsigned int)m692a,
	    (unsigned int)m692e);
	s_stackslot_watch_count++;
#else
	(void)addr;
	(void)oldVal;
	(void)newVal;
#endif
}

/* Post-write guard for exception vector area AND IOCS vector table.
 *
 * During SCSI boot mode, loaded programs (e.g. COMMAND.X at $410B6) may
 * sweep through vectors writing zeros, destroying kernel-installed handlers.
 *
 * Protected ranges:
 *   $008-$0BF  Exception vectors 2-47 (bus error, F-line, TRAP, etc.)
 *   $400-$7FF  IOCS function table (256 entries, 4 bytes each)
 *
 * Strategy: ALLOW all writes, but after each write, check if the full
 * 4-byte entry became zero.  If so, immediately restore from saved snapshot. */
static DWORD s_excvec_saved[48];   /* exception vectors 0-47 */
static DWORD s_iocsvec_saved[256]; /* IOCS function table entries */
static BYTE  s_iocsvec_hardpin[256]; /* 1 = hard-pin (always restore on any change) */

static inline void
Memory_VecPostWriteCheck(DWORD addr)
{
#if defined(HAVE_C68K)
	if (!s_excvec_guard_active) return;
	addr &= 0x00ffffffU;

	DWORD vecAddr;
	DWORD restore;
	const char *tag;

	if (addr >= 0x00000008U && addr <= 0x000000bfU) {
		/* Exception vector area */
		unsigned int vecIdx = addr >> 2;
		if (vecIdx < 2 || vecIdx > 47) return;
		if (s_excvec_saved[vecIdx] == 0) return;
		vecAddr = vecIdx * 4;
		restore = s_excvec_saved[vecIdx];
		tag = "EXCVEC";
	} else if (addr >= 0x00000400U && addr <= 0x000007ffU) {
		/* IOCS function vector table */
		unsigned int fnIdx = (addr - 0x400U) >> 2;
		if (fnIdx > 255) return;
		if (s_iocsvec_saved[fnIdx] == 0) return;
		vecAddr = 0x400U + fnIdx * 4;
		restore = s_iocsvec_saved[fnIdx];
		tag = "IOCSVEC";
	} else {
		return;
	}

	/* Read the full 4-byte entry from MEM (XOR 1 byte ordering) */
	DWORD val = ((DWORD)MEM[vecAddr ^ 1] << 24) |
	            ((DWORD)MEM[(vecAddr + 1) ^ 1] << 16) |
	            ((DWORD)MEM[(vecAddr + 2) ^ 1] << 8) |
	            ((DWORD)MEM[(vecAddr + 3) ^ 1]);

	if (val == restore) return;  /* unchanged - nothing to do */

	/* For hard-pinned IOCS entries (fn=$0C, fn=$FF etc.): ANY change from
	 * saved value triggers restore.  Their native kernel handlers reference
	 * $FC0000+ subroutines overlaid by SCSIIMG - must never run.
	 * For other entries: only restore if value looks like garbage (< $1000). */
	if (tag[0] == 'I') {
		/* IOCS entry */
		unsigned int fnChk = (vecAddr - 0x400U) >> 2;
		if (fnChk < 256 && s_iocsvec_hardpin[fnChk]) {
			/* hard-pinned: restore on ANY change */
		} else {
			DWORD masked = val & 0x00ffffffU;
			if (masked >= 0x1000U) return;  /* valid handler - allow */
		}
	} else {
		/* Exception vector: only restore if garbage */
		DWORD masked = val & 0x00ffffffU;
		if (masked >= 0x1000U) return;
	}

	/* Entry was corrupted - restore from saved snapshot */
	MEM[vecAddr ^ 1]       = (BYTE)((restore >> 24) & 0xFF);
	MEM[(vecAddr + 1) ^ 1] = (BYTE)((restore >> 16) & 0xFF);
	MEM[(vecAddr + 2) ^ 1] = (BYTE)((restore >> 8) & 0xFF);
	MEM[(vecAddr + 3) ^ 1] = (BYTE)(restore & 0xFF);

	if (s_excvec_watch_count < 128) {
		DWORD pc = C68k_Get_PC(&C68K) & 0x00ffffffU;
		Memory_LogRuntimeEvent(
		    "%s_RESTORED idx=%02X was=%08X saved=%08X pc=%08X",
		    tag, (unsigned int)((vecAddr - (tag[0]=='I' ? 0x400U : 0)) >> 2),
		    (unsigned int)val, (unsigned int)restore, (unsigned int)pc);
		s_excvec_watch_count++;
	}
#else
	(void)addr;
#endif
}


/*
 * write function
 */
void 
dma_writemem24(DWORD addr, BYTE val)
{

	MemByteAccess = 0;

	wm_main(addr, val);
}

void 
dma_writemem24_word(DWORD addr, WORD val)
{

	MemByteAccess = 0;

	if (addr & 1) {
		BusErrFlag |= 4;
		return;
	}

	wm_main(addr, (val >> 8) & 0xff);
	wm_main(addr + 1, val & 0xff);
}

void 
dma_writemem24_dword(DWORD addr, DWORD val)
{

	MemByteAccess = 0;

	if (addr & 1) {
		BusErrFlag |= 4;
		return;
	}

	wm_main(addr, (val >> 24) & 0xff);
	wm_main(addr + 1, (val >> 16) & 0xff);
	wm_main(addr + 2, (val >> 8) & 0xff);
	wm_main(addr + 3, val & 0xff);
}

void 
cpu_writemem24(DWORD addr, BYTE val)
{

	MemByteAccess = 0;
	BusErrFlag = 0;

	wm_cnt(addr, val);
	if (BusErrFlag & 2) {
		Memory_ErrTrace();
		BusError(addr, val);
	}
}

void 
cpu_writemem24_word(DWORD addr, WORD val)
{

	MemByteAccess = 0;

	if (addr & 1) {
		AdrError(addr, val);
		return;
	}

	BusErrFlag = 0;

	wm_cnt(addr, (val >> 8) & 0xff);
	wm_main(addr + 1, val & 0xff);

	if (BusErrFlag & 2) {
		Memory_ErrTrace();
		BusError(addr, val);
	}
}

void 
cpu_writemem24_dword(DWORD addr, DWORD val)
{

	MemByteAccess = 0;

	if (addr & 1) {
		AdrError(addr, val);
		return;
	}

	BusErrFlag = 0;

	wm_cnt(addr, (val >> 24) & 0xff);
	wm_main(addr + 1, (val >> 16) & 0xff);
	wm_main(addr + 2, (val >> 8) & 0xff);
	wm_main(addr + 3, val & 0xff);

	if (BusErrFlag & 2) {
		Memory_ErrTrace();
		BusError(addr, val);
	}
}

static void 
wm_main(DWORD addr, BYTE val)
{

	if ((BusErrFlag & 7) == 0)
		wm_cnt(addr, val);
}

static void 
wm_cnt(DWORD addr, BYTE val)
{

	addr &= 0x00ffffff;
	if (addr < 0x00c00000) {	// Use RAM upto 12MB
		BYTE oldVal = MEM[addr ^ 1];
		MEM[addr ^ 1] = val;
		Memory_VecPostWriteCheck(addr);
		Memory_LogQueueWriteWatch(addr, oldVal, val);
		Memory_LogSysPtrWriteWatch(addr, oldVal, val);
		Memory_LogStackSlotWriteWatch(addr, oldVal, val);
	} else if (addr < 0x00e00000) {
		GVRAM_Write(addr, val);
	} else {
		MemWriteTable[(addr >> 13) & 0xff](addr, val);
	}
}

static void 
wm_buserr(DWORD addr, BYTE val)
{
	addr &= 0x00ffffffU;
	// ROM write cycles are ignored on real hardware; don't raise false bus errors.
	if (addr >= 0x00f00000U && addr <= 0x00ffffffU) {
#if defined(HAVE_C68K)
		if (s_rom_write_ignored_count < 16) {
			Memory_LogRuntimeEvent("ROM_WRITE_IGNORED adr=%08X pc=%08X val=%02X",
			                      (unsigned int)addr,
			                      (unsigned int)(C68k_Get_PC(&C68K) & 0x00ffffffU),
			                      (unsigned int)val);
		}
#endif
		s_rom_write_ignored_count++;
		return;
	}

	BusErrFlag = 2;
	BusErrAdr = addr;
	(void)val;
}

static void 
wm_opm(DWORD addr, BYTE val)
{
	BYTE t;
#ifdef RFMDRV
	char buf[2];
#endif

	t = addr & 3;
	if (t == 1) {
		OPM_Write(0, val);
	} else if (t == 3) {
		OPM_Write(1, val);
	}
#ifdef RFMDRV
	buf[0] = t;
	buf[1] = val;
	send(rfd_sock, buf, sizeof(buf), 0);
#endif
}

static void 
wm_e82(DWORD addr, BYTE val)
{

	if (addr < 0x00e82400) {
		Pal_Write(addr, val);
	} else if (addr < 0x00e82700) {
		VCtrl_Write(addr, val);
	}
}

static void 
wm_nop(DWORD addr, BYTE val)
{

	/* Nothing to do */
	(void)addr;
	(void)val;
}

/*
 * read function
 */
BYTE 
dma_readmem24(DWORD addr)
{

	return rm_main(addr);
}

WORD 
dma_readmem24_word(DWORD addr)
{
	WORD v;

	if (addr & 1) {
		BusErrFlag = 3;
		return 0;
	}

	v = rm_main(addr++) << 8;
	v |= rm_main(addr);
	return v;
}

DWORD 
dma_readmem24_dword(DWORD addr)
{
	DWORD v;

	if (addr & 1) {
		BusErrFlag = 3;
		return 0;
	}

	v = rm_main(addr++) << 24;
	v |= rm_main(addr++) << 16;
	v |= rm_main(addr++) << 8;
	v |= rm_main(addr);
	return v;
}

BYTE
cpu_readmem24(DWORD addr)
{
	BYTE v;

	BusErrFlag = 0;
	BusErrAdr = 0;

	v = rm_main(addr);
	if (BusErrFlag & 1) {
		// Suppress SCSI-related logs (normal when SCSI not connected)
		if (!((addr >= 0xe9a000 && addr < 0xe9c000) || (addr >= 0xea0000 && addr < 0xeb0000))) {
			p6logd("func = %s addr = %x flag = %d\n", __func__, addr, BusErrFlag);
		}
		Memory_ErrTrace();
		BusError(addr, 0);
	}
	return v;
}

WORD
cpu_readmem24_word(DWORD addr)
{
	WORD v;

	if (addr & 1) {
		AdrError(addr, 0);
		return 0;
	}

	BusErrFlag = 0;
	BusErrAdr = 0;

	v = rm_main(addr++) << 8;
	v |= rm_main(addr);
	if (BusErrFlag & 1) {
		// Suppress SCSI-related logs
		if (!((addr >= 0xe9a000 && addr < 0xe9c000) || (addr >= 0xea0000 && addr < 0xeb0000))) {
			p6logd("func = %s addr = %x flag = %d\n", __func__, addr, BusErrFlag);
		}
		Memory_ErrTrace();
		BusError(addr, 0);
	}
	return v;
}

DWORD
cpu_readmem24_dword(DWORD addr)
{
	DWORD v;

	MemByteAccess = 0;

	if (addr & 1) {
		BusErrFlag = 3;
		p6logd("func = %s addr = %x\n", __func__, addr);
		return 0;
	}

	BusErrFlag = 0;
	BusErrAdr = 0;

	v = rm_main(addr++) << 24;
	v |= rm_main(addr++) << 16;
	v |= rm_main(addr++) << 8;
	v |= rm_main(addr);
	return v;
}

static BYTE 
rm_main(DWORD addr)
{
	BYTE v;

	addr &= 0x00ffffff;
	if (addr < 0x00c00000) {	// Use RAM upto 12MB
		v = MEM[addr ^ 1];
	} else if (addr < 0x00e00000) {
		v = GVRAM_Read(addr);
	} else {
#if 0 // for debug @GOROman
        //eafa01
        //00eafa09
        DWORD masked = addr & 0xfffff0;
        switch (masked) {
                case 0xe94000:  // IOCTRL
                case 0xe96000:  // SASI
                case 0xe90000:  // OPM
                case 0xe98000:  // SCC

                break;
            default:
                printf( "MemReadTable:%08x\n", addr );
                break;
        }
#endif
		v = MemReadTable[(addr >> 13) & 0xff](addr);
	}

	return v;
}

static BYTE 
rm_font(DWORD addr)
{

	return FONT[addr & 0xfffff];
}

static BYTE 
rm_ipl(DWORD addr)
{

	return IPL[(addr & 0x3ffff) ^ 1];
}

static BYTE 
rm_nop(DWORD addr)
{

	(void)addr;
	return 0;
}

static BYTE 
rm_opm(DWORD addr)
{

	if ((addr & 3) == 3) {
		return OPM_Read(0);
	}
	return 0;
}

static BYTE 
rm_e82(DWORD addr)
{

	if (addr < 0x00e82400) {
		return Pal_Read(addr);
	} else if (addr < 0x00e83000) {
		return VCtrl_Read(addr);
	}
	return 0;
}

static BYTE
rm_buserr(DWORD addr)
{
	// Suppress frequent SCSI-related BusError logs
	if (!((addr >= 0xe9a000 && addr < 0xe9c000) || (addr >= 0xea0000 && addr < 0xeb0000))) {
		p6logd("func = %s addr = %x flag = %d\n", __func__, addr, BusErrFlag);
	}

	BusErrFlag = 1;
	BusErrAdr = addr;

	return 0;
}

// Dummy read for SCSI-related address range (0xea2000-0xeaffff)
// Returns 0xFF to indicate device not present, without triggering BusError
// This allows IPL to detect that SCSI is not available and proceed to FDD
static BYTE
rm_scsi_dummy(DWORD addr)
{
	(void)addr;
	return 0xff;
}

// MIDI board (CZ-6BM1) registers live at 0xEAF A01-0xEAF A0F.
// Keep other addresses in this block as dummy reads to preserve SCSI probing behavior.
static BYTE
rm_midi(DWORD addr)
{
	if (addr >= 0x00eafa01 && addr < 0x00eafa10) {
		return MIDI_Read(addr);
	}
	return 0xff;
}

static void
wm_midi(DWORD addr, BYTE val)
{
	if (addr >= 0x00eafa01 && addr < 0x00eafa10) {
		MIDI_Write(addr, val);
		return;
	}
	(void)addr;
	(void)val;
}

// Dummy write for SCSI-related address range (0xea2000-0xeaffff)
// Silently ignores writes without triggering BusError
static void
wm_scsi_dummy(DWORD addr, BYTE val)
{
	(void)addr;
	(void)val;
}

/*
 * Memory misc
 */
void Memory_Init(void)
{

//        cpu_setOPbase24((DWORD)C68k_Get_Reg(&C68K, C68K_PC));
#if defined (HAVE_CYCLONE)
	cpu_setOPbase24((DWORD)m68000_get_reg(M68K_PC));
#elif defined (HAVE_C68K)
    cpu_setOPbase24((DWORD)C68k_Get_PC(&C68K));
#endif /* HAVE_C68K */
}

void
cpu_setOPbase24(DWORD addr)
{
	(void)0;
	switch ((addr >> 20) & 0xf) {
	case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
	case 8: case 9: case 0xa: case 0xb:
		OP_ROM = MEM;
		break;

	case 0xc: case 0xd:
		OP_ROM = GVRAM - 0x00c00000;
		break;

	case 0xe:
		if (addr < 0x00e80000) 
			OP_ROM = TVRAM - 0x00e00000;
		else if ((addr >= 0x00ea0000) && (addr < 0x00ea2000))
			OP_ROM = SCSIIPL - 0x00ea0000;
		else if ((addr >= 0x00ed0000) && (addr < 0x00ed4000))
			OP_ROM = SRAM - 0x00ed0000;
		else {
			BusErrFlag = 3;
			BusErrAdr = addr;
			Memory_ErrTrace();
			BusError(addr, 0);
		}
		break;

	case 0xf:
		if (addr >= 0x00fc0000)
			OP_ROM = IPL - 0x00fc0000;
		else {
			BusErrFlag = 3;
			BusErrAdr = addr;
			Memory_ErrTrace();
			BusError(addr, 0);
		}
		break;
	}
}

void
Memory_SetSCSIMode(void)
{
	int i;
	SCSIModeEnabled = 1;
	// Route SASI I/O page (0xE96000) to SCSI emulation while SCSI mode is active.
	MemReadTable[0x4b] = SCSI_Read;
	MemWriteTable[0x4b] = SCSI_Write;
	// $E9E000-$E9FFFF (0x4F) is always SCSI_Read/Write (default table).
	// SASI-mode behavior handled by bus_mode check inside SCSI_Read/Write.
	// Keep $FC0000-$FDFFFF readable from IPL image in SCSI mode.
	// Forcing bus errors here causes repeated fc0025/fc0027 faults during
	// post-boot driver probes and can leave the machine in a black-screen loop.
	for (i = 0xe0; i < 0xf0; i++) {
		MemReadTable[i] = rm_ipl;
	}
}

void
Memory_ClearSCSIMode(void)
{
	int i;
	SCSIModeEnabled = 0;
	s_excvec_guard_active = 0;
	memset(s_iocsvec_hardpin, 0, sizeof(s_iocsvec_hardpin));
	MemReadTable[0x4b] = SASI_Read;
	MemWriteTable[0x4b] = SASI_Write;
	// $E9E000-$E9FFFF (0x4F) stays as SCSI_Read/Write (default table).
	// SASI-mode behavior handled by bus_mode check inside SCSI_Read/Write.
	// Restore $FC0000-$FDFFFF data reads to IPL ROM
	for (i = 0xe0; i < 0xf0; i++) {
		MemReadTable[i] = rm_ipl;
	}
}

void
Memory_RefreshSCSIRomOverlay(void)
{
	/* No-op: SCSI ROM is mapped at $EA0000 only, not overlaid at $FC0000 */
}

void
Memory_SetSASIBootMode(void)
{
	/* SASI+HDD boot: enable SCSI ROM access ($EA0xxx) and traps ($E9F8xx)
	 * but KEEP SASI register routing at $E96xxx for the IPL ROM's native
	 * SASI protocol.  Do NOT reroute $E96xxx to SPC register handling.
	 * Do NOT make $FC0000 bus error (IPL ROM data reads needed). */
	SCSIModeEnabled = 1;
	/* $E96xxx stays as SASI_Read/SASI_Write (default from ClearSCSIMode) */
	/* $EA0xxx reads go to SCSI_Read (always routed via MemReadTable[0x50]) */
	/* $E9F8xx writes go to SCSI_Write (always routed via MemWriteTable[0x4F]) */
}

int
Memory_IsSCSIModeEnabled(void)
{
	return SCSIModeEnabled;
}

void
Memory_EnableExcVecGuard(const DWORD *savedVectors, int count)
{
	int i;
	for (i = 0; i < 48 && i < count; i++) {
		s_excvec_saved[i] = savedVectors[i];
	}
	/* Also save the IOCS function vector table ($400-$7FF) */
	for (i = 0; i < 256; i++) {
		DWORD addr = 0x400U + (DWORD)i * 4;
		s_iocsvec_saved[i] =
		    ((DWORD)MEM[addr ^ 1] << 24) |
		    ((DWORD)MEM[(addr + 1) ^ 1] << 16) |
		    ((DWORD)MEM[(addr + 2) ^ 1] << 8) |
		    ((DWORD)MEM[(addr + 3) ^ 1]);
	}
	memset(s_iocsvec_hardpin, 0, sizeof(s_iocsvec_hardpin));
	s_excvec_guard_active = 1;
	/* Log key saved IOCS values for diagnostics */
	Memory_LogRuntimeEvent(
	    "IOCSVEC_GUARD fn0=$%08X fn1=$%08X fnC=$%08X fnF=$%08X fn20=$%08X fn2E=$%08X",
	    (unsigned)s_iocsvec_saved[0],  (unsigned)s_iocsvec_saved[1],
	    (unsigned)s_iocsvec_saved[0x0C], (unsigned)s_iocsvec_saved[0x0F],
	    (unsigned)s_iocsvec_saved[0x20], (unsigned)s_iocsvec_saved[0x2E]);
}

void
Memory_IOCSHardPin(int fnIdx)
{
	if (fnIdx >= 0 && fnIdx < 256) {
		s_iocsvec_hardpin[fnIdx] = 1;
	}
}

void Memory_ErrTrace(void)
{
#ifdef WIN68DEBUG
	FILE *fp;
	fp=fopen("_buserr.txt", "a");
	if (BusErrFlag==3)
		fprintf(fp, "BusErr - SetOP to $%08X  @ $%08X\n", BusErrAdr, regs.pc);
	else if (BusErrFlag==2)
		fprintf(fp, "BusErr - Write to $%08X  @ $%08X\n", BusErrAdr, regs.pc);
	else
		fprintf(fp, "BusErr - Read from $%08X  @ $%08X\n", BusErrAdr, regs.pc);
	fclose(fp);
//	traceflag ++;
//	m68000_ICount = 0;
#endif
}

void 
Memory_IntErr(int i)
{
#ifdef WIN68DEBUG
	FILE *fp;
	fp=fopen("_interr.txt", "a");
	fprintf(fp, "IntErr - Int.No%d  @ $%08X\n", i, regs.pc);
	fclose(fp);
#else
	(void)i;
#endif
}

void
AdrError(DWORD adr, DWORD unknown)
{
    DWORD pc = 0;
	DWORD adr24 = adr & 0x00ffffff;

	(void)adr;
	(void)unknown;
#if defined(HAVE_C68K)
	pc = C68k_Get_PC(&C68K);
#endif
	if (s_adr_error_count < 256 || (s_adr_error_count & 0xffU) == 0) {
		Memory_LogRuntimeEvent("ADR_ERROR adr=%08X pc=%08X", (unsigned int)adr, (unsigned int)pc);
		if (s_adr_error_count == 256) {
			Memory_LogRuntimeEvent("ADR_ERROR (repeats throttled; logging every 256th)");
		}
	}
#if defined(HAVE_C68K)
	if (s_adr_error_detail_count < 8) {
		BYTE logDump[16];
		DWORD a0 = C68k_Get_AReg(&C68K, 0);
		DWORD a1 = C68k_Get_AReg(&C68K, 1);
		DWORD a2 = C68k_Get_AReg(&C68K, 2);
		DWORD a3 = C68k_Get_AReg(&C68K, 3);
		DWORD a4 = C68k_Get_AReg(&C68K, 4);
		DWORD a5 = C68k_Get_AReg(&C68K, 5);
		DWORD a6 = C68k_Get_AReg(&C68K, 6);
		DWORD a7 = C68k_Get_AReg(&C68K, 7);
		DWORD d0 = C68k_Get_DReg(&C68K, 0);
		DWORD d1 = C68k_Get_DReg(&C68K, 1);
		DWORD d2 = C68k_Get_DReg(&C68K, 2);
		DWORD d3 = C68k_Get_DReg(&C68K, 3);
		DWORD d4 = C68k_Get_DReg(&C68K, 4);
		DWORD d5 = C68k_Get_DReg(&C68K, 5);
		DWORD d6 = C68k_Get_DReg(&C68K, 6);
		DWORD d7 = C68k_Get_DReg(&C68K, 7);
		DWORD a0m = a0 & 0x00ffffffU;
		DWORD a0even = a0m & ~1U;
		DWORD vec7d4 = Memory_LogReadLong24(0x000007d4) & 0x00ffffffU;
		DWORD vec47 = Memory_LogReadLong24(47 * 4) & 0x00ffffffU;
		DWORD fn00 = Memory_LogReadLong24(0x00000400 + (0x00U * 4U)) & 0x00ffffffU;
		DWORD fn01 = Memory_LogReadLong24(0x00000400 + (0x01U * 4U)) & 0x00ffffffU;
		DWORD fn3d = Memory_LogReadLong24(0x00000400 + (0x3dU * 4U)) & 0x00ffffffU;
		DWORD fn8d = Memory_LogReadLong24(0x00000400 + (0x8dU * 4U)) & 0x00ffffffU;
		DWORD fnff = Memory_LogReadLong24(0x00000400 + (0xffU * 4U)) & 0x00ffffffU;
		DWORD qptr = Memory_LogReadLong24(0x00000bcc) & 0x00ffffffU;
		WORD qcount = Memory_LogReadWord24(0x00000bca);
		BYTE qmode = (BYTE)(Memory_LogReadWord24(0x00000bc6) & 0xffU);
		BYTE qkana = (BYTE)(Memory_LogReadWord24(0x00000bc7) & 0xffU);
		BYTE qerr = (BYTE)(Memory_LogReadWord24(0x00000c1a) & 0xffU);
		WORD trapFnWord = Memory_LogReadWord24(0x00000a0e);
		WORD trapStateWord = Memory_LogReadWord24(0x00000a10);
		WORD trapErrWord = Memory_LogReadWord24(0x00000a12);
		BYTE trapFn8 = (BYTE)(trapFnWord & 0xffU);
		DWORD fnTrap = Memory_LogReadLong24(0x00000400 + ((DWORD)trapFn8 * 4U)) & 0x00ffffffU;
		int fn = 0;
		int iocsHitCount = 0;
		Memory_LogRuntimeEvent("ADR_ERROR_IOCS v47=%08X v7d4=%08X fn00=%08X fn01=%08X fn3d=%08X fn8d=%08X fnff=%08X fn%02X=%08X",
			                      (unsigned int)vec47,
			                      (unsigned int)vec7d4,
			                      (unsigned int)fn00,
			                      (unsigned int)fn01,
			                      (unsigned int)fn3d,
			                      (unsigned int)fn8d,
			                      (unsigned int)fnff,
			                      (unsigned int)trapFn8,
			                      (unsigned int)fnTrap);
		Memory_LogRuntimeEvent("ADR_ERROR_TRAP15 fn=$%04X state=$%04X err=$%04X",
		                      (unsigned int)trapFnWord,
		                      (unsigned int)trapStateWord,
		                      (unsigned int)trapErrWord);
		Memory_LogRuntimeEvent("ADR_ERROR_QUE bca=$%04X bcc=%08X bc6=$%02X bc7=$%02X c1a=$%02X",
		                      (unsigned int)qcount,
		                      (unsigned int)qptr,
		                      (unsigned int)qmode,
		                      (unsigned int)qkana,
		                      (unsigned int)qerr);
		for (fn = 0; fn < 256 && iocsHitCount < 8; ++fn) {
			DWORD ent = Memory_LogReadLong24(0x00000400 + (DWORD)fn * 4) & 0x00ffffffU;
			if (ent == a0m || ent == a0even || (ent + 1U) == a0m) {
				Memory_LogRuntimeEvent("ADR_ERROR_IOCS_HIT fn=%02X ent=%08X a0=%08X",
				                      (unsigned int)fn,
				                      (unsigned int)ent,
				                      (unsigned int)a0m);
				iocsHitCount++;
			}
		}
		if (iocsHitCount == 0) {
			Memory_LogRuntimeEvent("ADR_ERROR_IOCS_HIT none a0=%08X", (unsigned int)a0m);
		}
		Memory_LogRuntimeEvent("ADR_ERROR_CTX adr24=%08X d0=%08X d1=%08X d2=%08X a0=%08X a1=%08X a2=%08X a7=%08X",
		                      (unsigned int)adr24, (unsigned int)d0, (unsigned int)d1, (unsigned int)d2,
		                      (unsigned int)a0, (unsigned int)a1, (unsigned int)a2, (unsigned int)a7);
		Memory_LogRuntimeEvent("ADR_ERROR_CTX2 d3=%08X d4=%08X d5=%08X d6=%08X d7=%08X a3=%08X a4=%08X a5=%08X a6=%08X",
		                      (unsigned int)d3, (unsigned int)d4, (unsigned int)d5, (unsigned int)d6, (unsigned int)d7,
		                      (unsigned int)a3, (unsigned int)a4, (unsigned int)a5, (unsigned int)a6);
		if (Memory_PeekLogBytes((pc & 0x00ffffff) & ~0x0fU, logDump, 16)) {
			DWORD base = (pc & 0x00ffffff) & ~0x0fU;
			Memory_LogRuntimeEvent(
			    "ADR_ERROR_PC  base=%08X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			    (unsigned int)base,
			    (unsigned int)logDump[0], (unsigned int)logDump[1],
			    (unsigned int)logDump[2], (unsigned int)logDump[3],
			    (unsigned int)logDump[4], (unsigned int)logDump[5],
			    (unsigned int)logDump[6], (unsigned int)logDump[7],
			    (unsigned int)logDump[8], (unsigned int)logDump[9],
			    (unsigned int)logDump[10], (unsigned int)logDump[11],
			    (unsigned int)logDump[12], (unsigned int)logDump[13],
			    (unsigned int)logDump[14], (unsigned int)logDump[15]);
		}
		if ((a0 & 0x00ffffff) >= 0x20 && (a0 & 0x00ffffff) < 0x00c00000 &&
		    Memory_PeekLogBytes(((a0 & 0x00ffffff) - 0x20U) & ~0x0fU, logDump, 16)) {
			DWORD base = ((a0 & 0x00ffffff) - 0x20U) & ~0x0fU;
			Memory_LogRuntimeEvent(
			    "ADR_ERROR_A0P base=%08X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			    (unsigned int)base,
			    (unsigned int)logDump[0], (unsigned int)logDump[1],
			    (unsigned int)logDump[2], (unsigned int)logDump[3],
			    (unsigned int)logDump[4], (unsigned int)logDump[5],
			    (unsigned int)logDump[6], (unsigned int)logDump[7],
			    (unsigned int)logDump[8], (unsigned int)logDump[9],
			    (unsigned int)logDump[10], (unsigned int)logDump[11],
			    (unsigned int)logDump[12], (unsigned int)logDump[13],
			    (unsigned int)logDump[14], (unsigned int)logDump[15]);
		}
		if ((a0 & 0x00ffffff) >= 0x10 && (a0 & 0x00ffffff) < 0x00c00000 &&
		    Memory_PeekLogBytes(((a0 & 0x00ffffff) - 0x10U) & ~0x0fU, logDump, 16)) {
			DWORD base = ((a0 & 0x00ffffff) - 0x10U) & ~0x0fU;
			Memory_LogRuntimeEvent(
			    "ADR_ERROR_A0Q base=%08X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			    (unsigned int)base,
			    (unsigned int)logDump[0], (unsigned int)logDump[1],
			    (unsigned int)logDump[2], (unsigned int)logDump[3],
			    (unsigned int)logDump[4], (unsigned int)logDump[5],
			    (unsigned int)logDump[6], (unsigned int)logDump[7],
			    (unsigned int)logDump[8], (unsigned int)logDump[9],
			    (unsigned int)logDump[10], (unsigned int)logDump[11],
			    (unsigned int)logDump[12], (unsigned int)logDump[13],
			    (unsigned int)logDump[14], (unsigned int)logDump[15]);
		}
		if (adr24 < 0x00c00000) {
			DWORD base = adr24 & ~0x0fU;
			Memory_LogRuntimeEvent(
			    "ADR_ERROR_MEM base=%08X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			    (unsigned int)base,
			    (unsigned int)MEM[(base + 0x0) ^ 1], (unsigned int)MEM[(base + 0x1) ^ 1],
			    (unsigned int)MEM[(base + 0x2) ^ 1], (unsigned int)MEM[(base + 0x3) ^ 1],
			    (unsigned int)MEM[(base + 0x4) ^ 1], (unsigned int)MEM[(base + 0x5) ^ 1],
			    (unsigned int)MEM[(base + 0x6) ^ 1], (unsigned int)MEM[(base + 0x7) ^ 1],
			    (unsigned int)MEM[(base + 0x8) ^ 1], (unsigned int)MEM[(base + 0x9) ^ 1],
			    (unsigned int)MEM[(base + 0xA) ^ 1], (unsigned int)MEM[(base + 0xB) ^ 1],
			    (unsigned int)MEM[(base + 0xC) ^ 1], (unsigned int)MEM[(base + 0xD) ^ 1],
			    (unsigned int)MEM[(base + 0xE) ^ 1], (unsigned int)MEM[(base + 0xF) ^ 1]);
		}
		if ((a1 & 0x00ffffff) < 0x00c00000) {
			DWORD base = (a1 & 0x00ffffff) & ~0x0fU;
			Memory_LogRuntimeEvent(
			    "ADR_ERROR_A1  base=%08X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			    (unsigned int)base,
			    (unsigned int)MEM[(base + 0x0) ^ 1], (unsigned int)MEM[(base + 0x1) ^ 1],
			    (unsigned int)MEM[(base + 0x2) ^ 1], (unsigned int)MEM[(base + 0x3) ^ 1],
			    (unsigned int)MEM[(base + 0x4) ^ 1], (unsigned int)MEM[(base + 0x5) ^ 1],
			    (unsigned int)MEM[(base + 0x6) ^ 1], (unsigned int)MEM[(base + 0x7) ^ 1],
			    (unsigned int)MEM[(base + 0x8) ^ 1], (unsigned int)MEM[(base + 0x9) ^ 1],
			    (unsigned int)MEM[(base + 0xA) ^ 1], (unsigned int)MEM[(base + 0xB) ^ 1],
			    (unsigned int)MEM[(base + 0xC) ^ 1], (unsigned int)MEM[(base + 0xD) ^ 1],
			    (unsigned int)MEM[(base + 0xE) ^ 1], (unsigned int)MEM[(base + 0xF) ^ 1]);
		}
		if ((a7 & 0x00ffffff) < 0x00c00000) {
			DWORD base = (a7 & 0x00ffffff) & ~0x0fU;
			Memory_LogRuntimeEvent(
			    "ADR_ERROR_A7  base=%08X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			    (unsigned int)base,
			    (unsigned int)MEM[(base + 0x0) ^ 1], (unsigned int)MEM[(base + 0x1) ^ 1],
			    (unsigned int)MEM[(base + 0x2) ^ 1], (unsigned int)MEM[(base + 0x3) ^ 1],
			    (unsigned int)MEM[(base + 0x4) ^ 1], (unsigned int)MEM[(base + 0x5) ^ 1],
			    (unsigned int)MEM[(base + 0x6) ^ 1], (unsigned int)MEM[(base + 0x7) ^ 1],
			    (unsigned int)MEM[(base + 0x8) ^ 1], (unsigned int)MEM[(base + 0x9) ^ 1],
			    (unsigned int)MEM[(base + 0xA) ^ 1], (unsigned int)MEM[(base + 0xB) ^ 1],
			    (unsigned int)MEM[(base + 0xC) ^ 1], (unsigned int)MEM[(base + 0xD) ^ 1],
			    (unsigned int)MEM[(base + 0xE) ^ 1], (unsigned int)MEM[(base + 0xF) ^ 1]);
			if (s_adr_error_detail_count == 0) {
				DWORD base2 = (a7 & 0x00ffffff) & ~0x1fU;
				Memory_LogRuntimeEvent(
				    "ADR_ERROR_A7X base=%08X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				    (unsigned int)base2,
				    (unsigned int)MEM[(base2 + 0x00) ^ 1], (unsigned int)MEM[(base2 + 0x01) ^ 1],
				    (unsigned int)MEM[(base2 + 0x02) ^ 1], (unsigned int)MEM[(base2 + 0x03) ^ 1],
				    (unsigned int)MEM[(base2 + 0x04) ^ 1], (unsigned int)MEM[(base2 + 0x05) ^ 1],
				    (unsigned int)MEM[(base2 + 0x06) ^ 1], (unsigned int)MEM[(base2 + 0x07) ^ 1],
				    (unsigned int)MEM[(base2 + 0x08) ^ 1], (unsigned int)MEM[(base2 + 0x09) ^ 1],
				    (unsigned int)MEM[(base2 + 0x0A) ^ 1], (unsigned int)MEM[(base2 + 0x0B) ^ 1],
				    (unsigned int)MEM[(base2 + 0x0C) ^ 1], (unsigned int)MEM[(base2 + 0x0D) ^ 1],
				    (unsigned int)MEM[(base2 + 0x0E) ^ 1], (unsigned int)MEM[(base2 + 0x0F) ^ 1],
				    (unsigned int)MEM[(base2 + 0x10) ^ 1], (unsigned int)MEM[(base2 + 0x11) ^ 1],
				    (unsigned int)MEM[(base2 + 0x12) ^ 1], (unsigned int)MEM[(base2 + 0x13) ^ 1],
				    (unsigned int)MEM[(base2 + 0x14) ^ 1], (unsigned int)MEM[(base2 + 0x15) ^ 1],
				    (unsigned int)MEM[(base2 + 0x16) ^ 1], (unsigned int)MEM[(base2 + 0x17) ^ 1],
				    (unsigned int)MEM[(base2 + 0x18) ^ 1], (unsigned int)MEM[(base2 + 0x19) ^ 1],
				    (unsigned int)MEM[(base2 + 0x1A) ^ 1], (unsigned int)MEM[(base2 + 0x1B) ^ 1],
				    (unsigned int)MEM[(base2 + 0x1C) ^ 1], (unsigned int)MEM[(base2 + 0x1D) ^ 1],
				    (unsigned int)MEM[(base2 + 0x1E) ^ 1], (unsigned int)MEM[(base2 + 0x1F) ^ 1]);
			}
		}
		s_adr_error_detail_count++;
	}
#endif
	s_adr_error_count++;
	p6logd("AdrError: %x\n", adr);
	//	assert(0);
}

void
BusError(DWORD adr, DWORD unknown)
{
    DWORD pc = 0;
    int isScsiRange = 0;
    static int s_buserr_detail_count = 0;

	(void)adr;
	(void)unknown;
#if defined(HAVE_C68K)
	pc = C68k_Get_PC(&C68K);
#endif

	// Suppress frequent SCSI-related BusError logs (normal when SCSI not connected)
	// 0xe9a000-0xe9bfff: SCSI I/O, 0xea0000-0xeaffff: SCSI IPL
	DWORD logAddr = (BusErrAdr != 0) ? BusErrAdr : adr;
	isScsiRange = ((logAddr >= 0xe9a000 && logAddr < 0xe9c000) || (logAddr >= 0xea0000 && logAddr < 0xeb0000));
	Memory_LogRuntimeEvent("BUS_ERROR adr=%08X flag=%u pc=%08X scsiRange=%u",
	                      (unsigned int)logAddr, (unsigned int)BusErrFlag,
	                      (unsigned int)pc, (unsigned int)(isScsiRange ? 1 : 0));
	if (!isScsiRange) {
#if defined(HAVE_C68K)
		if (s_buserr_detail_count < 32) {
			DWORD sr = C68k_Get_SR(&C68K);
			DWORD d0 = C68k_Get_DReg(&C68K, 0);
			DWORD d1 = C68k_Get_DReg(&C68K, 1);
			DWORD d2 = C68k_Get_DReg(&C68K, 2);
			DWORD a7 = C68k_Get_AReg(&C68K, 7);
			p6logd("BusError: %x (flag=%d pc=%08x sr=%04x d0=%08x d1=%08x d2=%08x a7=%08x)\n",
			       logAddr, BusErrFlag, pc, (unsigned int)(sr & 0xffff),
			       d0, d1, d2, a7);
			if (MEM != NULL) {
				DWORD pc24 = pc & 0x00ffffffU;
				DWORD sp24 = a7 & 0x00ffffffU;
				if (pc24 + 7U < 0x00c00000U) {
					p6logd("BusError: pcbytes @%06x = %02x %02x %02x %02x %02x %02x %02x %02x\n",
					       (unsigned int)pc24,
					       (unsigned int)MEM[(pc24 + 0U) ^ 1],
					       (unsigned int)MEM[(pc24 + 1U) ^ 1],
					       (unsigned int)MEM[(pc24 + 2U) ^ 1],
					       (unsigned int)MEM[(pc24 + 3U) ^ 1],
					       (unsigned int)MEM[(pc24 + 4U) ^ 1],
					       (unsigned int)MEM[(pc24 + 5U) ^ 1],
					       (unsigned int)MEM[(pc24 + 6U) ^ 1],
					       (unsigned int)MEM[(pc24 + 7U) ^ 1]);
				}
				if (sp24 + 7U < 0x00c00000U) {
					p6logd("BusError: spbytes @%06x = %02x %02x %02x %02x %02x %02x %02x %02x\n",
					       (unsigned int)sp24,
					       (unsigned int)MEM[(sp24 + 0U) ^ 1],
					       (unsigned int)MEM[(sp24 + 1U) ^ 1],
					       (unsigned int)MEM[(sp24 + 2U) ^ 1],
					       (unsigned int)MEM[(sp24 + 3U) ^ 1],
					       (unsigned int)MEM[(sp24 + 4U) ^ 1],
					       (unsigned int)MEM[(sp24 + 5U) ^ 1],
					       (unsigned int)MEM[(sp24 + 6U) ^ 1],
					       (unsigned int)MEM[(sp24 + 7U) ^ 1]);
				}
			}
			s_buserr_detail_count++;
		} else {
			p6logd("BusError: %x (flag=%d)\n", logAddr, BusErrFlag);
		}
#else
		p6logd("BusError: %x (flag=%d)\n", logAddr, BusErrFlag);
#endif
	}
	BusErrHandling = 1;
	//assert(0);
}
