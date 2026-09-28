// ---------------------------------------------------------------------------------------
//  SRAM.C - SRAM (16kb) 領域
// ---------------------------------------------------------------------------------------

#include	"common.h"
#include	"fileio.h"
#include	"prop.h"
#include	"winx68k.h"
#include	"sysport.h"
#include	"x68kmemory.h"
#include	"sram.h"

	BYTE	SRAM[0x4000];
	BYTE	SRAMFILE[] = "SRAM.DAT";

static int s_scsi_board_overlay = 0;

// $ED006F: 'V' = SCSI 設定有効, $ED0070: bit0-2 本体SCSI ID / bit3 外付けSCSI
#define SRAM_SCSI_VALID_ADDR 0x6f
#define SRAM_SCSI_CONF_ADDR  0x70
#define SRAM_SCSI_VALID_MARK 0x56
#define SRAM_SCSI_CONF_EXT   0x08
#define SRAM_SCSI_CONF_DEF   0x0f  // 外付けSCSI, 本体ID=7 (FORMAT.X の既定値と同じ)

void SRAM_SetSCSIBoardOverlay(int enable)
{
	s_scsi_board_overlay = enable ? 1 : 0;
}

static BYTE SRAM_ApplySCSIBoardOverlay(DWORD adr, BYTE val)
{
	// adr is the byte-swapped SRAM[] index
	if (!s_scsi_board_overlay) {
		return val;
	}
	if ((adr ^ 1) == SRAM_SCSI_VALID_ADDR) {
		return SRAM_SCSI_VALID_MARK;
	}
	if ((adr ^ 1) == SRAM_SCSI_CONF_ADDR) {
		// Keep a user-configured SCSI ID (SWITCH.X), only force bit3.
		if (SRAM[SRAM_SCSI_VALID_ADDR ^ 1] == SRAM_SCSI_VALID_MARK) {
			return (BYTE)(val | SRAM_SCSI_CONF_EXT);
		}
		return SRAM_SCSI_CONF_DEF;
	}
	return val;
}


// -----------------------------------------------------------------------
//   役に立たないうぃるすチェック
// -----------------------------------------------------------------------
void SRAM_VirusCheck(void)
{
	//int i, ret;

	if (!Config.SRAMWarning) return;				// Warning発生モードでなければ帰る

	if ( (cpu_readmem24_dword(0xed3f60)==0x60000002)
	   &&(cpu_readmem24_dword(0xed0010)==0x00ed3f60) )		// 特定うぃるすにしか効かないよ～
	{
#if 0 /* XXX */
		ret = MessageBox(hWndMain,
			"このSRAMデータはウィルスに感染している可能性があります。\n該当個所のクリーンアップを行いますか？",
			"けろぴーからの警告", MB_ICONWARNING | MB_YESNO);
		if (ret == IDYES)
		{
			for (i=0x3c00; i<0x4000; i++)
				SRAM[i] = 0xFF;
			SRAM[0x11] = 0x00;
			SRAM[0x10] = 0xed;
			SRAM[0x13] = 0x01;
			SRAM[0x12] = 0x00;
			SRAM[0x19] = 0x00;
		}
#endif /* XXX */
		SRAM_Cleanup();
		SRAM_Init();			// Virusクリーンアップ後のデータを書き込んでおく
	}
}


