// ---------------------------------------------------------------------------------------
//  SASI.C - Shugart Associates System Interface (SASI HDD)
// ---------------------------------------------------------------------------------------

#include "common.h"
#include "fileio.h"
#include "prop.h"
#include "status.h"
#include "../m68000/m68000.h"
#include "ioc.h"
#include "sasi.h"
#include "irqh.h"

BYTE SASI_Buf[256];
BYTE SASI_Phase = 0;
DWORD SASI_Sector = 0;
DWORD SASI_Blocks = 0;
BYTE SASI_Cmd[6];
BYTE SASI_CmdPtr = 0;
WORD SASI_Device = 0;
BYTE SASI_Unit = 0;
short SASI_BufPtr = 0;
BYTE SASI_RW = 0;
BYTE SASI_Stat = 0;
BYTE SASI_Mes = 0;
BYTE SASI_Error = 0;
BYTE SASI_SenseStatBuf[4];
BYTE SASI_SenseStatPtr = 0;
WORD SASI_BufSize = 256;  // Current buffer size (256 for normal sectors, 8 for READ CAPACITY)

int        hddtrace = 0;

int SASI_IsReady(void)
{
	if ( (SASI_Phase==2)||(SASI_Phase==3)||(SASI_Phase==9) )
		return 1;
	else
		return 0;
}

#define File_Open	Sasi_Open
#define File_Close	Sasi_Close
#define File_Read	Sasi_Read
#define File_Seek	Sasi_Seek
#define File_Write	Sasi_Write
extern BYTE* s_disk_image_buffer[5];

static int s_Sasi_pos;
static DWORD s_Sasi_image_size[5] = {0}; // Track HDD image sizes for capacity reporting
static BYTE s_Sasi_dirty_flag[5] = {0}; // Track if HDD data has been modified
int Sasi_Open(const char* filename) {
//	printf( "%s( \"%s\" )\n", __FUNCTION__, filename );
	s_Sasi_pos = 0;
	return (HANDLE)s_disk_image_buffer[4];
}

int Sasi_Read( HANDLE fp, void* buf, int size ) {
//	printf( "%s( %p, %p, %d )\n", __FUNCTION__, fp, buf, size );
	BYTE* pos = s_disk_image_buffer[4];
	pos += s_Sasi_pos;
	memcpy( buf, pos, size );
	s_Sasi_pos += size;
	
	return size;
}
int Sasi_Write( HANDLE fp, void* buf, int size ) {
//	printf( "%s( %p, %p, %d )\n", __FUNCTION__, fp, buf, size );
	BYTE* pos = s_disk_image_buffer[4];
	pos += s_Sasi_pos;
	memcpy( pos, buf, size );
	s_Sasi_pos += size;
	
	return size;
}

int Sasi_Seek( HANDLE fp, int pos, int type ) {
	switch(type) {
		case FSEEK_SET:
			s_Sasi_pos = pos;
			break;
		default:
			printf("***Unknown Type***\n");
	}
	return pos;
}

int Sasi_Close( HANDLE fp ) {
	return 0;
}

// Set HDD image size for capacity reporting (called when loading HDD)
void SASI_SetImageSize(int drive, DWORD size_bytes) {
	if (drive >= 0 && drive < 5) {
		s_Sasi_image_size[drive] = size_bytes;
		printf("SASI_SetImageSize: Set drive %d size to %d bytes (%d MB)\n", 
		       drive, size_bytes, size_bytes / (1024*1024));
	} else {
		printf("SASI_SetImageSize: Invalid drive index %d\n", drive);
	}
}

// Get number of sectors for specified drive
static DWORD SASI_GetSectorCount(int drive) {
	if (drive >= 0 && drive < 5 && s_Sasi_image_size[drive] > 0) {
		return s_Sasi_image_size[drive] / 256;  // 256 bytes per sector
	}
	return 0;
}

// Get HDD image size for specified drive (public function)
DWORD SASI_GetImageSize(int drive) {
	if (drive >= 0 && drive < 5) {
		DWORD size = s_Sasi_image_size[drive];
		printf("SASI_GetImageSize: Drive %d size is %d bytes\n", drive, size);
		return size;
	}
	printf("SASI_GetImageSize: Invalid drive index %d\n", drive);
	return 0;
}

// Check if HDD has been modified since last save
BYTE SASI_IsDirty(int drive) {
	if (drive >= 0 && drive < 5) {
		return s_Sasi_dirty_flag[drive];
	}
	return 0;
}

