#ifdef  __cplusplus
#include <atomic>

extern "C" {
#endif
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "common.h"
#include "fileio.h"
#include "timer.h"
#include "keyboard.h"
#include "prop.h"
#include "status.h"
#include "joystick.h"
//#include "mkcgrom.h"
#include "winx68k.h"
#include "windraw.h"
//#include "winui.h"
#include "../x68k/m68000.h" // xxx ����Ϥ����줤��ʤ��ʤ�Ϥ�
#include "../m68000/m68000.h"
#include "../x68k/x68kmemory.h"
#include "mfp.h"
#include "opm.h"
#include "bg.h"
#include "adpcm.h"
//#include "mercury.h"
#include "crtc.h"
#include "mfp.h"
#include "fdc.h"
#include "fdd.h"
#include "dmac.h"
#include "irqh.h"
#include "ioc.h"
#include "rtc.h"
#include "sasi.h"
#include "scsi.h"
#include "sysport.h"
#include "bg.h"
#include "palette.h"
#include "crtc.h"
#include "pia.h"
#include "ppi.h"
#include "scc.h"
#include "midi.h"
#include "sram.h"
#include "gvram.h"
#include "tvram.h"
#include "mouse.h"

#include "dswin.h"
#include "fmg_wrap.h"

#ifdef RFMDRV
int rfd_sock;
#endif

  //#define WIN68DEBUG

#ifdef WIN68DEBUG
#include "d68k.h"
#endif

//#include "../icons/keropi_mono.xbm"

#define    APPNAME    "Keropi"

extern    WORD    BG_CHREND;
extern    WORD    BG_BGTOP;
extern    WORD    BG_BGEND;
extern    BYTE    BG_CHRSIZE;

//const    BYTE    PrgName[] = "Keropi";
const    BYTE    PrgTitle[] = APPNAME;

char    winx68k_dir[MAX_PATH];
char    winx68k_ini[MAX_PATH];

WORD    VLINE_TOTAL = 567;
DWORD    VLINE = 0;
DWORD    vline = 0;


BYTE DispFrame = 0;

unsigned int hTimerID = 0;
DWORD TimerICount = 0;
extern DWORD timertick;
void Memory_SetSCSIMode(void);
void Memory_ClearSCSIMode(void);
void Memory_SetSASIBootMode(void);
BYTE traceflag = 0;

BYTE ForceDebugMode = 0;
DWORD skippedframes = 0;

static int ClkUsed = 0;
static int FrameSkipCount = 0;
static int FrameSkipQueue = 0;
static int g_storage_bus_mode = 0; // 0 = SASI, 1 = SCSI
static unsigned char SASI_IPLROM[0x20000] = {0};
static int SASI_IPLROM_loaded = 0;
static int s_scsi_ipl_needs_restore = 0;  // set when SASI IPLROM0 overrides IPL
static BYTE s_sram_sasi[0x4000];          // separate SRAM state for SASI mode
static int s_sram_sasi_valid = 0;         // 1 = s_sram_sasi has been initialized
extern BYTE* s_disk_image_buffer[5];
extern long s_disk_image_buffer_size[5];

static int WinX68k_SASIImageHasBootSector(void) {
	int i;
	const int bootOffset = 4 * 256;
	if (s_disk_image_buffer[4] == NULL || s_disk_image_buffer_size[4] < (bootOffset + 256)) {
		return 0;
	}
	for (i = 0; i < 256; ++i) {
		if (s_disk_image_buffer[4][bootOffset + i] != 0x00) {
			return 1;
		}
	}
	return 0;
}

void WinX68k_SaveSASI_SRAM(void) {
    memcpy(s_sram_sasi, SRAM, 0x4000);
    s_sram_sasi_valid = 1;
}

static void WinX68k_RestoreSASI_SRAM(void) {
	if (s_sram_sasi_valid) {
		memcpy(SRAM, s_sram_sasi, 0x4000);
	}
}
static int g_scsi0_mounted = 0;
static char g_scsi0_path[MAX_PATH] = {0};
static int g_scsi_boot_pending = 0;
static int g_enable_scsi_dev_driver = 0;
static int g_scsi_boot_watchdog_ticks = 0;
static int g_scsi_boot_forced_once = 0;
static int g_scsi_boot_handoff_ticks = 0;
static int g_scsi_post_handoff_ticks = 0;
static int g_scsi_post_handoff_log_count = 0;
static int g_scsi_pc_stall_ticks = 0;
static int g_scsi_pc_stall_log_count = 0;
static DWORD g_scsi_last_pc = 0xffffffffU;
static int g_frame_dirty_idle_polls = 0;
static int g_force_image_readback_pending = 0;
static int g_image_probe_counter = 0;
static int g_scsi_restore_iocs_pending = 0;
static int g_scsi_auto_enter_attempts = 0;
static int g_scsi_auto_key_enabled = 0;
static int g_scsi_auto_ari_enabled = 0;
static int g_scsi_auto_ari_index = 0;
static int g_scsi_force_redraw_attempts = 0;
static void X68000_AppendSCSILog(const char* message);
static DWORD X68000_ReadIplLong(DWORD offset, int useDirect);
static DWORD X68000_DecodeIplVectorAddress(DWORD raw);

static DWORD
X68000_NormalizeDecodedIplIocsHandler(DWORD decoded, DWORD sourceRaw, int* outCanonicalized)
{
    DWORD eff = decoded & 0x00ffffffU;
    int canonicalized = 0;
    // Some IPL tables encode ROM handlers with bit0 metadata.
    if ((eff & 1U) != 0U &&
        (((eff >= 0x00fe0000U && eff <= 0x00ffffffU) ||
          (eff >= 0x00ea0000U && eff < 0x00ea2000U)))) {
        eff &= ~1U;
        canonicalized = 1;
    }
    // Do not force-map low decoded addresses to $FFxxxx when the original
    // table word has a non-zero high byte. Those are often opcodes/data from
    // a false-positive scan hit (for example 0x610000E8), not truncated vectors.
    if (eff != 0U &&
        eff < 0x00000400U &&
        (eff & 1U) == 0U &&
        (sourceRaw & 0xff000000U) == 0U) {
        eff |= 0x00ff0000U;
        canonicalized = 1;
    }
    if (outCanonicalized) {
        *outCanonicalized = canonicalized;
    }
    return eff;
}

extern BYTE* s_disk_image_buffer[5];
extern long s_disk_image_buffer_size[5];
// Debug: write to /tmp (always accessible) AND normal log path
static void DebugLog(const char* msg) {
    FILE* fp;
    // Always write to /tmp first (no sandbox issues)
    fp = fopen("/tmp/x68k_debug.txt", "a");
    if (fp) { fprintf(fp, "%s\n", msg); fclose(fp); }
    // Also try normal log path
    const char* home = getenv("HOME");
    char path[512];
    if (home && home[0] != '\0')
        snprintf(path, sizeof(path), "%s/Documents/X68000/_scsi_iocs.txt", home);
    else
        snprintf(path, sizeof(path), "X68000/_scsi_iocs.txt");
    fp = fopen(path, "a");
    if (fp) { fprintf(fp, "%s\n", msg); fclose(fp); }
}

static int s_appendlog_total = 0;
#define APPENDLOG_LIMIT 5000

static void
X68000_AppendSCSILog(const char* message)
{
#ifdef __APPLE__
    if (s_appendlog_total >= APPENDLOG_LIMIT) return;
    s_appendlog_total++;
    const char* home = getenv("HOME");
    char path[512];
    if (home && home[0] != '\0') {
        snprintf(path, sizeof(path), "%s/Documents/X68000/_scsi_iocs.txt", home);
    } else {
        snprintf(path, sizeof(path), "X68000/_scsi_iocs.txt");
    }
    FILE* fp = fopen(path, "a");
    if (fp) {
        fprintf(fp, "%s\n", message);
        fclose(fp);
    }
    fp = fopen("/tmp/x68000_scsi_iocs.txt", "a");
    if (fp) {
        fprintf(fp, "%s\n", message);
        fclose(fp);
    }
#else
    (void)message;
#endif
}


static DWORD
X68000_ReadIplSwappedLong(DWORD offset)
{
    if (IPL == NULL) {
        return 0;
    }
    if (offset + 3 >= 0x40000) {
        return 0;
    }
    return ((DWORD)IPL[offset + 1] << 24) |
           ((DWORD)IPL[offset + 0] << 16) |
           ((DWORD)IPL[offset + 3] << 8) |
           ((DWORD)IPL[offset + 2]);
}

static DWORD
X68000_ReadIplDirectLong(DWORD offset)
{
    if (IPL == NULL) {
        return 0;
    }
    if (offset + 3 >= 0x40000) {
        return 0;
    }
    return ((DWORD)IPL[offset + 0] << 24) |
           ((DWORD)IPL[offset + 1] << 16) |
           ((DWORD)IPL[offset + 2] << 8) |
           ((DWORD)IPL[offset + 3]);
}

static DWORD
X68000_ReadIplLong(DWORD offset, int useDirect)
{
    if (useDirect) {
        return X68000_ReadIplDirectLong(offset);
    }
    return X68000_ReadIplSwappedLong(offset);
}

static DWORD
X68000_ReadIplVector(int vectorNumber)
{
    if (vectorNumber < 0) {
        return 0;
    }
    return X68000_ReadIplSwappedLong(0x30000 + (DWORD)vectorNumber * 4);
}

static DWORD
X68000_DecodeIplVectorAddress(DWORD raw)
{
    // Unused/sentinel entries in ROM tables are often all-zeros/all-ones.
    // Treat them as invalid instead of decoding to $00FFFFFF, otherwise the
    // IOCS table scanner can falsely lock onto an FF-filled region.
    if (raw == 0x00000000 || raw == 0xffffffff) {
        return 0;
    }
    // Some IPLs store ROM vector/table entries in a packed form like:
    //   0x03D32080 -> 0x00FFD320
    // The top byte selects the ROM page ($FC-$FF), the middle 16 bits are the
    // offset, and the low byte is a tag ($80).  A plain 24-bit mask would
    // misdecode this as 0x00D32080 and the IOCS table scanner will miss most
    // handlers (many live in $FC/$FD/$FE pages).
    if ((raw & 0xff) == 0x80) {
        DWORD page = (raw >> 24) & 0xff;
        if (page <= 0x03) {
            return 0x00fc0000 | (page << 16) | ((raw >> 8) & 0xffff);
        }
    }
    return raw & 0x00ffffff;
}

#ifdef __cplusplus
};
#endif