// -----------------------------------------------------------------------
//   初期化
// -----------------------------------------------------------------------
void SRAM_Init(void)
{
    // Initialize SRAM with 0x00 (X68000 default)
    for (int i=0; i<0x4000; i++) {
        SRAM[i] = 0x00;
    }

    // Set up basic SRAM configuration for X68000
    // SRAM is accessed with byte-swap (addr^1), so values need to be swapped

    // $ED0008-$ED000B: Main memory size (12MB = 0x00C00000)
    SRAM[0x08^1] = 0x00; SRAM[0x09^1] = 0xC0;
    SRAM[0x0A^1] = 0x00; SRAM[0x0B^1] = 0x00;

    // $ED0010-$ED0013: SRAM signature (0x0001ED00 indicates valid SRAM)
    SRAM[0x10^1] = 0x00; SRAM[0x11^1] = 0x01;
    SRAM[0x12^1] = 0xED; SRAM[0x13^1] = 0x00;

    // $ED0018: Boot device (bit7=HDD boot enable, 0x00=FDD, 0x80=HDD)
    SRAM[0x18^1] = 0x00; SRAM[0x19^1] = 0x00;
    SRAM[0x1A^1] = 0x00; SRAM[0x1B^1] = 0x00;

    // $ED0070: Boot device setting (0x00 = standard boot sequence)
    SRAM[0x70^1] = 0x00;

    // $ED0072-$ED0073: ROM start mode (0x0000 = normal)
    SRAM[0x72^1] = 0x00; SRAM[0x73^1] = 0x00;

#if 0 // X68iOS
    BYTE tmp;

    FILEH fp = File_OpenCurDir(SRAMFILE);
	if (fp)
	{
		File_Read(fp, SRAM, 0x4000);
		File_Close(fp);
		for (int i=0; i<0x4000; i+=2)
		{
			tmp = SRAM[i];
			SRAM[i] = SRAM[i+1];
			SRAM[i+1] = tmp;
		}
	}
#endif
}


// -----------------------------------------------------------------------
//   撤収～
// -----------------------------------------------------------------------
void SRAM_Cleanup(void)
{
#if 0 // X68iOS
	int i;
	BYTE tmp;
	FILEH fp;

	for (i=0; i<0x4000; i+=2)
	{
		tmp = SRAM[i];
		SRAM[i] = SRAM[i+1];
		SRAM[i+1] = tmp;
	}

	fp = File_OpenCurDir(SRAMFILE);
	if (!fp)
		fp = File_CreateCurDir(SRAMFILE, FTYPE_SRAM);
	if (fp)
	{
		File_Write(fp, SRAM, 0x4000);
		File_Close(fp);
	}
#endif
}


// -----------------------------------------------------------------------
//   りーど
// -----------------------------------------------------------------------
BYTE FASTCALL SRAM_Read(DWORD adr)
{
	BYTE val;
	adr &= 0xffff;
	adr ^= 1;
	if (adr<0x4000)
		val = SRAM_ApplySCSIBoardOverlay(adr, SRAM[adr]);
	else
		val = 0xff;

	/* Log reads of boot device byte $ED0018/$ED0019 */
	if ((adr & 0xfffe) == 0x0018) {
		extern void SCSI_LogText(const char *text);
		char sl[96];
		snprintf(sl, sizeof(sl), "SRAM_R adr=$ED%04X val=$%02X (raw_idx=%04X)",
			(unsigned)((adr ^ 1) + 0xED0000), (unsigned)val, (unsigned)adr);
		SCSI_LogText(sl);
	}

	return val;
}


// -----------------------------------------------------------------------
//   らいと
// -----------------------------------------------------------------------
void FASTCALL SRAM_Write(DWORD adr, BYTE data)
{
	//int ret;

	if ( (SysPort[5]==0x31)&&(adr<0xed4000) )
	{
		if ((adr==0xed0018)&&(data==0xb0))	// SRAM起動への切り替え（簡単なウィルス対策）
		{
			if (Config.SRAMWarning)		// Warning発生モード（デフォルト）
			{
#if 0 /* XXX */
				ret = MessageBox(hWndMain,
					"SRAMブートに切り替えようとしています。\nウィルスの危険がない事を確認してください。\nSRAMブートに切り替え、継続しますか？",
					"けろぴーからの警告", MB_ICONWARNING | MB_YESNO);
				if (ret != IDYES)
				{
					data = 0;	// STDブートにする
				}
#endif /* XXX */
			}
		}
		adr &= 0xffff;
		adr ^= 1;
		SRAM[adr] = data;
	}
}