// Clear dirty flag after successful save
void SASI_ClearDirtyFlag(int drive) {
	if (drive >= 0 && drive < 5) {
		s_Sasi_dirty_flag[drive] = 0;
	}
}

// -----------------------------------------------------------------------
//   わりこみ～
// -----------------------------------------------------------------------
DWORD FASTCALL SASI_Int(BYTE irq)
{
	IRQH_IRQCallBack(irq);
if (hddtrace) {
FILE *fp;
fp=fopen("_trace68.txt", "a");
fprintf(fp, "Int (IRQ:%d)\n", irq);
fclose(fp);
}
	if (irq==1)
		return ((DWORD)IOC_IntVect+2);
	else
		return -1;
}


// -----------------------------------------------------------------------
//   初期化
// -----------------------------------------------------------------------
void SASI_Init(void)
{
	SASI_Phase = 0;
	SASI_Sector = 0;
	SASI_Blocks = 0;
	SASI_CmdPtr = 0;
	SASI_Device = 0;
	SASI_Unit = 0;
	SASI_BufPtr = 0;
	SASI_BufSize = 256;
	SASI_RW = 0;
	SASI_Stat = 0;
	SASI_Error = 0;
	SASI_SenseStatPtr = 0;
	
	// Initialize image size and dirty flag arrays
	for (int i = 0; i < 5; i++) {
		s_Sasi_image_size[i] = 0;
		s_Sasi_dirty_flag[i] = 0;
	}
	printf("SASI_Init: Initialized image size and dirty flag arrays\n");
	
	// Restore HDD size if already loaded (after reset)
	if (Config.HDImage[0][0] != '\0') {
		// HDD is already loaded, restore size information
		FILE* fp = fopen(Config.HDImage[0], "rb");
		if (fp) {
			fseek(fp, 0, SEEK_END);
			DWORD size = ftell(fp);
			fclose(fp);
			
			// Restore size for all drive indices
			for (int i = 0; i < 5; i++) {
				s_Sasi_image_size[i] = size;
			}
			printf("SASI_Init: Restored HDD size after reset: %d bytes (%d MB)\n", 
			       size, size / (1024*1024));
		} else {
			printf("SASI_Init: Warning - Could not access HDD file after reset: %s\n", Config.HDImage[0]);
		}
	}
}


// -----------------------------------------------------------------------
//   し−く（リード時）
// -----------------------------------------------------------------------
short SASI_Seek(void)
{
	// Direct file I/O - bypass SASI macros to read from actual file
	FILE* fp;

if (hddtrace) {
FILE *fp_trace;
fp_trace=fopen("_trace68.txt", "a");
fprintf(fp_trace, "Seek  - Sector:%d  (Time:%08X)\n", SASI_Sector, timeGetTime());
fclose(fp_trace);
}
	ZeroMemory(SASI_Buf, 256);
	fp = fopen(Config.HDImage[SASI_Device*2+SASI_Unit], "rb");
	if (!fp)
	{
		ZeroMemory(SASI_Buf, 256);
		return -1;
	}
	if (fseek(fp, SASI_Sector<<8, SEEK_SET) != 0) 
	{
		fclose(fp);
		return 0;
	}
	if (fread(SASI_Buf, 1, 256, fp) != 256)
	{
		fclose(fp);
		return 0;
	}
	fclose(fp);

	return 1;
}


// -----------------------------------------------------------------------
//   しーく（ライト時）
// -----------------------------------------------------------------------
short SASI_Flush(void)
{
	// Direct file I/O - bypass SASI macros to write to actual file
	FILE* fp;

	fp = fopen(Config.HDImage[SASI_Device*2+SASI_Unit], "r+b");
	if (!fp) return -1;
	if (fseek(fp, SASI_Sector<<8, SEEK_SET) != 0)
	{
		fclose(fp);
		return 0;
	}
	if (fwrite(SASI_Buf, 1, 256, fp) != 256)
	{
		fclose(fp);
		return 0;
	}
	fflush(fp);  // Ensure data is written to disk immediately
	fclose(fp);
	
	// Data successfully written to file - no longer needs dirty flag tracking
	printf("SASI: Sector %d written directly to file (Device:%d Unit:%d)\n", SASI_Sector, SASI_Device, SASI_Unit);

if (hddtrace) {
FILE *fp;
fp=fopen("_trace68.txt", "a");
fprintf(fp, "Sec Write  - Sector:%d  (Time:%08X)\n", SASI_Sector, timeGetTime());
fclose(fp);
}
	return 1;
}