void
WinX68k_SCSICheck(void)
{
    WORD *p1, *p2;
    int scsi;
    int i;

    scsi = 0;
    for (i = 0x30600; i < 0x30c00; i += 2) {
        p1 = (WORD *)(&IPL[i]);
        p2 = p1 + 1;
        // xxx: works only for little endian guys
        if (*p1 == 0xfc00 && *p2 == 0x0000) {
            scsi = 1;
            break;
        }
    }

    // SCSI model: zero first 8KB, fill rest with $FF, copy small SCSI BIOS stub
    if (scsi) {
        ZeroMemory(IPL, 0x2000);
        memset(&IPL[0x2000], 0xff, 0x1e000);
        static const BYTE SCSIIMG_STUB[] = {
            0x00, 0xfc, 0x00, 0x14,            // $fc0000 SCSI boot entry
            0x00, 0xfc, 0x00, 0x16,            // $fc0004 IOCS vector setup entry
            0x00, 0x00, 0x00, 0x00,            // $fc0008 ?
            0x48, 0x75, 0x6d, 0x61,            // $fc000c
            0x6e, 0x36, 0x38, 0x6b,            // $fc0010 ID "Human68k"
            0x4e, 0x75,                        // $fc0014 "rts"
            0x23, 0xfc, 0x00, 0xfc, 0x00, 0x2a, // $fc0016
            0x00, 0x00, 0x07, 0xd4,            // $fc001c "move.l #$fc002a, $7d4.l"
            0x74, 0xff,                        // $fc0020 "moveq #-1, d2"
            0x4e, 0x75,                        // $fc0022 "rts"
            0x44, 0x55, 0x4d, 0x4d, 0x59, 0x20, // $fc0024 ID "DUMMY "
            0x70, 0xff,                        // $fc002a "moveq #-1, d0"
            0x4e, 0x75,                        // $fc002c "rts"
        };
        memcpy(IPL, SCSIIMG_STUB, sizeof(SCSIIMG_STUB));
        p6logd("SCSI IPL detected and enabled\n");
    } else {
        // SASI model: use IPL ROM as-is
        memcpy(IPL, &IPL[0x20000], 0x20000);
        p6logd("SCSI IPL not detected\n");
    }
}

int
WinX68k_LoadROMs(void)
{
    int i;
    BYTE tmp;
#if 0
    static const char *BIOSFILE[] = {
        "iplrom.dat", "iplrom30.dat", "iplromco.dat", "iplromxv.dat"
    };
    static const char FONTFILE[] = "cgrom.dat";
    static const char FONTFILETMP[] = "cgrom.tmp";
    FILEH fp;

    for (fp = 0, i = 0; fp == 0 && i < NELEMENTS(BIOSFILE); ++i) {
        fp = File_OpenCurDir((char *)BIOSFILE[i]);
    }

    if (fp == 0) {
        Error("BIOS ROM ���᡼�������Ĥ���ޤ���.");
        return FALSE;
    }

    File_Read(fp, &IPL[0x20000], 0x20000);
    File_Close(fp);
#else
    extern unsigned char IPLROM_DAT[0x20000]; // modified by Awed (remove const)
    memcpy( &IPL[0x20000], IPLROM_DAT, 0x20000);
#endif
    WinX68k_SCSICheck();    // SCSI IPL�ʤ顢$fc0000����SCSI BIOS���֤�

    for (i = 0; i < 0x40000; i += 2) {
        tmp = IPL[i];
        IPL[i] = IPL[i + 1];
        IPL[i + 1] = tmp;
    }

    // Lightweight IPL ROM IOCS table scan — no Memory subsystem dependency.
    // Scan for fn=$20 (B_PUTC) pointing to $FE_xxxx (IPL ROM native).
    // Strategy: for each offset, quickly check fn=$20 + fn=$21 only.
    {
        int altHits = 0;
        DWORD altFn20Best = 0;
        int scsiHits = 0;
        DWORD scsiFn20 = 0, scsiFn21 = 0;
        DWORD scsiOff = 0;
        int scsiUd = 0;
        for (int ud = 0; ud <= 1; ++ud) {
            for (DWORD off = 0; off + 256*4 <= 0x40000; off += 4) {
                DWORD raw20 = X68000_ReadIplLong(off + 0x20*4, ud);
                DWORD eff20 = X68000_NormalizeDecodedIplIocsHandler(
                    X68000_DecodeIplVectorAddress(raw20), raw20, NULL);
                DWORD raw21 = X68000_ReadIplLong(off + 0x21*4, ud);
                DWORD eff21 = X68000_NormalizeDecodedIplIocsHandler(
                    X68000_DecodeIplVectorAddress(raw21), raw21, NULL);
                // Both must be in ROM range
                if ((eff21 < 0x00fe0000U || eff21 > 0x00ffffffU) || (eff21 & 1))
                    continue;
                // Validate: also check fn=$04 and fn=$25 for table plausibility
                DWORD raw04 = X68000_ReadIplLong(off + 0x04*4, ud);
                DWORD eff04 = X68000_NormalizeDecodedIplIocsHandler(
                    X68000_DecodeIplVectorAddress(raw04), raw04, NULL);
                DWORD raw25 = X68000_ReadIplLong(off + 0x25*4, ud);
                DWORD eff25 = X68000_NormalizeDecodedIplIocsHandler(
                    X68000_DecodeIplVectorAddress(raw25), raw25, NULL);
                int plausible = 0;
                if (eff04 >= 0x00fc0000U && eff04 <= 0x00ffffffU && (eff04 & 1) == 0)
                    plausible++;
                if (eff25 >= 0x00fc0000U && eff25 <= 0x00ffffffU && (eff25 & 1) == 0)
                    plausible++;
                if (plausible < 2)
                    continue;

                if (eff20 >= 0x00fe0000U && eff20 <= 0x00ffffffU && (eff20 & 1) == 0) {
                    if (altHits < 4)
                        p6logd("IPL_SCAN_ALT: off=$%05X ud=%d fn20=$%08X fn21=$%08X\n",
                               (unsigned int)off, ud, (unsigned int)eff20, (unsigned int)eff21);
                    if (altFn20Best == 0) altFn20Best = eff20;
                    altHits++;
                } else if (eff20 >= 0x00ea0000U && eff20 < 0x00ea2000U) {
                    if (scsiHits == 0) {
                        scsiFn20 = eff20; scsiFn21 = eff21;
                        scsiOff = off; scsiUd = ud;
                    }
                    scsiHits++;
                }
            }
        }
        p6logd("IPL_SCAN: scsiHits=%d scsiOff=$%05X fn20=$%08X fn21=$%08X | altHits=%d altFn20=$%08X\n",
               scsiHits, (unsigned int)scsiOff, (unsigned int)scsiFn20, (unsigned int)scsiFn21,
               altHits, (unsigned int)altFn20Best);
    }

#if 0
    fp = File_OpenCurDir((char *)FONTFILE);
    if (fp == 0) {
        // cgrom.tmp�����롩
        fp = File_OpenCurDir((char *)FONTFILETMP);
        if (fp == 0) {
            // �ե�������� XXX
            printf("�ե����ROM���᡼�������Ĥ���ޤ���\n");
            return FALSE;
        }
    }
    File_Read(fp, FONT, 0xc0000);
    File_Close(fp);
#else
    extern unsigned char CGROM_DAT[0xc0000]; // modified by Awed (remove const)
    memcpy(FONT, CGROM_DAT, 0xc0000);

#endif
    
    
    return TRUE;
}

