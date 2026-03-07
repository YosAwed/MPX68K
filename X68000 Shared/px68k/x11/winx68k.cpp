#ifdef  __cplusplus
extern "C" {
#endif
#include <math.h>
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
BYTE traceflag = 0;

BYTE ForceDebugMode = 0;
DWORD skippedframes = 0;

static int ClkUsed = 0;
static int FrameSkipCount = 0;
static int FrameSkipQueue = 0;
static int g_storage_bus_mode = 0; // 0 = SASI, 1 = SCSI
static int g_scsi0_mounted = 0;
static char g_scsi0_path[MAX_PATH] = {0};
static DWORD g_last_exec_pc = 0xffffffff;
static DWORD g_last_stall_pc = 0xffffffff;
static int g_same_pc_slices = 0;
static int g_scsi_fdc_wait_loop_slices = 0;
static int g_pc_heartbeat_counter = 0;
static int g_pc_heartbeat_logs = 0;
static int g_scsi_fdc_unblock_count = 0;
static int g_use_synthetic_scsi_hooks = 0;
static int g_enable_scsi_dev_driver = 0;
static int g_scsi_iocs_rehook_logs = 0;
static int g_scsi_iocs_check_counter = 0;
static const int kScsiIocsRuntimeCheckInterval = 256;
static DWORD g_last_scsi_iocs_vector = 0xffffffff;
static int g_scsi_iocs_fix_logs = 0;
static int g_scsi_iocs_default_hit_logs = 0;
static int g_scsi_lowpc_logs = 0;
static int g_scsi_fnff_entry_logs = 0;
static int g_scsi_iocs_shadow_valid = 0;
static DWORD g_scsi_iocs_shadow[256];
static DWORD g_scsi_native_trap15_vector = 0;
static int g_scsi_trap15_rehook_logs = 0;
static int g_scsi_trap15_pc_logs = 0;
static int g_scsi_trap_special_logs = 0;
static int g_scsi_iocs_entry_pc_logs = 0;
static int g_scsi_boot_path_logs = 0;
static DWORD g_scsi_boot_path_last_pc = 0xffffffff;
static int g_exec_probe_logs = 0;
static int g_scsi_fn2f_logs = 0;
static int g_scsi_qloop_logs = 0;
static int g_scsi_qloop_fe4c_logs = 0;
static int g_scsi_qloop_884e_fix_logs = 0;
static int g_scsi_qtrace_logs = 0;
static int g_scsi_native_iocs_entry_logs = 0;
static int g_scsi_qcode_dumped = 0;
static int g_scsi_qstr_logs = 0;
static int g_scsi_low_rts_logs = 0;
static int g_scsi_low_rte_logs = 0;
static int g_scsi_lowf75_logs = 0;
static int g_scsi_jmpa0_logs = 0;
static int g_scsi_jmpa0_fix_logs = 0;
static int g_scsi_jmpa0_code_dumped = 0;
static int g_scsi_dbg_27c0_logs = 0;
static DWORD g_scsi_dbg_27c0_last = 0xffffffffU;
static DWORD g_scsi_last_sys_ptr_1c98 = 0;
static int g_scsi_jmpa0_repair_logs = 0;
static int g_scsi_f75_code_dumped = 0;
static int g_scsi_fa_code_dumped = 0;
static DWORD g_scsi_pc_trace_ring[64];
static DWORD g_scsi_sp_trace_ring[64];
static DWORD g_scsi_d0_trace_ring[64];
static DWORD g_scsi_a0_trace_ring[64];
static int g_scsi_pc_trace_pos = 0;
static int g_scsi_pc_trace_filled = 0;
static int g_scsi_pc_trace_dumped = 0;

static int
X68000_IsScsiFdcWaitPc(DWORD pc)
{
    return (pc == 0x00ff9496 || pc == 0x00ff949c || pc == 0x00ff94a0);
}

static DWORD
X68000_NormalizeIocsHandler(DWORD raw, int* outCanonicalized)
{
    DWORD eff = raw & 0x00ffffffU;
    int canonicalized = 0;
    // Some IPL tables encode handler pointers with bit0 as metadata.
    // 68000 code addresses must be even, so strip bit0 for ROM handlers.
    if ((eff & 1U) != 0U &&
        (((eff >= 0x00fe0000U && eff <= 0x00ffffffU) ||
          (eff >= 0x00fc0000U && eff < 0x00fc2000U)))) {
        eff &= ~1U;
        canonicalized = 1;
    }
    // Do not force-map low addresses to $FFxxxx here.  Runtime IOCS table
    // corruption can transiently produce values like $00000034; treating those
    // as ROM addresses ($FF0034) hides the corruption and prevents repair.
    if (outCanonicalized) {
        *outCanonicalized = canonicalized;
    }
    return eff;
}

static DWORD
X68000_NormalizeDecodedIplIocsHandler(DWORD decoded, DWORD sourceRaw, int* outCanonicalized)
{
    DWORD eff = decoded & 0x00ffffffU;
    int canonicalized = 0;
    // Some IPL tables encode ROM handlers with bit0 metadata.
    if ((eff & 1U) != 0U &&
        (((eff >= 0x00fe0000U && eff <= 0x00ffffffU) ||
          (eff >= 0x00fc0000U && eff < 0x00fc2000U)))) {
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

static int
X68000_IsRomIocsHandler(DWORD eff)
{
    if ((eff & 1U) != 0U) {
        return 0;
    }
    // Instruction fetch is executable from IPL ROM ($FE0000-$FFFFFF)
    // and synthetic internal SCSI ROM window ($FC0000-$FC1FFF).
    if (eff >= 0x00fe0000U && eff <= 0x00ffffffU) {
        return 1;
    }
    if (eff >= 0x00fc0000U && eff < 0x00fc2000U) {
        return 1;
    }
    return 0;
}

static int
X68000_IsValidIocsHandler(DWORD eff)
{
    if ((eff & 1U) != 0U) {
        return 0;
    }
    if (X68000_IsRomIocsHandler(eff)) {
        return 1;
    }
    if (eff >= 0x00000400U && eff < 0x00c00000U) {
        return 1;
    }
    return 0;
}

static int
X68000_IsSyntheticIocsPadding(DWORD eff)
{
    // Synthetic SCSI ROM embeds zero-filled padding only in these ranges.
    // Keep synthetic helper stubs ($FC00D2-$FC00F9) usable.
    if (eff >= 0x00fc009eU && eff < 0x00fc00d2U) {
        return 1;
    }
    if (eff >= 0x00fc00feU && eff < 0x00fc0100U) {
        return 1;
    }
    return 0;
}

static int
X68000_IsUsableIocsHandler(DWORD eff)
{
    if (!X68000_IsValidIocsHandler(eff)) {
        return 0;
    }
    if (X68000_IsSyntheticIocsPadding(eff)) {
        return 0;
    }
    return 1;
}

static int
X68000_IsExecutableIocsHandler(DWORD eff)
{
    WORD op0;
    if (!X68000_IsUsableIocsHandler(eff)) {
        return 0;
    }
    op0 = Memory_ReadW(eff);
    if (op0 == 0x0000U || op0 == 0xffffU) {
        return 0;
    }
    return 1;
}

static DWORD
X68000_GetSyntheticIocsFallback(BYTE fn)
{
		    switch (fn) {
	    case 0x00:
	        return SCSI_SYNTH_IOCS_DIRECT;
	    case 0x01:
	        return SCSI_SYNTH_IOCS_FN10_OK;
		    case 0x02:
		        return SCSI_SYNTH_IOCS_FN32_OK;
	    case 0x04:
	    case 0x17:
	    case 0x18:
	    case 0x1e:
	    case 0x1f:
	    case 0x3c:
	    case 0x46:
	    case 0x47:
    case 0x4f:
    case 0x54:
    case 0x55:
    case 0x56:
    case 0x57:
    case 0x8e:
        return SCSI_SYNTH_IOCS_FN04_OK;
	    case 0x10:
	        return SCSI_SYNTH_IOCS_FN10_OK;
	    case 0x11:
	        // _B_SUPER fallback: keep boot path moving without trapping into
	        // synthetic C-side SR/USP manipulation.
	        return SCSI_SYNTH_IOCS_FN10_OK;
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x26:
    case 0x27:
    case 0x28:
    case 0x29:
    case 0x2a:
    case 0x2b:
    case 0x2c:
    case 0x2d:
    case 0x2e:
    case 0x2f:
        // Route SCSI IOCS through synthetic direct trap so C-side emulation
        // handles SPC-less environments consistently.
        return SCSI_SYNTH_IOCS_DIRECT;
    case 0x34:
        return SCSI_SYNTH_IOCS_FN34_OK;
    case 0x33:
        return SCSI_SYNTH_IOCS_FN33_OK;
    case 0xaf:
        return SCSI_SYNTH_IOCS_FNAF_OK;
	    case 0xfd:
	        return SCSI_SYNTH_IOCS_FN04_OK;
    case 0xfc:
        return SCSI_SYNTH_IOCS_FN10_OK;
    case 0xff:
        return SCSI_SYNTH_IOCS_FF_FALLBACK;
    default:
        return SCSI_SYNTH_IOCS_DEFAULT;
    }
}

extern BYTE* s_disk_image_buffer[5];
extern long s_disk_image_buffer_size[5];

static void
X68000_AppendSCSILog(const char* message)
{
#ifdef __APPLE__
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
#else
    (void)message;
#endif
}

static void
X68000_ResetSCSILog(void)
{
#ifdef __APPLE__
    const char* home = getenv("HOME");
    char path[512];
    if (home && home[0] != '\0') {
        snprintf(path, sizeof(path), "%s/Documents/X68000/_scsi_iocs.txt", home);
    } else {
        snprintf(path, sizeof(path), "X68000/_scsi_iocs.txt");
    }
    FILE* fp = fopen(path, "w");
    if (fp) {
        fclose(fp);
    }
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

static DWORD
X68000_FindIplIocsTableOffset(int* outFFCount, int* outUseDirect)
{
    static const int s_bootFns[] = { 0x21, 0x25, 0x26, 0x34 };
    DWORD bestOffset = 0;
    int bestScore = -1;
    int bestFF = -1;
    int bestUseDirect = 0;

    for (int useDirect = 0; useDirect <= 1; ++useDirect) {
        for (DWORD off = 0x00000; off + (DWORD)(256 * 4) <= 0x40000; off += 4) {
            int ffCount = 0;
            int bootFnOk = 0;
            int score;

            for (int fn = 0; fn < 256; ++fn) {
                DWORD raw = X68000_ReadIplLong(off + (DWORD)fn * 4, useDirect);
                if (raw == 0x00000000 || raw == 0xffffffff) {
                    continue;
                }
                DWORD eff = X68000_NormalizeDecodedIplIocsHandler(
                    X68000_DecodeIplVectorAddress(raw),
                    raw,
                    NULL);
                if (X68000_IsRomIocsHandler(eff)) {
                    WORD op0 = Memory_ReadW(eff);
                    if (op0 != 0x0000U && op0 != 0xffffU) {
                        ffCount++;
                    }
                }
            }

            for (int bi = 0; bi < (int)(sizeof(s_bootFns) / sizeof(s_bootFns[0])); ++bi) {
                int fn = s_bootFns[bi];
                DWORD raw = X68000_ReadIplLong(off + (DWORD)fn * 4, useDirect);
                DWORD eff = X68000_NormalizeDecodedIplIocsHandler(
                    X68000_DecodeIplVectorAddress(raw),
                    raw,
                    NULL);
                if (X68000_IsRomIocsHandler(eff)) {
                    WORD op0 = Memory_ReadW(eff);
                    if (op0 != 0x0000U && op0 != 0xffffU) {
                        bootFnOk++;
                    }
                }
            }

            // Strongly prioritize offsets that decode boot-critical IOCS
            // entries into ROM addresses.
            score = ffCount + (bootFnOk * 1000);
            if (score > bestScore || (score == bestScore && ffCount > bestFF)) {
                bestScore = score;
                bestFF = ffCount;
                bestOffset = off;
                bestUseDirect = useDirect;
            }
        }
    }
    if (outFFCount) {
        *outFFCount = bestFF;
    }
    if (outUseDirect) {
        *outUseDirect = bestUseDirect;
    }
    return bestOffset;
}

static DWORD
X68000_IplIocsFnEffective(DWORD iocsTableOffset, int fn, int useDirect)
{
    DWORD raw = X68000_ReadIplLong(iocsTableOffset + (DWORD)fn * 4, useDirect);
    return X68000_NormalizeDecodedIplIocsHandler(
        X68000_DecodeIplVectorAddress(raw),
        raw,
        NULL);
}

static int
X68000_IplIocsTableHasBootFns(DWORD iocsTableOffset, int useDirect)
{
    static const int s_bootFns[] = { 0x21, 0x25, 0x26, 0x34 };
    for (int i = 0; i < (int)(sizeof(s_bootFns) / sizeof(s_bootFns[0])); ++i) {
        int fn = s_bootFns[i];
        DWORD eff = X68000_IplIocsFnEffective(iocsTableOffset, fn, useDirect);
        if (!X68000_IsRomIocsHandler(eff) ||
            Memory_ReadW(eff) == 0x0000U ||
            Memory_ReadW(eff) == 0xffffU) {
            return 0;
        }
    }
    return 1;
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

    if (scsi) {
        memcpy(IPL, &IPL[0x20000], 0x20000);
        Memory_SetSCSIMode();
        p6logd("SCSI IPL detected and enabled\n");
    } else {
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
    X68000_ResetSCSILog();
    X68000_AppendSCSILog("--- core reset ---");
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
    // Safety check: Ensure IPL ROM is loaded before setting CPU registers
	    if (IPL != NULL) {
	        // Extract reset vectors from IPL ROM
	        DWORD stack_pointer = X68000_ReadIplVector(0);
	        DWORD program_counter = X68000_ReadIplVector(1);


        // Validate reset vectors are reasonable for X68000 architecture
        // Stack pointer should be in RAM (0x000000-0x00BFFFFF) or high memory (0x00ED0000+)
        // Program counter should be in ROM area (0x00FE0000-0x00FFFFFF)
        bool valid_stack = (stack_pointer >= 0x00000000 && stack_pointer <= 0x00BFFFFF) ||
                          (stack_pointer >= 0x00ED0000 && stack_pointer <= 0x01000000);
        bool valid_pc = (program_counter >= 0x00FE0000 && program_counter <= 0x01000000);

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
	// Some IPL variants never jump into the internal SCSI entry on their own.
	// If SCSI is explicitly selected and a disk is mounted, force entry to
	// the synthetic SCSI boot entry.
		g_use_synthetic_scsi_hooks = (g_storage_bus_mode == 1 && g_scsi0_mounted);
		g_scsi_iocs_shadow_valid = 0;
		memset(g_scsi_iocs_shadow, 0, sizeof(g_scsi_iocs_shadow));
		g_scsi_native_trap15_vector = 0;
		// Enable synthetic block-driver chaining for SCSI boot. It is now
	// registered as a true block device (not a remote device), so Human68k
	// should issue normal block request packets instead of CR_* extensions.
	g_enable_scsi_dev_driver = (g_storage_bus_mode == 1 && g_scsi0_mounted);
		if (g_storage_bus_mode == 1 && g_scsi0_mounted) {
				if (s_disk_image_buffer[4] != NULL && s_disk_image_buffer_size[4] > 0) {
								DWORD trap15_vector = X68000_ReadIplVector(47); // trap #15 vector at $000000BC
								DWORD trap15_effective = X68000_DecodeIplVectorAddress(trap15_vector);
								DWORD trap15_ram = Memory_ReadD(47 * 4) & 0x00ffffff;
								// If IPL reset vectors are unavailable on this ROM variant,
								// keep stack in SRAM top instead of boot-sector buffer area.
								DWORD forcedSsp = 0x00ed3ffcU;
								if (X68000_IsRomIocsHandler(trap15_effective)) {
									g_scsi_native_trap15_vector = trap15_effective;
								}
								// Recompute IOCS transfer base after media/reset boundary.
							SCSI_InvalidateTransferCache();

						{
							DWORD resetSp = X68000_ReadIplVector(0);
							DWORD resetPcRaw = X68000_ReadIplVector(1);
							DWORD resetPc = X68000_DecodeIplVectorAddress(resetPcRaw);
							int resetSpValid = 0;
							if ((resetSp & 1U) == 0U &&
							    ((resetSp >= 0x00000400U && resetSp < 0x00c00000U) ||
							     (resetSp >= 0x00ed0000U && resetSp < 0x00ee0000U))) {
								resetSpValid = 1;
							}
							if (resetSpValid) {
								forcedSsp = resetSp;
							}
							C68k_Set_AReg(&C68K, 7, forcedSsp);
							if (resetPc == 0) {
								resetPc = resetPcRaw & 0x00ffffffU;
							}
							// Some boot sectors re-read vectors $000000/$000004 to
							// initialize SSP/entry state. Keep them consistent with IPL.
						Memory_WriteD(0x00000000, resetSp);
						Memory_WriteD(0x00000004, resetPc);
					}
					DWORD fallbackException = X68000_DecodeIplVectorAddress(X68000_ReadIplVector(8));
					fallbackException = X68000_NormalizeIocsHandler(fallbackException, NULL);
					if (!X68000_IsRomIocsHandler(fallbackException)) {
						fallbackException = 0x00ff0654U;
					}
					// Seed exception/interrupt vectors so forced boot never jumps
					// through uninitialized RAM vectors.
					int highVecFallbackLogs = 0;
					for (int vec = 2; vec < 256; ++vec) {
						// Skip IPL's trap #15 vector here. We install a synthetic
						// IOCS dispatcher (SCSI_SYNTH_TRAP15_ENTRY) below so forced
						// boot does not depend on RAM being pre-initialized.
						if (vec == 47) {
							continue;
						}
						DWORD vectorValue = 0;
						DWORD vectorEffective = 0;
						int vectorValid = 0;
						vectorValue = X68000_ReadIplVector(vec);
						vectorEffective = X68000_DecodeIplVectorAddress(vectorValue);
						// Some IPL variants encode non-address words in vector slots.
						// Prefer decoded ROM handlers, then fall back to plain 24-bit mask.
						if (X68000_IsRomIocsHandler(vectorEffective)) {
							vectorValid = 1;
						} else {
							DWORD masked = vectorValue & 0x00ffffffU;
							if (X68000_IsRomIocsHandler(masked)) {
								vectorEffective = masked;
								vectorValid = 1;
							}
						}
						if (!vectorValid && vec >= 64) {
							// For device vectors, keep already-populated ROM handlers in RAM
							// when present (warm boot / firmware-installed vectors).
							DWORD ramValue = Memory_ReadD((DWORD)vec * 4);
							DWORD ramEffective = X68000_NormalizeIocsHandler(ramValue, NULL);
							if (X68000_IsRomIocsHandler(ramEffective)) {
								vectorValue = ramValue;
								vectorEffective = ramEffective;
								vectorValid = 1;
							}
						}
							// Last-resort guard for critical exception vectors.
							// - vec2-15: bus/address/illegal/trap/line/FPU style core faults.
							// - vec24-31: autovector interrupts; a zero entry here can route
							//   timer/IO IRQ to address $00000000 and hang in low RAM.
							// - vec64-255: device vectors (for example MFP vector $44) that
							//   can fire before OS vector install.
							if (!vectorValid &&
							    ((vec >= 2 && vec <= 15) ||
							     (vec >= 24 && vec <= 31) ||
							     (vec >= 64 && vec <= 255))) {
								vectorEffective = fallbackException;
								vectorValid = 1;
								if (vec >= 64 && highVecFallbackLogs < 16) {
									char vecLog[112];
									snprintf(vecLog, sizeof(vecLog),
									         "FORCE BOOT VEC fallback vec=$%02X old=$%08X new=$%08X",
									         (unsigned int)vec,
									         (unsigned int)(vectorValue & 0x00ffffffU),
									         (unsigned int)fallbackException);
									X68000_AppendSCSILog(vecLog);
									highVecFallbackLogs++;
								}
							}
							// Forced-boot path can trigger spurious IRQ (vector 24) before
							// peripheral state is fully initialized. Prefer the generic
							// fallback handler to avoid entering IPL-specific branches that
							// can jump into low RAM under emulation timing.
							if (vec == 24) {
								vectorEffective = fallbackException;
								vectorValid = 1;
							}
						if (vectorValid) {
							Memory_WriteD((DWORD)vec * 4, vectorEffective);
						}
					}
				// Forced boot skips part of the normal IPL startup path, so the
				// IOCS dispatch table ($400-$7FC) may still be uninitialized even
				// when trap #15 itself points to a valid RAM/ROM handler.
				int iocsFFCount = 0;
				int iocsUseDirect = 0;
				DWORD iocsTableOffset = X68000_FindIplIocsTableOffset(&iocsFFCount, &iocsUseDirect);
				int iocsBootFnsOk = X68000_IplIocsTableHasBootFns(iocsTableOffset, iocsUseDirect);
					DWORD iocsFn04Eff = X68000_IplIocsFnEffective(iocsTableOffset, 0x04, iocsUseDirect);
						int iocsFn04Ok = X68000_IsRomIocsHandler(iocsFn04Eff);
						// Some IPL variants do not expose fn=$04 as a ROM IOCS entry
						// even when the table is otherwise valid for boot paths.
						// Allow seeding when boot-related functions decode correctly.
						int iocsSeedReady = (iocsFFCount >= 32 && iocsBootFnsOk);
						{
							static const int s_probeFns[] = { 0x00, 0x01, 0x04, 0x10, 0x17, 0x1E, 0x1F, 0x21, 0x23, 0x25, 0x26, 0x2F, 0x32, 0x33, 0x34, 0x35, 0x46, 0x47, 0x4F, 0x54, 0x55, 0x56, 0x57, 0x8E, 0xAE, 0xAF, 0xF0, 0xFC, 0xFF, 0xF5 };
						for (int pi = 0; pi < (int)(sizeof(s_probeFns) / sizeof(s_probeFns[0])); ++pi) {
							int fn = s_probeFns[pi];
							DWORD pre = Memory_ReadD(0x00000400 + (DWORD)fn * 4);
						char preLog[80];
						snprintf(preLog, sizeof(preLog),
						         "FORCE BOOT IOCS RAMPRE fn=$%02X pre=$%08X",
						         (unsigned int)fn,
						         (unsigned int)pre);
						X68000_AppendSCSILog(preLog);
					}
				}
							int iocsSeedWriteCount = 0;
							int iocsCanonicalizeCount = 0;
							// Keep already-populated IOCS entries and only patch obviously
							// invalid slots below. Some firmware paths pre-seed working
							// handlers (including fn=$34) before forced boot takes over.
									if (iocsSeedReady) {
										for (int fn = 0; fn < 256; ++fn) {
											DWORD iocsRaw = X68000_ReadIplLong(iocsTableOffset + (DWORD)fn * 4, iocsUseDirect);
										int canonicalized = 0;
										DWORD iocsEffective = X68000_NormalizeDecodedIplIocsHandler(
										    X68000_DecodeIplVectorAddress(iocsRaw),
										    iocsRaw,
										    &canonicalized);
										// Seed only ROM-backed handlers from IPL.
										// RAM pointers from false-positive table scans are unsafe.
										if (X68000_IsRomIocsHandler(iocsEffective) &&
										    !X68000_IsSyntheticIocsPadding(iocsEffective)) {
											Memory_WriteD(0x00000400 + (DWORD)fn * 4, iocsEffective);
											iocsSeedWriteCount++;
										if (canonicalized) {
										iocsCanonicalizeCount++;
									}
								}
							}
						}
						// Keep existing valid IOCS entries, but guarantee that clearly invalid
						// slots (out-of-ROM/sentinel/odd) have a safe default handler.
							int iocsFallbackCount = 0;
							int iocsFallbackLogs = 0;
								for (int fn = 0; fn < 256; ++fn) {
									DWORD addr = 0x00000400 + (DWORD)fn * 4;
									DWORD entry = Memory_ReadD(addr);
									if (fn == 0xFF) {
										Memory_WriteD(addr, SCSI_SYNTH_IOCS_FF_FALLBACK);
									iocsFallbackCount++;
									if (iocsFallbackLogs < 12) {
										char fallbackLog[112];
										snprintf(fallbackLog, sizeof(fallbackLog),
										         "FORCE BOOT IOCS fallback fn=$%02X old=$%08X new=$%08X",
										         (unsigned int)fn,
										         (unsigned int)entry,
										         (unsigned int)SCSI_SYNTH_IOCS_FF_FALLBACK);
										X68000_AppendSCSILog(fallbackLog);
										iocsFallbackLogs++;
										}
										continue;
									}
											if (fn == 0x00) {
												Memory_WriteD(addr, SCSI_SYNTH_IOCS_DIRECT);
												iocsFallbackCount++;
												if (iocsFallbackLogs < 12) {
													char fallbackLog[112];
													snprintf(fallbackLog, sizeof(fallbackLog),
													         "FORCE BOOT IOCS fallback fn=$%02X old=$%08X new=$%08X",
													         (unsigned int)fn,
													         (unsigned int)entry,
													         (unsigned int)SCSI_SYNTH_IOCS_DIRECT);
													X68000_AppendSCSILog(fallbackLog);
													iocsFallbackLogs++;
												}
											continue;
										}
										if (fn == 0x01) {
											Memory_WriteD(addr, SCSI_SYNTH_IOCS_FN10_OK);
											iocsFallbackCount++;
											if (iocsFallbackLogs < 12) {
												char fallbackLog[112];
												snprintf(fallbackLog, sizeof(fallbackLog),
												         "FORCE BOOT IOCS fallback fn=$%02X old=$%08X new=$%08X",
												         (unsigned int)fn,
												         (unsigned int)entry,
												         (unsigned int)SCSI_SYNTH_IOCS_FN10_OK);
												X68000_AppendSCSILog(fallbackLog);
												iocsFallbackLogs++;
											}
											continue;
										}
											if (fn == 0x04) {
												Memory_WriteD(addr, SCSI_SYNTH_IOCS_FN04_OK);
											iocsFallbackCount++;
											if (iocsFallbackLogs < 12) {
											char fallbackLog[112];
											snprintf(fallbackLog, sizeof(fallbackLog),
											         "FORCE BOOT IOCS fallback fn=$%02X old=$%08X new=$%08X",
											         (unsigned int)fn,
											         (unsigned int)entry,
											         (unsigned int)SCSI_SYNTH_IOCS_FN04_OK);
											X68000_AppendSCSILog(fallbackLog);
											iocsFallbackLogs++;
										}
											continue;
										}
										if (fn == 0x02) {
											Memory_WriteD(addr, SCSI_SYNTH_IOCS_FN32_OK);
											iocsFallbackCount++;
											if (iocsFallbackLogs < 12) {
												char fallbackLog[112];
												snprintf(fallbackLog, sizeof(fallbackLog),
												         "FORCE BOOT IOCS fallback fn=$%02X old=$%08X new=$%08X",
												         (unsigned int)fn,
												         (unsigned int)entry,
												         (unsigned int)SCSI_SYNTH_IOCS_FN32_OK);
												X68000_AppendSCSILog(fallbackLog);
												iocsFallbackLogs++;
											}
											continue;
										}
											if (fn == 0x10) {
												Memory_WriteD(addr, SCSI_SYNTH_IOCS_FN10_OK);
												iocsFallbackCount++;
											if (iocsFallbackLogs < 12) {
												char fallbackLog[112];
												snprintf(fallbackLog, sizeof(fallbackLog),
												         "FORCE BOOT IOCS fallback fn=$%02X old=$%08X new=$%08X",
												         (unsigned int)fn,
												         (unsigned int)entry,
												         (unsigned int)SCSI_SYNTH_IOCS_FN10_OK);
												X68000_AppendSCSILog(fallbackLog);
												iocsFallbackLogs++;
											}
													continue;
												}
											if (fn == 0x11) {
												Memory_WriteD(addr, SCSI_SYNTH_IOCS_FN10_OK);
												iocsFallbackCount++;
												if (iocsFallbackLogs < 12) {
													char fallbackLog[112];
													snprintf(fallbackLog, sizeof(fallbackLog),
													         "FORCE BOOT IOCS fallback fn=$%02X old=$%08X new=$%08X",
													         (unsigned int)fn,
													         (unsigned int)entry,
													         (unsigned int)SCSI_SYNTH_IOCS_FN10_OK);
													X68000_AppendSCSILog(fallbackLog);
													iocsFallbackLogs++;
												}
												continue;
											}
	                                            if (fn >= 0x20 && fn <= 0x2F) {
	                                                Memory_WriteD(addr, SCSI_SYNTH_IOCS_DIRECT);
	                                                iocsFallbackCount++;
	                                                if (iocsFallbackLogs < 12) {
	                                                    char fallbackLog[112];
	                                                    snprintf(fallbackLog, sizeof(fallbackLog),
	                                                             "FORCE BOOT IOCS fallback fn=$%02X old=$%08X new=$%08X",
	                                                             (unsigned int)fn,
	                                                             (unsigned int)entry,
	                                                             (unsigned int)SCSI_SYNTH_IOCS_DIRECT);
	                                                    X68000_AppendSCSILog(fallbackLog);
	                                                    iocsFallbackLogs++;
	                                                }
	                                                continue;
	                                            }
	                                            if (fn == 0xFD) {
	                                                Memory_WriteD(addr, SCSI_SYNTH_IOCS_FN04_OK);
	                                                iocsFallbackCount++;
												if (iocsFallbackLogs < 12) {
												char fallbackLog[112];
												snprintf(fallbackLog, sizeof(fallbackLog),
												         "FORCE BOOT IOCS fallback fn=$%02X old=$%08X new=$%08X",
												         (unsigned int)fn,
												         (unsigned int)entry,
												         (unsigned int)SCSI_SYNTH_IOCS_FN04_OK);
												X68000_AppendSCSILog(fallbackLog);
												iocsFallbackLogs++;
                                                }
                                                continue;
                                            }
                                            if (fn == 0x34) {
                                                Memory_WriteD(addr, SCSI_SYNTH_IOCS_FN34_OK);
                                                iocsFallbackCount++;
                                                if (iocsFallbackLogs < 12) {
                                                    char fallbackLog[112];
                                                    snprintf(fallbackLog, sizeof(fallbackLog),
                                                             "FORCE BOOT IOCS fallback fn=$%02X old=$%08X new=$%08X",
                                                             (unsigned int)fn,
                                                             (unsigned int)entry,
                                                             (unsigned int)SCSI_SYNTH_IOCS_FN34_OK);
                                                    X68000_AppendSCSILog(fallbackLog);
                                                    iocsFallbackLogs++;
                                                }
                                                continue;
                                            }
                                            if (fn == 0x33) {
                                                Memory_WriteD(addr, SCSI_SYNTH_IOCS_FN33_OK);
                                                iocsFallbackCount++;
                                                if (iocsFallbackLogs < 12) {
                                                    char fallbackLog[112];
                                                    snprintf(fallbackLog, sizeof(fallbackLog),
                                                             "FORCE BOOT IOCS fallback fn=$%02X old=$%08X new=$%08X",
                                                             (unsigned int)fn,
                                                             (unsigned int)entry,
                                                             (unsigned int)SCSI_SYNTH_IOCS_FN33_OK);
                                                    X68000_AppendSCSILog(fallbackLog);
                                                    iocsFallbackLogs++;
                                                }
                                                continue;
                                            }
                                            if (fn == 0x17 || fn == 0x18 || fn == 0x1e ||
                                                fn == 0x1f || fn == 0x3c || fn == 0x8e ||
                                            fn == 0x46 || fn == 0x47 ||
										    fn == 0x4f ||
										    fn == 0x54 || fn == 0x55 ||
										    fn == 0x56 || fn == 0x57) {
											Memory_WriteD(addr, SCSI_SYNTH_IOCS_FN04_OK);
											iocsFallbackCount++;
											if (iocsFallbackLogs < 12) {
												char fallbackLog[112];
												snprintf(fallbackLog, sizeof(fallbackLog),
												         "FORCE BOOT IOCS fallback fn=$%02X old=$%08X new=$%08X",
												         (unsigned int)fn,
												         (unsigned int)entry,
												         (unsigned int)SCSI_SYNTH_IOCS_FN04_OK);
												X68000_AppendSCSILog(fallbackLog);
												iocsFallbackLogs++;
											}
											continue;
										}
											if (fn == 0xAF) {
												Memory_WriteD(addr, SCSI_SYNTH_IOCS_FNAF_OK);
											iocsFallbackCount++;
											if (iocsFallbackLogs < 12) {
												char fallbackLog[112];
												snprintf(fallbackLog, sizeof(fallbackLog),
												         "FORCE BOOT IOCS fallback fn=$%02X old=$%08X new=$%08X",
												         (unsigned int)fn,
												         (unsigned int)entry,
												         (unsigned int)SCSI_SYNTH_IOCS_FNAF_OK);
												X68000_AppendSCSILog(fallbackLog);
												iocsFallbackLogs++;
												}
												continue;
											}
										if (fn == 0xfc) {
											Memory_WriteD(addr, SCSI_SYNTH_IOCS_FN10_OK);
											iocsFallbackCount++;
											if (iocsFallbackLogs < 12) {
												char fallbackLog[112];
												snprintf(fallbackLog, sizeof(fallbackLog),
												         "FORCE BOOT IOCS fallback fn=$%02X old=$%08X new=$%08X",
												         (unsigned int)fn,
												         (unsigned int)entry,
												         (unsigned int)SCSI_SYNTH_IOCS_FN10_OK);
												X68000_AppendSCSILog(fallbackLog);
												iocsFallbackLogs++;
											}
											continue;
										}
											int canonicalized = 0;
											DWORD eff = X68000_NormalizeIocsHandler(entry, &canonicalized);
								int isValid;
								// Keep upper metadata bits (some IPLs encode fn tags in the top byte,
								// e.g. $F0FF1F0E). Only canonicalize the effective low 24-bit address
								// when NormalizeIocsHandler explicitly says so.
								if (canonicalized &&
								    ((entry & 0x00ffffffU) != eff)) {
									DWORD rewritten = (entry & 0xff000000U) | eff;
									Memory_WriteD(addr, rewritten);
									entry = rewritten;
									iocsCanonicalizeCount++;
								}
								isValid = X68000_IsUsableIocsHandler(eff);
								if (isValid) {
									WORD op0 = Memory_ReadW(eff);
									if (op0 == 0x0000U || op0 == 0xffffU) {
										isValid = 0;
									}
								}
										if (!isValid) {
											DWORD repair = SCSI_SYNTH_IOCS_DEFAULT;
											if (fn == 0xFF) {
												repair = SCSI_SYNTH_IOCS_FF_FALLBACK;
											} else if (fn == 0x00) {
												repair = SCSI_SYNTH_IOCS_DIRECT;
											} else if (fn == 0x01) {
												repair = SCSI_SYNTH_IOCS_FN10_OK;
											} else if (fn == 0x11) {
                                                repair = SCSI_SYNTH_IOCS_FN10_OK;
                                            } else if (fn == 0xFD) {
                                                repair = SCSI_SYNTH_IOCS_FN04_OK;
                                            } else if (fn == 0x34) {
                                                repair = SCSI_SYNTH_IOCS_FN34_OK;
                                            } else if (fn == 0x33) {
                                                repair = SCSI_SYNTH_IOCS_FN33_OK;
                                            } else if (fn == 0x02) {
                                                repair = SCSI_SYNTH_IOCS_FN32_OK;
                                                    } else if (fn == 0x17 || fn == 0x18 ||
                                                               fn == 0x1e || fn == 0x1f ||
                                                               fn == 0x3c || fn == 0x8e ||
                                                       fn == 0x46 ||
											           fn == 0x47 || fn == 0x4f ||
											           fn == 0x54 || fn == 0x55 ||
											           fn == 0x56 ||
											           fn == 0x57) {
												repair = SCSI_SYNTH_IOCS_FN04_OK;
                                            }
									Memory_WriteD(addr, repair);
									iocsFallbackCount++;
								if (iocsFallbackLogs < 12) {
									char fallbackLog[112];
									snprintf(fallbackLog, sizeof(fallbackLog),
									         "FORCE BOOT IOCS fallback fn=$%02X old=$%08X new=$%08X",
									         (unsigned int)fn,
									         (unsigned int)entry,
									         (unsigned int)repair);
									X68000_AppendSCSILog(fallbackLog);
									iocsFallbackLogs++;
										}
								}
							}
							// Final forced pass: SCSI IOCS functions ($20-$2F) and
							// extended SCSI-area functions ($30-$39) MUST route through
							// synthetic handlers.  The seed phase can incorrectly map
							// IPL ROM SCSI handler addresses to system function slots
							// ($32-$38 etc.), causing B_xPEEK/B_xPOKE to execute SPC
							// code and enter the IPL error console.
							for (int fn = 0x20; fn <= 0x2F; ++fn) {
								Memory_WriteD(0x00000400 + (DWORD)fn * 4, SCSI_SYNTH_IOCS_DIRECT);
							}
							for (int fn = 0x30; fn <= 0x39; ++fn) {
								// fn=$33 (B_WPEEK) and fn=$34 (B_LPEEK) keep their
								// dedicated synthetic stubs; others route to C handler.
								if (fn == 0x33) {
									Memory_WriteD(0x00000400 + (DWORD)fn * 4, SCSI_SYNTH_IOCS_FN33_OK);
								} else if (fn == 0x34) {
									Memory_WriteD(0x00000400 + (DWORD)fn * 4, SCSI_SYNTH_IOCS_FN34_OK);
								} else if (fn == 0x35) {
									Memory_WriteD(0x00000400 + (DWORD)fn * 4, SCSI_SYNTH_IOCS_FN35_OK);
								} else {
									Memory_WriteD(0x00000400 + (DWORD)fn * 4, SCSI_SYNTH_IOCS_DIRECT);
								}
							}
							// Save a known-good IOCS table snapshot. Runtime monitor can restore
							// critical entries if kernels partially clobber them later.
							for (int fn = 0; fn < 256; ++fn) {
							g_scsi_iocs_shadow[fn] = Memory_ReadD(0x00000400 + (DWORD)fn * 4) & 0x00ffffffU;
						}
						g_scsi_iocs_shadow_valid = 1;
					// Debug: verify a few IOCS table entries used during forced boot.
							{
								static const int s_probeFns[] = { 0x00, 0x01, 0x04, 0x10, 0x17, 0x1E, 0x1F, 0x21, 0x23, 0x25, 0x26, 0x2F, 0x32, 0x33, 0x34, 0x35, 0x46, 0x47, 0x4F, 0x54, 0x55, 0x56, 0x57, 0x8E, 0xAE, 0xAF, 0xF0, 0xFC, 0xFF, 0xF5 };
							for (int pi = 0; pi < (int)(sizeof(s_probeFns) / sizeof(s_probeFns[0])); ++pi) {
								int fn = s_probeFns[pi];
							DWORD raw = X68000_ReadIplLong(iocsTableOffset + (DWORD)fn * 4, iocsUseDirect);
						DWORD dec = X68000_DecodeIplVectorAddress(raw);
						DWORD ram = Memory_ReadD(0x00000400 + (DWORD)fn * 4);
						char dbgLog[128];
						snprintf(dbgLog, sizeof(dbgLog),
						         "FORCE BOOT IOCS fn=$%02X raw=$%08X dec=$%08X ram=$%08X",
						         (unsigned int)fn,
						         (unsigned int)raw,
						         (unsigned int)dec,
						         (unsigned int)ram);
						X68000_AppendSCSILog(dbgLog);
					}
				}
									// Keep forced-boot trap #15 on synthetic dispatcher. We rely on
									// forced IOCS table patching above, including direct C-side routes
									// for SCSI IOCS functions.
									Memory_WriteD(47 * 4, SCSI_SYNTH_TRAP15_ENTRY);
									Memory_WriteD(0x000007d4, SCSI_SYNTH_IOCS_ENTRY);
									// Forced boot can start with stale MFP timer state and trigger
									// continuous level-6 interrupts before OS handlers are installed.
									// Mask/clear MFP interrupt sources here; Human68k reprograms MFP
									// during normal startup.
									MFP[MFP_IERA] = 0;
									MFP[MFP_IERB] = 0;
									MFP[MFP_IMRA] = 0;
									MFP[MFP_IMRB] = 0;
									MFP[MFP_IPRA] = 0;
									MFP[MFP_IPRB] = 0;
									MFP[MFP_ISRA] = 0;
									MFP[MFP_ISRB] = 0;
									MFP[MFP_TACR] = 0;
									MFP[MFP_TBCR] = 0;
									MFP[MFP_TCDCR] = 0;
									X68000_AppendSCSILog("FORCE BOOT MFP masked (IER/IMR/IPR/ISR/TCR)");
				C68k_Set_PC(&C68K, SCSI_SYNTH_BOOT_ENTRY);
				cpu_setOPbase24(SCSI_SYNTH_BOOT_ENTRY);
				p6logd("Forcing SCSI boot entry: PC=$%08X path=%s\n",
				       (unsigned int)SCSI_SYNTH_BOOT_ENTRY,
			       g_scsi0_path[0] ? g_scsi0_path : "<unknown>");
			{
				char vecWarn[96];
					snprintf(vecWarn, sizeof(vecWarn),
					         "FORCE BOOT DECODE TRAP15 raw=$%08X dec=$%08X",
					         (unsigned int)trap15_vector,
					         (unsigned int)trap15_effective);
					X68000_AppendSCSILog(vecWarn);
				}
				{
						char trap15Log[128];
					char iocsLog[64];
					char pcLog[48];
					char spLog[48];
						snprintf(pcLog, sizeof(pcLog), "FORCE BOOT PC=$%08X",
						         (unsigned int)SCSI_SYNTH_BOOT_ENTRY);
						snprintf(spLog, sizeof(spLog), "FORCE BOOT SSP=$%08X",
						         (unsigned int)forcedSsp);
					snprintf(iocsLog, sizeof(iocsLog), "FORCE BOOT INIT IOCS $0007D4=$%08X",
					         (unsigned int)SCSI_SYNTH_IOCS_ENTRY);
						snprintf(trap15Log, sizeof(trap15Log),
						         "FORCE BOOT INIT TRAP15 $0000BC=$%08X (force synth ram=$%08X ipl=$%08X)",
						         (unsigned int)SCSI_SYNTH_TRAP15_ENTRY,
						         (unsigned int)trap15_ram,
						         (unsigned int)trap15_effective);
								X68000_AppendSCSILog("FORCE BOOT INIT VECTORS $000008-$0000FC (from IPL)");
								if (iocsSeedReady) {
									char iocsTblLog[192];
									snprintf(iocsTblLog, sizeof(iocsTblLog),
									         "FORCE BOOT INIT IOCS TABLE $000400-$0007FC (ipl_off=$%05X mode=%d ff=%d boot=%d fn04=%d seed=%d canon=%d fallback=%d)",
									         (unsigned int)iocsTableOffset, iocsUseDirect, iocsFFCount, iocsBootFnsOk, iocsFn04Ok,
									         iocsSeedWriteCount,
									         iocsCanonicalizeCount,
									         iocsFallbackCount);
									X68000_AppendSCSILog(iocsTblLog);
								} else {
									char iocsSkipLog[192];
									snprintf(iocsSkipLog, sizeof(iocsSkipLog),
									         "FORCE BOOT IOCS TABLE seed skipped (ipl_off=$%05X mode=%d ff=%d boot=%d fn04=%d seed=%d canon=%d fallback=%d)",
									         (unsigned int)iocsTableOffset, iocsUseDirect, iocsFFCount, iocsBootFnsOk, iocsFn04Ok,
									         iocsSeedWriteCount,
									         iocsCanonicalizeCount,
									         iocsFallbackCount);
								X68000_AppendSCSILog(iocsSkipLog);
							}
						X68000_AppendSCSILog(trap15Log);
					X68000_AppendSCSILog(iocsLog);
					X68000_AppendSCSILog(pcLog);
					X68000_AppendSCSILog(spLog);
				}
		} else {
			p6logd("SCSI mounted but HDD buffer is missing; forced SCSI boot skipped\n");
			X68000_AppendSCSILog("FORCE BOOT SKIPPED (no HDD buffer)");
		}
	} else {
		X68000_AppendSCSILog("FORCE BOOT SKIPPED (SCSI not mounted)");
	}
#endif

//    C68K.ICount = 0;
    m68000_ICountBk = 0;
    ICount = 0;

    DSound_Stop();
    SRAM_VirusCheck();
    //CDROM_Init();
    DSound_Play();
	    g_last_exec_pc = 0xffffffff;
	    g_last_stall_pc = 0xffffffff;
	    g_same_pc_slices = 0;
	    g_pc_heartbeat_counter = 0;
	    g_pc_heartbeat_logs = 0;
	    g_scsi_fdc_unblock_count = 0;
	    g_scsi_iocs_rehook_logs = 0;
	    g_scsi_iocs_check_counter = 0;
	    g_last_scsi_iocs_vector = 0xffffffff;
	    g_scsi_iocs_fix_logs = 0;
	    g_scsi_iocs_default_hit_logs = 0;
	    g_scsi_lowpc_logs = 0;
	    g_scsi_fnff_entry_logs = 0;
    g_scsi_trap15_rehook_logs = 0;
	    g_scsi_trap15_pc_logs = 0;
	    g_scsi_iocs_entry_pc_logs = 0;
		                            g_scsi_boot_path_logs = 0;
		                            g_scsi_boot_path_last_pc = 0xffffffff;
	                            g_exec_probe_logs = 0;
	                            g_scsi_fn2f_logs = 0;
		                            g_scsi_qloop_logs = 0;
	                            g_scsi_qloop_fe4c_logs = 0;
	                                    g_scsi_qtrace_logs = 0;
	                                    g_scsi_native_iocs_entry_logs = 0;
		                                    g_scsi_low_rts_logs = 0;
		                                    g_scsi_low_rte_logs = 0;
		                                    g_scsi_lowf75_logs = 0;
		                                    g_scsi_f75_code_dumped = 0;
		                                    g_scsi_fa_code_dumped = 0;
		                                    g_scsi_trap_special_logs = 0;
	                                    memset(g_scsi_pc_trace_ring, 0, sizeof(g_scsi_pc_trace_ring));
	                                    memset(g_scsi_sp_trace_ring, 0, sizeof(g_scsi_sp_trace_ring));
	                                    memset(g_scsi_d0_trace_ring, 0, sizeof(g_scsi_d0_trace_ring));
	                                    memset(g_scsi_a0_trace_ring, 0, sizeof(g_scsi_a0_trace_ring));
	                                    g_scsi_pc_trace_pos = 0;
	                                    g_scsi_pc_trace_filled = 0;
	                                    g_scsi_pc_trace_dumped = 0;
	                                    g_scsi_jmpa0_logs = 0;
	                                    g_scsi_jmpa0_fix_logs = 0;
	                                    g_scsi_jmpa0_code_dumped = 0;
	                                    g_scsi_dbg_27c0_logs = 0;
	                                    g_scsi_dbg_27c0_last = 0xffffffffU;

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
    const int execSlice = g_use_synthetic_scsi_hooks ? 32 : CLOCK_SLICE;

    do {
        int m, n = (ICount > execSlice) ? execSlice : ICount;
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
	                        if (g_exec_probe_logs < 8) {
	                            DWORD probePc = C68k_Get_PC(&C68K) & 0x00ffffffU;
	                            DWORD probeTrap15 = Memory_ReadD(47 * 4) & 0x00ffffffU;
	                            DWORD probeIocsF5 = Memory_ReadD(0x000007d4) & 0x00ffffffU;
	                            char execProbe[208];
	                            snprintf(execProbe, sizeof(execProbe),
	                                     "EXEC_PROBE pc=$%08X sr=$%04X d0=%08X d1=%08X trap15=$%08X f5=$%08X use=%d bus=%d mounted=%d",
	                                     (unsigned int)probePc,
	                                     (unsigned int)(C68k_Get_SR(&C68K) & 0xffff),
	                                     (unsigned int)C68k_Get_DReg(&C68K, 0),
	                                     (unsigned int)C68k_Get_DReg(&C68K, 1),
	                                     (unsigned int)probeTrap15,
	                                     (unsigned int)probeIocsF5,
	                                     g_use_synthetic_scsi_hooks,
	                                     g_storage_bus_mode,
	                                     g_scsi0_mounted);
	                            X68000_AppendSCSILog(execProbe);
	                            g_exec_probe_logs++;
	                        }
			                        if (g_use_synthetic_scsi_hooks) {
			                            DWORD curPc = C68k_Get_PC(&C68K) & 0x00ffffff;
			                            {
			                                DWORD curSysPtr = Memory_ReadD(0x00001c98U) & 0x00ffffffU;
			                                if (curSysPtr >= 0x00000400U &&
			                                    curSysPtr < 0x00c00000U &&
			                                    (curSysPtr & 1U) == 0U) {
			                                    WORD op = Memory_ReadW(curSysPtr);
			                                    if (op != 0x0000U && op != 0xffffU) {
			                                        g_scsi_last_sys_ptr_1c98 = curSysPtr;
			                                    }
			                                }
			                            }
			                            if (g_scsi_dbg_27c0_logs < 128) {
			                                DWORD dbg27c0 = Memory_ReadD(0x000027c0U);
			                                if (dbg27c0 != g_scsi_dbg_27c0_last) {
			                                    char dbgLog[160];
			                                    snprintf(dbgLog, sizeof(dbgLog),
			                                             "CPU_DBG_27C0 pc=$%08X val=$%08X w0=%04X w1=%04X",
			                                             (unsigned int)curPc,
			                                             (unsigned int)dbg27c0,
			                                             (unsigned int)Memory_ReadW(0x000027c0U),
			                                             (unsigned int)Memory_ReadW(0x000027c2U));
			                                    X68000_AppendSCSILog(dbgLog);
			                                    g_scsi_dbg_27c0_last = dbg27c0;
			                                    g_scsi_dbg_27c0_logs++;
			                                }
			                            }
			                            if (curPc == 0x000087beU) {
			                                DWORD a0Reg = C68k_Get_AReg(&C68K, 0) & 0x00ffffffU;
			                                WORD a0Op = Memory_ReadW(a0Reg);
			                                if (!g_scsi_jmpa0_code_dumped) {
			                                    char codeLog[320];
			                                    snprintf(codeLog, sizeof(codeLog),
			                                             "CPU_CODE_86F0 %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			                                             (unsigned int)Memory_ReadB(0x000086f0U), (unsigned int)Memory_ReadB(0x000086f1U),
			                                             (unsigned int)Memory_ReadB(0x000086f2U), (unsigned int)Memory_ReadB(0x000086f3U),
			                                             (unsigned int)Memory_ReadB(0x000086f4U), (unsigned int)Memory_ReadB(0x000086f5U),
			                                             (unsigned int)Memory_ReadB(0x000086f6U), (unsigned int)Memory_ReadB(0x000086f7U),
			                                             (unsigned int)Memory_ReadB(0x000086f8U), (unsigned int)Memory_ReadB(0x000086f9U),
			                                             (unsigned int)Memory_ReadB(0x000086faU), (unsigned int)Memory_ReadB(0x000086fbU),
			                                             (unsigned int)Memory_ReadB(0x000086fcU), (unsigned int)Memory_ReadB(0x000086fdU),
			                                             (unsigned int)Memory_ReadB(0x000086feU), (unsigned int)Memory_ReadB(0x000086ffU));
			                                    X68000_AppendSCSILog(codeLog);
			                                    snprintf(codeLog, sizeof(codeLog),
			                                             "CPU_CODE_87B0 %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			                                             (unsigned int)Memory_ReadB(0x000087b0U), (unsigned int)Memory_ReadB(0x000087b1U),
			                                             (unsigned int)Memory_ReadB(0x000087b2U), (unsigned int)Memory_ReadB(0x000087b3U),
			                                             (unsigned int)Memory_ReadB(0x000087b4U), (unsigned int)Memory_ReadB(0x000087b5U),
			                                             (unsigned int)Memory_ReadB(0x000087b6U), (unsigned int)Memory_ReadB(0x000087b7U),
			                                             (unsigned int)Memory_ReadB(0x000087b8U), (unsigned int)Memory_ReadB(0x000087b9U),
			                                             (unsigned int)Memory_ReadB(0x000087baU), (unsigned int)Memory_ReadB(0x000087bbU),
			                                             (unsigned int)Memory_ReadB(0x000087bcU), (unsigned int)Memory_ReadB(0x000087bdU),
			                                             (unsigned int)Memory_ReadB(0x000087beU), (unsigned int)Memory_ReadB(0x000087bfU));
			                                    X68000_AppendSCSILog(codeLog);
			                                    g_scsi_jmpa0_code_dumped = 1;
			                                }
			                                if (a0Reg < 0x00000400U ||
			                                    a0Op == 0x0000U ||
			                                    a0Op == 0xffffU ||
			                                    a0Op == 0x4e73U ||
			                                    a0Op == 0x4e75U) {
			                                    DWORD spReg = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
			                                    DWORD ret0 = Memory_ReadD(spReg + 0);
			                                    DWORD ret1 = Memory_ReadD(spReg + 4);
			                                    DWORD a0Hint = (DWORD)(Memory_ReadW(spReg + 4) & 0x0000ffffU);
			                                    WORD a0HintOp = Memory_ReadW(a0Hint);
			                                    DWORD d0Reg = C68k_Get_DReg(&C68K, 0);
			                                    DWORD ptr1c6a = Memory_ReadD(0x00001c6aU) & 0x00ffffffU;
			                                    if (g_scsi_last_sys_ptr_1c98 >= 0x00000400U &&
			                                        g_scsi_last_sys_ptr_1c98 < 0x00c00000U &&
			                                        (g_scsi_last_sys_ptr_1c98 & 1U) == 0U &&
			                                        g_scsi_jmpa0_repair_logs < 64) {
			                                        WORD altOp = Memory_ReadW(g_scsi_last_sys_ptr_1c98);
			                                        char repLog[224];
			                                        snprintf(repLog, sizeof(repLog),
			                                                 "CPU_JMPA0_CAND pc=$%08X a0=%08X cand=%08X candOp=%04X sp=%08X ret0=%08X",
			                                                 (unsigned int)curPc,
			                                                 (unsigned int)a0Reg,
			                                                 (unsigned int)g_scsi_last_sys_ptr_1c98,
			                                                 (unsigned int)altOp,
			                                                 (unsigned int)spReg,
			                                                 (unsigned int)ret0);
			                                        X68000_AppendSCSILog(repLog);
			                                        g_scsi_jmpa0_repair_logs++;
			                                    }
			                                    // Some Human68k startup paths clear $1C98 (work pointer) just
			                                    // before jumping through $1C6A.  Under forced boot, $1C6A can
			                                    // remain zero and JMP (A0) collapses into low RAM.  If we still
			                                    // have a recent executable $1C98 candidate and D0 carries the
			                                    // "X68\0" tag prepared at $1C18, seed $1C6A/A0 once.
			                                    if (ptr1c6a == 0U &&
			                                        d0Reg == 0x58363800U) {
			                                        DWORD repairTarget = 0U;
			                                        DWORD fnffTarget = Memory_ReadD(0x000007fcU) & 0x00ffffffU;
			                                        WORD candOp = 0U;
			                                        // Prefer current IOCS fn=$FF handler if it points to
			                                        // executable RAM. This keeps the callback path aligned
			                                        // with the kernel-installed dispatcher.
			                                        if (fnffTarget >= 0x00000400U &&
			                                            fnffTarget < 0x00c00000U &&
			                                            (fnffTarget & 1U) == 0U) {
			                                            WORD fnffOp = Memory_ReadW(fnffTarget);
			                                            if (fnffOp != 0x0000U &&
			                                                fnffOp != 0xffffU &&
			                                                fnffOp != 0x4e73U &&
			                                                fnffOp != 0x4e75U) {
			                                                repairTarget = fnffTarget;
			                                            }
			                                        }
			                                        if (repairTarget == 0U &&
			                                            g_scsi_last_sys_ptr_1c98 >= 0x00000400U &&
			                                            g_scsi_last_sys_ptr_1c98 < 0x00c00000U &&
			                                            (g_scsi_last_sys_ptr_1c98 & 1U) == 0U) {
			                                            repairTarget = g_scsi_last_sys_ptr_1c98;
			                                        }
			                                        // Prefer stack-provided continuation hint when it points to
			                                        // executable RAM/ROM. This path commonly reports $000086A2.
			                                        if (a0Hint >= 0x00000400U &&
			                                            a0Hint < 0x00c00000U &&
			                                            (a0Hint & 1U) == 0U) {
			                                            WORD hintOp = Memory_ReadW(a0Hint);
			                                            if (hintOp != 0x0000U &&
			                                                hintOp != 0xffffU &&
			                                                hintOp != 0x4e73U &&
			                                                hintOp != 0x4e75U) {
			                                                repairTarget = a0Hint;
			                                            }
			                                        }
			                                        if (repairTarget != 0U) {
			                                            candOp = Memory_ReadW(repairTarget);
			                                        }
			                                        if (candOp != 0x0000U &&
			                                            candOp != 0xffffU &&
			                                            candOp != 0x4e73U &&
			                                            candOp != 0x4e75U) {
			                                            C68k_Set_AReg(&C68K, 0, repairTarget);
			                                            Memory_WriteD(0x00001c6aU, repairTarget);
			                                            a0Reg = repairTarget;
			                                            a0Op = candOp;
			                                            if (g_scsi_jmpa0_repair_logs < 64) {
			                                                char repLog[256];
			                                                snprintf(repLog, sizeof(repLog),
			                                                         "CPU_JMPA0_REPAIR pc=$%08X a0=%08X d0=%08X ptr1c6a=%08X -> %08X op=%04X",
			                                                         (unsigned int)curPc,
			                                                         (unsigned int)(C68k_Get_AReg(&C68K, 0) & 0x00ffffffU),
			                                                         (unsigned int)d0Reg,
			                                                         (unsigned int)ptr1c6a,
			                                                         (unsigned int)repairTarget,
			                                                         (unsigned int)candOp);
			                                                X68000_AppendSCSILog(repLog);
			                                                g_scsi_jmpa0_repair_logs++;
			                                            }
			                                        }
			                                    }
			                                    if (g_scsi_jmpa0_fix_logs < 64) {
			                                        char fixLog[320];
			                                        snprintf(fixLog, sizeof(fixLog),
			                                                 "CPU_JMPA0_INVALID pc=$%08X a0=%08X a0op=%04X hint=%08X hop=%04X a7=%08X ret0=%08X ret1=%08X",
			                                                 (unsigned int)curPc,
			                                                 (unsigned int)a0Reg,
			                                                 (unsigned int)a0Op,
			                                                 (unsigned int)a0Hint,
			                                                 (unsigned int)a0HintOp,
			                                                 (unsigned int)spReg,
			                                                 (unsigned int)ret0,
			                                                 (unsigned int)ret1);
			                                        X68000_AppendSCSILog(fixLog);
			                                        g_scsi_jmpa0_fix_logs++;
			                                    }
			                                }
			                            }
			                            {
			                                int tr = g_scsi_pc_trace_pos;
			                                g_scsi_pc_trace_ring[tr] = curPc;
			                                g_scsi_sp_trace_ring[tr] = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
			                                g_scsi_d0_trace_ring[tr] = C68k_Get_DReg(&C68K, 0);
			                                g_scsi_a0_trace_ring[tr] = C68k_Get_AReg(&C68K, 0) & 0x00ffffffU;
				                                g_scsi_pc_trace_pos = (tr + 1) & 63;
				                                if (g_scsi_pc_trace_filled < 64) {
				                                    g_scsi_pc_trace_filled++;
				                                }
				                            }
			                            if (curPc >= 0x0000f700U &&
			                                curPc < 0x00010000U &&
			                                (g_scsi_low_rts_logs < 192 || g_scsi_low_rte_logs < 96)) {
			                                WORD op0 = Memory_ReadW(curPc);
			                                if (op0 == 0x4e75U) {
			                                    DWORD a7Reg = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
			                                    DWORD ret0 = Memory_ReadD(a7Reg + 0);
			                                    DWORD ret1 = Memory_ReadD(a7Reg + 4);
			                                    BYTE b0 = Memory_ReadB(a7Reg + 0);
			                                    BYTE b1 = Memory_ReadB(a7Reg + 1);
			                                    BYTE b2 = Memory_ReadB(a7Reg + 2);
			                                    BYTE b3 = Memory_ReadB(a7Reg + 3);
			                                    BYTE b4 = Memory_ReadB(a7Reg + 4);
			                                    BYTE b5 = Memory_ReadB(a7Reg + 5);
			                                    BYTE b6 = Memory_ReadB(a7Reg + 6);
			                                    BYTE b7 = Memory_ReadB(a7Reg + 7);
			                                    DWORD d0Reg = C68k_Get_DReg(&C68K, 0);
			                                    DWORD d1Reg = C68k_Get_DReg(&C68K, 1);
			                                    char rtsLog[256];
			                                    snprintf(rtsLog, sizeof(rtsLog),
			                                             "CPU_LOWRTS pc=$%08X a7=%08X ret0=%08X ret1=%08X d0=%08X d1=%08X bytes=%02X %02X %02X %02X %02X %02X %02X %02X",
			                                             (unsigned int)curPc,
			                                             (unsigned int)a7Reg,
			                                             (unsigned int)ret0,
			                                             (unsigned int)ret1,
			                                             (unsigned int)d0Reg,
			                                             (unsigned int)d1Reg,
			                                             (unsigned int)b0,
			                                             (unsigned int)b1,
			                                             (unsigned int)b2,
			                                             (unsigned int)b3,
			                                             (unsigned int)b4,
			                                             (unsigned int)b5,
			                                             (unsigned int)b6,
			                                             (unsigned int)b7);
			                                    X68000_AppendSCSILog(rtsLog);
			                                    g_scsi_low_rts_logs++;
			                                } else if (op0 == 0x4e73U && g_scsi_low_rte_logs < 96) {
			                                    DWORD a7Reg = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
			                                    WORD frameSr = Memory_ReadW(a7Reg + 0);
			                                    DWORD framePc = Memory_ReadD(a7Reg + 2);
			                                    BYTE b0 = Memory_ReadB(a7Reg + 0);
			                                    BYTE b1 = Memory_ReadB(a7Reg + 1);
			                                    BYTE b2 = Memory_ReadB(a7Reg + 2);
			                                    BYTE b3 = Memory_ReadB(a7Reg + 3);
			                                    BYTE b4 = Memory_ReadB(a7Reg + 4);
			                                    BYTE b5 = Memory_ReadB(a7Reg + 5);
			                                    BYTE b6 = Memory_ReadB(a7Reg + 6);
			                                    BYTE b7 = Memory_ReadB(a7Reg + 7);
			                                    BYTE b8 = Memory_ReadB(a7Reg + 8);
			                                    BYTE b9 = Memory_ReadB(a7Reg + 9);
			                                    DWORD d0Reg = C68k_Get_DReg(&C68K, 0);
			                                    DWORD d1Reg = C68k_Get_DReg(&C68K, 1);
			                                    char rteLog[288];
			                                    snprintf(rteLog, sizeof(rteLog),
			                                             "CPU_LOWRTE pc=$%08X a7=%08X sr=%04X framePc=%08X d0=%08X d1=%08X bytes=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			                                             (unsigned int)curPc,
			                                             (unsigned int)a7Reg,
			                                             (unsigned int)frameSr,
			                                             (unsigned int)framePc,
			                                             (unsigned int)d0Reg,
			                                             (unsigned int)d1Reg,
			                                             (unsigned int)b0,
			                                             (unsigned int)b1,
			                                             (unsigned int)b2,
			                                             (unsigned int)b3,
			                                             (unsigned int)b4,
			                                             (unsigned int)b5,
			                                             (unsigned int)b6,
			                                             (unsigned int)b7,
			                                             (unsigned int)b8,
			                                             (unsigned int)b9);
			                                    X68000_AppendSCSILog(rteLog);
			                                    g_scsi_low_rte_logs++;
			                                }
			                            }
			                            if (curPc >= 0x0000f740U &&
			                                curPc <= 0x0000f76aU &&
			                                g_scsi_lowf75_logs < 128) {
			                                WORD op0 = Memory_ReadW(curPc);
			                                DWORD a7Reg = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
				                                DWORD ret0 = Memory_ReadD(a7Reg + 0);
				                                DWORD ret1 = Memory_ReadD(a7Reg + 4);
				                                DWORD d0Reg = C68k_Get_DReg(&C68K, 0);
				                                DWORD d1Reg = C68k_Get_DReg(&C68K, 1);
				                                DWORD d7Reg = C68k_Get_DReg(&C68K, 7);
				                                DWORD a0Reg = C68k_Get_AReg(&C68K, 0) & 0x00ffffffU;
					                                char f75Log[224];
					                                snprintf(f75Log, sizeof(f75Log),
					                                         "CPU_F75_TRACE pc=$%08X op=%04X a7=%08X ret0=%08X ret1=%08X d0=%08X d1=%08X d7=%08X a0=%08X",
					                                         (unsigned int)curPc,
					                                         (unsigned int)op0,
					                                         (unsigned int)a7Reg,
					                                         (unsigned int)ret0,
					                                         (unsigned int)ret1,
					                                         (unsigned int)d0Reg,
					                                         (unsigned int)d1Reg,
					                                         (unsigned int)d7Reg,
					                                         (unsigned int)a0Reg);
				                                X68000_AppendSCSILog(f75Log);
				                                if (!g_scsi_f75_code_dumped) {
				                                    char codeLog1[256];
				                                    char codeLog2[256];
				                                    snprintf(codeLog1, sizeof(codeLog1),
				                                             "CPU_CODE_F74C %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				                                             (unsigned int)Memory_ReadB(0x0000f74cU),
				                                             (unsigned int)Memory_ReadB(0x0000f74dU),
				                                             (unsigned int)Memory_ReadB(0x0000f74eU),
				                                             (unsigned int)Memory_ReadB(0x0000f74fU),
				                                             (unsigned int)Memory_ReadB(0x0000f750U),
				                                             (unsigned int)Memory_ReadB(0x0000f751U),
				                                             (unsigned int)Memory_ReadB(0x0000f752U),
				                                             (unsigned int)Memory_ReadB(0x0000f753U),
				                                             (unsigned int)Memory_ReadB(0x0000f754U),
				                                             (unsigned int)Memory_ReadB(0x0000f755U),
				                                             (unsigned int)Memory_ReadB(0x0000f756U),
				                                             (unsigned int)Memory_ReadB(0x0000f757U),
				                                             (unsigned int)Memory_ReadB(0x0000f758U),
				                                             (unsigned int)Memory_ReadB(0x0000f759U),
				                                             (unsigned int)Memory_ReadB(0x0000f75aU),
				                                             (unsigned int)Memory_ReadB(0x0000f75bU));
				                                    X68000_AppendSCSILog(codeLog1);
					                                    snprintf(codeLog2, sizeof(codeLog2),
					                                             "CPU_CODE_F75C %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
					                                             (unsigned int)Memory_ReadB(0x0000f75cU),
					                                             (unsigned int)Memory_ReadB(0x0000f75dU),
					                                             (unsigned int)Memory_ReadB(0x0000f75eU),
					                                             (unsigned int)Memory_ReadB(0x0000f75fU),
					                                             (unsigned int)Memory_ReadB(0x0000f760U),
					                                             (unsigned int)Memory_ReadB(0x0000f761U),
					                                             (unsigned int)Memory_ReadB(0x0000f762U),
					                                             (unsigned int)Memory_ReadB(0x0000f763U),
					                                             (unsigned int)Memory_ReadB(0x0000f764U),
					                                             (unsigned int)Memory_ReadB(0x0000f765U),
					                                             (unsigned int)Memory_ReadB(0x0000f766U),
					                                             (unsigned int)Memory_ReadB(0x0000f767U),
					                                             (unsigned int)Memory_ReadB(0x0000f768U),
					                                             (unsigned int)Memory_ReadB(0x0000f769U),
					                                             (unsigned int)Memory_ReadB(0x0000f76aU),
					                                             (unsigned int)Memory_ReadB(0x0000f76bU));
					                                    X68000_AppendSCSILog(codeLog2);
					                                    snprintf(codeLog2, sizeof(codeLog2),
					                                             "CPU_CODE_F76C %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
					                                             (unsigned int)Memory_ReadB(0x0000f76cU),
					                                             (unsigned int)Memory_ReadB(0x0000f76dU),
					                                             (unsigned int)Memory_ReadB(0x0000f76eU),
					                                             (unsigned int)Memory_ReadB(0x0000f76fU),
					                                             (unsigned int)Memory_ReadB(0x0000f770U),
					                                             (unsigned int)Memory_ReadB(0x0000f771U),
					                                             (unsigned int)Memory_ReadB(0x0000f772U),
					                                             (unsigned int)Memory_ReadB(0x0000f773U),
					                                             (unsigned int)Memory_ReadB(0x0000f774U),
					                                             (unsigned int)Memory_ReadB(0x0000f775U),
					                                             (unsigned int)Memory_ReadB(0x0000f776U),
					                                             (unsigned int)Memory_ReadB(0x0000f777U),
					                                             (unsigned int)Memory_ReadB(0x0000f778U),
					                                             (unsigned int)Memory_ReadB(0x0000f779U),
					                                             (unsigned int)Memory_ReadB(0x0000f77aU),
					                                             (unsigned int)Memory_ReadB(0x0000f77bU));
					                                    X68000_AppendSCSILog(codeLog2);
					                                    snprintf(codeLog2, sizeof(codeLog2),
					                                             "CPU_CODE_F77C %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
					                                             (unsigned int)Memory_ReadB(0x0000f77cU),
					                                             (unsigned int)Memory_ReadB(0x0000f77dU),
					                                             (unsigned int)Memory_ReadB(0x0000f77eU),
					                                             (unsigned int)Memory_ReadB(0x0000f77fU),
					                                             (unsigned int)Memory_ReadB(0x0000f780U),
					                                             (unsigned int)Memory_ReadB(0x0000f781U),
					                                             (unsigned int)Memory_ReadB(0x0000f782U),
					                                             (unsigned int)Memory_ReadB(0x0000f783U),
					                                             (unsigned int)Memory_ReadB(0x0000f784U),
					                                             (unsigned int)Memory_ReadB(0x0000f785U),
					                                             (unsigned int)Memory_ReadB(0x0000f786U),
					                                             (unsigned int)Memory_ReadB(0x0000f787U),
					                                             (unsigned int)Memory_ReadB(0x0000f788U),
					                                             (unsigned int)Memory_ReadB(0x0000f789U),
					                                             (unsigned int)Memory_ReadB(0x0000f78aU),
					                                             (unsigned int)Memory_ReadB(0x0000f78bU));
					                                    X68000_AppendSCSILog(codeLog2);
					                                    g_scsi_f75_code_dumped = 1;
					                                }
				                                g_scsi_lowf75_logs++;
				                            }
				                            if (!g_scsi_fa_code_dumped &&
				                                curPc >= 0x0000fa1aU &&
				                                curPc <= 0x0000fa30U) {
				                                char faLog[256];
				                                snprintf(faLog, sizeof(faLog),
				                                         "CPU_CODE_FA1A %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				                                         (unsigned int)Memory_ReadB(0x0000fa1aU),
				                                         (unsigned int)Memory_ReadB(0x0000fa1bU),
				                                         (unsigned int)Memory_ReadB(0x0000fa1cU),
				                                         (unsigned int)Memory_ReadB(0x0000fa1dU),
				                                         (unsigned int)Memory_ReadB(0x0000fa1eU),
				                                         (unsigned int)Memory_ReadB(0x0000fa1fU),
				                                         (unsigned int)Memory_ReadB(0x0000fa20U),
				                                         (unsigned int)Memory_ReadB(0x0000fa21U),
				                                         (unsigned int)Memory_ReadB(0x0000fa22U),
				                                         (unsigned int)Memory_ReadB(0x0000fa23U),
				                                         (unsigned int)Memory_ReadB(0x0000fa24U),
				                                         (unsigned int)Memory_ReadB(0x0000fa25U),
				                                         (unsigned int)Memory_ReadB(0x0000fa26U),
				                                         (unsigned int)Memory_ReadB(0x0000fa27U),
				                                         (unsigned int)Memory_ReadB(0x0000fa28U),
				                                         (unsigned int)Memory_ReadB(0x0000fa29U));
				                                X68000_AppendSCSILog(faLog);
				                                g_scsi_fa_code_dumped = 1;
				                            }
		                            if (((curPc >= 0x00002000U && curPc < 0x00002400U) ||
		                                 (curPc >= SCSI_SYNTH_BOOT_ENTRY && curPc < (SCSI_SYNTH_BOOT_ENTRY + 0x20U))) &&
		                                curPc != g_scsi_boot_path_last_pc &&
                                g_scsi_boot_path_logs < 160) {
                                WORD op0 = Memory_ReadW(curPc + 0);
                                char bootPathLog[216];
                                snprintf(bootPathLog, sizeof(bootPathLog),
                                         "SCSI_BOOT_PATH pc=$%08X sr=$%04X d0=%08X d1=%08X d2=%08X d3=%08X d4=%08X d5=%08X a1=%08X a7=%08X op=%04X",
                                         (unsigned int)curPc,
                                         (unsigned int)(C68k_Get_SR(&C68K) & 0xffff),
                                         (unsigned int)C68k_Get_DReg(&C68K, 0),
                                         (unsigned int)C68k_Get_DReg(&C68K, 1),
                                         (unsigned int)C68k_Get_DReg(&C68K, 2),
                                         (unsigned int)C68k_Get_DReg(&C68K, 3),
                                         (unsigned int)C68k_Get_DReg(&C68K, 4),
                                         (unsigned int)C68k_Get_DReg(&C68K, 5),
                                         (unsigned int)C68k_Get_AReg(&C68K, 1),
                                         (unsigned int)C68k_Get_AReg(&C68K, 7),
                                         (unsigned int)op0);
		                                X68000_AppendSCSILog(bootPathLog);
		                                g_scsi_boot_path_last_pc = curPc;
		                                g_scsi_boot_path_logs++;
		                            }
	                            if (curPc >= SCSI_SYNTH_TRAP15_ENTRY &&
	                                curPc < (SCSI_SYNTH_TRAP15_ENTRY + 0x40) &&
	                                g_scsi_trap15_pc_logs < 512) {
		                                DWORD d0Reg = C68k_Get_DReg(&C68K, 0);
		                                DWORD d1Reg = C68k_Get_DReg(&C68K, 1);
		                                BYTE fn = (BYTE)(d0Reg & 0xff);
		                                DWORD fnRaw = Memory_ReadD(0x00000400 + (DWORD)fn * 4);
		                                DWORD fnEntry = fnRaw & 0x00ffffffU;
		                                DWORD vec47 = Memory_ReadD(47 * 4) & 0x00ffffffU;
		                                DWORD a0Reg = C68k_Get_AReg(&C68K, 0);
		                                char trapPcLog[224];
		                                snprintf(trapPcLog, sizeof(trapPcLog),
		                                         "SCSI_TRAP15_PC pc=$%08X d0=%08X d1=%08X a0=%08X fn=$%02X fnRaw=$%08X fnEntry=$%08X vec47=$%08X",
		                                         (unsigned int)curPc,
		                                         (unsigned int)d0Reg,
		                                         (unsigned int)d1Reg,
		                                         (unsigned int)a0Reg,
		                                         (unsigned int)fn,
		                                         (unsigned int)fnRaw,
		                                         (unsigned int)fnEntry,
		                                         (unsigned int)vec47);
                                X68000_AppendSCSILog(trapPcLog);
                                g_scsi_trap15_pc_logs++;
                                if (curPc == SCSI_SYNTH_TRAP15_ENTRY &&
                                    fn == 0x2f &&
                                    g_scsi_fn2f_logs < 96) {
                                    DWORD a1Reg = C68k_Get_AReg(&C68K, 1) & 0x00ffffffU;
                                    DWORD d2Reg = C68k_Get_DReg(&C68K, 2);
                                    DWORD d3Reg = C68k_Get_DReg(&C68K, 3);
                                    DWORD d4Reg = C68k_Get_DReg(&C68K, 4);
                                    char fn2fLog[192];
                                    snprintf(fn2fLog, sizeof(fn2fLog),
                                             "SCSI_TRAP15_FN2F a1=%08X d1=%08X d2=%08X d3=%08X d4=%08X",
                                             (unsigned int)a1Reg,
                                             (unsigned int)d1Reg,
                                             (unsigned int)d2Reg,
                                             (unsigned int)d3Reg,
                                             (unsigned int)d4Reg);
	                                    X68000_AppendSCSILog(fn2fLog);
	                                    g_scsi_fn2f_logs++;
	                                }
		                            }
	                            if (curPc == SCSI_SYNTH_TRAP15_ENTRY &&
	                                g_scsi_trap_special_logs < 128) {
	                                DWORD d0Reg = C68k_Get_DReg(&C68K, 0);
	                                BYTE fn = (BYTE)(d0Reg & 0xff);
	                                if (fn == 0x00 || fn == 0x01 || fn == 0xfd || fn == 0xff) {
	                                    DWORD fnRaw = Memory_ReadD(0x00000400 + (DWORD)fn * 4);
	                                    DWORD fnEntry = fnRaw & 0x00ffffffU;
	                                    DWORD d1Reg = C68k_Get_DReg(&C68K, 1);
	                                    DWORD a7Reg = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
	                                    DWORD ret0 = Memory_ReadD(a7Reg + 0);
	                                    char specialLog[224];
	                                    snprintf(specialLog, sizeof(specialLog),
	                                             "SCSI_TRAP15_SPECIAL fn=$%02X d0=%08X d1=%08X entry=$%08X a7=%08X ret0=%08X",
	                                             (unsigned int)fn,
	                                             (unsigned int)d0Reg,
	                                             (unsigned int)d1Reg,
	                                             (unsigned int)fnEntry,
	                                             (unsigned int)a7Reg,
	                                             (unsigned int)ret0);
	                                    X68000_AppendSCSILog(specialLog);
	                                    g_scsi_trap_special_logs++;
	                                }
	                            }
			                            if (curPc >= SCSI_SYNTH_IOCS_ENTRY &&
			                                curPc < (SCSI_SYNTH_IOCS_ENTRY + 0x10) &&
			                                g_scsi_iocs_entry_pc_logs < 1024) {
			                                DWORD d0Reg = C68k_Get_DReg(&C68K, 0);
			                                DWORD d1Reg = C68k_Get_DReg(&C68K, 1);
			                                BYTE fn = (BYTE)(d0Reg & 0xff);
			                                DWORD fnEntry = Memory_ReadD(0x00000400 + (DWORD)fn * 4) & 0x00ffffffU;
			                                DWORD a7Reg = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
			                                DWORD ret0 = Memory_ReadD(a7Reg + 0);
			                                char iocsPcLog[192];
			                                snprintf(iocsPcLog, sizeof(iocsPcLog),
			                                         "SCSI_IOCS_ENTRY_PC pc=$%08X d0=%08X d1=%08X fn=$%02X entry=$%08X a7=%08X ret0=%08X",
			                                         (unsigned int)curPc,
			                                         (unsigned int)d0Reg,
			                                         (unsigned int)d1Reg,
			                                         (unsigned int)fn,
			                                         (unsigned int)fnEntry,
			                                         (unsigned int)a7Reg,
			                                         (unsigned int)ret0);
			                                X68000_AppendSCSILog(iocsPcLog);
			                                g_scsi_iocs_entry_pc_logs++;
			                            }
			                            {
			                                DWORD fnFFEntry = Memory_ReadD(0x000007fc) & 0x00ffffffU;
			                                if (fnFFEntry >= 0x00000400U &&
			                                    curPc == fnFFEntry &&
			                                    g_scsi_fnff_entry_logs < 64) {
			                                    DWORD d0Reg = C68k_Get_DReg(&C68K, 0);
			                                    DWORD d1Reg = C68k_Get_DReg(&C68K, 1);
			                                    DWORD d2Reg = C68k_Get_DReg(&C68K, 2);
			                                    DWORD a0Reg = C68k_Get_AReg(&C68K, 0) & 0x00ffffffU;
			                                    DWORD a1Reg = C68k_Get_AReg(&C68K, 1) & 0x00ffffffU;
			                                    DWORD a7Reg = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
			                                    WORD op0 = Memory_ReadW(curPc);
			                                    DWORD ret0 = Memory_ReadD(a7Reg + 0);
			                                    char fnffLog[256];
			                                    snprintf(fnffLog, sizeof(fnffLog),
			                                             "SCSI_IOCS_FF_ENTRY pc=$%08X op0=%04X d0=%08X d1=%08X d2=%08X a0=%08X a1=%08X a7=%08X ret0=%08X",
			                                             (unsigned int)curPc,
			                                             (unsigned int)op0,
			                                             (unsigned int)d0Reg,
			                                             (unsigned int)d1Reg,
			                                             (unsigned int)d2Reg,
			                                             (unsigned int)a0Reg,
			                                             (unsigned int)a1Reg,
			                                             (unsigned int)a7Reg,
			                                             (unsigned int)ret0);
			                                    X68000_AppendSCSILog(fnffLog);
			                                    g_scsi_fnff_entry_logs++;
			                                }
			                            }
			                            if (curPc == 0x000087beU &&
			                                g_scsi_jmpa0_logs < 64) {
			                                DWORD d0Reg = C68k_Get_DReg(&C68K, 0);
			                                DWORD d1Reg = C68k_Get_DReg(&C68K, 1);
			                                DWORD d2Reg = C68k_Get_DReg(&C68K, 2);
			                                DWORD a0Reg = C68k_Get_AReg(&C68K, 0) & 0x00ffffffU;
			                                DWORD a1Reg = C68k_Get_AReg(&C68K, 1) & 0x00ffffffU;
			                                DWORD a2Reg = C68k_Get_AReg(&C68K, 2) & 0x00ffffffU;
			                                DWORD a7Reg = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
			                                WORD op0 = Memory_ReadW(curPc);
			                                WORD opA0 = Memory_ReadW(a0Reg);
			                                DWORD ret0 = Memory_ReadD(a7Reg + 0);
			                                DWORD ret1 = Memory_ReadD(a7Reg + 4);
			                                char jmpLog[320];
			                                snprintf(jmpLog, sizeof(jmpLog),
			                                         "CPU_JMPA0 pc=$%08X op=%04X d0=%08X d1=%08X d2=%08X a0=%08X a1=%08X a2=%08X a7=%08X a0op=%04X ret0=%08X ret1=%08X",
			                                         (unsigned int)curPc,
			                                         (unsigned int)op0,
			                                         (unsigned int)d0Reg,
			                                         (unsigned int)d1Reg,
			                                         (unsigned int)d2Reg,
			                                         (unsigned int)a0Reg,
			                                         (unsigned int)a1Reg,
			                                         (unsigned int)a2Reg,
			                                         (unsigned int)a7Reg,
			                                         (unsigned int)opA0,
			                                         (unsigned int)ret0,
			                                         (unsigned int)ret1);
			                                X68000_AppendSCSILog(jmpLog);
			                                g_scsi_jmpa0_logs++;
			                            }
			                            if (curPc == SCSI_SYNTH_IOCS_DEFAULT &&
			                                g_scsi_iocs_default_hit_logs < 64) {
		                                DWORD d0Reg = C68k_Get_DReg(&C68K, 0);
		                                BYTE fn = (BYTE)(d0Reg & 0xff);
		                                DWORD fnEntry = Memory_ReadD(0x00000400 + (DWORD)fn * 4) & 0x00ffffffU;
		                                DWORD a7 = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
		                                DWORD ret0 = Memory_ReadD(a7 + 0);
		                                char defaultLog[160];
		                                snprintf(defaultLog, sizeof(defaultLog),
		                                         "SCSI_IOCS_DEFAULT_HIT fn=$%02X d0=%08X entry=$%08X a7=%08X ret0=%08X",
		                                         (unsigned int)fn,
		                                         (unsigned int)d0Reg,
		                                         (unsigned int)fnEntry,
		                                         (unsigned int)a7,
		                                         (unsigned int)ret0);
		                                X68000_AppendSCSILog(defaultLog);
		                                g_scsi_iocs_default_hit_logs++;
		                            }
				                            if (curPc < 0x00000400U &&
				                                g_scsi_lowpc_logs < 16) {
			                                if (g_scsi_lowpc_logs == 0) {
			                                    char codeLog[256];
			                                    snprintf(codeLog, sizeof(codeLog),
			                                             "CPU_CODE_F74C %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			                                             (unsigned int)Memory_ReadB(0x0000f74c),
			                                             (unsigned int)Memory_ReadB(0x0000f74d),
			                                             (unsigned int)Memory_ReadB(0x0000f74e),
			                                             (unsigned int)Memory_ReadB(0x0000f74f),
			                                             (unsigned int)Memory_ReadB(0x0000f750),
			                                             (unsigned int)Memory_ReadB(0x0000f751),
			                                             (unsigned int)Memory_ReadB(0x0000f752),
			                                             (unsigned int)Memory_ReadB(0x0000f753),
			                                             (unsigned int)Memory_ReadB(0x0000f754),
			                                             (unsigned int)Memory_ReadB(0x0000f755),
			                                             (unsigned int)Memory_ReadB(0x0000f756),
			                                             (unsigned int)Memory_ReadB(0x0000f757),
			                                             (unsigned int)Memory_ReadB(0x0000f758),
			                                             (unsigned int)Memory_ReadB(0x0000f759),
			                                             (unsigned int)Memory_ReadB(0x0000f75a),
			                                             (unsigned int)Memory_ReadB(0x0000f75b));
			                                    X68000_AppendSCSILog(codeLog);
			                                    snprintf(codeLog, sizeof(codeLog),
			                                             "CPU_CODE_F75C %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			                                             (unsigned int)Memory_ReadB(0x0000f75c),
			                                             (unsigned int)Memory_ReadB(0x0000f75d),
			                                             (unsigned int)Memory_ReadB(0x0000f75e),
			                                             (unsigned int)Memory_ReadB(0x0000f75f),
			                                             (unsigned int)Memory_ReadB(0x0000f760),
			                                             (unsigned int)Memory_ReadB(0x0000f761),
			                                             (unsigned int)Memory_ReadB(0x0000f762),
			                                             (unsigned int)Memory_ReadB(0x0000f763),
			                                             (unsigned int)Memory_ReadB(0x0000f764),
			                                             (unsigned int)Memory_ReadB(0x0000f765),
			                                             (unsigned int)Memory_ReadB(0x0000f766),
			                                             (unsigned int)Memory_ReadB(0x0000f767),
			                                             (unsigned int)Memory_ReadB(0x0000f768),
			                                             (unsigned int)Memory_ReadB(0x0000f769),
			                                             (unsigned int)Memory_ReadB(0x0000f76a),
			                                             (unsigned int)Memory_ReadB(0x0000f76b));
			                                    X68000_AppendSCSILog(codeLog);
			                                    snprintf(codeLog, sizeof(codeLog),
			                                             "CPU_CODE_F8C0 %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			                                             (unsigned int)Memory_ReadB(0x0000f8c0),
			                                             (unsigned int)Memory_ReadB(0x0000f8c1),
			                                             (unsigned int)Memory_ReadB(0x0000f8c2),
			                                             (unsigned int)Memory_ReadB(0x0000f8c3),
			                                             (unsigned int)Memory_ReadB(0x0000f8c4),
			                                             (unsigned int)Memory_ReadB(0x0000f8c5),
			                                             (unsigned int)Memory_ReadB(0x0000f8c6),
			                                             (unsigned int)Memory_ReadB(0x0000f8c7),
			                                             (unsigned int)Memory_ReadB(0x0000f8c8),
			                                             (unsigned int)Memory_ReadB(0x0000f8c9),
			                                             (unsigned int)Memory_ReadB(0x0000f8ca),
			                                             (unsigned int)Memory_ReadB(0x0000f8cb),
			                                             (unsigned int)Memory_ReadB(0x0000f8cc),
			                                             (unsigned int)Memory_ReadB(0x0000f8cd),
			                                             (unsigned int)Memory_ReadB(0x0000f8ce),
			                                             (unsigned int)Memory_ReadB(0x0000f8cf));
			                                    X68000_AppendSCSILog(codeLog);
			                                    snprintf(codeLog, sizeof(codeLog),
			                                             "CPU_CODE_F8D0 %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			                                             (unsigned int)Memory_ReadB(0x0000f8d0),
			                                             (unsigned int)Memory_ReadB(0x0000f8d1),
			                                             (unsigned int)Memory_ReadB(0x0000f8d2),
			                                             (unsigned int)Memory_ReadB(0x0000f8d3),
			                                             (unsigned int)Memory_ReadB(0x0000f8d4),
			                                             (unsigned int)Memory_ReadB(0x0000f8d5),
			                                             (unsigned int)Memory_ReadB(0x0000f8d6),
			                                             (unsigned int)Memory_ReadB(0x0000f8d7),
			                                             (unsigned int)Memory_ReadB(0x0000f8d8),
			                                             (unsigned int)Memory_ReadB(0x0000f8d9),
			                                             (unsigned int)Memory_ReadB(0x0000f8da),
			                                             (unsigned int)Memory_ReadB(0x0000f8db),
			                                             (unsigned int)Memory_ReadB(0x0000f8dc),
			                                             (unsigned int)Memory_ReadB(0x0000f8dd),
			                                             (unsigned int)Memory_ReadB(0x0000f8de),
			                                             (unsigned int)Memory_ReadB(0x0000f8df));
			                                    X68000_AppendSCSILog(codeLog);
			                                    for (DWORD dump = 0x0000f640U; dump < 0x0000fb40U; dump += 0x10U) {
			                                        char blockLog[256];
			                                        snprintf(blockLog, sizeof(blockLog),
			                                                 "CPU_CODE_BLK %06X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			                                                 (unsigned int)dump,
			                                                 (unsigned int)Memory_ReadB(dump + 0),
			                                                 (unsigned int)Memory_ReadB(dump + 1),
			                                                 (unsigned int)Memory_ReadB(dump + 2),
			                                                 (unsigned int)Memory_ReadB(dump + 3),
			                                                 (unsigned int)Memory_ReadB(dump + 4),
			                                                 (unsigned int)Memory_ReadB(dump + 5),
			                                                 (unsigned int)Memory_ReadB(dump + 6),
			                                                 (unsigned int)Memory_ReadB(dump + 7),
			                                                 (unsigned int)Memory_ReadB(dump + 8),
			                                                 (unsigned int)Memory_ReadB(dump + 9),
			                                                 (unsigned int)Memory_ReadB(dump + 10),
			                                                 (unsigned int)Memory_ReadB(dump + 11),
			                                                 (unsigned int)Memory_ReadB(dump + 12),
			                                                 (unsigned int)Memory_ReadB(dump + 13),
			                                                 (unsigned int)Memory_ReadB(dump + 14),
			                                                 (unsigned int)Memory_ReadB(dump + 15));
			                                        X68000_AppendSCSILog(blockLog);
			                                    }
			                                }
			                                if (!g_scsi_pc_trace_dumped && g_scsi_pc_trace_filled > 0) {
				                                    int traceCount = (g_scsi_pc_trace_filled < 32) ? g_scsi_pc_trace_filled : 32;
				                                    for (int ti = 0; ti < traceCount; ++ti) {
				                                        int idx = (g_scsi_pc_trace_pos - traceCount + ti + 64) & 63;
				                                        DWORD trPc = g_scsi_pc_trace_ring[idx];
				                                        DWORD trSp = g_scsi_sp_trace_ring[idx];
				                                        DWORD trD0 = g_scsi_d0_trace_ring[idx];
				                                        DWORD trA0 = g_scsi_a0_trace_ring[idx];
				                                        WORD trOp = Memory_ReadW(trPc);
				                                        WORD trA0Op = Memory_ReadW(trA0);
				                                        DWORD trStk0 = Memory_ReadD(trSp + 0);
				                                        DWORD trStk1 = Memory_ReadD(trSp + 4);
				                                        char traceLog[256];
				                                        snprintf(traceLog, sizeof(traceLog),
				                                                 "CPU_PRELOW idx=%02d pc=$%08X sp=%08X d0=%08X a0=%08X op=%04X a0op=%04X stk0=%08X stk1=%08X",
				                                                 ti,
				                                                 (unsigned int)trPc,
				                                                 (unsigned int)trSp,
				                                                 (unsigned int)trD0,
				                                                 (unsigned int)trA0,
				                                                 (unsigned int)trOp,
				                                                 (unsigned int)trA0Op,
				                                                 (unsigned int)trStk0,
				                                                 (unsigned int)trStk1);
				                                        X68000_AppendSCSILog(traceLog);
				                                    }
				                                    g_scsi_pc_trace_dumped = 1;
			                                }
			                                DWORD d0Reg = C68k_Get_DReg(&C68K, 0);
			                                BYTE fn = (BYTE)(d0Reg & 0xff);
			                                DWORD fnEntry = Memory_ReadD(0x00000400 + (DWORD)fn * 4) & 0x00ffffffU;
			                                DWORD fn17 = Memory_ReadD(0x0000045c) & 0x00ffffffU;
			                                DWORD fn1e = Memory_ReadD(0x00000478) & 0x00ffffffU;
			                                DWORD fn1f = Memory_ReadD(0x0000047c) & 0x00ffffffU;
			                                DWORD fnFF = Memory_ReadD(0x000007fc) & 0x00ffffffU;
			                                DWORD fnF5 = Memory_ReadD(0x000007d4) & 0x00ffffffU;
			                                DWORD vec2 = Memory_ReadD(0x00000008) & 0x00ffffffU;
			                                DWORD vec3 = Memory_ReadD(0x0000000c) & 0x00ffffffU;
			                                DWORD vec4 = Memory_ReadD(0x00000010) & 0x00ffffffU;
			                                DWORD vec5 = Memory_ReadD(0x00000014) & 0x00ffffffU;
				                                DWORD vec8 = Memory_ReadD(0x00000020) & 0x00ffffffU;
				                                DWORD vec9 = Memory_ReadD(0x00000024) & 0x00ffffffU;
					                                DWORD vec14 = Memory_ReadD(0x00000038) & 0x00ffffffU;
					                                DWORD vec15 = Memory_ReadD(0x0000003c) & 0x00ffffffU;
					                                DWORD vec24 = Memory_ReadD(0x00000060) & 0x00ffffffU;
					                                DWORD vec25 = Memory_ReadD(0x00000064) & 0x00ffffffU;
					                                DWORD vec26 = Memory_ReadD(0x00000068) & 0x00ffffffU;
					                                DWORD vec27 = Memory_ReadD(0x0000006c) & 0x00ffffffU;
					                                DWORD vec28 = Memory_ReadD(0x00000070) & 0x00ffffffU;
					                                DWORD vec29 = Memory_ReadD(0x00000074) & 0x00ffffffU;
					                                DWORD vec30 = Memory_ReadD(0x00000078) & 0x00ffffffU;
					                                DWORD vec31 = Memory_ReadD(0x0000007c) & 0x00ffffffU;
					                                DWORD a7 = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
					                                DWORD retm1 = Memory_ReadD((a7 - 4U) & 0x00ffffffU);
					                                DWORD ret0 = Memory_ReadD(a7 + 0);
					                                DWORD ret1 = Memory_ReadD(a7 + 4);
					                                DWORD ret2 = Memory_ReadD(a7 + 8);
				                                DWORD ret3 = Memory_ReadD(a7 + 12);
				                                char lowPcLog[448];
				                                snprintf(lowPcLog, sizeof(lowPcLog),
				                                         "CPU_LOWPC pc=$%08X sr=$%04X d0=%08X fn=$%02X fnEntry=$%08X fn17=$%08X fn1E=$%08X fn1F=$%08X fnFF=$%08X fnF5=$%08X v2=$%08X v3=$%08X v4=$%08X v5=$%08X v8=$%08X v9=$%08X v14=$%08X v15=$%08X a7=%08X stk_m1=%08X stk0=%08X stk1=%08X stk2=%08X stk3=%08X",
				                                         (unsigned int)curPc,
				                                         (unsigned int)(C68k_Get_SR(&C68K) & 0xffff),
				                                         (unsigned int)d0Reg,
			                                         (unsigned int)fn,
			                                         (unsigned int)fnEntry,
			                                         (unsigned int)fn17,
			                                         (unsigned int)fn1e,
			                                         (unsigned int)fn1f,
			                                         (unsigned int)fnFF,
			                                         (unsigned int)fnF5,
			                                         (unsigned int)vec2,
			                                         (unsigned int)vec3,
			                                         (unsigned int)vec4,
			                                         (unsigned int)vec5,
			                                         (unsigned int)vec8,
				                                         (unsigned int)vec9,
				                                         (unsigned int)vec14,
				                                         (unsigned int)vec15,
				                                         (unsigned int)a7,
				                                         (unsigned int)retm1,
				                                         (unsigned int)ret0,
				                                         (unsigned int)ret1,
					                                         (unsigned int)ret2,
					                                         (unsigned int)ret3);
			                                X68000_AppendSCSILog(lowPcLog);
			                                if (g_scsi_lowpc_logs == 0) {
				                                char lowVecLog[256];
				                                snprintf(lowVecLog, sizeof(lowVecLog),
				                                         "CPU_LOWVEC v24=$%08X v25=$%08X v26=$%08X v27=$%08X v28=$%08X v29=$%08X v30=$%08X v31=$%08X",
				                                         (unsigned int)vec24,
				                                         (unsigned int)vec25,
				                                         (unsigned int)vec26,
				                                         (unsigned int)vec27,
				                                         (unsigned int)vec28,
				                                         (unsigned int)vec29,
				                                         (unsigned int)vec30,
				                                         (unsigned int)vec31);
				                                X68000_AppendSCSILog(lowVecLog);
			                                }
			                                g_scsi_lowpc_logs++;
			                            }
		                            int isScsiFdcWaitPc = X68000_IsScsiFdcWaitPc(curPc);
	                            g_scsi_iocs_check_counter++;
	                            // Full IOCS table repair on every instruction is too heavy
	                            // in DBRA wait loops (for example around $0000884E).
	                            // Keep repair active, but run it at a fixed interval.
	                            if (g_scsi_iocs_check_counter >= kScsiIocsRuntimeCheckInterval) {
	                                DWORD iocsVector;
	                                DWORD trap15Vector;
		                                g_scsi_iocs_check_counter = 0;
		                                iocsVector = Memory_ReadD(0x000007d4) & 0x00ffffff;
		                                trap15Vector = Memory_ReadD(47 * 4) & 0x00ffffff;
                                g_last_scsi_iocs_vector = iocsVector;
                                if (!SCSI_IsDeviceLinked() &&
                                    iocsVector != 0 &&
                                    iocsVector != SCSI_SYNTH_IOCS_ENTRY) {
                                    Memory_WriteD(0x000007d4, SCSI_SYNTH_IOCS_ENTRY);
                                    if (g_scsi_iocs_rehook_logs < 64) {
                                        char rehookLog[128];
                                        snprintf(rehookLog, sizeof(rehookLog),
                                                 "SCSI_IOCS_REHOOK $0007D4 old=$%08X new=$%08X linked=%d",
                                                 (unsigned int)iocsVector,
                                                 (unsigned int)SCSI_SYNTH_IOCS_ENTRY,
                                                 SCSI_IsDeviceLinked() ? 1 : 0);
                                        X68000_AppendSCSILog(rehookLog);
                                        g_scsi_iocs_rehook_logs++;
                                    }
                                }
                                if (trap15Vector != 0 &&
                                    trap15Vector != SCSI_SYNTH_TRAP15_ENTRY) {
                                    Memory_WriteD(47 * 4, SCSI_SYNTH_TRAP15_ENTRY);
                                    if (g_scsi_trap15_rehook_logs < 64) {
                                        char rehookLog[128];
                                        snprintf(rehookLog, sizeof(rehookLog),
                                                 "SCSI_TRAP15_REHOOK $0000BC old=$%08X new=$%08X",
                                                 (unsigned int)trap15Vector,
                                                 (unsigned int)SCSI_SYNTH_TRAP15_ENTRY);
                                        X68000_AppendSCSILog(rehookLog);
                                        g_scsi_trap15_rehook_logs++;
                                    }
                                }
                                // Runtime IOCS guard:
                                // validate all entries and repair broken pointers before dispatch.
	                                {
	                                    for (int fn = 0; fn < 256; ++fn) {
	                                        DWORD addr = 0x00000400 + (DWORD)fn * 4;
	                                        DWORD curRaw = Memory_ReadD(addr);
	                                        DWORD curEff = X68000_NormalizeIocsHandler(curRaw, NULL);
		                                        if (fn == 0xff) {
		                                            // Human68k installs IOCS $FF to RAM handlers (for example $000086F2).
		                                            // Keep RAM hooks, but reject broken targets (opcode 0000/FFFF) so
		                                            // we don't spin on corrupted handlers such as $0000859C.
		                                            if (!X68000_IsExecutableIocsHandler(curEff)) {
		                                                DWORD repair = SCSI_SYNTH_IOCS_FF_FALLBACK;
		                                                Memory_WriteD(addr, repair);
	                                                if (g_scsi_iocs_fix_logs < 64) {
	                                                    char fixLog[160];
	                                                    snprintf(fixLog, sizeof(fixLog),
	                                                             "SCSI_IOCS_REPAIR fn=$%02X old=$%08X new=$%08X",
	                                                             (unsigned int)fn,
	                                                             (unsigned int)(curRaw & 0x00ffffffU),
	                                                             (unsigned int)repair);
	                                                    X68000_AppendSCSILog(fixLog);
	                                                    g_scsi_iocs_fix_logs++;
	                                                }
	                                            }
	                                            continue;
	                                        }
	                                        if (!X68000_IsExecutableIocsHandler(curEff)) {
	                                            DWORD repair = 0;
	                                            if (g_scsi_iocs_shadow_valid) {
	                                                DWORD shadowRaw = g_scsi_iocs_shadow[fn] & 0x00ffffffU;
	                                                DWORD shadowEff = X68000_NormalizeIocsHandler(shadowRaw, NULL);
	                                                if (X68000_IsExecutableIocsHandler(shadowEff)) {
	                                                    repair = shadowRaw;
                                                }
                                            }
                                            if (repair == 0) {
                                                repair = X68000_GetSyntheticIocsFallback((BYTE)fn);
                                            }
                                            Memory_WriteD(addr, repair);
                                            if (g_scsi_iocs_fix_logs < 64) {
                                                char fixLog[160];
                                                snprintf(fixLog, sizeof(fixLog),
                                                         "SCSI_IOCS_REPAIR fn=$%02X old=$%08X new=$%08X",
                                                         (unsigned int)fn,
                                                         (unsigned int)(curRaw & 0x00ffffffU),
                                                         (unsigned int)repair);
                                                X68000_AppendSCSILog(fixLog);
                                                g_scsi_iocs_fix_logs++;
                                            }
                                        }
                                    }
                                }
	                                // Keep runtime IOCS handlers selected by IPL/OS.
	                                // The generic IOCS repair path above already patches
	                                // clearly invalid entries to safe synthetic fallbacks.
	                                // Exceptions:
	                                //   - SCSI IOCS fn=$20-$2F stay on synthetic direct handlers.
	                                //   - fn=$33/$34 and fn=$3C remain pinned to safe synthetic stubs.
	                                {
	                                    for (int fn = 0x20; fn <= 0x2f; ++fn) {
	                                        DWORD addr = 0x00000400 + (DWORD)fn * 4;
	                                        DWORD cur = Memory_ReadD(addr);
	                                        if ((cur & 0x00ffffffU) != SCSI_SYNTH_IOCS_DIRECT) {
	                                            Memory_WriteD(addr, SCSI_SYNTH_IOCS_DIRECT);
	                                        }
	                                    }
	                                    DWORD addr17 = 0x00000400 + (DWORD)0x17 * 4;
	                                    DWORD addr18 = 0x00000400 + (DWORD)0x18 * 4;
	                                    DWORD addr1e = 0x00000400 + (DWORD)0x1e * 4;
                                    DWORD addr1f = 0x00000400 + (DWORD)0x1f * 4;
                                    DWORD addr33 = 0x00000400 + (DWORD)0x33 * 4;
                                    DWORD addr34 = 0x00000400 + (DWORD)0x34 * 4;
                                    DWORD addr3c = 0x00000400 + (DWORD)0x3c * 4;
	                                    DWORD addr46 = 0x00000400 + (DWORD)0x46 * 4;
	                                    DWORD addr47 = 0x00000400 + (DWORD)0x47 * 4;
	                                    DWORD addr4f = 0x00000400 + (DWORD)0x4f * 4;
	                                    DWORD addr54 = 0x00000400 + (DWORD)0x54 * 4;
	                                    DWORD addr55 = 0x00000400 + (DWORD)0x55 * 4;
	                                    DWORD addr56 = 0x00000400 + (DWORD)0x56 * 4;
	                                    DWORD addr57 = 0x00000400 + (DWORD)0x57 * 4;
	                                    DWORD addr8e = 0x00000400 + (DWORD)0x8e * 4;
	                                    DWORD cur17 = Memory_ReadD(addr17);
                                    DWORD cur18 = Memory_ReadD(addr18);
                                    DWORD cur1e = Memory_ReadD(addr1e);
                                    DWORD cur1f = Memory_ReadD(addr1f);
                                    DWORD cur33 = Memory_ReadD(addr33);
                                    DWORD cur34 = Memory_ReadD(addr34);
                                    DWORD cur3c = Memory_ReadD(addr3c);
	                                    DWORD cur46 = Memory_ReadD(addr46);
	                                    DWORD cur47 = Memory_ReadD(addr47);
	                                    DWORD cur4f = Memory_ReadD(addr4f);
	                                    DWORD cur54 = Memory_ReadD(addr54);
	                                    DWORD cur55 = Memory_ReadD(addr55);
	                                    DWORD cur56 = Memory_ReadD(addr56);
	                                    DWORD cur57 = Memory_ReadD(addr57);
	                                    DWORD cur8e = Memory_ReadD(addr8e);
	                                    if ((cur17 & 0x00ffffffU) == SCSI_SYNTH_IOCS_DEFAULT) {
	                                        Memory_WriteD(addr17, SCSI_SYNTH_IOCS_FN04_OK);
	                                    }
	                                    if ((cur18 & 0x00ffffffU) == SCSI_SYNTH_IOCS_DEFAULT) {
	                                        Memory_WriteD(addr18, SCSI_SYNTH_IOCS_FN04_OK);
	                                    }
	                                    if ((cur1e & 0x00ffffffU) == SCSI_SYNTH_IOCS_DEFAULT) {
	                                        Memory_WriteD(addr1e, SCSI_SYNTH_IOCS_FN04_OK);
	                                    }
                                    if ((cur1f & 0x00ffffffU) == SCSI_SYNTH_IOCS_DEFAULT) {
                                        Memory_WriteD(addr1f, SCSI_SYNTH_IOCS_FN04_OK);
                                    }
                                    if ((cur33 & 0x00ffffffU) != SCSI_SYNTH_IOCS_FN33_OK) {
                                        Memory_WriteD(addr33, SCSI_SYNTH_IOCS_FN33_OK);
                                    }
                                    if ((cur34 & 0x00ffffffU) != SCSI_SYNTH_IOCS_FN34_OK) {
                                        Memory_WriteD(addr34, SCSI_SYNTH_IOCS_FN34_OK);
                                    }
                                    if ((cur3c & 0x00ffffffU) != SCSI_SYNTH_IOCS_FN04_OK) {
                                        Memory_WriteD(addr3c, SCSI_SYNTH_IOCS_FN04_OK);
                                    }
	                                    if ((cur46 & 0x00ffffffU) == SCSI_SYNTH_IOCS_DEFAULT) {
	                                        Memory_WriteD(addr46, SCSI_SYNTH_IOCS_FN04_OK);
	                                    }
	                                    if ((cur47 & 0x00ffffffU) == SCSI_SYNTH_IOCS_DEFAULT) {
	                                        Memory_WriteD(addr47, SCSI_SYNTH_IOCS_FN04_OK);
	                                    }
	                                    if ((cur4f & 0x00ffffffU) == SCSI_SYNTH_IOCS_DEFAULT) {
	                                        Memory_WriteD(addr4f, SCSI_SYNTH_IOCS_FN04_OK);
	                                    }
	                                    if ((cur54 & 0x00ffffffU) == SCSI_SYNTH_IOCS_DEFAULT) {
	                                        Memory_WriteD(addr54, SCSI_SYNTH_IOCS_FN04_OK);
	                                    }
	                                    if ((cur55 & 0x00ffffffU) == SCSI_SYNTH_IOCS_DEFAULT) {
	                                        Memory_WriteD(addr55, SCSI_SYNTH_IOCS_FN04_OK);
	                                    }
	                                    if ((cur56 & 0x00ffffffU) == SCSI_SYNTH_IOCS_DEFAULT) {
	                                        Memory_WriteD(addr56, SCSI_SYNTH_IOCS_FN04_OK);
	                                    }
	                                    if ((cur57 & 0x00ffffffU) == SCSI_SYNTH_IOCS_DEFAULT) {
	                                        Memory_WriteD(addr57, SCSI_SYNTH_IOCS_FN04_OK);
	                                    }
	                                    if ((cur8e & 0x00ffffffU) == SCSI_SYNTH_IOCS_DEFAULT) {
	                                        Memory_WriteD(addr8e, SCSI_SYNTH_IOCS_FN04_OK);
	                                    }
	                                }
                                // Synthetic block driver chaining is still experimental.
                                // Enable only when explicitly requested.
	                                if (g_enable_scsi_dev_driver &&
	                                    !SCSI_IsDeviceLinked()) {
	                                    SCSI_LinkDeviceDriver();
	                                }
	                            }
                                if ((curPc == 0x00fe2230U || curPc == 0x00fe2258U) &&
                                    g_scsi_native_iocs_entry_logs < 96) {
                                    char nativeIocsLog[224];
                                    snprintf(nativeIocsLog, sizeof(nativeIocsLog),
                                             "SCSI_IOCS_NATIVE_ENTRY pc=$%08X d0=%08X d1=%08X d2=%08X d3=%08X d4=%08X d5=%08X a0=%08X a1=%08X a2=%08X a7=%08X",
                                             (unsigned int)curPc,
                                             (unsigned int)C68k_Get_DReg(&C68K, 0),
                                             (unsigned int)C68k_Get_DReg(&C68K, 1),
                                             (unsigned int)C68k_Get_DReg(&C68K, 2),
                                             (unsigned int)C68k_Get_DReg(&C68K, 3),
                                             (unsigned int)C68k_Get_DReg(&C68K, 4),
                                             (unsigned int)C68k_Get_DReg(&C68K, 5),
                                             (unsigned int)C68k_Get_AReg(&C68K, 0),
                                             (unsigned int)C68k_Get_AReg(&C68K, 1),
                                             (unsigned int)C68k_Get_AReg(&C68K, 2),
                                             (unsigned int)C68k_Get_AReg(&C68K, 7));
                                    X68000_AppendSCSILog(nativeIocsLog);
                                    g_scsi_native_iocs_entry_logs++;
                                }
	                                if (curPc >= 0x00fe4ca8U &&
	                                    curPc <= 0x00fe4cc0U &&
	                                    g_scsi_qloop_fe4c_logs < 256) {
                                    DWORD a5 = C68k_Get_AReg(&C68K, 5) & 0x00ffffffU;
                                    DWORD a6 = C68k_Get_AReg(&C68K, 6) & 0x00ffffffU;
                                    DWORD a7 = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
                                    BYTE a5b0 = Memory_ReadB(a5 + 0);
                                    BYTE a5b1 = Memory_ReadB(a5 + 1);
                                    BYTE a6b0 = Memory_ReadB(a6 + 0);
                                    BYTE a6b1 = Memory_ReadB(a6 + 1);
                                    WORD qbc6 = Memory_ReadW(0x00000bc6U);
                                    WORD qbc8 = Memory_ReadW(0x00000bc8U);
                                    WORD qbca = Memory_ReadW(0x00000bcaU);
                                    WORD qbcc = Memory_ReadW(0x00000bccU);
                                    DWORD ret0 = Memory_ReadD(a7 + 0);
                                    DWORD ret1 = Memory_ReadD(a7 + 4);
                                    char qLoopLog[256];
                                    snprintf(qLoopLog, sizeof(qLoopLog),
                                             "SCSI_QLOOP pc=$%08X d0=%08X d1=%08X d2=%08X a5=%08X a6=%08X a7=%08X ret0=%08X ret1=%08X a5b=%02X/%02X a6b=%02X/%02X qbc6=%04X qbc8=%04X qbca=%04X qbcc=%04X",
                                             (unsigned int)curPc,
                                             (unsigned int)C68k_Get_DReg(&C68K, 0),
                                             (unsigned int)C68k_Get_DReg(&C68K, 1),
                                             (unsigned int)C68k_Get_DReg(&C68K, 2),
                                             (unsigned int)a5,
                                             (unsigned int)a6,
                                             (unsigned int)a7,
                                             (unsigned int)ret0,
                                             (unsigned int)ret1,
                                             (unsigned int)a5b0,
                                             (unsigned int)a5b1,
                                             (unsigned int)a6b0,
                                             (unsigned int)a6b1,
                                             (unsigned int)qbc6,
                                             (unsigned int)qbc8,
                                             (unsigned int)qbca,
                                             (unsigned int)qbcc);
		                                    X68000_AppendSCSILog(qLoopLog);
		                                    g_scsi_qloop_fe4c_logs++;
		                                }
	                                if (!g_scsi_qcode_dumped &&
	                                    curPc >= 0x00fe4b6eU &&
	                                    curPc <= 0x00fe4cc0U) {
	                                    for (DWORD dumpPc = 0x00fe4b60U; dumpPc <= 0x00fe4cd0U; dumpPc += 16U) {
	                                        WORD w0 = Memory_ReadW(dumpPc + 0);
	                                        WORD w1 = Memory_ReadW(dumpPc + 2);
	                                        WORD w2 = Memory_ReadW(dumpPc + 4);
	                                        WORD w3 = Memory_ReadW(dumpPc + 6);
	                                        WORD w4 = Memory_ReadW(dumpPc + 8);
	                                        WORD w5 = Memory_ReadW(dumpPc + 10);
	                                        WORD w6 = Memory_ReadW(dumpPc + 12);
	                                        WORD w7 = Memory_ReadW(dumpPc + 14);
	                                        char codeLog[224];
	                                        snprintf(codeLog, sizeof(codeLog),
	                                                 "SCSI_QCODE pc=$%08X %04X %04X %04X %04X %04X %04X %04X %04X",
	                                                 (unsigned int)dumpPc,
	                                                 (unsigned int)w0,
	                                                 (unsigned int)w1,
	                                                 (unsigned int)w2,
	                                                 (unsigned int)w3,
	                                                 (unsigned int)w4,
	                                                 (unsigned int)w5,
	                                                 (unsigned int)w6,
	                                                 (unsigned int)w7);
	                                        X68000_AppendSCSILog(codeLog);
	                                    }
	                                    g_scsi_qcode_dumped = 1;
	                                }
	                                if (curPc >= 0x00fe4b6eU &&
	                                    curPc <= 0x00fe4ca6U &&
	                                    g_scsi_qtrace_logs < 192) {
                                    DWORD a5 = C68k_Get_AReg(&C68K, 5) & 0x00ffffffU;
                                    DWORD a6 = C68k_Get_AReg(&C68K, 6) & 0x00ffffffU;
                                    DWORD a7 = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
                                    DWORD ret0 = Memory_ReadD(a7 + 0);
                                    DWORD ret1 = Memory_ReadD(a7 + 4);
                                    WORD op0 = Memory_ReadW(curPc + 0);
                                    WORD op1 = Memory_ReadW(curPc + 2);
                                    WORD op2 = Memory_ReadW(curPc + 4);
                                    WORD op3 = Memory_ReadW(curPc + 6);
                                    char qTraceLog[256];
                                    snprintf(qTraceLog, sizeof(qTraceLog),
                                             "SCSI_QTRACE pc=$%08X d0=%08X d1=%08X d2=%08X a5=%08X a6=%08X a7=%08X ret0=%08X ret1=%08X op=%04X/%04X/%04X/%04X",
                                             (unsigned int)curPc,
                                             (unsigned int)C68k_Get_DReg(&C68K, 0),
                                             (unsigned int)C68k_Get_DReg(&C68K, 1),
                                             (unsigned int)C68k_Get_DReg(&C68K, 2),
                                             (unsigned int)a5,
                                             (unsigned int)a6,
                                             (unsigned int)a7,
                                             (unsigned int)ret0,
                                             (unsigned int)ret1,
                                             (unsigned int)op0,
                                             (unsigned int)op1,
                                             (unsigned int)op2,
                                             (unsigned int)op3);
	                                    X68000_AppendSCSILog(qTraceLog);
	                                    g_scsi_qtrace_logs++;
	                                }
	                                if ((curPc == 0x00fe4bc8U || curPc == 0x00fe4bcaU) &&
	                                    g_scsi_qstr_logs < 128) {
	                                    DWORD a0 = C68k_Get_AReg(&C68K, 0) & 0x00ffffffU;
	                                    BYTE b0 = Memory_ReadB(a0 + 0);
	                                    BYTE b1 = Memory_ReadB(a0 + 1);
	                                    BYTE b2 = Memory_ReadB(a0 + 2);
	                                    BYTE b3 = Memory_ReadB(a0 + 3);
	                                    char qStrLog[224];
	                                    snprintf(qStrLog, sizeof(qStrLog),
	                                             "SCSI_QSTR pc=$%08X a0=$%08X d0=%08X b=%02X %02X %02X %02X",
	                                             (unsigned int)curPc,
	                                             (unsigned int)a0,
	                                             (unsigned int)C68k_Get_DReg(&C68K, 0),
	                                             (unsigned int)b0,
	                                             (unsigned int)b1,
	                                             (unsigned int)b2,
	                                             (unsigned int)b3);
	                                    X68000_AppendSCSILog(qStrLog);
	                                    g_scsi_qstr_logs++;
	                                }
				                            if (curPc == g_last_exec_pc) {
				                                g_same_pc_slices++;
				                            } else {
                                g_last_exec_pc = curPc;
                                g_same_pc_slices = 0;
                            }
	                            if (isScsiFdcWaitPc) {
	                                g_scsi_fdc_wait_loop_slices++;
	                            } else {
	                                g_scsi_fdc_wait_loop_slices = 0;
	                            }
	                            if (curPc == 0x0000884eU &&
	                                g_same_pc_slices > 800) {
	                                DWORD d2Wait = C68k_Get_DReg(&C68K, 2);
	                                if ((d2Wait & 0xffff0000U) != 0U ||
	                                    (d2Wait & 0x0000ffffU) > 0x0200U) {
	                                    // Some forced-boot paths land in a very long
	                                    // DBRA+SWAP wait loop at $0000884E. Keep timing
	                                    // behavior, but clamp pathological counters.
	                                    C68k_Set_DReg(&C68K, 2, 0x00000020U);
	                                    if (g_scsi_qloop_884e_fix_logs < 32) {
	                                        char qFixLog[160];
	                                        snprintf(qFixLog, sizeof(qFixLog),
	                                                 "CPU_QLOOP_884E_CLAMP old_d2=%08X new_d2=%08X sr=%04X",
	                                                 (unsigned int)d2Wait,
	                                                 (unsigned int)0x00000020U,
	                                                 (unsigned int)(C68k_Get_SR(&C68K) & 0xffffU));
	                                        X68000_AppendSCSILog(qFixLog);
	                                        g_scsi_qloop_884e_fix_logs++;
	                                    }
	                                    g_same_pc_slices = 0;
	                                }
	                            }
	                            if (g_scsi_fdc_unblock_count < 16 &&
	                                g_scsi_fdc_wait_loop_slices > 400 &&
	                                isScsiFdcWaitPc) {
                                BYTE fdcStatus = Memory_ReadB(0x00e94001);
                                if ((fdcStatus & 0x1f) == 0x10) {
                                    FDC_ClearPendingState();
                                    X68000_AppendSCSILog("FDC_UNBLOCK cleared pending result in SCSI boot wait loop");
                                    g_scsi_fdc_unblock_count++;
                                    g_same_pc_slices = 0;
                                    g_scsi_fdc_wait_loop_slices = 0;
                                }
                            }
                            g_pc_heartbeat_counter++;
	                            if (g_same_pc_slices > 400 && curPc != g_last_stall_pc) {
	                                char stallLog[320];
                                DWORD a7 = C68k_Get_AReg(&C68K, 7) & 0x00ffffff;
                                DWORD ret0 = Memory_ReadD(a7 + 0);
                                BYTE fdcStatus = Memory_ReadB(0x00e94001);
                                WORD op0 = Memory_ReadW(curPc + 0);
                                WORD op1 = Memory_ReadW(curPc + 2);
                                WORD op2 = Memory_ReadW(curPc + 4);
                                WORD op3 = Memory_ReadW(curPc + 6);
	                                snprintf(stallLog, sizeof(stallLog),
	                                         "CPU_STALL pc=$%08X sr=$%04X d0=%08X d1=%08X d2=%08X d3=%08X a7=%08X ret0=%08X fdc=$%02X op=%04X/%04X/%04X/%04X",
	                                         (unsigned int)(curPc & 0x00ffffff),
	                                         (unsigned int)(C68k_Get_SR(&C68K) & 0xffff),
	                                         (unsigned int)C68k_Get_DReg(&C68K, 0),
	                                         (unsigned int)C68k_Get_DReg(&C68K, 1),
	                                         (unsigned int)C68k_Get_DReg(&C68K, 2),
	                                         (unsigned int)C68k_Get_DReg(&C68K, 3),
	                                         (unsigned int)a7,
	                                         (unsigned int)ret0,
	                                         (unsigned int)fdcStatus,
                                         (unsigned int)op0,
                                         (unsigned int)op1,
                                         (unsigned int)op2,
                                         (unsigned int)op3);
                                X68000_AppendSCSILog(stallLog);
                                if (curPc >= 0x00ff6420U &&
                                    curPc <= 0x00ff6470U &&
                                    g_scsi_qloop_logs < 128) {
                                    char qLog[192];
                                    WORD q812 = Memory_ReadW(0x00000812);
                                    DWORD q818 = Memory_ReadD(0x00000818) & 0x00ffffffU;
                                    WORD q81c = Memory_ReadW(0x0000081c);
                                    WORD q89c = Memory_ReadW(0x0000089c);
                                    snprintf(qLog, sizeof(qLog),
                                             "CPU_STALL_Q q812=%04X q818=%08X q81C=%04X q89C=%04X",
                                             (unsigned int)q812,
                                             (unsigned int)q818,
                                             (unsigned int)q81c,
                                             (unsigned int)q89c);
                                    X68000_AppendSCSILog(qLog);
                                    g_scsi_qloop_logs++;
                                }
                                g_last_stall_pc = curPc;
                            }
	                            if (g_pc_heartbeat_counter >= 4000 && g_pc_heartbeat_logs < 64) {
	                                char hbLog[384];
                                DWORD a7 = C68k_Get_AReg(&C68K, 7) & 0x00ffffff;
                                DWORD ret0 = Memory_ReadD(a7 + 0);
                                DWORD ret1 = Memory_ReadD(a7 + 4);
                                BYTE fdcStatus = Memory_ReadB(0x00e94001);
                                WORD op0 = Memory_ReadW(curPc + 0);
                                WORD op1 = Memory_ReadW(curPc + 2);
                                WORD op2 = Memory_ReadW(curPc + 4);
                                WORD op3 = Memory_ReadW(curPc + 6);
	                                snprintf(hbLog, sizeof(hbLog),
	                                         "CPU_HEARTBEAT pc=$%08X sr=$%04X d0=%08X d1=%08X d2=%08X d3=%08X a7=%08X ret0=%08X ret1=%08X fdc=$%02X op=%04X/%04X/%04X/%04X",
	                                         (unsigned int)(curPc & 0x00ffffff),
	                                         (unsigned int)(C68k_Get_SR(&C68K) & 0xffff),
	                                         (unsigned int)C68k_Get_DReg(&C68K, 0),
	                                         (unsigned int)C68k_Get_DReg(&C68K, 1),
	                                         (unsigned int)C68k_Get_DReg(&C68K, 2),
	                                         (unsigned int)C68k_Get_DReg(&C68K, 3),
	                                         (unsigned int)a7,
	                                         (unsigned int)ret0,
	                                         (unsigned int)ret1,
                                         (unsigned int)fdcStatus,
                                         (unsigned int)op0,
                                         (unsigned int)op1,
                                         (unsigned int)op2,
                                         (unsigned int)op3);
                                X68000_AppendSCSILog(hbLog);
                                if (curPc >= 0x00ff6420U &&
                                    curPc <= 0x00ff6470U &&
                                    g_scsi_qloop_logs < 128) {
                                    char qLog[192];
                                    WORD q812 = Memory_ReadW(0x00000812);
                                    DWORD q818 = Memory_ReadD(0x00000818) & 0x00ffffffU;
                                    WORD q81c = Memory_ReadW(0x0000081c);
                                    WORD q89c = Memory_ReadW(0x0000089c);
                                    snprintf(qLog, sizeof(qLog),
                                             "CPU_HEARTBEAT_Q q812=%04X q818=%08X q81C=%04X q89C=%04X",
                                             (unsigned int)q812,
                                             (unsigned int)q818,
                                             (unsigned int)q81c,
                                             (unsigned int)q89c);
                                    X68000_AppendSCSILog(qLog);
                                    g_scsi_qloop_logs++;
                                }
                                g_pc_heartbeat_counter = 0;
                                g_pc_heartbeat_logs++;
                            }
                        } else {
                            g_last_exec_pc = 0xffffffff;
                            g_last_stall_pc = 0xffffffff;
                            g_same_pc_slices = 0;
                            g_scsi_fdc_wait_loop_slices = 0;
	                            g_pc_heartbeat_counter = 0;
	                            g_pc_heartbeat_logs = 0;
		                            g_scsi_fdc_unblock_count = 0;
			                            g_scsi_iocs_rehook_logs = 0;
			                            g_scsi_iocs_check_counter = 0;
			                            g_last_scsi_iocs_vector = 0xffffffff;
				                            g_scsi_fnff_entry_logs = 0;
					                            g_scsi_low_rts_logs = 0;
					                            g_scsi_low_rte_logs = 0;
					                            g_scsi_lowf75_logs = 0;
					                            g_scsi_jmpa0_logs = 0;
					                            g_scsi_jmpa0_fix_logs = 0;
					                            g_scsi_jmpa0_code_dumped = 0;
					                            g_scsi_dbg_27c0_logs = 0;
					                            g_scsi_dbg_27c0_last = 0xffffffffU;
					                            g_scsi_f75_code_dumped = 0;
					                            g_scsi_fa_code_dumped = 0;
					                            g_scsi_trap_special_logs = 0;
				                            memset(g_scsi_a0_trace_ring, 0, sizeof(g_scsi_a0_trace_ring));
				                            g_scsi_pc_trace_pos = 0;
				                            g_scsi_pc_trace_filled = 0;
				                            g_scsi_pc_trace_dumped = 0;
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

void X68000_SetStorageBusMode(int mode)
{
	g_storage_bus_mode = (mode == 1) ? 1 : 0;
	if (g_storage_bus_mode) {
		Memory_SetSCSIMode();
	} else {
		Memory_ClearSCSIMode();
	}
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

}
//extern "C"