// -----------------------------------------------------------------------
//   I/O Read
// -----------------------------------------------------------------------
BYTE FASTCALL SASI_Read(DWORD adr)
{
	BYTE ret = 0;
	short result;

	if (adr==0xe96003)
	{
		if (SASI_Phase)
			ret |= 2;		// Busy
		if (SASI_Phase>1)
			ret |= 1;		// Req
		if (SASI_Phase==2)
			ret |= 8;		// C/D
		if ((SASI_Phase==3)&&(SASI_RW))	// SASI_RW=1:Read
			ret |= 4;		// I/O
		if (SASI_Phase==9)		// Phase=9:SenseStatus中
			ret |= 4;		// I/O
		if ((SASI_Phase==4)||(SASI_Phase==5))
			ret |= 0x0c;		// I/O & C/D
		if (SASI_Phase==5)
			ret |= 0x10;		// MSG
	}
	else if (adr ==0xe96001)
	{
		if ((SASI_Phase==3)&&(SASI_RW))	// データリード中～
		{
			ret = SASI_Buf[SASI_BufPtr++];
			if (SASI_BufPtr==SASI_BufSize)
			{
				SASI_Blocks--;
				if (SASI_Blocks)		// まだ読むブロックがある？
				{
					SASI_Sector++;
					SASI_BufPtr = 0;
					SASI_BufSize = 256;  // Reset to normal sector size for subsequent reads
					result = SASI_Seek();	// 次のセクタをバッファに読む
					if (!result)		// result=0：イメージの最後（＝無効なセクタ）なら
					{
						SASI_Error = 0x0f;
						SASI_Phase++;
					}
				}
				else
					SASI_Phase++;		// 指定ブロックのリード完了
			}
		}
		else if (SASI_Phase==4)				// Status Phase
		{
			if (SASI_Error)
				ret = 0x02;
			else
				ret = SASI_Stat;
			SASI_Phase++;
		}
		else if (SASI_Phase==5)				// MessagePhase
		{
			SASI_Phase = 0;				// 0を返すだけ～。BusFreeに帰ります
		}
		else if (SASI_Phase==9)				// DataPhase(SenseStat専用)
		{
			ret = SASI_SenseStatBuf[SASI_SenseStatPtr++];
			if (SASI_SenseStatPtr==4)
			{
				SASI_Error = 0;
				SASI_Phase = 4;				// StatusPhaseへ
			}
		}
		if (SASI_Phase==4)
		{
			IOC_IntStat|=0x10;
			if (IOC_IntStat&8) IRQH_Int(1, &SASI_Int);
		}
	}

	if (hddtrace&&((SASI_Phase!=3)||(adr!=0xe96001)))
	{
		FILE *fp;
		fp=fopen("_trace68.txt", "a");
		//fprintf(fp, "Read  - Adr:%08X  Ret:%02X  Phase:%d BufPtr:%d  (Time:%08X)  @ $%08X\n", adr, ret, SASI_Phase, SASI_BufPtr, timeGetTime(), C68k_Get_Reg(&C68K, C68K_PC));

#if defined (HAVE_CYCLONE)	
		fprintf(fp, "Read  - Adr:%08X  Ret:%02X  Phase:%d BufPtr:%d  (Time:%08X)  @ $%08X\n", adr, ret, SASI_Phase, SASI_BufPtr, timeGetTime(), m68000_get_reg(M68K_PC));
#elif defined (HAVE_C68K)
		fprintf(fp, "Read  - Adr:%08X  Ret:%02X  Phase:%d BufPtr:%d  (Time:%08X)  @ $%08X\n", adr, ret, SASI_Phase, SASI_BufPtr, timeGetTime(), C68k_Get_PC(&C68K));
#endif /* HAVE_C68K */
		fclose(fp);
	}

	StatBar_HDD((SASI_Phase)?2:0);

	return ret;
}