int
WinX68k_Reset(void)
{
    // Don't clear the log on reset — preserve boot log for debugging.
    s_appendlog_total = 0;  // Reset log counter so post-reset entries are logged
    X68000_AppendSCSILog("--- core reset ---");
    {
        char dbg[256];
        snprintf(dbg, sizeof(dbg), "RESET bus_mode=%d scsi0=%d HDImage='%.40s' SRAM18=%02X",
            g_storage_bus_mode, g_scsi0_mounted, Config.HDImage[0], SRAM[0x18 ^ 1]);
        DebugLog(dbg);
    }
    printf("WinX68k_Reset called: g_storage_bus_mode=%d g_scsi0_mounted=%d\n",
           g_storage_bus_mode, g_scsi0_mounted);
    {   // @added by GOROman
        VLINE_TOTAL = 567;
        VLINE = 0;
        vline = 0;


        DispFrame = 0;

        hTimerID = 0;
        TimerICount = 0;
        traceflag = 0;

        ForceDebugMode = 0;
        skippedframes = 0;

        ClkUsed = 0;
        FrameSkipCount = 0;
        FrameSkipQueue = 0;
    }
    
    OPM_Reset();

#if defined (HAVE_CYCLONE)
    m68000_reset();
    m68000_set_reg(M68K_A7, (IPL[0x30001]<<24)|(IPL[0x30000]<<16)|(IPL[0x30003]<<8)|IPL[0x30002]);
    m68000_set_reg(M68K_PC, (IPL[0x30005]<<24)|(IPL[0x30004]<<16)|(IPL[0x30007]<<8)|IPL[0x30006]);
#elif defined (HAVE_C68K)
    C68k_Reset(&C68K);
/*
    C68k_Set_Reg(&C68K, C68K_A7, (IPL[0x30001]<<24)|(IPL[0x30000]<<16)|(IPL[0x30003]<<8)|IPL[0x30002]);
    C68k_Set_Reg(&C68K, C68K_PC, (IPL[0x30005]<<24)|(IPL[0x30004]<<16)|(IPL[0x30007]<<8)|IPL[0x30006]);
*/
    // Set reset vectors directly from IPL ROM (original px68k behavior).
    // The previous "safety check" rejected valid vectors from SASI-era ROMs
    // whose PC pointed to $FC0000 range instead of $FE0000+.
	    if (IPL != NULL) {
	        DWORD stack_pointer = X68000_ReadIplVector(0);
	        DWORD program_counter = X68000_ReadIplVector(1);

        // Accept any non-zero vectors (original px68k had no validation)
        bool valid_stack = (stack_pointer != 0);
        bool valid_pc = (program_counter != 0);

        if (valid_stack && valid_pc) {
            C68k_Set_AReg(&C68K, 7, stack_pointer);
            C68k_Set_PC(&C68K, program_counter);
        } else {
            // IPL vectors seem invalid - use safe defaults
            printf("WARNING: IPL vectors invalid (SP: 0x%08X, PC: 0x%08X) - using safe defaults\n",
                   stack_pointer, program_counter);
            C68k_Set_AReg(&C68K, 7, 0x00002000);  // Safe stack pointer in RAM
            C68k_Set_PC(&C68K, 0x00FE0000);       // Safe program counter in IPL ROM
        }
    } else {
        // IPL ROM not loaded - set safe default values to prevent bus error
        printf("WARNING: IPL ROM not loaded during reset - using safe defaults\n");
        C68k_Set_AReg(&C68K, 7, 0x00002000);  // Safe stack pointer
        C68k_Set_PC(&C68K, 0x00FE0000);       // Safe program counter
    }
#endif /* HAVE_C68K */

    // Clear main RAM before IPL ROM runs on reset.
    // After a previous SCSI boot, RAM contains stale OS data (IOCS
    // vectors, device chains, kernel code, system variables) that
    // interfere with a clean re-boot.  The synthetic SCSI driver
    // state is reset by SCSI_Init(), making stale RAM pointers to
    // the driver invalid.  Clearing RAM simulates a cold boot state.
    // Use the actual allocated buffer size (MEM_SIZE), not a hardcoded
    // value, since the X68000 memory size varies by model/SWITCH.X.
#ifndef MEM_SIZE
#define MEM_SIZE 0xc00000
#endif
    if (MEM != NULL) {
        memset(MEM, 0, MEM_SIZE);
    }

    Memory_Init();
    CRTC_Init();
    DMA_Init();
    MFP_Init();
    FDC_Init();
    FDD_Reset();
    SASI_Init();
    SCSI_Init();
    IOC_Init();
    SCC_Init();
    PIA_Init();
    RTC_Init();
    TVRAM_Init();
    GVRAM_Init();
    BG_Init();
    Pal_Init();
    IRQH_Init();
    MIDI_Init();
    //WinDrv_Init();
#if defined(HAVE_C68K)
	// IPL-ROM-first SCSI boot: IPL ROMに通常起動させ、SASIデバイススキャン時に
	// SCSIブートセクタを注入する。
	if (g_storage_bus_mode == 1 && g_scsi0_mounted) {
		// Save SASI SRAM if switching from SASI mode
		{
			extern void WinX68k_SaveSASI_SRAM(void);
			WinX68k_SaveSASI_SRAM();
		}
		g_scsi_boot_pending = 1;
		SASI_ArmSCSIBootIntercept(1);
		SCSI_InvalidateTransferCache();
		g_enable_scsi_dev_driver = 1;
		// Restore SCSI IPL ROM if SASI IPLROM0.DAT overrode it in a previous reset.
		if (s_scsi_ipl_needs_restore) {
			extern unsigned char IPLROM_DAT[0x20000];
			int ii; BYTE tmp;
			printf("WinX68k_Reset: Restoring SCSI IPL... step 1: copy IPLROM_DAT to both halves\n");
			// Overwrite BOTH halves — IPL[0..0x20000] still held the
			// byte-swapped SASI IPLROM from the previous reset, so byte-
			// swapping the full 0x40000 would double-swap the first half
			// and leave $FC0000-$FDFFFF garbled. Refill both halves first.
			memcpy(IPL,             IPLROM_DAT, 0x20000);
			memcpy(&IPL[0x20000],   IPLROM_DAT, 0x20000);
			printf("WinX68k_Reset: step 2: SCSICheck\n");
			WinX68k_SCSICheck();
			printf("WinX68k_Reset: step 3: byte-swap\n");
			for (ii = 0; ii < 0x40000; ii += 2) {
				tmp = IPL[ii]; IPL[ii] = IPL[ii+1]; IPL[ii+1] = tmp;
			}
			printf("WinX68k_Reset: step 4: re-set vectors\n");
			{
				DWORD sp = X68000_ReadIplVector(0);
				DWORD pc = X68000_ReadIplVector(1);
				printf("WinX68k_Reset: vectors SP=$%08X PC=$%08X\n",
				       (unsigned)sp, (unsigned)pc);
				if (sp != 0 && pc != 0) {
					C68k_Set_AReg(&C68K, 7, sp);
					C68k_Set_PC(&C68K, pc);
				}
			}
			s_scsi_ipl_needs_restore = 0;
			printf("WinX68k_Reset: SCSI IPL restored OK\n");
		}
		// Keep $E96000 on SASI until the IPL ROM performs its native device
		// select sequence; SCSI_InjectBoot() switches to full SCSI mode after
		// the intercept fires.
		Memory_ClearSCSIMode();
		Memory_SetSASIBootMode();
		SRAM[0x18 ^ 1] = 0x80;
		X68000_AppendSCSILog("IPL-ROM-FIRST: SCSI boot, SRAM=$80");
	} else {
		g_scsi_boot_pending = 0;
		SASI_ArmSCSIBootIntercept(0);
		Memory_ClearSCSIMode();
		g_enable_scsi_dev_driver = 0;
		// Use SASI IPL ROM (IPLROM0.DAT) if available.
		// IPLROM0.DAT is a SASI-era ROM that is NOT detected as SCSI IPL.
		// It keeps $FC0000 intact with native SASI boot code, unlike
		// IPLROM.DAT which gets replaced by the SCSI BIOS stub.
		if (SASI_IPLROM_loaded) {
			int ii;
			BYTE tmp;
			memcpy(IPL, SASI_IPLROM, 0x20000);
			memcpy(&IPL[0x20000], SASI_IPLROM, 0x20000);
			for (ii = 0; ii < 0x40000; ii += 2) {
				tmp = IPL[ii];
				IPL[ii] = IPL[ii + 1];
				IPL[ii + 1] = tmp;
			}
			s_scsi_ipl_needs_restore = 1;
			printf("WinX68k_Reset: Using SASI IPL ROM (IPLROM0.DAT) [byte-swapped]\n");
		}
		// Restore SCSIIPL to original minimal SCSI ROM (50d6a5b).
		// SCSI_Init fills SCSIIPL with large synthetic ROM containing
		// device driver code. The IPL ROM stumbles into this during
		// native SASI boot when SCSI_Read returns SCSIIPL data.
		{
			static const BYTE SCSIIMG_ORIG[] = {
				0x00, 0xea, 0x00, 0x34,
				0x00, 0xea, 0x00, 0x36,
				0x00, 0xea, 0x00, 0x4a,
				0x48, 0x75, 0x6d, 0x61,
				0x6e, 0x36, 0x38, 0x6b,
				0x4e, 0x75,
				0x23, 0xfc, 0x00, 0xea, 0x00, 0x4a,
				0x00, 0x00, 0x07, 0xd4,
				0x74, 0xff,
				0x4e, 0x75,
				0x53, 0x43, 0x53, 0x49, 0x45, 0x58,
				0x13, 0xc1, 0x00, 0xe9, 0xf8, 0x00,
				0x4e, 0x75,
			};
			extern BYTE SCSIIPL[0x2000];
			int jj; BYTE t;
			ZeroMemory(SCSIIPL, 0x2000);
			memcpy(&SCSIIPL[0x20], SCSIIMG_ORIG, sizeof(SCSIIMG_ORIG));
			for (jj = 0; jj < 0x2000; jj += 2) {
				t = SCSIIPL[jj]; SCSIIPL[jj] = SCSIIPL[jj+1]; SCSIIPL[jj+1] = t;
			}
		}
		if (Config.HDImage[0][0] != '\0') {
			// Use separate SRAM state for SASI mode (original X68000 ROM).
			// First time: init $FF + 12MB. After that: restore previous SASI state.
			if (!s_sram_sasi_valid) {
				memset(s_sram_sasi, 0xFF, 0x4000);
				// RAM size 12MB (0x00C00000)
				s_sram_sasi[0x09] = 0x00; s_sram_sasi[0x08] = 0xC0;
				s_sram_sasi[0x0B] = 0x00; s_sram_sasi[0x0A] = 0x00;
				// SRAM signature
				s_sram_sasi[0x11] = 0x00; s_sram_sasi[0x10] = 0x01;
				s_sram_sasi[0x13] = 0xED; s_sram_sasi[0x12] = 0x00;
				s_sram_sasi_valid = 1;
				printf("WinX68k_Reset: SASI SRAM initialized ($FF + 12MB)\n");
			} else {
				printf("WinX68k_Reset: SASI SRAM restored (SWITCH.X preserved)\n");
			}
			memcpy(SRAM, s_sram_sasi, 0x4000);
			if ((SRAM[0x18 ^ 1] == 0x80) && !WinX68k_SASIImageHasBootSector()) {
				SRAM[0x18 ^ 1] = 0x00;
				X68000_AppendSCSILog("SASI boot bypass: image has no boot sector, falling back from HDD boot");
			}
		} else {
			printf("WinX68k_Reset: SASI/FDD path, no HDImage\n");
		}
	}
	// FORCE BOOT block removed: IPL ROM runs its full initialization,
	// then SASI_ArmSCSIBootIntercept triggers SCSI_InjectBoot at
	// the right moment (SASI device select).
#endif

//    C68K.ICount = 0;
    m68000_ICountBk = 0;
    ICount = 0;

    DSound_Stop();
    SRAM_VirusCheck();
    //CDROM_Init();
    DSound_Play();

    // add retro log
    p6logd("Restarting PX68K...\n");

    return TRUE;
}


int
WinX68k_Init(void)
{
#define MEM_SIZE 0xc00000
    IPL = (BYTE*)malloc(0x40000);
    MEM = (BYTE*)malloc(MEM_SIZE);
    FONT = (BYTE*)malloc(0xc0000);

    if (MEM)
        ZeroMemory(MEM, MEM_SIZE);

    if (MEM && FONT && IPL) {
          m68000_init();
        return TRUE;
    } else
        return FALSE;
}

void
WinX68k_Cleanup(void)
{

    if (IPL) {
        free(IPL);
        IPL = NULL;
    }
    if (MEM) {
        free(MEM);
        MEM = NULL;
    }
    if (FONT) {
        free(FONT);
        FONT = NULL;
    }
}

