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
BYTE traceflag = 0;

BYTE ForceDebugMode = 0;
DWORD skippedframes = 0;

static int ClkUsed = 0;
static int FrameSkipCount = 0;
static int FrameSkipQueue = 0;

#ifdef __cplusplus
};
#endif


void
WinX68k_SCSICheck(void)
{
    static const BYTE SCSIIMG[] = {
        0x00, 0xfc, 0x00, 0x14,            // $fc0000 SCSI��ư�ѤΥ���ȥꥢ�ɥ쥹
        0x00, 0xfc, 0x00, 0x16,            // $fc0004 IOCS�٥�������Υ���ȥꥢ�ɥ쥹(ɬ��"Human"��8�Х�����)
        0x00, 0x00, 0x00, 0x00,            // $fc0008 ?
        0x48, 0x75, 0x6d, 0x61,            // $fc000c ��
        0x6e, 0x36, 0x38, 0x6b,            // $fc0010 ID "Human68k"    (ɬ����ư����ȥ�ݥ���Ȥ�ľ��)
        0x4e, 0x75,                // $fc0014 "rts"        (��ư����ȥ�ݥ����)
        0x23, 0xfc, 0x00, 0xfc, 0x00, 0x2a,    // $fc0016 ��        (IOCS�٥������ꥨ��ȥ�ݥ����)
        0x00, 0x00, 0x07, 0xd4,            // $fc001c "move.l #$fc002a, $7d4.l"
        0x74, 0xff,                // $fc0020 "moveq #-1, d2"
        0x4e, 0x75,                // $fc0022 "rts"
//        0x53, 0x43, 0x53, 0x49, 0x49, 0x4e,    // $fc0024 ID "SCSIIN"
// ��¢SCSI��ON�ˤ���ȡ�SASI�ϼ�ưŪ��OFF�ˤʤä��㤦�餷����
// ��äơ�ID�ϥޥå����ʤ��褦�ˤ��Ƥ�����
        0x44, 0x55, 0x4d, 0x4d, 0x59, 0x20,    // $fc0024 ID "DUMMY "
        0x70, 0xff,                // $fc002a "moveq #-1, d0"    (SCSI IOCS�����륨��ȥ�ݥ����)
        0x4e, 0x75,                // $fc002c "rts"
    };

#if 0
    DWORD *p;
#endif
    WORD *p1, *p2;
    int scsi;
    int i;

    scsi = 0;
    for (i = 0x30600; i < 0x30c00; i += 2) {
#if 0 // 4���ܿ��ǤϤʤ��������ɥ쥹�����4�Х���Ĺ����������MIPS�ˤ�̵��
        p = (DWORD *)(&IPL[i]);
        if (*p == 0x0000fc00)
            scsi = 1;
#else
        p1 = (WORD *)(&IPL[i]);
        p2 = p1 + 1;
        // xxx: works only for little endian guys
        if (*p1 == 0xfc00 && *p2 == 0x0000) {
            scsi = 1;
            break;
        }
#endif
    }

    // SCSI��ǥ�ΤȤ�
    if (scsi) {
        ZeroMemory(IPL, 0x2000);        // ���Τ�8kb
        memset(&IPL[0x2000], 0xff, 0x1e000);    // �Ĥ��0xff
        memcpy(IPL, SCSIIMG, sizeof(SCSIIMG));    // �������SCSI BIOS
//        Memory_SetSCSIMode();
    } else {
        // SASI��ǥ��IPL�����Τޤ޸�����
        memcpy(IPL, &IPL[0x20000], 0x20000);
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
    C68k_Set_AReg(&C68K, 7, (IPL[0x30001]<<24)|(IPL[0x30000]<<16)|(IPL[0x30003]<<8)|IPL[0x30002]);
    C68k_Set_PC(&C68K, (IPL[0x30005]<<24)|(IPL[0x30004]<<16)|(IPL[0x30007]<<8)|IPL[0x30006]);
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

#define CLOCK_SLICE 200
// -----------------------------------------------------------------------------------
//  �����Τᤤ��롼��
// -----------------------------------------------------------------------------------
void WinX68k_Exec(const long clockMHz, const long vsync)
{
    //char *test = NULL;
    int clk_total, clkdiv, usedclk, hsync, clk_next, clk_count, clk_line=0;
    int KeyIntCnt = 0, MouseIntCnt = 0;
    DWORD t_start = timeGetTime(), t_end;

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
    clkdiv = clockMHz;
    clk_total = (clk_total*clkdiv)/10;

#endif
    ICount += clk_total;
    clk_next = (clk_total/VLINE_TOTAL);
    hsync = 1;

    do {
        int m, n = (ICount>CLOCK_SLICE)?CLOCK_SLICE:ICount;
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
            if ( MouseIntCnt>(VLINE_TOTAL/8) ) {
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
        printf("FrameSkipQueue:%d\n", FrameSkipQueue);
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
    file_setcd("./");
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

    printf("FD:%s\n",Config.FDDImage[0] );

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
    if ( !DispFrame ) {
        WinDraw_Draw(data);
    } else {
        printf("DispFrame\n");
    }
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
    printf("X68000_LoadFDD( %d, \"%s\" )\n", drive, filename);
    FDD_SetFD(drive, (char*)filename, 0);
}

void X68000_EjectFDD( const long drive )
{
    printf("X68000_EjectFDD( %d )\n", drive);
    FDD_EjectFD(drive);
}

const int X68000_IsFDDReady( const long drive )
{
    return FDD_IsReady(drive);
}

const char* X68000_GetFDDFilename( const long drive )
{
    // Simple implementation - return empty string for now
    // Could be enhanced to track loaded filenames if needed
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
	strncpy( Config.HDImage[0], filename , MAX_PATH);

}

void X68000_EjectHDD()
{
	printf("X68000_EjectHDD()\n");
	Config.HDImage[0][0] = '\0';
}

const int X68000_IsHDDReady()
{
	return (Config.HDImage[0][0] != '\0') ? 1 : 0;
}

const char* X68000_GetHDDFilename()
{
	return Config.HDImage[0];
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
	if ( abs(dx) < 2 ) dx = 0;
	if ( abs(dy) < 2 ) dy = 0;

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
//    printf(" nx:%3d ny:%3d tx:%3d ty:%3d dx:%3d dy:%3d\n", nx, ny, tx, ty, dx, dy );


    MouseX = dx;
    MouseY = dy;
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
    MouseX = (int)x;
    MouseY = (int)y;
    MouseSt = button;
    
    
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