// コマンドのチェック。正直、InsideX68k内の記述ではちと足りない ^^;。
// 未記述のものとして、
//   - C2h（初期化系？）。Unit以外のパラメータは無し。DataPhaseで10個のデータを書きこむ。
//   - 06h（フォーマット？）。論理ブロック指定あり（21hおきに指定している）。ブロック数のとこは6が指定されている。
void SASI_CheckCmd(void)
{
	short result;
	SASI_Unit = (SASI_Cmd[1]>>5)&1;			// X68kでは、ユニット番号は0か1しか取れない

	switch(SASI_Cmd[0])
	{
	case 0x00:					// Test Drive Ready
		if (Config.HDImage[SASI_Device*2+SASI_Unit][0])
			SASI_Stat = 0;
		else
		{
			SASI_Stat = 0x02;
			SASI_Error = 0x7f;
		}
		SASI_Phase += 2;
		break;
	case 0x01:					// Recalibrate
		if (Config.HDImage[SASI_Device*2+SASI_Unit][0])
		{
			SASI_Sector = 0;
			SASI_Stat = 0;
		}
		else
		{
			SASI_Stat = 0x02;
			SASI_Error = 0x7f;
		}
		SASI_Phase += 2;
		break;
	case 0x03:					// Request Sense Status
		SASI_SenseStatBuf[0] = SASI_Error;
		SASI_SenseStatBuf[1] = (BYTE)((SASI_Unit<<5)|((SASI_Sector>>16)&0x1f));
		SASI_SenseStatBuf[2] = (BYTE)(SASI_Sector>>8);
		SASI_SenseStatBuf[3] = (BYTE)SASI_Sector;
		SASI_Error = 0;
		SASI_Phase=9;
		SASI_Stat = 0;
		SASI_SenseStatPtr = 0;
		break;
	case 0x04:					// Format Drive
		SASI_Phase += 2;
		SASI_Stat = 0;
		break;
	case 0x08:					// Read Data
		SASI_Sector = (((DWORD)SASI_Cmd[1]&0x1f)<<16)|(((DWORD)SASI_Cmd[2])<<8)|((DWORD)SASI_Cmd[3]);
		SASI_Blocks = (DWORD)SASI_Cmd[4];
		SASI_Phase++;
		SASI_RW = 1;
		SASI_BufPtr = 0;
		SASI_BufSize = 256;  // Normal 256-byte sectors
		SASI_Stat = 0;
		result = SASI_Seek();
		if ( (result==0)||(result==-1) )
		{
//			SASI_Phase++;
			SASI_Error = 0x0f;
		}
		break;
	case 0x0a:					// Write Data
		SASI_Sector = (((DWORD)SASI_Cmd[1]&0x1f)<<16)|(((DWORD)SASI_Cmd[2])<<8)|((DWORD)SASI_Cmd[3]);
		SASI_Blocks = (DWORD)SASI_Cmd[4];
		SASI_Phase++;
		SASI_RW = 0;
		SASI_BufPtr = 0;
		SASI_BufSize = 256;  // Normal 256-byte sectors
		SASI_Stat = 0;
		ZeroMemory(SASI_Buf, 256);
		result = SASI_Seek();
		if ( (result==0)||(result==-1) )
		{
//			SASI_Phase++;
			SASI_Error = 0x0f;
		}
		break;
	case 0x0b:					// Seek
		if (Config.HDImage[SASI_Device*2+SASI_Unit][0])
		{
			SASI_Stat = 0;
		}
		else
		{
			SASI_Stat = 0x02;
			SASI_Error = 0x7f;
		}
		SASI_Phase += 2;
//		SASI_Phase = 9;
		break;
	case 0x25:					// Read Capacity (SCSI command for HDD capacity)
		if (Config.HDImage[SASI_Device*2+SASI_Unit][0])
		{
			DWORD sectorCount = SASI_GetSectorCount(SASI_Device*2+SASI_Unit);
			if (sectorCount > 0) {
				// Return capacity data: Last logical block address (4 bytes) + Block length (4 bytes)
				DWORD lastLBA = sectorCount - 1;
				SASI_Buf[0] = (BYTE)(lastLBA >> 24);
				SASI_Buf[1] = (BYTE)(lastLBA >> 16);
				SASI_Buf[2] = (BYTE)(lastLBA >> 8);
				SASI_Buf[3] = (BYTE)lastLBA;
				SASI_Buf[4] = 0x00;  // Block length = 256 bytes
				SASI_Buf[5] = 0x00;
				SASI_Buf[6] = 0x01;
				SASI_Buf[7] = 0x00;
				
				SASI_BufPtr = 0;
				SASI_Phase = 3;  // Data phase
				SASI_RW = 1;     // Read operation
				SASI_Blocks = 1; // 1 block to read
				SASI_BufSize = 8; // 8 bytes for READ CAPACITY
				SASI_Stat = 0;
				printf("SASI: READ CAPACITY - sectors: %d, last LBA: %d\n", sectorCount, lastLBA);
			} else {
				SASI_Stat = 0x02;
				SASI_Error = 0x7f;
				SASI_Phase += 2;
			}
		}
		else
		{
			SASI_Stat = 0x02;
			SASI_Error = 0x7f;
			SASI_Phase += 2;
		}
		break;
	case 0xc2:
		SASI_Phase = 10;
		SASI_SenseStatPtr = 0;
		if (Config.HDImage[SASI_Device*2+SASI_Unit][0])
			SASI_Stat = 0;
		else
		{
			SASI_Stat = 0x02;
			SASI_Error = 0x7f;
		}
		break;
	default:
		SASI_Phase += 2;
	}
if (hddtrace) {
FILE *fp;
fp=fopen("_trace68.txt", "a");
fprintf(fp, "Com.  - %02X  Dev:%d Unit:%d\n", SASI_Cmd[0], SASI_Device, SASI_Unit);
fclose(fp);
}
}