#define CLOCK_SLICE 1500
// -----------------------------------------------------------------------------------
//  �����Τᤤ��롼��
// -----------------------------------------------------------------------------------
void WinX68k_Exec(const long clockMHz, const long vsync)
{
    //char *test = NULL;
    int clk_total, clkdiv, usedclk, hsync, clk_next, clk_count, clk_line=0;
    int KeyIntCnt = 0, MouseIntCnt = 0;
    DWORD t_start = timeGetTime(), t_end;

    // Minimal debug for now
    (void)clockMHz; // suppress unused warning

    if ( Config.FrameRate != 7 ) {
        DispFrame = (DispFrame+1)%Config.FrameRate;
    } else {                // Auto Frame Skip
        if ( FrameSkipQueue ) {
            if ( FrameSkipCount>15 ) {
                FrameSkipCount = 0;
                FrameSkipQueue++;
                DispFrame = 0;
            } else {
                FrameSkipCount++;
                FrameSkipQueue--;
                DispFrame = 1;
            }
        } else {
            FrameSkipCount = 0;
            DispFrame = 0;
        }
    }

    vline = 0;
    clk_count = -ICount;
    clk_total = (CRTC_Regs[0x29] & 0x10) ? VSYNC_HIGH : VSYNC_NORM;
#if 0 // GOROman
    if (Config.XVIMode == 1) {
        clk_total = (clk_total*16)/10;
        clkdiv = 16;
        } else if (Config.XVIMode == 2) {
            clk_total = (clk_total*24)/10;
            clkdiv = 24;
        } else if (Config.XVIMode == 3) {
            clkdiv = 250;
            clk_total = (clk_total*clkdiv)/10;
    } else {
        clkdiv = 10;
    }
#else
    // The C68K core measures cycles in 1/5 MHz units, so scale
    // the requested clock to match actual X68000 MHz settings.
    clkdiv = (DWORD)(clockMHz * 5);
    clk_total = (clk_total * clkdiv) / 10;
#endif
    ICount += clk_total;
    clk_next = (clk_total/VLINE_TOTAL);
    hsync = 1;
    do {
        int m, n = (ICount > CLOCK_SLICE) ? CLOCK_SLICE : ICount;
//        C68K.ICount = m68000_ICountBk = 0;            // ������ȯ������Ϳ���Ƥ����ʤ��ȥ����CARAT��

        if ( hsync ) {
            hsync = 0;
            clk_line = 0;
            MFP_Int(0);
            if ( (vline>=CRTC_VSTART)&&(vline<CRTC_VEND) )
                VLINE = ((vline-CRTC_VSTART)*CRTC_VStep)/2;
            else
                VLINE = (DWORD)-1;
            if ( (!(MFP[MFP_AER]&0x40))&&(vline==CRTC_IntLine) )
                MFP_Int(1);
            if ( MFP[MFP_AER]&0x10 ) {
                if ( vline==CRTC_VSTART )
                    MFP_Int(9);
            } else {
                if ( CRTC_VEND>=VLINE_TOTAL ) {
                    if ( (long)vline==(CRTC_VEND-VLINE_TOTAL) ) MFP_Int(9);        // ���������ƥ��󥰥���Ȥ���TOTAL<VEND��
                } else {
                    if ( (long)vline==(VLINE_TOTAL-1) ) MFP_Int(9);            // ���쥤�������饤�ޡ��ϥ���Ǥʤ��ȥ��ᡩ
                }
            }
        }

#ifdef WIN68DEBUG
        if (traceflag/*&&fdctrace*/)
        {
            FILE *fp;
            static DWORD oldpc;
            int i;
            char buf[200];
            fp=fopen("_trace68.txt", "a");
            for (i=0; i<HSYNC_CLK; i++)
            {
///                m68k_disassemble(buf, C68k_Get_Reg(&C68K, C68K_PC));
//                if (MEM[0xa84c0]) /**test=1; */tracing=1000;
//                if (regs.pc==0x9d2a) tracing=5000;
//                if ((regs.pc>=0x2000)&&((regs.pc<=0x8e0e0))) tracing=50000;
//                if (regs.pc<0x10000) tracing=1;
//                if ( (regs.pc&1) )
//                fp=fopen("_trace68.txt", "a");
//                if ( (regs.pc==0x7176) /*&& (Memory_ReadW(oldpc)==0xff1a)*/ ) tracing=100;
//                if ( (/*((regs.pc>=0x27000) && (regs.pc<=0x29000))||*/((regs.pc>=0x27000) && (regs.pc<=0x29000))) && (oldpc!=regs.pc))
                if (/*fdctrace&&*/(oldpc != C68k_Get_Reg(&C68K, C68K_PC)))
                {
//                    //tracing--;
                  printf( "D0:%08X D1:%08X D2:%08X D3:%08X D4:%08X D5:%08X D6:%08X D7:%08X CR:%04X\n", C68K.D[0], C68K.D[1], C68K.D[2], C68K.D[3], C68K.D[4], C68K.D[5], C68K.D[6], C68K.D[7], 0/* xxx �Ȥꤢ����0 C68K.ccr */);
                  printf( "A0:%08X A1:%08X A2:%08X A3:%08X A4:%08X A5:%08X A6:%08X A7:%08X SR:%04X\n", C68K.A[0], C68K.A[1], C68K.A[2], C68K.A[3], C68K.A[4], C68K.A[5], C68K.A[6], C68K.A[7], C68k_Get_Reg(&C68K, C68K_SR) >> 8/* regs.sr_high*/);
                    printf( "<%04X> (%08X ->) %08X : \n", Memory_ReadW(C68k_Get_Reg(&C68K, C68K_PC)), oldpc, C68k_Get_Reg(&C68K, C68K_PC));
                }
                oldpc = C68k_Get_Reg(&C68K, C68K_PC);
                C68K.ICount = 1;
                C68k_Exec(&C68K, C68K.ICount);
            }
            fclose(fp);
            usedclk = clk_line = HSYNC_CLK;
            clk_count = clk_next;
        }
        else
#endif
                    {
            //            C68K.ICount = n;
            //            C68k_Exec(&C68K, C68K.ICount);
            #if defined (HAVE_CYCLONE)
                        m68000_execute(n);
#elif defined (HAVE_C68K)
                        C68k_Exec(&C68K, n);
                        if (SCSI_HasDeferredBoot()) {
                            SCSI_CommitDeferredBoot();
                        }
#endif /* HAVE_C68K */
                        m = (n-m68000_ICountBk);
            //            m = (n-C68K.ICount-m68000_ICountBk);            // clockspeed progress
                        ClkUsed += m*10;
                        usedclk = ClkUsed/clkdiv;
                        clk_line += usedclk;
                        ClkUsed -= usedclk*clkdiv;
                        ICount -= m;
                        clk_count += m;
#if defined(HAVE_C68K)
	                        // Lightweight SCSI boot checks (IPL-ROM-first architecture)
	                        if (g_scsi_boot_pending) {
                            // Device driver chain linking
                            if (g_enable_scsi_dev_driver && !SCSI_IsDeviceLinked()) {
                                SCSI_LinkDeviceDriver();
                            }
                            // IOCS[$F5] safety pin: ensure SCSI IOCS handler stays patched
                            DWORD f5 = Memory_ReadD(0x7D4) & 0x00FFFFFFU;
                            if (f5 != 0 && f5 != (SCSI_SYNTH_IOCS_ENTRY & 0x00FFFFFFU)) {
                                Memory_WriteD(0x7D4, SCSI_SYNTH_IOCS_ENTRY);
                            }
                        }
#endif
            //            C68K.ICount = m68000_ICountBk = 0;
                    }

        MFP_Timer(usedclk);
        RTC_Timer(usedclk);
        DMA_Exec(0);
        DMA_Exec(1);
        DMA_Exec(2);

        if ( clk_count>=clk_next ) {
            //OPM_RomeoOut(Config.BufferSize*5);
            MIDI_DelayOut((Config.MIDIAutoDelay)?(Config.BufferSize*5):Config.MIDIDelay);
            MFP_TimerA();
            if ( (MFP[MFP_AER]&0x40)&&(vline==CRTC_IntLine) )
                MFP_Int(1);
            if ( (!DispFrame)&&(vline>=CRTC_VSTART)&&(vline<CRTC_VEND) ) {
                if ( CRTC_VStep==1 ) {                // HighReso 256dot��2���ɤߡ�
                    if ( vline%2 )
                        WinDraw_DrawLine();
                } else if ( CRTC_VStep==4 ) {        // LowReso 512dot
                    WinDraw_DrawLine();                // 1��������2�������ʥ��󥿡��졼����
                    VLINE++;
                    WinDraw_DrawLine();
                } else {                            // High 512dot / Low 256dot
                    WinDraw_DrawLine();
                }
            }

            ADPCM_PreUpdate(clk_line);
            OPM_Timer(clk_line);
            MIDI_Timer(clk_line);

            KeyIntCnt++;
            if ( KeyIntCnt>(VLINE_TOTAL/4) ) {
                KeyIntCnt = 0;
                Keyboard_Int();
            }
            MouseIntCnt++;
            if ( MouseIntCnt>(VLINE_TOTAL/8) ) {  // 修正: 元の頻度に戻す
                MouseIntCnt = 0;
                SCC_IntCheck();
            }
            DSound_Send0(clk_line);

            vline++;
            clk_next  = (clk_total*(vline+1))/VLINE_TOTAL;
            hsync = 1;
        }
    } while ( vline<VLINE_TOTAL );

    if ( CRTC_Mode&2 ) {        // FastClr�ӥåȤ�Ĵ����PITAPAT��
        if ( CRTC_FastClr ) {    // FastClr=1 ��� CRTC_Mode&2 �ʤ� ��λ
            CRTC_FastClr--;
            if ( !CRTC_FastClr )
                CRTC_Mode &= 0xfd;
        } else {                // FastClr����
            if ( CRTC_Regs[0x29]&0x10 )
                CRTC_FastClr = 1;
            else
                CRTC_FastClr = 2;
            TVRAM_SetAllDirty();
            GVRAM_FastClear();
        }
    }

    Joystick_Update();
    FDD_SetFDInt();
/*@GOROman
*/
    TimerICount += clk_total;
    t_end = timeGetTime();
    const int dt = (int)(t_end-t_start);
    if (( dt>((CRTC_Regs[0x29]&0x10)?14:16) ) && ( vsync == 1 )) {
        FrameSkipQueue += ((t_end-t_start)/((CRTC_Regs[0x29]&0x10)?14:16))+1;
        if ( FrameSkipQueue>100 )
            FrameSkipQueue = 100;
    }
    if ( FrameSkipQueue != 0 ) {
        // printf("FrameSkipQueue:%d\n", FrameSkipQueue);
    }
}

//
// main
//






int original_main(int argc, const char *argv[], const long samplingrate )
{
#ifdef TARGET_IOS
    extern const unsigned char X68000_for_iOSVersionString[];
    extern const double X68000_for_iOSVersionNumber;
    p6logd("PX68K Ver.%s -- %s Build:%d\n", PX68KVERSTR,X68000_for_iOSVersionString, int(X68000_for_iOSVersionNumber));
#else
    p6logd("PX68K Ver.%s -- macOS Build\n", PX68KVERSTR);
#endif

#ifdef RFMDRV
    struct sockaddr_in dest;

    memset(&dest, 0, sizeof(dest));
    dest.sin_port = htons(2151);
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = inet_addr("127.0.0.1");

    rfd_sock = socket(AF_INET, SOCK_STREAM, 0);
    connect (rfd_sock, (struct sockaddr *)&dest, sizeof(dest));
#endif

//    if (set_modulepath(winx68k_dir, sizeof(winx68k_dir)))
//        return 1;

    dosio_init();
    file_setcd(const_cast<char*>("./"));
    LoadConfig();
	
	Config.SampleRate = (int)samplingrate;

    StatBar_Show(Config.WindowFDDStat);
    WinDraw_ChangeSize();
    WinDraw_ChangeMode(FALSE);

//    WinUI_Init();
    WinDraw_StartupScreen();

    if (!WinX68k_Init()) {
        WinX68k_Cleanup();
        WinDraw_Cleanup();
        return 1;
    }

    if (!WinX68k_LoadROMs()) {
        WinX68k_Cleanup();
        WinDraw_Cleanup();
        exit (1);
    }

    Keyboard_Init(); //WinDraw_Init()���˰�ư

    if (!WinDraw_Init()) {
        WinDraw_Cleanup();
        Error("Error: Can't init screen.\n");
        return 1;
    }

    ADPCM_Init(Config.SampleRate);
    OPM_Init(4000000, Config.SampleRate);
    
    FDD_Init();
    SysPort_Init();
    Mouse_Init();
    Joystick_Init();
    SRAM_Init();
    SRAM_Cleanup(); //@


    WinX68k_Reset();
    Timer_Init();

    MIDI_Init();
    MIDI_SetMimpiMap(Config.ToneMapFile);    // ��������ե��������ȿ��
    MIDI_EnableMimpiDef(Config.ToneMap);

    DSound_Init(Config.SampleRate, Config.BufferSize);

    ADPCM_SetVolume((BYTE)Config.PCM_VOL);
    OPM_SetVolume((BYTE)Config.OPM_VOL);
    DSound_Play();

#ifdef TARGET_IOS
#else
    // command line ������ꤷ�����
    switch (argc) {
    case 3:
        strcpy(Config.FDDImage[1], argv[2]);
    case 2:
        strcpy(Config.FDDImage[0], argv[1]);
        break;
    }

    // printf("FD:%s\n",Config.FDDImage[0] );

    FDD_SetFD(0, Config.FDDImage[0], 0);
    FDD_SetFD(1, Config.FDDImage[1], 0);
#endif
    //SDL_StartTextInput();
    return 0;
}


void Update(const long clockMHz, const int vsync ) {

	if ((Config.NoWaitMode || Timer_GetCount()) || vsync == 0) {
        WinX68k_Exec(clockMHz, vsync);
    }

 }



void Finalize() {
        Memory_WriteB(0xe8e00d, 0x31);    // SRAM�񤭹��ߵ���
        Memory_WriteD(0xed0040, Memory_ReadD(0xed0040)+1); // �ѻ���Ư����(min.)
        Memory_WriteD(0xed0044, Memory_ReadD(0xed0044)+1); // �ѻ���ư���

        OPM_Cleanup();

        Joystick_Cleanup();
        SRAM_Cleanup();
        FDD_Cleanup();
        //CDROM_Cleanup();
        MIDI_Cleanup();
        DSound_Cleanup();
        WinX68k_Cleanup();
        WinDraw_Cleanup();
        WinDraw_CleanupScreen();

        SaveConfig();

}


