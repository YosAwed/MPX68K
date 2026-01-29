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

    // $ED0018-$ED001B: RAM size for BASIC (default 0)
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
	adr &= 0xffff;
	adr ^= 1;
	if (adr<0x4000)
		return SRAM[adr];
	else
		return 0xff;
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