// -----------------------------------------------------------------------
//   I/O Write
// -----------------------------------------------------------------------
void FASTCALL SASI_Write(DWORD adr, BYTE data)
{
	short result;
	int i;
	BYTE bit;

	if (hddtrace&&((SASI_Phase!=3)||(adr!=0xe96001)))
	{
		FILE *fp;
		fp=fopen("_trace68.txt", "a");
		//fprintf(fp, "Write - Adr:%08X Data:%02X  Phase:%d  (Time:%08X)  @ $%08X\n", adr, data, SASI_Phase, timeGetTime(), C68k_Get_Reg(&C68K, C68K_PC));

#if defined (HAVE_CYCLONE)
		fprintf(fp, "Write - Adr:%08X Data:%02X  Phase:%d  (Time:%08X)  @ $%08X\n", adr, data, SASI_Phase, timeGetTime(), m68000_get_reg(M68K_PC));
#elif defined (HAVE_C68K)
		fprintf(fp, "Write - Adr:%08X Data:%02X  Phase:%d  (Time:%08X)  @ $%08X\n", adr, data, SASI_Phase, timeGetTime(), C68k_Get_PC(&C68K));
#endif
		fclose(fp);
	}
	if ( (adr==0xe96007)&&(SASI_Phase==0) )
	{
		SASI_Device = 0x7f;
		if (data)
		{
			for (i=0, bit=1; bit; i++, bit<<=1)
			{
				if (data&bit)
				{
					SASI_Device = i;
					break;
				}
			}
		}
		if ( (Config.HDImage[SASI_Device*2][0])||(Config.HDImage[SASI_Device*2+1][0]) )
		{
			SASI_Phase++;
			SASI_CmdPtr = 0;
		}
		else
		{
			SASI_Phase = 0;
		}
	}
	else if ( (adr==0xe96003)&&(SASI_Phase==1) )
	{
		SASI_Phase++;
	}
	else if (adr==0xe96005)						// SASI Reset
	{
		SASI_Phase = 0;
		SASI_Sector = 0;
		SASI_Blocks = 0;
		SASI_CmdPtr = 0;
		SASI_Device = 0;
		SASI_Unit = 0;
		SASI_BufPtr = 0;
		SASI_BufSize = 256;
		SASI_RW = 0;
		SASI_Stat = 0;
		SASI_Error = 0;
		SASI_SenseStatPtr = 0;
	}
	else if (adr==0xe96001)
	{
		if (SASI_Phase==2)
		{
			SASI_Cmd[SASI_CmdPtr++] = data;
			if (SASI_CmdPtr==6)			// コマンド発行終了
			{
//				SASI_Phase++;
				SASI_CheckCmd();
			}
		}
		else if ((SASI_Phase==3)&&(!SASI_RW))		// データライト中～
		{
			SASI_Buf[SASI_BufPtr++] = data;
			if (SASI_BufPtr==SASI_BufSize)
			{
				result = SASI_Flush();		// 現在のバッファを書き出す
				SASI_Blocks--;
				if (SASI_Blocks)		// まだ書くブロックがある？
				{
					SASI_Sector++;
					SASI_BufPtr = 0;
					SASI_BufSize = 256;  // Reset to normal sector size for subsequent writes
					result = SASI_Seek();	// 次のセクタをバッファに読む
					if (!result)		// result=0：イメージの最後（＝無効なセクタ）なら
					{
						SASI_Error = 0x0f;
						SASI_Phase++;
					}
				}
				else
					SASI_Phase++;		// 指定ブロックのライト完了
			}
		}
		else if (SASI_Phase==10)
		{
			SASI_SenseStatPtr++;
			if (SASI_SenseStatPtr==10)			// コマンド発行終了
			{
				SASI_Phase = 4;
			}
		}
		if (SASI_Phase==4)
		{
			IOC_IntStat|=0x10;
			if (IOC_IntStat&8) IRQH_Int(1, &SASI_Int);
		}
	}
	StatBar_HDD((SASI_Phase)?2:0);
}