extern "C" {

void X68000_Init( const long samplingrate) {
	const char* arg[] = {
		"X68000",
	};
	
	original_main(1, arg, samplingrate );
}


void X68000_Update( const long clockMHz, const bool vsync  ) {
	Update(clockMHz, vsync);
}


void X68000_Key_Down( unsigned int vkcode ) {
    Keyboard_KeyDown(vkcode);
}
void X68000_Key_Up( unsigned int vkcode ) {
    Keyboard_KeyUp(vkcode);
}
const int X68000_GetScreenWidth()
{
    return TextDotX;
}
const int X68000_GetScreenHeight()
{
    return TextDotY;
}

void X68000_Reset()
{
    WinX68k_Reset();
}

void X68000_Quit(){
    Finalize();
}

void X68000_GetImage( unsigned char* data ) {
    // Draw when frame is not skipped, or when something actually changed.
    if ( !DispFrame || Draw_DrawFlag ) {
        WinDraw_Draw(data);
    } else {
        // printf("DispFrame\n");
    }
}

const int X68000_IsFrameDirty()
{
    return Draw_DrawFlag ? 1 : 0;
}

// Accumulators for sub-pixel mouse deltas routed via X68000_Mouse_Set
static float s_mouseAccX = 0.0f;
static float s_mouseAccY = 0.0f;

// Expose core mouse capture toggle to Swift
void X68000_Mouse_StartCapture(int flag) {
    Mouse_StartCapture(flag);
    if (flag) {
        // Do nothing else here; Swift side already reset before enabling
        s_mouseAccX = 0.0f;
        s_mouseAccY = 0.0f;
    }
}

// Bridge Mouse_Event for movement and button state updates
void X68000_Mouse_Event(int param, float dx, float dy) {
    Mouse_Event(param, dx, dy);
    // Debug trace for VS.X double-click investigation (enable with SCC_MOUSE_TRACE=1)
    static int scc_trace_enabled = -1;
    if (scc_trace_enabled == -1) {
        const char* trace_env = getenv("SCC_MOUSE_TRACE");
        scc_trace_enabled = (trace_env && trace_env[0] == '1') ? 1 : 0;
    }
    if (scc_trace_enabled) {
        printf("[X68000_Mouse_Event] param=%d dx=%.3f dy=%.3f\n", param, dx, dy);
    }
}

// Expose mouse state reset (clears accumulated deltas and last state)
void X68000_Mouse_ResetState(void) {
    Mouse_ResetState();
    // Also clear fractional accumulators to avoid drift after resets
    s_mouseAccX = 0.0f;
    s_mouseAccY = 0.0f;
}

// Control double-click movement suppression
void X68000_Mouse_SetDoubleClickInProgress(int flag) {
    Mouse_SetDoubleClickInProgress(flag);
}

// Set absolute mouse position in X68K memory, with no relative movement
void X68000_Mouse_SetAbsolute(float x, float y) {
    WORD xx = (WORD)x;
    WORD yy = (WORD)y;
    BYTE* mouse = &MEM[0xace];
    *mouse++ = ((BYTE*)&xx)[0];
    *mouse++ = ((BYTE*)&xx)[1];
    *mouse++ = ((BYTE*)&yy)[0];
    *mouse++ = ((BYTE*)&yy)[1];
    // Do not touch MouseX/MouseY here; leave relative queue untouched
    // Verbose trace disabled by default to reduce log noise
    // printf("[winx68k] SetAbsolute x=%d y=%d -> MEM[0xACE..]=(%02X %02X %02X %02X)\n",
    //        (int)x, (int)y,
    //        MEM[0xace], MEM[0xacf], MEM[0xad0], MEM[0xad1]);
}

void FDD_SetFD(int drive, char* filename, int readonly);

BYTE* s_disk_image_buffer[5] = {0};    // Dynamic allocation up to 80MB
long s_disk_image_buffer_size[5] = {0};

BYTE* X68000_GetDiskImageBufferPointer( const long drive, const long size ){
	if ( s_disk_image_buffer[drive] != NULL ){
		free( s_disk_image_buffer[drive] );
		s_disk_image_buffer[drive] = NULL;
		s_disk_image_buffer_size[drive] = 0;
	}

	s_disk_image_buffer[drive] = (BYTE*)malloc( size );
	s_disk_image_buffer_size[drive] = size;
    return s_disk_image_buffer[drive];
}
void X68000_LoadFDD( const long drive, const char* filename )
{
    printf("X68000_LoadFDD( %ld, \"%s\" )\n", drive, filename);
    
    // Update Config.FDDImage to track the loaded filename
    if (drive >= 0 && drive < 2) {
        strncpy(Config.FDDImage[drive], filename, MAX_PATH - 1);
        Config.FDDImage[drive][MAX_PATH - 1] = '\0';  // Ensure null termination
        printf("Config.FDDImage[%ld] updated to: '%s'\n", drive, Config.FDDImage[drive]);
    }
    
    FDD_SetFD((int)drive, (char*)filename, 0);
}

void X68000_EjectFDD( const long drive )
{
    printf("X68000_EjectFDD( %ld )\n", drive);
    
    // Clear Config.FDDImage when disk is ejected
    if (drive >= 0 && drive < 2) {
        Config.FDDImage[drive][0] = '\0';
    }
    
    FDD_EjectFD((int)drive);
}

const int X68000_IsFDDReady( const long drive )
{
    return FDD_IsReady((int)drive);
}

const char* X68000_GetFDDFilename( const long drive )
{
    if (drive >= 0 && drive < 2) {
        printf("X68000_GetFDDFilename(%ld) returning: '%s'\n", drive, Config.FDDImage[drive]);
        return Config.FDDImage[drive];
    }
    printf("X68000_GetFDDFilename(%ld) - invalid drive, returning empty string\n", drive);
    return "";
}
/*
BYTE* X68000_GetHDDImageBufferPointer( const long size ){
	if ( s_disk_image_buffer[4] != NULL ){
		free( s_disk_image_buffer[4] );
		s_disk_image_buffer[4] = NULL;
	}
	s_disk_image_buffer[4] = (BYTE*)malloc( size );
	return s_disk_image_buffer[4];
}
*/
void X68000_LoadHDD( const char* filename )
{
	printf("X68000_LoadHDD( \"%s\" )\n", filename);

	// Set HDD image path for all SASI device indices
	// SASI uses Config.HDImage[SASI_Device*2+SASI_Unit] for access
	// Human68k may use different SASI IDs, so set all indices
	for (int i = 0; i < 16; i++) {
		strncpy(Config.HDImage[i], filename, MAX_PATH);
	}

	// SRAM boot device is managed by SRAM.DAT / SRAM_Init.
	// Do NOT force $80 here - respect the user's SWITCH.X settings.
	{
		char dbg2[256];
		snprintf(dbg2, sizeof(dbg2), "LOADHDD SRAM[18]=$%02X file='%.60s' buf=%p size=%ld",
			(unsigned)SRAM[0x18 ^ 1],
			filename, s_disk_image_buffer[4], s_disk_image_buffer_size[4]);
		DebugLog(dbg2);
	}

	// Get HDD image size from memory buffer (set by Swift side)
	// This avoids file access issues in sandboxed environment
	if (s_disk_image_buffer[4] != NULL && s_disk_image_buffer_size[4] > 0) {
		DWORD size = (DWORD)s_disk_image_buffer_size[4];

		// Report size to SASI for all drive indices
		for (int i = 0; i < 5; i++) {
			SASI_SetImageSize(i, size);
		}

		printf("HDD image size: %ld bytes (%ld MB) - from memory buffer\n",
		       s_disk_image_buffer_size[4], s_disk_image_buffer_size[4] / (1024*1024));
	} else {
		printf("Warning: HDD buffer not initialized for: %s\n", filename);
	}
}

void X68000_EjectHDD()
{
	printf("X68000_EjectHDD()\n");
	// Clear all SASI device indices
	for (int i = 0; i < 16; i++) {
		Config.HDImage[i][0] = '\0';
	}
}

const int X68000_IsHDDReady()
{
	return (Config.HDImage[0][0] != '\0') ? 1 : 0;
}

const char* X68000_GetHDDFilename()
{
	return Config.HDImage[0];
}

void X68000_SaveHDD()
{
	// Save memory buffer to file
	if (Config.HDImage[0][0] == '\0') {
		printf("X68000_SaveHDD: No HDD image path set\n");
		return;
	}

	if (s_disk_image_buffer[4] == NULL || s_disk_image_buffer_size[4] <= 0) {
		printf("X68000_SaveHDD: No HDD data in buffer\n");
		return;
	}

	FILE* fp = fopen(Config.HDImage[0], "wb");
	if (!fp) {
		printf("X68000_SaveHDD: Failed to open file for writing: %s\n", Config.HDImage[0]);
		return;
	}

	size_t written = fwrite(s_disk_image_buffer[4], 1, s_disk_image_buffer_size[4], fp);
	fclose(fp);

	if (written == (size_t)s_disk_image_buffer_size[4]) {
		printf("X68000_SaveHDD: Saved %ld bytes to %s\n", s_disk_image_buffer_size[4], Config.HDImage[0]);
		SASI_ClearDirtyFlag(0);
	} else {
		printf("X68000_SaveHDD: Write error - only %zu of %ld bytes written\n", written, s_disk_image_buffer_size[4]);
	}
}

const int X68000_IsHDDDirty()
{
	// Return dirty flag status for primary HDD (drive 0)
	return SASI_IsDirty(0) ? 1 : 0;
}

int X68000_GetStorageBusMode()
{
	return g_storage_bus_mode;
}

int X68000_IsSCSIBootPending(void)
{
	return g_scsi_boot_pending;
}

void X68000_SetStorageBusMode(int mode)
{
	if (g_storage_bus_mode == 0 && mode == 1) {
		WinX68k_SaveSASI_SRAM();
	}

	g_storage_bus_mode = (mode == 1) ? 1 : 0;
	if (g_storage_bus_mode == 0) {
		// Restore the native SASI register map immediately so a prior SCSI
		// session does not leave $E96000 routed to the SPC emulation.
		// Also restore the pre-SCSI SRAM view before any Swift-side saveSRAM().
		Memory_ClearSCSIMode();
		WinX68k_RestoreSASI_SRAM();
	}
	// Note: SRAM boot device ($ED0018) is set by WinX68k_Reset for SCSI mode.
	// For SASI/FDD, we don't modify SRAM to respect user's SWITCH.X setting.
}

int X68000_SCSI_IsMounted(int host, int id)
{
	if (host == 0 && id == 0) {
		return g_scsi0_mounted;
	}
	return 0;
}

const char* X68000_SCSI_GetImagePath(int host, int id)
{
	if (host == 0 && id == 0 && g_scsi0_mounted) {
		return g_scsi0_path;
	}
	return NULL;
}

int X68000_SCSI_Mount(int host, int id, const char* path, int flags)
{
	(void)flags;
	if (host != 0 || id != 0 || path == NULL || path[0] == '\0') {
		return 0;
	}

	if (s_disk_image_buffer[4] == NULL || s_disk_image_buffer_size[4] <= 0) {
		printf("X68000_SCSI_Mount: HDD buffer not initialized for %s\n", path);
		return 0;
	}

	g_storage_bus_mode = 1;
	Memory_SetSCSIMode();
	SCSI_InvalidateTransferCache();
	g_scsi0_mounted = 1;
	strncpy(g_scsi0_path, path, MAX_PATH - 1);
	g_scsi0_path[MAX_PATH - 1] = '\0';
	// Keep SASI metadata in sync as a compatibility fallback.
	// Some Human68k builds still probe SASI path during late init even
	// when SCSI boot was used; clearing this state breaks COMMAND load.
	X68000_LoadHDD(path);
	X68000_AppendSCSILog("SCSI_MOUNT OK");

	return 1;
}

int X68000_SCSI_Eject(int host, int id)
{
	if (host != 0 || id != 0) {
		return 0;
	}

	g_scsi0_mounted = 0;
	g_scsi0_path[0] = '\0';
	SCSI_InvalidateTransferCache();
	X68000_EjectHDD();
	return 1;
}

unsigned char* X68000_GetSRAMPointer()
{
    return &SRAM[0];
}

unsigned char* X68000_GetCGROMPointer()
{
    return &CGROM_DAT[0];
}

unsigned char* X68000_GetIPLROMPointer()
{
    return &IPLROM_DAT[0];
}

unsigned char* X68000_GetSCSIIPLPointer()
{
    return &SCSIROM_DAT[0];
}

unsigned char* X68000_GetSASI_IPLROMPointer()
{
    SASI_IPLROM_loaded = 1;
    return &SASI_IPLROM[0];
}

/*
 
 */
void X68000_Mouse_SetDirect( float x, float y, const long button )
{
//    MouseX = (int)x;
//    MouseY = (int)y;

        MouseSt = button;
    
    WORD xx, yy;
    signed short MovementRange_MinX;
    signed short MovementRange_MaxX;
    signed short MovementAmountX;
    signed short HotspotX;
    signed short HotspotY;
    *(((BYTE*) &MovementRange_MinX)+0) = MEM[0xa9a];
    *(((BYTE*) &MovementRange_MinX)+1) = MEM[0xa9b];
    *(((BYTE*) &MovementRange_MaxX)+0) = MEM[0xa9e];
    *(((BYTE*) &MovementRange_MaxX)+1) = MEM[0xa9f];
    *(((BYTE*) &MovementAmountX)+0) = MEM[0xaca];
    *(((BYTE*) &MovementAmountX)+1) = MEM[0xacb];
    *(((BYTE*) &HotspotX)+0) = MEM[0xad6];
    *(((BYTE*) &HotspotX)+1) = MEM[0xad7];
    *(((BYTE*) &HotspotY)+0) = MEM[0xad8];
    *(((BYTE*) &HotspotY)+1) = MEM[0xad9];

    *(((BYTE*) &xx)+0) = MEM[0xace];
    *(((BYTE*) &xx)+1) = MEM[0xacf];
    *(((BYTE*) &yy)+0) = MEM[0xad0];
    *(((BYTE*) &yy)+1) = MEM[0xad1];
  #if 1
    int nx = (int)xx;
    int ny = (int)yy;
 
    int tx = (int)(x);
    int ty = (int)(y);
    
 
    int dx = tx - nx;
    int dy = ty - ny;
    // Lower deadzone for fine-grained apps (e.g., VisualShell)
    if ( abs(dx) < 1 ) dx = 0;
    if ( abs(dy) < 1 ) dy = 0;

	const int max = 15;
//	printf("nx:%3d ny:%3d dx:%3d dy:%3d\n", nx, ny, dx, dy);

	if ( dx == 0 ){
	} else if ( dx < -max ) {
		dx = -max;
	} else if ( dx > +max ) {
		dx = +max;
	}

    if ( dy == 0 ) {
	} else if ( dy < -max ) {
		dy = -max;
	} else if ( dy > +max ) {
		dy = +max;
	}
    // Do not invert here; Swift side uses scene coordinates consistently
//    printf(" nx:%3d ny:%3d tx:%3d ty:%3d dx:%3d dy:%3d\n", nx, ny, tx, ty, dx, dy );


    MouseX = dx;
    MouseY = dy;
    // Verbose trace disabled by default to reduce log noise
    // printf("[winx68k] SetDirect target=(%d,%d) current=(%d,%d) -> d=(%d,%d) btn=0x%02lX\n",
    //        tx, ty, nx, ny, dx, dy, button & 0xff);
#else
    xx = (WORD)x;
    yy = (WORD)y;
   
    BYTE* mouse = &MEM[0xace];
	*mouse++ = ((BYTE*) &xx)[0];
	*mouse++ = ((BYTE*) &xx)[1];
	*mouse++ = ((BYTE*) &yy)[0];
	*mouse++ = ((BYTE*) &yy)[1];
	MouseX = 1;
	MouseY = 1;
#endif
    

}

void X68000_Mouse_Set( float x, float y, const long button )
{
    // Accumulate fractional deltas so tiny movements aren't rounded away
    s_mouseAccX += x;
    s_mouseAccY += y;

    // Convert accumulated movement to integer steps
    int ix = (int)lrintf(s_mouseAccX);
    int iy = (int)lrintf(s_mouseAccY);

    // Clamp to SCC 8-bit signed range
    if (ix > 127) ix = 127; else if (ix < -128) ix = -128;
    if (iy > 127) iy = 127; else if (iy < -128) iy = -128;

    MouseX = (signed char)ix;
    MouseY = (signed char)iy;
    MouseSt = button;

    // Remove the integer portion we just emitted, keep leftover fraction
    s_mouseAccX -= (float)ix;
    s_mouseAccY -= (float)iy;

    /*
    BYTE* mouse = &MEM[0xace];
    WORD xx, yy;
    *(((BYTE*) &xx)+0) = MEM[0xace];
    *(((BYTE*) &xx)+1) = MEM[0xacf];
    *(((BYTE*) &yy)+0) = MEM[0xad0];
    *(((BYTE*) &yy)+1) = MEM[0xad1];
    ++xx;
        if ( xx >= 512 )  xx = 0;

    *mouse++ = ((BYTE*) &xx)[0];
    *mouse++ = ((BYTE*) &xx)[1];
    *mouse++ = ((BYTE*) &yy)[0];
    *mouse++ = ((BYTE*) &yy)[1];
    
    
    printf("%3d %3d\n", xx, yy );
*/
}

// Keep the SASI-side SRAM snapshot in sync with the live SRAM contents.
// This preserves SWITCH.X boot settings when returning from a SCSI session.
void WinX68k_UpdateSASIRamSize(void) {
    memcpy(s_sram_sasi, SRAM, 0x4000);
    s_sram_sasi_valid = 1;
}

// ---------------------------------------------------------------------------
// Machine Monitor API
// ---------------------------------------------------------------------------

#ifdef __cplusplus
}
static std::atomic<int> g_monitor_paused(0);
extern "C" {
#else
static int g_monitor_paused = 0;
#endif

void X68000_Monitor_SetPaused(int paused) {
#ifdef __cplusplus
    g_monitor_paused.store(paused ? 1 : 0, std::memory_order_relaxed);
#else
    g_monitor_paused = paused ? 1 : 0;
#endif
}

int X68000_Monitor_IsPaused(void) {
#ifdef __cplusplus
    return g_monitor_paused.load(std::memory_order_relaxed);
#else
    return g_monitor_paused;
#endif
}

unsigned char X68000_Monitor_ReadB(unsigned int addr) {
    return cpu_readmem24(addr & 0x00ffffffu);
}

unsigned short X68000_Monitor_ReadW(unsigned int addr) {
    return cpu_readmem24_word(addr & 0x00ffffffu);
}

unsigned int X68000_Monitor_ReadD(unsigned int addr) {
    return cpu_readmem24_dword(addr & 0x00ffffffu);
}

void X68000_Monitor_WriteB(unsigned int addr, unsigned char val) {
    cpu_writemem24(addr & 0x00ffffffu, val);
}

void X68000_Monitor_WriteW(unsigned int addr, unsigned short val) {
    cpu_writemem24_word(addr & 0x00ffffffu, val);
}

void X68000_Monitor_WriteD(unsigned int addr, unsigned int val) {
    cpu_writemem24_dword(addr & 0x00ffffffu, val);
}

typedef struct {
    unsigned int d[8];
    unsigned int a[8];
    unsigned int pc;
    unsigned int sr;
} X68000MonitorCPUState;

void X68000_Monitor_GetCPUState(X68000MonitorCPUState* s) {
    if (!s) return;
    for (int i = 0; i < 8; i++) s->d[i] = m68000_get_reg(M68K_D0 + i);
    for (int i = 0; i < 8; i++) s->a[i] = m68000_get_reg(M68K_A0 + i);
    s->pc = m68000_get_reg(M68K_PC);
    s->sr = m68000_get_reg(M68K_SR);
}

void X68000_Monitor_SetDReg(int n, unsigned int val) {
    if (n < 0 || n > 7) return;
    m68000_set_reg(M68K_D0 + n, val);
}
void X68000_Monitor_SetAReg(int n, unsigned int val) {
    if (n < 0 || n > 7) return;
    m68000_set_reg(M68K_A0 + n, val);
}
void X68000_Monitor_SetPC(unsigned int val)          { m68000_set_reg(M68K_PC, val); }
void X68000_Monitor_SetSR(unsigned int val)          { m68000_set_reg(M68K_SR, val); }

enum {
    X68_MON_HW_ALL = 0,
    X68_MON_HW_IRQ = 1,
    X68_MON_HW_MFP = 2,
    X68_MON_HW_DMA = 3,
    X68_MON_HW_FDD = 4,
    X68_MON_HW_FDC = 5,
    X68_MON_HW_CRTC = 6,
    X68_MON_HW_SCSI = 7,
    X68_MON_HW_AUDIO = 8,
    X68_MON_HW_ADPCM = 9,
    X68_MON_HW_INPUT = 10,
    X68_MON_HW_VIDEO = 11,
    X68_MON_HW_MIDI = 12,
    X68_MON_HW_MEM = 13,
    X68_MON_HW_SUMMARY = 14
};

static void monitor_appendf(char** cursor, int* remaining, const char* format, ...)
{
    if (!cursor || !*cursor || !remaining || *remaining <= 0) return;

    va_list args;
    va_start(args, format);
    int written = vsnprintf(*cursor, (size_t)*remaining, format, args);
    va_end(args);

    if (written < 0) return;
    if (written >= *remaining) {
        *cursor += *remaining - 1;
        *remaining = 1;
    } else {
        *cursor += written;
        *remaining -= written;
    }
}

static void monitor_format_irq(char** cursor, int* remaining)
{
    int active = 0;
    monitor_appendf(cursor, remaining, "IRQ pending:");
    for (int i = 1; i < 8; i++) {
        monitor_appendf(cursor, remaining, " %d=%d", i, IRQH_IRQ[i] ? 1 : 0);
        if (IRQH_IRQ[i]) active = i;
    }
    monitor_appendf(cursor, remaining, "  highest=%d\n", active);
}

static void monitor_format_mfp(char** cursor, int* remaining)
{
    monitor_appendf(cursor, remaining,
                    "MFP GPIP=%02X AER=%02X DDR=%02X VR=%02X LastKey=%02X\n",
                    MFP[MFP_GPIP], MFP[MFP_AER], MFP[MFP_DDR], MFP[MFP_VR], LastKey);
    monitor_appendf(cursor, remaining,
                    "MFP IER A/B=%02X/%02X  IPR A/B=%02X/%02X  ISR A/B=%02X/%02X  IMR A/B=%02X/%02X\n",
                    MFP[MFP_IERA], MFP[MFP_IERB],
                    MFP[MFP_IPRA], MFP[MFP_IPRB],
                    MFP[MFP_ISRA], MFP[MFP_ISRB],
                    MFP[MFP_IMRA], MFP[MFP_IMRB]);
    monitor_appendf(cursor, remaining,
                    "MFP TIMER TACR=%02X TBCR=%02X TCDCR=%02X TADR=%02X TBDR=%02X TCDR=%02X TDDR=%02X\n",
                    MFP[MFP_TACR], MFP[MFP_TBCR], MFP[MFP_TCDCR],
                    MFP[MFP_TADR], MFP[MFP_TBDR], MFP[MFP_TCDR], MFP[MFP_TDDR]);
    monitor_appendf(cursor, remaining,
                    "MFP USART SCR=%02X UCR=%02X RSR=%02X TSR=%02X UDR=%02X\n",
                    MFP[MFP_SCR], MFP[MFP_UCR], MFP[MFP_RSR], MFP[MFP_TSR], MFP[MFP_UDR]);
}

static void monitor_format_dma(char** cursor, int* remaining)
{
    for (int ch = 0; ch < 4; ch++) {
        monitor_appendf(cursor, remaining,
                        "DMA%d CSR=%02X CER=%02X CCR=%02X OCR=%02X DCR=%02X SCR=%02X MTC=%04X MAR=%06X DAR=%06X BTC=%04X BAR=%06X NIV=%02X EIV=%02X\n",
                        ch,
                        DMA[ch].CSR, DMA[ch].CER, DMA[ch].CCR, DMA[ch].OCR, DMA[ch].DCR, DMA[ch].SCR,
                        DMA[ch].MTC,
                        (unsigned int)(DMA[ch].MAR & 0x00ffffffu),
                        (unsigned int)(DMA[ch].DAR & 0x00ffffffu),
                        DMA[ch].BTC,
                        (unsigned int)(DMA[ch].BAR & 0x00ffffffu),
                        DMA[ch].NIV, DMA[ch].EIV);
    }
}

static void monitor_format_fdd(char** cursor, int* remaining)
{
    for (int drive = 0; drive < 4; drive++) {
        FDCID id;
        memset(&id, 0, sizeof(id));
        int hasID = FDD_GetCurrentID(drive, &id);
        const char* filename = X68000_GetFDDFilename(drive);
        monitor_appendf(cursor, remaining,
                        "FDD%d ready=%d ro=%d id=%s C/H/R/N=%02X/%02X/%02X/%02X file=%s\n",
                        drive,
                        FDD_IsReady(drive),
                        FDD_IsReadOnly(drive),
                        hasID ? "ok" : "--",
                        id.c, id.h, id.r, id.n,
                        (filename && filename[0]) ? filename : "(none)");
    }
}

static void monitor_format_fdc(char** cursor, int* remaining)
{
    FDCMonitorState state;
    memset(&state, 0, sizeof(state));
    FDC_GetMonitorState(&state);
    monitor_appendf(cursor, remaining,
                    "FDC cmd=%02X drv=%d cyl=%d ready=%d ctrl=%02X wexec=%d st0/st1/st2=%02X/%02X/%02X\n",
                    state.cmd, state.drv, state.cyl, state.ready, state.ctrl, state.wexec,
                    state.st0, state.st1, state.st2);
    monitor_appendf(cursor, remaining,
                    "FDC rdptr=%d wrptr=%d rdnum=%d wrnum=%d bufnum=%d dataReady=%d\n",
                    state.rdptr, state.wrptr, state.rdnum, state.wrnum, state.bufnum, FDC_IsDataReady());
}

static void monitor_format_crtc(char** cursor, int* remaining)
{
    monitor_appendf(cursor, remaining,
                    "CRTC mode=%02X screen=%ux%u h=%u-%u v=%u-%u intLine=%u hsyncClk=%d VLINE=%u vline=%u\n",
                    CRTC_Mode,
                    (unsigned int)TextDotX, (unsigned int)TextDotY,
                    CRTC_HSTART, CRTC_HEND, CRTC_VSTART, CRTC_VEND,
                    CRTC_IntLine, HSYNC_CLK,
                    (unsigned int)VLINE, (unsigned int)vline);
    monitor_appendf(cursor, remaining,
                    "CRTC textScroll=%u,%u graphScroll0=%u,%u graphScroll1=%u,%u graphScroll2=%u,%u graphScroll3=%u,%u\n",
                    (unsigned int)TextScrollX, (unsigned int)TextScrollY,
                    (unsigned int)GrphScrollX[0], (unsigned int)GrphScrollY[0],
                    (unsigned int)GrphScrollX[1], (unsigned int)GrphScrollY[1],
                    (unsigned int)GrphScrollX[2], (unsigned int)GrphScrollY[2],
                    (unsigned int)GrphScrollX[3], (unsigned int)GrphScrollY[3]);
    monitor_appendf(cursor, remaining,
                    "CRTC regs 00-0F: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X\n",
                    CRTC_Regs[0], CRTC_Regs[1], CRTC_Regs[2], CRTC_Regs[3],
                    CRTC_Regs[4], CRTC_Regs[5], CRTC_Regs[6], CRTC_Regs[7],
                    CRTC_Regs[8], CRTC_Regs[9], CRTC_Regs[10], CRTC_Regs[11],
                    CRTC_Regs[12], CRTC_Regs[13], CRTC_Regs[14], CRTC_Regs[15]);
    monitor_appendf(cursor, remaining,
                    "CRTC regs 20-2F: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X\n",
                    CRTC_Regs[0x20], CRTC_Regs[0x21], CRTC_Regs[0x22], CRTC_Regs[0x23],
                    CRTC_Regs[0x24], CRTC_Regs[0x25], CRTC_Regs[0x26], CRTC_Regs[0x27],
                    CRTC_Regs[0x28], CRTC_Regs[0x29], CRTC_Regs[0x2a], CRTC_Regs[0x2b],
                    CRTC_Regs[0x2c], CRTC_Regs[0x2d], CRTC_Regs[0x2e], CRTC_Regs[0x2f]);
}

static void monitor_format_scsi(char** cursor, int* remaining)
{
    const char* path = X68000_SCSI_GetImagePath(0, 0);
    monitor_appendf(cursor, remaining,
                    "SCSI busMode=%d mounted=%d rom=%d bootPending=%d deferredBoot=%d devLinked=%d bootActivity=%d driverActivity=%d\n",
                    X68000_GetStorageBusMode(),
                    X68000_SCSI_IsMounted(0, 0),
                    SCSI_IsROMPresent(),
                    X68000_IsSCSIBootPending(),
                    SCSI_HasDeferredBoot(),
                    SCSI_IsDeviceLinked(),
                    SCSI_HasBootActivity(),
                    SCSI_HasDriverActivity());
    monitor_appendf(cursor, remaining, "SCSI host0 id0 image=%s\n", (path && path[0]) ? path : "(none)");
    monitor_appendf(cursor, remaining,
                    "SASI ready=%d dirty0=%d dirty1=%d size0=%u size1=%u name0=%s name1=%s\n",
                    SASI_IsReady(),
                    SASI_IsDirty(0), SASI_IsDirty(1),
                    (unsigned int)SASI_GetImageSize(0), (unsigned int)SASI_GetImageSize(1),
                    SASI_Name[0][0] ? SASI_Name[0] : "(none)",
                    SASI_Name[1][0] ? SASI_Name[1] : "(none)");
}

static void monitor_format_audio(char** cursor, int* remaining)
{
    DSoundMonitorState state;
    memset(&state, 0, sizeof(state));
    DSound_GetMonitorState(&state);
    monitor_appendf(cursor, remaining,
                    "AUDIO rate=%luHz direct=%u buffer=%ld data=%ld free=%ld read=%ld write=%ld lastCallback=%u refillCount=%u preCounter=%ld\n",
                    state.ratebase,
                    state.directCallback,
                    state.bufferBytes,
                    state.dataBytes,
                    state.freeBytes,
                    state.readOffset,
                    state.writeOffset,
                    state.lastCallbackBytes,
                    state.refillCount,
                    state.preCounter);
}

static void monitor_format_adpcm(char** cursor, int* remaining)
{
    ADPCMMonitorState state;
    memset(&state, 0, sizeof(state));
    ADPCM_GetMonitorState(&state);
    monitor_appendf(cursor, remaining,
                    "ADPCM playing=%d ready=%d pan=%02X clock=%02X clockRate=%u sampleRate=%u count=%u preCounter=%lld\n",
                    state.playing,
                    ADPCM_IsReady(),
                    state.pan,
                    ADPCM_Clock,
                    (unsigned int)state.clockRate,
                    (unsigned int)state.sampleRate,
                    (unsigned int)state.count,
                    state.preCounter);
    monitor_appendf(cursor, remaining,
                    "ADPCM buffer wr=%ld rd=%ld size=%ld diff=%d dmaReady=%d step=%d out=%d oldL/R=%d/%d volumeShift=%d\n",
                    state.writePtr,
                    state.readPtr,
                    state.bufferSize,
                    state.diffBuffer,
                    state.dmaReady,
                    state.step,
                    state.output,
                    state.oldLeft,
                    state.oldRight,
                    state.volumeShift);
}

static void monitor_format_input(char** cursor, int* remaining)
{
    MouseMonitorState mouseState;
    memset(&mouseState, 0, sizeof(mouseState));
    Mouse_GetMonitorState(&mouseState);

    int keyDepth = (KeyBufWP >= KeyBufRP) ? (KeyBufWP - KeyBufRP) : (KeyBufSize - KeyBufRP + KeyBufWP);
    monitor_appendf(cursor, remaining,
                    "KEYBOARD enable=%02X intFlag=%02X lastKey=%02X bufRP=%u bufWP=%u depth=%d\n",
                    KeyEnable,
                    KeyIntFlag,
                    LastKey,
                    (unsigned int)KeyBufRP,
                    (unsigned int)KeyBufWP,
                    keyDepth);
    monitor_appendf(cursor, remaining,
                    "MOUSE sw=%u stat=%02X dx=%.2f dy=%.2f sccX=%d sccY=%d sccSt=%02X queue=%d sent=%d compat=%d doubleClick=%d acc=%.3f,%.3f memACE=%02X %02X %02X %02X\n",
                    mouseState.sw,
                    mouseState.stat,
                    mouseState.dx,
                    mouseState.dy,
                    (int)mouseState.sccX,
                    (int)mouseState.sccY,
                    mouseState.sccStatus,
                    mouseState.queueCount,
                    mouseState.sentCount,
                    mouseState.compatMode,
                    mouseState.doubleClickInProgress,
                    s_mouseAccX,
                    s_mouseAccY,
                    MEM ? MEM[0xace] : 0,
                    MEM ? MEM[0xacf] : 0,
                    MEM ? MEM[0xad0] : 0,
                    MEM ? MEM[0xad1] : 0);
    monitor_appendf(cursor, remaining,
                    "PPI joyportU=%d joyActive=%u/%u joyPort=%02X/%02X joyState0=%02X/%02X joyState1=%02X/%02X\n",
                    PPI_GetJoyportUMode(),
                    (unsigned int)joy[0],
                    (unsigned int)joy[1],
                    (unsigned int)JoyPortData[0],
                    (unsigned int)JoyPortData[1],
                    (unsigned int)JoyState0[0],
                    (unsigned int)JoyState0[1],
                    (unsigned int)JoyState1[0],
                    (unsigned int)JoyState1[1]);
}

static void monitor_format_video(char** cursor, int* remaining)
{
    int dirtyCount = 0;
    int firstDirty = -1;
    int lastDirty = -1;
    for (int i = 0; i < 1024; i++) {
        if (TextDirtyLine[i]) {
            if (firstDirty < 0) firstDirty = i;
            lastDirty = i;
            dirtyCount++;
        }
    }

    monitor_appendf(cursor, remaining,
                    "VIDEO frameDirty=%d dispFrame=%u configFrameRate=%d textDirty=%d first=%d last=%d fastClr=%u fastClrLine=%u fastClrMask=%04X\n",
                    X68000_IsFrameDirty(),
                    (unsigned int)DispFrame,
                    Config.FrameRate,
                    dirtyCount,
                    firstDirty,
                    lastDirty,
                    CRTC_FastClr,
                    (unsigned int)CRTC_FastClrLine,
                    CRTC_FastClrMask);
    monitor_appendf(cursor, remaining,
                    "VIDEO BG double=%d regs0-11: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                    BG_DoubleBuffer,
                    BG_Regs[0], BG_Regs[1], BG_Regs[2], BG_Regs[3],
                    BG_Regs[4], BG_Regs[5], BG_Regs[6], BG_Regs[7],
                    BG_Regs[8], BG_Regs[9], BG_Regs[10], BG_Regs[11],
                    BG_Regs[12], BG_Regs[13], BG_Regs[14], BG_Regs[15],
                    BG_Regs[16], BG_Regs[17]);
    monitor_appendf(cursor, remaining,
                    "VIDEO BG0=%u,%u BG1=%u,%u HAdjust=%ld VLINEBG=%u BG_VLINE=%ld GrpDouble=%d\n",
                    (unsigned int)BG0ScrollX,
                    (unsigned int)BG0ScrollY,
                    (unsigned int)BG1ScrollX,
                    (unsigned int)BG1ScrollY,
                    BG_HAdjust,
                    (unsigned int)VLINEBG,
                    BG_VLINE,
                    Grp_DoubleBuffer);
}

static void monitor_format_midi(char** cursor, int* remaining)
{
    MIDIMonitorState state;
    memset(&state, 0, sizeof(state));
    MIDI_GetMonitorState(&state);
    monitor_appendf(cursor, remaining,
                    "MIDI enabled=%d output=%d module=%d ctrl=%d pos=%d sysCount=%d excWait=%d playing=%d regHigh=%d\n",
                    state.enabled,
                    state.hasOutput,
                    state.module,
                    state.ctrl,
                    state.pos,
                    state.sysCount,
                    state.exclusiveWait,
                    state.playing,
                    state.regHigh);
    monitor_appendf(cursor, remaining,
                    "MIDI int enable=%02X flag=%02X vect=%02X vectorBase=%02X buffered=%u txFull=%d bufTimer=%ld R05=%02X swiftBuffer=%ld delay=%d/%d count=%d\n",
                    state.intEnable,
                    state.intFlag,
                    state.intVect,
                    state.vector,
                    (unsigned int)state.buffered,
                    state.txFull,
                    state.bufTimer,
                    state.r05,
                    state.swiftBufferSize,
                    state.delayRead,
                    state.delayWrite,
                    state.delayCount);
    monitor_appendf(cursor, remaining,
                    "MIDI timers G=%u/%ld M=%u/%ld\n",
                    (unsigned int)state.gTimerMax,
                    state.gTimerVal,
                    (unsigned int)state.mTimerMax,
                    state.mTimerVal);
}

static void monitor_format_mem(char** cursor, int* remaining)
{
    BYTE sramBoot = SRAM[0x18 ^ 1];
    monitor_appendf(cursor, remaining,
                    "MEM ptr RAM=%p IPL=%p FONT=%p OP_ROM=%p SCSIIPL=%p SCSIMode=%d busErrFlag=%u busErrAdr=%06X byteAccess=%u\n",
                    MEM,
                    IPL,
                    FONT,
                    OP_ROM,
                    SCSIIPL,
                    Memory_IsSCSIModeEnabled(),
                    (unsigned int)BusErrFlag,
                    (unsigned int)(BusErrAdr & 0x00ffffffu),
                    (unsigned int)MemByteAccess);
    monitor_appendf(cursor, remaining,
                    "MEM SRAM boot=%02X signature=%02X %02X %02X %02X sasiIPLLoaded=%d sasiSRAMValid=%d scsiIPLRestore=%d\n",
                    sramBoot,
                    SRAM[0x10 ^ 1],
                    SRAM[0x11 ^ 1],
                    SRAM[0x12 ^ 1],
                    SRAM[0x13 ^ 1],
                    SASI_IPLROM_loaded,
                    s_sram_sasi_valid,
                    s_scsi_ipl_needs_restore);
    monitor_appendf(cursor, remaining,
                    "MEM exception vectors 0000-001F: %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X\n",
                    MEM ? MEM[0x00 ^ 1] : 0, MEM ? MEM[0x01 ^ 1] : 0, MEM ? MEM[0x02 ^ 1] : 0, MEM ? MEM[0x03 ^ 1] : 0,
                    MEM ? MEM[0x04 ^ 1] : 0, MEM ? MEM[0x05 ^ 1] : 0, MEM ? MEM[0x06 ^ 1] : 0, MEM ? MEM[0x07 ^ 1] : 0,
                    MEM ? MEM[0x08 ^ 1] : 0, MEM ? MEM[0x09 ^ 1] : 0, MEM ? MEM[0x0a ^ 1] : 0, MEM ? MEM[0x0b ^ 1] : 0,
                    MEM ? MEM[0x0c ^ 1] : 0, MEM ? MEM[0x0d ^ 1] : 0, MEM ? MEM[0x0e ^ 1] : 0, MEM ? MEM[0x0f ^ 1] : 0,
                    MEM ? MEM[0x10 ^ 1] : 0, MEM ? MEM[0x11 ^ 1] : 0, MEM ? MEM[0x12 ^ 1] : 0, MEM ? MEM[0x13 ^ 1] : 0,
                    MEM ? MEM[0x14 ^ 1] : 0, MEM ? MEM[0x15 ^ 1] : 0, MEM ? MEM[0x16 ^ 1] : 0, MEM ? MEM[0x17 ^ 1] : 0,
                    MEM ? MEM[0x18 ^ 1] : 0, MEM ? MEM[0x19 ^ 1] : 0, MEM ? MEM[0x1a ^ 1] : 0, MEM ? MEM[0x1b ^ 1] : 0,
                    MEM ? MEM[0x1c ^ 1] : 0, MEM ? MEM[0x1d ^ 1] : 0, MEM ? MEM[0x1e ^ 1] : 0, MEM ? MEM[0x1f ^ 1] : 0);
}

static void monitor_format_summary(char** cursor, int* remaining)
{
    int activeIrq = 0;
    for (int i = 1; i < 8; i++) {
        if (IRQH_IRQ[i]) activeIrq = i;
    }

    int keyDepth = (KeyBufWP >= KeyBufRP) ? (KeyBufWP - KeyBufRP) : (KeyBufSize - KeyBufRP + KeyBufWP);

    MouseMonitorState mouseState;
    memset(&mouseState, 0, sizeof(mouseState));
    Mouse_GetMonitorState(&mouseState);

    DSoundMonitorState audioState;
    memset(&audioState, 0, sizeof(audioState));
    DSound_GetMonitorState(&audioState);

    ADPCMMonitorState adpcmState;
    memset(&adpcmState, 0, sizeof(adpcmState));
    ADPCM_GetMonitorState(&adpcmState);

    MIDIMonitorState midiState;
    memset(&midiState, 0, sizeof(midiState));
    MIDI_GetMonitorState(&midiState);

    monitor_appendf(cursor, remaining,
                    "SUMMARY paused=%d PC=%06X SR=%04X IRQ=%d frameDirty=%d video=%ux%u mode=%02X\n",
                    X68000_Monitor_IsPaused(),
                    (unsigned int)(m68000_get_reg(M68K_PC) & 0x00ffffffu),
                    (unsigned int)(m68000_get_reg(M68K_SR) & 0xffffu),
                    activeIrq,
                    X68000_IsFrameDirty(),
                    (unsigned int)TextDotX,
                    (unsigned int)TextDotY,
                    CRTC_Mode);
    monitor_appendf(cursor, remaining,
                    "SUMMARY audio data=%ld free=%ld lastCallback=%u refillCount=%u adpcmPlaying=%d midiBuffered=%u\n",
                    audioState.dataBytes,
                    audioState.freeBytes,
                    audioState.lastCallbackBytes,
                    audioState.refillCount,
                    adpcmState.playing,
                    (unsigned int)midiState.buffered);
    monitor_appendf(cursor, remaining,
                    "SUMMARY storage fddReady=%d/%d/%d/%d scsiMounted=%d sasiReady=%d busMode=%d\n",
                    FDD_IsReady(0),
                    FDD_IsReady(1),
                    FDD_IsReady(2),
                    FDD_IsReady(3),
                    X68000_SCSI_IsMounted(0, 0),
                    SASI_IsReady(),
                    X68000_GetStorageBusMode());
    monitor_appendf(cursor, remaining,
                    "SUMMARY input lastKey=%02X keyDepth=%d mouseQueue=%d joy=%02X/%02X memBusErr=%u@%06X scsiMode=%d\n",
                    LastKey,
                    keyDepth,
                    mouseState.queueCount,
                    (unsigned int)JoyPortData[0],
                    (unsigned int)JoyPortData[1],
                    (unsigned int)BusErrFlag,
                    (unsigned int)(BusErrAdr & 0x00ffffffu),
                    Memory_IsSCSIModeEnabled());
}

void X68000_Monitor_GetHardwareState(int section, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    char* cursor = out;
    int remaining = outSize;
    out[0] = '\0';

    if (section == X68_MON_HW_SUMMARY) monitor_format_summary(&cursor, &remaining);
    if (section == X68_MON_HW_ALL || section == X68_MON_HW_IRQ) monitor_format_irq(&cursor, &remaining);
    if (section == X68_MON_HW_ALL || section == X68_MON_HW_MFP) monitor_format_mfp(&cursor, &remaining);
    if (section == X68_MON_HW_ALL || section == X68_MON_HW_DMA) monitor_format_dma(&cursor, &remaining);
    if (section == X68_MON_HW_ALL || section == X68_MON_HW_FDD) monitor_format_fdd(&cursor, &remaining);
    if (section == X68_MON_HW_ALL || section == X68_MON_HW_FDC) monitor_format_fdc(&cursor, &remaining);
    if (section == X68_MON_HW_ALL || section == X68_MON_HW_CRTC) monitor_format_crtc(&cursor, &remaining);
    if (section == X68_MON_HW_ALL || section == X68_MON_HW_SCSI) monitor_format_scsi(&cursor, &remaining);
    if (section == X68_MON_HW_ALL || section == X68_MON_HW_AUDIO) monitor_format_audio(&cursor, &remaining);
    if (section == X68_MON_HW_ALL || section == X68_MON_HW_ADPCM) monitor_format_adpcm(&cursor, &remaining);
    if (section == X68_MON_HW_ALL || section == X68_MON_HW_INPUT) monitor_format_input(&cursor, &remaining);
    if (section == X68_MON_HW_ALL || section == X68_MON_HW_VIDEO) monitor_format_video(&cursor, &remaining);
    if (section == X68_MON_HW_ALL || section == X68_MON_HW_MIDI) monitor_format_midi(&cursor, &remaining);
    if (section == X68_MON_HW_ALL || section == X68_MON_HW_MEM) monitor_format_mem(&cursor, &remaining);

    if (out[0] == '\0') {
        monitor_appendf(&cursor, &remaining, "Unknown hardware monitor section %d\n", section);
    }
}

}
//extern "C"
