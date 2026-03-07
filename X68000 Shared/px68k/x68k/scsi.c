// ---------------------------------------------------------------------------------------
//  SCSI.C - 外付けSCSIボード (CZ-6BS1) + SCSI IOCS エミュレーション
//    SCSI IOCSをトラップする形で対応（SPCはエミュレートしない）
//    ブートおよびIOCSコールは 0xE9F800 へのトラップ書き込みでC側で処理
// ---------------------------------------------------------------------------------------

#include	"common.h"
#include	"fileio.h"
#include	"winx68k.h"
#include	"m68000.h"
#include	"scsi.h"
#include	"sasi.h"
#include	"x68kmemory.h"
#include	<stdlib.h>
#include	<sys/stat.h>
#include	<sys/types.h>
#include	<string.h>

#if defined(HAVE_C68K)
#include	"../m68000/c68k/c68k.h"
extern c68k_struc C68K;
#endif

	BYTE	SCSIIPL[0x2000];
	BYTE	SCSIROM_DAT[0x2000];

extern BYTE* s_disk_image_buffer[5];
extern long s_disk_image_buffer_size[5];
static int s_spc_access_log_count = 0;
static int s_iocs_data_offset_cache_valid = 0;
static DWORD s_iocs_data_offset_cache = 0;
static int s_last_iocs_sig_valid = 0;
static DWORD s_last_iocs_sig_addr = 0;
static BYTE s_last_iocs_sig_cmd = 0;
static DWORD s_last_iocs_sig_d2 = 0;
static DWORD s_last_iocs_sig_d3 = 0;
static DWORD s_last_iocs_sig_d4 = 0;
static DWORD s_last_iocs_sig_d5 = 0;
static DWORD s_last_iocs_sig_a1 = 0;
static int s_fn00_boot_count = 0;
static int s_fn00_ari_count = 0;

// Minimal SPC (MB89352) register state for driver initialization.
// Human68k's SCSI driver writes to SCTL/BDID and reads back to verify
// the SPC chip exists.  Without this, the driver assumes "no SPC" and
// disables SCSI disk access entirely.
static BYTE s_spc_regs[0x20];  // mirrors for SPC register writes

// Block device driver state
static DWORD s_scsi_dev_reqpkt = 0;   // saved A5 from strategy call
static int s_scsi_dev_linked = 0;     // device chain linked flag
static DWORD s_scsi_partition_byte_offset = 0; // partition start in image (bytes)
static DWORD s_scsi_sector_size = 1024;        // logical sector size from BPB
static DWORD s_scsi_bpb_ram_addr = 0;
static DWORD s_scsi_bpbptr_ram_addr = 0;
static BYTE s_scsi_drvchk_state = 0x02;        // bit1=media inserted, bit2=ready(0)
static DWORD s_scsi_root_dir_start_sector = 0;
static DWORD s_scsi_root_dir_sector_count = 0;
// -1: unknown, 0: partition-relative sectors, 1: absolute sectors
static int s_scsi_dev_absolute_sectors = -1;

#define SCSI_BPB_RAM_ADDR    0x000C00
#define SCSI_BPBPTR_RAM_ADDR 0x000C30

static void SCSI_LogInit(void);
static int SCSI_HasExternalROM(void);
static void SCSI_HandleBoot(void);
static void SCSI_HandleIOCS(BYTE cmd);
static void SCSI_HandleDeviceCommand(void);
static void SCSI_LogDevicePacket(DWORD reqpkt, BYTE len);
static DWORD SCSI_FindPartitionBootOffset(void);
static DWORD SCSI_DetectBootBlockSize(DWORD bootOffset);
static int SCSI_ReadBPBFromImage(BYTE* outBpb, DWORD* outPartOffset);
static void SCSI_LogText(const char* text);
static void SCSI_LogSPCAccess(const char* op, DWORD adr, BYTE data);
static DWORD SCSI_GetPayloadOffset(void);
static DWORD SCSI_GetBlockSizeFromCode(DWORD sizeCode);
static DWORD SCSI_GetImageBlockSize(void);
static DWORD SCSI_GetIocsDataOffset(void);
static DWORD SCSI_GetTransferLBA(BYTE cmd, DWORD d2, DWORD d4);
static int SCSI_ResolveTransfer(DWORD lba, DWORD blocks, DWORD blockSize,
                                DWORD* imageOffset, DWORD* transferBytes);
static int SCSI_IsLinearRamRange(DWORD addr, DWORD len);
static DWORD SCSI_Mask24(DWORD addr);
static void SCSI_NormalizeRootShortNames(DWORD bufAddr, DWORD startSec,
                                         DWORD count, DWORD secSize);
static void SCSI_LogKernelQueueState(const char* tag);

// ---------------------------------------------------------------------------------------
//  合成SCSI ROM (実機レイアウト互換寄り)
//  - $FC0024: "SCSIEX" シグネチャ
//  - $FC0068: "Human68k" シグネチャ
//  - ブート/IOCSは $E9F800 トラップで C 側へ橋渡し
// ---------------------------------------------------------------------------------------
#define BE32(v) \
	(BYTE)(((v) >> 24) & 0xff), (BYTE)(((v) >> 16) & 0xff), \
	(BYTE)(((v) >> 8) & 0xff), (BYTE)((v) & 0xff)

static BYTE SCSIIMG[] = {
	// $FC0000-$FC001F: エントリテーブル（実機互換で init へ向ける）
	BE32(SCSI_SYNTH_INIT_ENTRY), BE32(SCSI_SYNTH_INIT_ENTRY),
	BE32(SCSI_SYNTH_INIT_ENTRY), BE32(SCSI_SYNTH_INIT_ENTRY),
	BE32(SCSI_SYNTH_INIT_ENTRY), BE32(SCSI_SYNTH_INIT_ENTRY),
	BE32(SCSI_SYNTH_INIT_ENTRY), BE32(SCSI_SYNTH_INIT_ENTRY),

	// $FC0020
	BE32(SCSI_SYNTH_BOOT_ENTRY),

	// $FC0024: "SCSIEX"（外付けSCSI ROM互換）
	'S', 'C', 'S', 'I', 'E', 'X',

	// $FC002A: ボード属性 / $FC002C: トラップI/Oアドレス
	0x00, 0x03,
	0x00, 0xe9, 0xf8, 0x00,

	// $FC0030: ブートエントリ
	0x13, 0xfc, 0x00, 0xff, 0x00, 0xe9, 0xf8, 0x00, // move.b #$ff,$e9f800
	0x4a, 0x80,                                     // tst.l d0
	0x66, 0x06,                                     // bne.s fail
	0x4e, 0xb9, 0x00, 0x00, 0x20, 0x00,             // jsr $00002000
	0x70, 0xff,                                     // fail: moveq #-1,d0
	0x4e, 0x75,                                     // rts

	// $FC0046-$FC0057: 予約
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	// $FC0058: "SCSI" / $FC005C: init / $FC0060: iocs
	// $FC0068: "Human68k"（実機互換位置）
	'S', 'C', 'S', 'I',
	BE32(SCSI_SYNTH_INIT_ENTRY),
	BE32(SCSI_SYNTH_IOCS_ENTRY),
	0x00, 0x00, 0x00, 0x00,
	'H', 'u', 'm', 'a', 'n', '6', '8', 'k',

	// $FC0070: IOCS初期化 (ID0 のみ存在を返す)
	0x23, 0xfc, 0x00, 0xfc, 0x00, 0x80, 0x00, 0x00, 0x07, 0xd4, // move.l #$fc0080,$7d4.l
	0x70, 0x00,                                                   // moveq #0,d0
	0x74, 0x01,                                                   // moveq #1,d2
	0x4e, 0x75,                                                   // rts

	// $FC0080: SCSI IOCSハンドラ
	0x13, 0xc1, 0x00, 0xe9, 0xf8, 0x00, // move.b d1,$e9f800
	0x4e, 0x75,                         // rts

			// $FC0088: trap #15 ディスパッチャ (58 bytes)
			// d0.b のファンクション番号で $400+(d0&0xFF)*4 のハンドラを JSR→RTE
			// d0/a0 を保存復帰して、呼び出し元レジスタ破壊を防ぐ
			// IOCSテーブルの bit0 メタデータを許容するため、呼び出し先は偶数化する。
			// さらに $00008000 未満の低位RAMアドレスは不正ハンドラとして拒否する。
			0x2F, 0x00,                         // move.l d0,-(sp)
			0x2F, 0x08,                         // move.l a0,-(sp)
			0x02, 0x40, 0x00, 0xFF,             // andi.w #$00FF,d0
			0xE5, 0x48,                         // lsl.w #2,d0
			0x41, 0xF8, 0x04, 0x00,             // lea $0400.w,a0
			0x20, 0x70, 0x00, 0x00,             // movea.l (a0,d0.w),a0
			0x20, 0x08,                         // move.l a0,d0
			0x67, 0x1C,                         // beq.s .unimpl
			0x02, 0x80, 0x00, 0xFF, 0xFF, 0xFE, // andi.l #$00FFFFFE,d0
			0x0C, 0x80, 0x00, 0x00, 0x80, 0x00, // cmpi.l #$00008000,d0
			0x65, 0x0E,                         // blo.s .unimpl
			0x20, 0x40,                         // movea.l d0,a0
			0x20, 0x2F, 0x00, 0x04,             // move.l 4(sp),d0
			0x4E, 0x90,                         // jsr (a0)
			0x20, 0x5F,                         // movea.l (sp)+,a0
			0x58, 0x8F,                         // addq.l #4,sp
			0x4E, 0x73,                         // rte
			0x20, 0x5F,                         // .unimpl: movea.l (sp)+,a0
			0x58, 0x8F,                         // addq.l #4,sp
			0x70, 0xFF,                         // moveq #-1,d0
			0x4E, 0x73,                         // rte

			// $FC00C2-$FC00D1: パディング (16 bytes)
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,   // $C2-$D1

			// $FC00D2: デフォルト IOCS ハンドラ (4 bytes)
		// 未実装 IOCS を安全に失敗させる（JSR 経由で呼ばれ RTS で返る）
		0x70, 0xff,                         // moveq #-1,d0
		0x4e, 0x75,                         // rts

		// $FC00D6: fn=$04 互換スタブ (4 bytes)
		// ブートセクタが期待する初期IOCS呼び出しを成功扱いで通す
		0x70, 0x00,                         // moveq #0,d0
		0x4e, 0x75,                         // rts

			// $FC00DA: fn=$34 互換スタブ (4 bytes)
			// 送信可として ready(1) を返す
			0x70, 0x01,                         // moveq #1,d0
			0x4e, 0x75,                         // rts

			// $FC00DE: fn=$35 互換スタブ (4 bytes)
			// 送信処理を成功扱いで通す
			0x70, 0x01,                         // moveq #1,d0
			0x4e, 0x75,                         // rts

			// $FC00E2: fn=$33 互換スタブ (4 bytes)
			// 無入力待ちに合わせて not-ready(0) を返す
			0x70, 0x00,                         // moveq #0,d0
			0x4e, 0x75,                         // rts

			// $FC00E6: fn=$32 互換スタブ (4 bytes)
			// 無入力扱い(0)を返して、キーボード待ち分岐を安定化
			0x70, 0x00,                         // moveq #0,d0
			0x4e, 0x75,                         // rts

		// $FC00EA: fn=$10 互換スタブ (4 bytes)
		// カーネル初期化が期待する成功返却を返す
		0x70, 0x00,                         // moveq #0,d0
		0x4e, 0x75,                         // rts

		// $FC00EE: fn=$AF 互換スタブ (4 bytes)
		// 初期化時の拡張IOCS呼び出しを成功扱いで通す
		0x70, 0x00,                         // moveq #0,d0
		0x4e, 0x75,                         // rts

		// $FC00F2: 直接IOCSラッパー (8 bytes)
		// d0.b をそのまま $E9F800 に出して C 側 SCSI_HandleIOCS へ渡す
		0x13, 0xC0, 0x00, 0xE9, 0xF8, 0x00, // move.b d0,$E9F800
		0x4E, 0x75,                         // rts

		// $FC00FA: fn=$00 互換スタブ (4 bytes)
		// ブート時の A/R/I 分岐待ちで従来互換の 'R'(0x52) を返す
		0x70, 0x52,                         // moveq #$52,d0
		0x4E, 0x75,                         // rts

		// $FC00FE-$FC00FF: パディング (2 bytes, 総サイズ不変)
		0,0,                                // $FE-$FF

	// ---------------------------------------------------------------
	// $FC0100: Human68k ブロックデバイスドライバヘッダ (22バイト)
	// ---------------------------------------------------------------
	// +$00: next device pointer (チェイン終端、リンク時にRAM側を書換)
	0xff, 0xff, 0xff, 0xff,
		// +$04: attribute ($0000 = block device)
		// NOTE: $2000 is a Human68k "remote device" attribute and makes the
		// kernel issue CR_* extended requests (0x40-0x58) instead of block I/O
		// requests. Our synthetic driver implements block-device packets, so the
		// attribute must remain clear.
		0x00, 0x00,
	// +$06: strategy entry = $FC0116
	BE32(0x00fc0116),
	// +$0A: interrupt entry = $FC0120
	BE32(0x00fc0120),
	// +$0E: units(1) + name "SCSI   "
	0x01, 'S', 'C', 'S', 'I', ' ', ' ', ' ',

	// ---------------------------------------------------------------
	// $FC0116: strategy ルーチン (10バイト)
	//   A5 = リクエストパケット → C側で退避
	// ---------------------------------------------------------------
	0x13, 0xfc, 0x00, 0x02, 0x00, 0xe9, 0xf8, 0x02, // move.b #$02,$E9F802
	0x4e, 0x75,                                       // rts

	// ---------------------------------------------------------------
	// $FC0120: interrupt ルーチン (10バイト)
	//   C側でコマンド処理
	// ---------------------------------------------------------------
	0x13, 0xfc, 0x00, 0x01, 0x00, 0xe9, 0xf8, 0x02, // move.b #$01,$E9F802
	0x4e, 0x75,                                       // rts
};

#undef BE32


// -----------------------------------------------------------------------
//   初期化
// -----------------------------------------------------------------------
void SCSI_Init(void)
{
	int i;
	BYTE tmp;
	ZeroMemory(SCSIIPL, 0x2000);
	if (SCSI_HasExternalROM()) {
		p6logd("SCSI_Init: external SCSI ROM detected, using synthetic trap ROM\n");
	}
	memcpy(SCSIIPL, SCSIIMG, sizeof(SCSIIMG));
	for (i=0; i<0x2000; i+=2)
	{
		tmp = SCSIIPL[i];
		SCSIIPL[i] = SCSIIPL[i+1];
		SCSIIPL[i+1] = tmp;
	}
	ZeroMemory(s_spc_regs, sizeof(s_spc_regs));
	// Some Human68k boot paths probe SCTL ($E96003) before issuing writes.
	// Seed bit7 so "SPC present" checks do not treat the controller as absent.
	s_spc_regs[0x03] = 0x80;
	s_scsi_dev_reqpkt = 0;
	s_scsi_dev_linked = 0;
	s_scsi_partition_byte_offset = 0;
	s_scsi_sector_size = 1024;
	s_scsi_bpb_ram_addr = SCSI_BPB_RAM_ADDR;
	s_scsi_bpbptr_ram_addr = SCSI_BPBPTR_RAM_ADDR;
	s_scsi_drvchk_state = 0x02;
	s_scsi_root_dir_start_sector = 0;
	s_scsi_root_dir_sector_count = 0;
	s_scsi_dev_absolute_sectors = -1;
	Memory_RefreshSCSIRomOverlay();
	SCSI_LogInit();
	s_spc_access_log_count = 0;
	SCSI_InvalidateTransferCache();
	s_last_iocs_sig_valid = 0;
	s_fn00_boot_count = 0;
	s_fn00_ari_count = 0;
	{
		char buildTag[96];
		snprintf(buildTag, sizeof(buildTag),
			         "SCSI_BUILD devdrv-partoff-v14 %s %s",
		         __DATE__, __TIME__);
		SCSI_LogText(buildTag);
	}
}


// -----------------------------------------------------------------------
//   撤収〜
// -----------------------------------------------------------------------
void SCSI_Cleanup(void)
{
}

void SCSI_InvalidateTransferCache(void)
{
	s_iocs_data_offset_cache_valid = 0;
	s_iocs_data_offset_cache = 0;
}


// -----------------------------------------------------------------------
//   ログ関連
// -----------------------------------------------------------------------
static void SCSI_GetLogPath(char* outPath, size_t outSize)
{
#ifdef __APPLE__
	const char* home = getenv("HOME");
	if (home && home[0] != '\0') {
		snprintf(outPath, outSize, "%s/Documents/X68000/_scsi_iocs.txt", home);
		return;
	}
#endif
	snprintf(outPath, outSize, "X68000/_scsi_iocs.txt");
}

static void SCSI_EnsureLogDir(void)
{
#ifdef __APPLE__
	const char* home = getenv("HOME");
	if (home && home[0] != '\0') {
		char dirPath[512];
		snprintf(dirPath, sizeof(dirPath), "%s/Documents/X68000", home);
		mkdir(dirPath, 0755);
	}
#endif
}

int SCSI_IsROMPresent(void)
{
	return SCSI_HasExternalROM();
}

static int SCSI_HasExternalROM(void)
{
	size_t i;
	for (i = 0; i < sizeof(SCSIROM_DAT); i++) {
		if (SCSIROM_DAT[i] != 0) {
			return 1;
		}
	}
	return 0;
}

static void SCSI_LogInit(void)
{
	char logPath[512];
	SCSI_GetLogPath(logPath, sizeof(logPath));
	SCSI_EnsureLogDir();
	FILE *fp = fopen(logPath, "w");
	if (fp) {
		fprintf(fp, "--- SCSI log start ---\n");
		fclose(fp);
	}
}

static void SCSI_LogIO(DWORD adr, const char* op, BYTE data)
{
	char logPath[512];
	SCSI_GetLogPath(logPath, sizeof(logPath));
	SCSI_EnsureLogDir();
	FILE *fp = fopen(logPath, "a");
	if (fp) {
		if (op[0] == 'R') {
			fprintf(fp, "SCSI %s @ 0x%06X\n", op, (unsigned int)adr);
		} else {
			fprintf(fp, "SCSI %s @ 0x%06X = 0x%02X\n", op, (unsigned int)adr, data);
		}
		fclose(fp);
	}
}

static void SCSI_LogText(const char* text)
{
	char logPath[512];
	SCSI_GetLogPath(logPath, sizeof(logPath));
	SCSI_EnsureLogDir();
	FILE *fp = fopen(logPath, "a");
	if (fp) {
		fprintf(fp, "%s\n", text);
		fclose(fp);
	}
}

static void SCSI_LogSPCAccess(const char* op, DWORD adr, BYTE data)
{
	char line[96];
	if (s_spc_access_log_count >= 256) {
		return;
	}
	snprintf(line, sizeof(line), "SPC_%s adr=$%06X data=$%02X",
	         op, (unsigned int)(adr & 0x00ffffff), (unsigned int)data);
	SCSI_LogText(line);
	s_spc_access_log_count++;
}

static DWORD SCSI_GetPayloadOffset(void)
{
	BYTE* buf;
	long size;

	if (s_disk_image_buffer[4] == NULL || s_disk_image_buffer_size[4] < 8) {
		return 0;
	}

	buf = s_disk_image_buffer[4];
	size = s_disk_image_buffer_size[4];

	// Standard container header.
	if (memcmp(buf, "X68SCSI1", 8) == 0) {
		return 0x400;
	}
	// Some SxSI-formatted images store 0x00 at byte 0, followed by "68SCSI1".
	if (memcmp(buf + 1, "68SCSI1", 7) == 0) {
		return 0x400;
	}
	// Metadata strings used by common container variants.
	if (size >= 0x20 && memcmp(buf + 0x10, "Human68K SCSI", 12) == 0) {
		return 0x400;
	}
	if (size >= 0x20 && memcmp(buf + 0x10, "This SCSI-UNIT", 14) == 0) {
		return 0x400;
	}
	return 0;
}

static DWORD SCSI_GetBlockSizeFromCode(DWORD sizeCode)
{
	DWORD code16 = sizeCode & 0xffff;

	// Legacy SCSI IPLs sometimes pass 0x8000 for 512-byte blocks.
	if (code16 == 0x8000) {
		return 512;
	}

	switch (sizeCode & 0xff) {
	case 0:
		return 256;
	case 1:
		return 512;
	case 2:
		return 1024;
	default:
		return SCSI_GetImageBlockSize();
	}
}

static DWORD SCSI_GetImageBlockSize(void)
{
	BYTE* buf;
	DWORD headerBlockSize;
	long size;
	DWORD cand512;
	DWORD cand1024;

	if (s_disk_image_buffer[4] == NULL || s_disk_image_buffer_size[4] < 16) {
		return 512;
	}
	buf = s_disk_image_buffer[4];
	size = s_disk_image_buffer_size[4];

	if (memcmp(buf, "X68SCSI1", 8) == 0 || memcmp(buf + 1, "68SCSI1", 7) == 0) {
		headerBlockSize = ((DWORD)buf[8] << 8) | (DWORD)buf[9];
		if (headerBlockSize == 256 || headerBlockSize == 512 || headerBlockSize == 1024) {
			return headerBlockSize;
		}
	}

	// Headerless raw images are typically 512B or 1024B sectors.
	// Prefer a signature-based guess from the partition table at LBA4.
	cand512 = 4U * 512U;
	if ((long)(cand512 + 4) <= size && memcmp(buf + cand512, "X68K", 4) == 0) {
		return 512;
	}
	cand1024 = 4U * 1024U;
	if ((long)(cand1024 + 4) <= size && memcmp(buf + cand1024, "X68K", 4) == 0) {
		return 1024;
	}

	return 512;
}

static DWORD SCSI_GetIocsDataOffset(void)
{
	// X68SCSI1/SxSI container headers occupy the first 2 sectors (0x400 bytes)
	// of the virtual SCSI disk.  The X68000 OS addresses the entire image
	// including those header sectors via IOCS LBAs (e.g. boot sector = LBA 2,
	// partition table = LBA 4 for 512-byte sectors).  Therefore IOCS LBA
	// resolution must always start from file offset 0, NOT from the payload.
	// SCSI_GetPayloadOffset() is used only by SCSI_HandleBoot to locate
	// the boot sector within the image.
	return 0;
}

static DWORD SCSI_GetTransferLBA(BYTE cmd, DWORD d2, DWORD d4)
{
	DWORD lba24 = d2 & 0x00ffffffU;
	DWORD fallbackLba;
	DWORD payloadOffset;
	DWORD imageBlockSize;
	DWORD payloadBlocks;
	// _S_READI ($2E) seen during boot can pass sign-extended 16-bit LBAs
	// (e.g. d2=$FFFF001E / $FFFFFFFF). Treat this form as 16-bit LBA.
	if (cmd == 0x2e && (d2 & 0xffff0000U) == 0xffff0000U) {
		return d2 & 0x0000ffffU;
	}
	// Human68k SCSI IOCS uses d2.l as logical block address for READ/WRITE
	// and their extended variants. d4 carries the target ID byte and can appear
	// non-zero even when LBA is 0, so using d4 as an LBA fallback causes
	// mis-addressed reads on LBA 0.
	// Some IOCS paths encode SCSI ID in the upper byte of d2 (e.g. $20xxxxxx).
	// Disk images are capped well below 24-bit LBA in this emulator, so strip
	// the upper control byte unconditionally.
	//
	// However, some _S_READEXT/_S_WRITEEXT boot paths transiently pass d2=0
	// while placing a usable coarse LBA in d4's upper byte. On SxSI container
	// images this coarse value includes the 2-sector container header, so adjust
	// by payload base blocks before issuing the read.
	if ((cmd == 0x26 || cmd == 0x27) &&
	    lba24 == 0 &&
	    (d4 & 0x0000ffffU) == 0x0000ffffU &&
	    ((d4 >> 24) & 0xffU) >= 2U) {
		fallbackLba = (d4 >> 24) & 0xffU;
		payloadOffset = SCSI_GetPayloadOffset();
		imageBlockSize = SCSI_GetImageBlockSize();
		if (payloadOffset != 0 && imageBlockSize != 0) {
			payloadBlocks = payloadOffset / imageBlockSize;
			if (payloadBlocks > 0 && fallbackLba >= payloadBlocks) {
				fallbackLba -= payloadBlocks;
			}
		}
		return fallbackLba;
	}
	return lba24;
}

static int SCSI_ResolveTransfer(DWORD lba, DWORD blocks, DWORD blockSize,
                                DWORD* imageOffset, DWORD* transferBytes)
{
	DWORD dataOffset;
	unsigned long long dataSize;
	unsigned long long start;
	unsigned long long bytes;

	if (s_disk_image_buffer[4] == NULL || s_disk_image_buffer_size[4] <= 0) {
		return 0;
	}
	if (blocks == 0 || blockSize == 0) {
		return 0;
	}

	dataOffset = SCSI_GetIocsDataOffset();
	dataSize = (unsigned long long)(DWORD)s_disk_image_buffer_size[4];
	if (dataSize == 0 || (unsigned long long)dataOffset >= dataSize) {
		return 0;
	}
	dataSize -= (unsigned long long)dataOffset;

	// Map IOCS LBA against detected data base (header-inclusive/exclusive).
	start = (unsigned long long)lba * (unsigned long long)blockSize;
	bytes = (unsigned long long)blocks * (unsigned long long)blockSize;
	if (start + bytes > dataSize) {
		return 0;
	}

	*imageOffset = dataOffset + (DWORD)start;
	*transferBytes = (DWORD)bytes;
	return 1;
}

static int SCSI_IsLinearRamRange(DWORD addr, DWORD len)
{
	unsigned long long start;
	unsigned long long end;

	if (len == 0) {
		return 1;
	}
	start = (unsigned long long)(addr & 0x00ffffff);
	end = start + (unsigned long long)len;
	if (start >= 0x00c00000ULL) {
		return 0;
	}
	if (end > 0x00c00000ULL || end < start) {
		return 0;
	}
	return 1;
}

static DWORD SCSI_Mask24(DWORD addr)
{
	return addr & 0x00ffffffU;
}

static void SCSI_SetReqStatus(DWORD reqpkt, int ok, BYTE errCode)
{
	// Human68k request status word at offset +3/+4 (big-endian):
	//   Bit 15 (byte+3 bit 7): Done (command completed)
	//   Bit  8 (byte+3 bit 0): Error flag
	//   Bits 7-0 (byte+4):     Error code
	if (ok) {
		Memory_WriteB(reqpkt + 3, 0x80);  // Done=1, Error=0
		Memory_WriteB(reqpkt + 4, 0x00);
	} else {
		Memory_WriteB(reqpkt + 3, 0x81);  // Done=1, Error=1
		Memory_WriteB(reqpkt + 4, errCode);
	}
}

static void SCSI_LogKernelQueueState(const char* tag)
{
	static int s_queueLogCount = 0;
	DWORD bcc;
	WORD bca;
	BYTE bc6;
	BYTE bc7;
	char line[160];

	if (s_queueLogCount >= 48) {
		return;
	}
	bcc = ((DWORD)Memory_ReadB(0x00000bcc) << 24) |
	      ((DWORD)Memory_ReadB(0x00000bcd) << 16) |
	      ((DWORD)Memory_ReadB(0x00000bce) << 8) |
	      ((DWORD)Memory_ReadB(0x00000bcf));
	bca = ((WORD)Memory_ReadB(0x00000bca) << 8) |
	      ((WORD)Memory_ReadB(0x00000bcb));
	bc6 = Memory_ReadB(0x00000bc6);
	bc7 = Memory_ReadB(0x00000bc7);
	snprintf(line, sizeof(line),
	         "%s bca=$%04X bcc=%08X bc6=$%02X bc7=$%02X",
	         (tag != NULL) ? tag : "SCSI_DEV QUE",
	         (unsigned int)bca,
	         (unsigned int)(bcc & 0x00ffffffU),
	         (unsigned int)bc6,
	         (unsigned int)bc7);
	SCSI_LogText(line);
	s_queueLogCount++;
}

static void SCSI_NormalizeRootShortNames(DWORD bufAddr, DWORD startSec,
                                         DWORD count, DWORD secSize)
{
	DWORD s;
	DWORD rootStart = s_scsi_root_dir_start_sector;
	DWORD rootCount = s_scsi_root_dir_sector_count;
	DWORD changed = 0;
	char logLine[128];

	if (rootCount == 0 || secSize < 32 || count == 0) {
		return;
	}

	for (s = 0; s < count; s++) {
		DWORD sec = startSec + s;
		DWORD secBase;
		DWORD off;
		if (sec < rootStart || sec >= rootStart + rootCount) {
			continue;
		}
		secBase = bufAddr + (s * secSize);
		if (!SCSI_IsLinearRamRange(secBase, secSize)) {
			continue;
		}
		for (off = 0; off + 32 <= secSize; off += 32) {
			DWORD ent = secBase + off;
			BYTE first = Memory_ReadB(ent + 0);
			BYTE attr;
			DWORD j;
			if (first == 0x00) {
				break;
			}
			if (first == 0xE5) {
				continue;
			}
			attr = Memory_ReadB(ent + 0x0B);
			if (attr == 0x0F) {
				continue; // VFAT long-name entry
			}
			for (j = 0; j < 11; j++) {
				BYTE ch = Memory_ReadB(ent + j);
				if (ch >= 'a' && ch <= 'z') {
					Memory_WriteB(ent + j, (BYTE)(ch - ('a' - 'A')));
					changed++;
				}
			}
		}
	}

	if (changed != 0) {
		snprintf(logLine, sizeof(logLine),
		         "SCSI_DEV READ normalized root 8.3 lowercase -> uppercase (%u chars)",
		         (unsigned int)changed);
		SCSI_LogText(logLine);
	}
}

static void SCSI_LogRootConfigEntry(DWORD bufAddr, DWORD startSec,
                                    DWORD count, DWORD secSize)
{
	DWORD s;
	DWORD rootStart = s_scsi_root_dir_start_sector;
	DWORD rootCount = s_scsi_root_dir_sector_count;
	char line[256];

	if (rootCount == 0 || secSize < 32 || count == 0) {
		return;
	}

	for (s = 0; s < count; s++) {
		DWORD sec = startSec + s;
		DWORD secBase;
		DWORD off;
		if (sec < rootStart || sec >= rootStart + rootCount) {
			continue;
		}
		secBase = bufAddr + (s * secSize);
		if (!SCSI_IsLinearRamRange(secBase, secSize)) {
			continue;
		}
		for (off = 0; off + 32 <= secSize; off += 32) {
			DWORD ent = secBase + off;
			BYTE first = Memory_ReadB(ent + 0);
			BYTE attr = Memory_ReadB(ent + 0x0B);
			WORD firstCluster;
			DWORD fileSize;

			if (first == 0x00) {
				break;
			}
			if (first == 0xE5 || attr == 0x0F) {
				continue;
			}
			if (Memory_ReadB(ent + 0) != 'C' || Memory_ReadB(ent + 1) != 'O' ||
			    Memory_ReadB(ent + 2) != 'N' || Memory_ReadB(ent + 3) != 'F' ||
			    Memory_ReadB(ent + 4) != 'I' || Memory_ReadB(ent + 5) != 'G' ||
			    Memory_ReadB(ent + 6) != ' ' || Memory_ReadB(ent + 7) != ' ' ||
			    Memory_ReadB(ent + 8) != 'S' || Memory_ReadB(ent + 9) != 'Y' ||
			    Memory_ReadB(ent + 10) != 'S') {
				continue;
			}

			snprintf(line, sizeof(line),
			         "SCSI_DEV READ CONFIG entry32=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			         (unsigned int)Memory_ReadB(ent + 0x00), (unsigned int)Memory_ReadB(ent + 0x01),
			         (unsigned int)Memory_ReadB(ent + 0x02), (unsigned int)Memory_ReadB(ent + 0x03),
			         (unsigned int)Memory_ReadB(ent + 0x04), (unsigned int)Memory_ReadB(ent + 0x05),
			         (unsigned int)Memory_ReadB(ent + 0x06), (unsigned int)Memory_ReadB(ent + 0x07),
			         (unsigned int)Memory_ReadB(ent + 0x08), (unsigned int)Memory_ReadB(ent + 0x09),
			         (unsigned int)Memory_ReadB(ent + 0x0A), (unsigned int)Memory_ReadB(ent + 0x0B),
			         (unsigned int)Memory_ReadB(ent + 0x0C), (unsigned int)Memory_ReadB(ent + 0x0D),
			         (unsigned int)Memory_ReadB(ent + 0x0E), (unsigned int)Memory_ReadB(ent + 0x0F),
			         (unsigned int)Memory_ReadB(ent + 0x10), (unsigned int)Memory_ReadB(ent + 0x11),
			         (unsigned int)Memory_ReadB(ent + 0x12), (unsigned int)Memory_ReadB(ent + 0x13),
			         (unsigned int)Memory_ReadB(ent + 0x14), (unsigned int)Memory_ReadB(ent + 0x15),
			         (unsigned int)Memory_ReadB(ent + 0x16), (unsigned int)Memory_ReadB(ent + 0x17),
			         (unsigned int)Memory_ReadB(ent + 0x18), (unsigned int)Memory_ReadB(ent + 0x19),
			         (unsigned int)Memory_ReadB(ent + 0x1A), (unsigned int)Memory_ReadB(ent + 0x1B),
			         (unsigned int)Memory_ReadB(ent + 0x1C), (unsigned int)Memory_ReadB(ent + 0x1D),
			         (unsigned int)Memory_ReadB(ent + 0x1E), (unsigned int)Memory_ReadB(ent + 0x1F));
			SCSI_LogText(line);

			firstCluster = (WORD)Memory_ReadB(ent + 0x1A) |
			               ((WORD)Memory_ReadB(ent + 0x1B) << 8);
			fileSize = (DWORD)Memory_ReadB(ent + 0x1C) |
			           ((DWORD)Memory_ReadB(ent + 0x1D) << 8) |
			           ((DWORD)Memory_ReadB(ent + 0x1E) << 16) |
			           ((DWORD)Memory_ReadB(ent + 0x1F) << 24);
			snprintf(line, sizeof(line),
			         "SCSI_DEV READ CONFIG meta attr=$%02X cluster=%u size=%u",
			         (unsigned int)attr,
			         (unsigned int)firstCluster,
			         (unsigned int)fileSize);
			SCSI_LogText(line);
			return;
		}
	}
}


// -----------------------------------------------------------------------
//   SCSI ブートトラップハンドラ
//   ブートセクタ (先頭8セクタ=2048バイト) を HDD イメージから読み込み
//   M68000 メモリ $002000 にコピーして d0=0 (成功) を返す
// -----------------------------------------------------------------------
static void SCSI_HandleBoot(void)
{
#if defined(HAVE_C68K)
	DWORD i;
	DWORD bootSize;
	DWORD bootOffset = 0;
	DWORD partBootOffset = 0;
	DWORD blockSize;
	DWORD bootBlockSize = 0;
	DWORD bootBaseLBA;
	DWORD d5Exp = 0;
	DWORD destAddr = 0x002000;
	DWORD imgSize;
	BYTE* imgBuf;
	char bootLog[128];

	printf("SCSI_HandleBoot: Attempting SCSI boot...\n");

	if (s_disk_image_buffer[4] == NULL || s_disk_image_buffer_size[4] <= 0) {
		printf("SCSI_HandleBoot: No HDD image loaded\n");
		SCSI_LogText("SCSI_BOOT: no HDD image");
		C68k_Set_DReg(&C68K, 0, 0xFFFFFFFF);
		return;
	}
	imgBuf = s_disk_image_buffer[4];
	imgSize = (DWORD)s_disk_image_buffer_size[4];

	blockSize = SCSI_GetImageBlockSize();
	if (blockSize == 0) {
		blockSize = 512;
	}
	// SCSI HDD boot code lives at LBA2.  X68SCSI1 containers place the payload
	// (virtual LBA2) at 0x400; raw images need an explicit +2 sector offset.
	bootOffset = SCSI_GetPayloadOffset();
	if (bootOffset == 0) {
		bootOffset = blockSize * 2;
	}
	partBootOffset = SCSI_FindPartitionBootOffset();

	// Prefer the actual partition boot sector when available.
	// Many SxSI container IPL stubs at LBA2 are menu/loader code that expects
	// firmware behaviors not fully emulated here.
	if (partBootOffset != 0 &&
	    partBootOffset + 4 <= imgSize &&
	    (imgBuf[partBootOffset] & 0xF0) == 0x60 &&
	    partBootOffset != bootOffset) {
		snprintf(bootLog, sizeof(bootLog),
		         "SCSI_BOOT prefer partBoot=0x%X over lba2=0x%X",
		         (unsigned int)partBootOffset, (unsigned int)bootOffset);
		SCSI_LogText(bootLog);
		bootOffset = partBootOffset;
	}
	// SxSI menu IPL often expects firmware-side console/IOCS behavior that
	// is incomplete in this synthetic path.  When a valid partition boot
	// sector exists, jump there directly instead of executing the menu layer.
	if (bootOffset + 0x40 <= imgSize &&
	    memcmp(imgBuf + bootOffset + 0x2A, "SxSI Disk IPL MENU", 17) == 0) {
		if (partBootOffset != 0 &&
		    partBootOffset + 4 <= imgSize &&
		    (imgBuf[partBootOffset] & 0xF0) == 0x60) {
			snprintf(bootLog, sizeof(bootLog),
			         "SCSI_BOOT bypass SxSI menu -> part=0x%X",
			         (unsigned int)partBootOffset);
			SCSI_LogText(bootLog);
			bootOffset = partBootOffset;
		}
	}
	// LBA2ブートが壊れているイメージに限り、パーティションブートへ退避。
	if (bootOffset + 4 <= imgSize && (imgBuf[bootOffset] & 0xF0) != 0x60) {
		if (partBootOffset != 0 &&
		    partBootOffset + 4 <= imgSize &&
		    (imgBuf[partBootOffset] & 0xF0) == 0x60) {
			snprintf(bootLog, sizeof(bootLog),
			         "SCSI_BOOT fallback part=0x%X lba2first=%02X%02X%02X%02X",
			         (unsigned int)partBootOffset,
			         (unsigned int)imgBuf[bootOffset + 0],
			         (unsigned int)imgBuf[bootOffset + 1],
			         (unsigned int)imgBuf[bootOffset + 2],
			         (unsigned int)imgBuf[bootOffset + 3]);
			SCSI_LogText(bootLog);
			bootOffset = partBootOffset;
		}
	}
	bootBlockSize = SCSI_DetectBootBlockSize(bootOffset);
	if (bootBlockSize != 0) {
		blockSize = bootBlockSize;
	}
	bootBaseLBA = bootOffset / blockSize;
	if (blockSize == 512) {
		d5Exp = 1;
	} else if (blockSize == 1024) {
		d5Exp = 2;
	} else if (blockSize >= 2048) {
		d5Exp = 3;
	}

	// ブートコードとして先頭8セクタをロード
	bootSize = blockSize * 8;
	if (bootOffset + bootSize > (DWORD)s_disk_image_buffer_size[4]) {
		bootSize = (DWORD)s_disk_image_buffer_size[4] - bootOffset;
	}
	if (bootSize < 16) {
		snprintf(bootLog, sizeof(bootLog),
		         "SCSI_BOOT: invalid bootSize=%u offset=0x%X",
		         (unsigned int)bootSize, (unsigned int)bootOffset);
		SCSI_LogText(bootLog);
		C68k_Set_DReg(&C68K, 0, 0xFFFFFFFF);
		return;
	}

	// M68000 メモリにコピー (バイト単位で書き込み、エンディアン考慮)
	for (i = 0; i < bootSize; i++) {
		Memory_WriteB(destAddr + i, s_disk_image_buffer[4][bootOffset + i]);
	}

	snprintf(bootLog, sizeof(bootLog),
	         "SCSI_BOOT: load=%u offset=0x%X lbaBase=%u blk=%u d5=%u first=%02X%02X%02X%02X",
	         (unsigned int)bootSize, (unsigned int)bootOffset,
	         (unsigned int)bootBaseLBA,
	         (unsigned int)blockSize,
	         (unsigned int)d5Exp,
	         s_disk_image_buffer[4][bootOffset + 0],
	         s_disk_image_buffer[4][bootOffset + 1],
	         s_disk_image_buffer[4][bootOffset + 2],
	         s_disk_image_buffer[4][bootOffset + 3]);
	SCSI_LogText(bootLog);

	// Real SCSI IPL passes sector-size code in d5 and target ID in d1.
	// Some boot sectors rely on these registers before the first IOCS call.
	C68k_Set_DReg(&C68K, 5, d5Exp);
	// Real SCSI IPL passes boot target ID in d1 (ID0 -> $20).  Some boot
	// sectors reuse this immediately for follow-up IOCS reads.
	C68k_Set_DReg(&C68K, 1, 0x00000020);

	printf("SCSI_HandleBoot: Loaded %d bytes from +0x%X to $%06X (d5=%u)\n",
	       (int)bootSize, (int)bootOffset, (int)destAddr, (unsigned int)d5Exp);
	C68k_Set_DReg(&C68K, 0, 0);  // d0 = 0 (成功)
#endif
}


// -----------------------------------------------------------------------
//   SCSI IOCS トラップハンドラ
//   d1.b = SCSI IOCS コマンド番号
//   d0.l = ステータス (0 成功, -1 失敗)
// -----------------------------------------------------------------------
static void SCSI_HandleIOCS(BYTE cmd)
{
#if defined(HAVE_C68K)
	DWORD d2 = C68k_Get_DReg(&C68K, 2);
	DWORD d3 = C68k_Get_DReg(&C68K, 3);
	DWORD d4 = C68k_Get_DReg(&C68K, 4);
	DWORD d5 = C68k_Get_DReg(&C68K, 5);
	DWORD d1 = C68k_Get_DReg(&C68K, 1);
	DWORD d7 = C68k_Get_DReg(&C68K, 7);
	DWORD a1 = C68k_Get_AReg(&C68K, 1);
	DWORD result = 0xFFFFFFFF;
	DWORD i;
	char logLine[192];

	snprintf(logLine, sizeof(logLine),
	         "SCSI_IOCS_BEGIN cmd=$%02X d1=%08X d2=%08X d3=%08X d4=%08X d5=%08X a1=%08X",
	         cmd, (unsigned int)d1, (unsigned int)d2, (unsigned int)d3, (unsigned int)d4,
	         (unsigned int)d5, (unsigned int)a1);
	SCSI_LogText(logLine);

		switch (cmd) {
			case 0x00: {  // synthetic fallback for IOCS fn=$00 (_B_KEYINP)
				// d7 contains random register values from the caller, NOT keyboard
				// state.  The old d7-flag logic falsely returned 'A' (Abort) when
				// d7 bit 4 happened to be set, derailing the boot sequence.
				if (d1 == 0x00000640U) {
					// Boot manager loop at $86F2: loops S_READ + fn=$00.
					// On real hardware fn=$00 blocks; simulate Enter press.
					s_fn00_boot_count++;
					if (s_fn00_boot_count >= 2) {
						result = 0x0DU;  // CR = Enter key
						s_fn00_boot_count = 0;
					} else {
						result = 0;
					}
					s_fn00_ari_count = 0;
				} else if (d1 == 0x0000000DU) {
					// A/R/I error handler context (d1=$0D = CR from prompt).
					// The handler at $F774 checks BTST #6,D7 before accepting
					// 'I', and BTST #5,D7 for 'R'.  When d7.b only has bit 4
					// (Abort-only), both are rejected and it loops forever.
					// Force bits 5+6 so 'I' is accepted.
					{
						DWORD curD7 = C68k_Get_DReg(&C68K, 7);
						if ((curD7 & 0x60U) == 0 && (curD7 & 0x10U) != 0) {
							C68k_Set_DReg(&C68K, 7, curD7 | 0x70U);
						}
					}
					result = (DWORD)'I';  // Always Ignore
					s_fn00_ari_count++;
				} else {
					// General keyboard input: return 0 (no key)
					result = 0;
				}
				snprintf(logLine, sizeof(logLine),
				         "SCSI_IOCS_FN00 d1=%08X d7=%08X ari=%d bcnt=%d -> d0=%08X",
				         (unsigned int)d1,
				         (unsigned int)d7,
				         s_fn00_ari_count,
				         s_fn00_boot_count,
				         (unsigned int)result);
				SCSI_LogText(logLine);
				break;
			}

			case 0x11: {  // _B_SUPER (supervisor/user stack switch)
				DWORD usp = C68k_Get_USP(&C68K) & 0x00ffffffU;
				DWORD d1super = d1;
				DWORD a7 = C68k_Get_AReg(&C68K, 7) & 0x00ffffffU;
				DWORD a1reg = C68k_Get_AReg(&C68K, 1) & 0x00ffffffU;
				DWORD d2reg = d2 & 0x00ffffffU;
				DWORD srAddr = (a7 + 12U) & 0x00ffffffU;
				WORD frameSr = Memory_ReadW(srAddr);

				if (d1super == 0xffffffffU) {
					// Enter/keep supervisor mode and return current USP.
					if (usp == 0U) {
						if (a1reg >= 0x00000400U && a1reg < 0x00c00000U) {
							usp = a1reg;
						} else if (a7 >= 0x00000400U && a7 < 0x00c00000U) {
							usp = a7;
						}
						if (usp != 0U) {
							C68k_Set_USP(&C68K, usp);
						}
					}
					frameSr |= 0x2000U;
					Memory_WriteW(srAddr, frameSr);
					result = usp;
				} else {
					// Leave supervisor mode. d1=0 callers usually pass target USP in d2.
					DWORD newUsp = d2reg;
					if (newUsp < 0x00010000U || newUsp >= 0x00c00000U) {
						newUsp = d1super & 0x00ffffffU;
					}
					if (newUsp < 0x00010000U || newUsp >= 0x00c00000U) {
						newUsp = usp;
					}
					C68k_Set_USP(&C68K, newUsp);
					frameSr &= (WORD)~0x2000U;
					Memory_WriteW(srAddr, frameSr);
					result = 0;
				}

				snprintf(logLine, sizeof(logLine),
				         "SCSI_IOCS_SUPER d1=%08X d2=%08X usp=%08X sr=%04X a1=%08X a7=%08X d0=%08X",
				         (unsigned int)d1super,
				         (unsigned int)d2reg,
				         (unsigned int)usp,
				         (unsigned int)frameSr,
				         (unsigned int)a1reg,
				         (unsigned int)a7,
				         (unsigned int)result);
				SCSI_LogText(logLine);
				break;
			}

	case 0x20: {  // _S_INQUIRY
		BYTE inquiry[36];
		DWORD len = d3 & 0xff;
		DWORD copyLen;

		memset(inquiry, 0x20, sizeof(inquiry));
		inquiry[0] = 0x00;  // Direct access device
		inquiry[1] = 0x00;  // non-removable
		inquiry[2] = 0x02;  // SCSI-2
		inquiry[3] = 0x00;
		inquiry[4] = 31;    // additional length
		memcpy(&inquiry[8], "PX68K   ", 8);
		memcpy(&inquiry[16], "SCSI HDD EMU    ", 16);
		memcpy(&inquiry[32], "1.00", 4);

		copyLen = (len < (DWORD)sizeof(inquiry)) ? len : (DWORD)sizeof(inquiry);
		if (!SCSI_IsLinearRamRange(a1, copyLen)) {
			result = 0xFFFFFFFF;
			break;
		}
		for (i = 0; i < copyLen; i++) {
			Memory_WriteB(a1 + i, inquiry[i]);
		}
		result = 0;
		break;
	}

		case 0x21:  // _S_READ
		case 0x26:  // _S_READEXT
		case 0x2e: {  // _S_READI
			DWORD lba;
			DWORD blocks;
			DWORD blockSize = SCSI_GetBlockSizeFromCode(d5);
			DWORD partBaseLba = 0;
			int usePackedLegacy = 0;
			DWORD imageOffset;
			DWORD transferBytes;

				lba = SCSI_GetTransferLBA(cmd, d2, d4);
				if (blockSize != 0 && s_scsi_partition_byte_offset != 0) {
					partBaseLba = (DWORD)((unsigned long long)s_scsi_partition_byte_offset /
					                     (unsigned long long)blockSize);
				}
				// Boot-path packed register encoding: when d1=$0640 the
				// registers d1-d3 carry data-structure values (d1=$0640,
				// d2=$0641, d3=$06420643). The real LBA is d2's low byte
				// plus the partition base, and block count comes from d4.
				if (cmd == 0x21 &&
				    d1 == 0x00000640U &&
				    (d2 & 0xffff0000U) == 0 &&
				    (d2 & 0x0000ff00U) != 0 &&
				    (d2 & 0x0000ff00U) == (d1 & 0x0000ff00U)) {
					DWORD packedLba = d2 & 0x000000ffU;
					if (partBaseLba != 0) {
						packedLba += partBaseLba;
					}
					if (packedLba != lba) {
						snprintf(logLine, sizeof(logLine),
						         "SCSI_XFER_LBA_PACKED cmd=$%02X d1=%08X d2=%08X lba=%u->%u base=%u",
						         (unsigned int)cmd,
						         (unsigned int)d1,
						         (unsigned int)d2,
						         (unsigned int)lba,
						         (unsigned int)packedLba,
						         (unsigned int)partBaseLba);
						SCSI_LogText(logLine);
						lba = packedLba;
					}
					usePackedLegacy = 1;
				}
				if ((cmd == 0x26 || cmd == 0x27) && (d2 & 0x00ffffffU) == 0 && lba != 0) {
					snprintf(logLine, sizeof(logLine),
					         "SCSI_XFER_LBA_FALLBACK cmd=$%02X d2=%08X d4=%08X -> lba=%u",
				         cmd, (unsigned int)d2, (unsigned int)d4, (unsigned int)lba);
				SCSI_LogText(logLine);
			}
			blocks = d3;
			if (cmd == 0x21) {
				blocks &= 0xff;
				if (blocks == 0) {
					blocks = 256;
				}
				if (usePackedLegacy) {
					DWORD packedBlocks = d4 & 0x0000ffffU;
					if (packedBlocks == 0) {
						packedBlocks = 1;
					}
					snprintf(logLine, sizeof(logLine),
					         "SCSI_XFER_BLK_PACKED cmd=$%02X d4=%08X blocks=%u->%u",
					         (unsigned int)cmd,
					         (unsigned int)d4,
					         (unsigned int)blocks,
					         (unsigned int)packedBlocks);
					SCSI_LogText(logLine);
					blocks = packedBlocks;
				}
			} else if (cmd == 0x2e) {
			// _S_READI uses its own parameter semantics; d3=0 appears as a probe/no-op
			// in this boot path, so do not force a sector transfer here.
			blocks &= 0xffff;
			if (blocks == 0) {
				snprintf(logLine, sizeof(logLine),
				         "SCSI_XFER_READI_NOOP d2=%08X d3=%08X d4=%08X d5=%08X",
				         (unsigned int)d2,
				         (unsigned int)d3,
				         (unsigned int)d4,
				         (unsigned int)d5);
				SCSI_LogText(logLine);
				result = 0;
				break;
			}
		}
		// d2=$FFFF in boot context = FAT end-of-chain marker.
		// Zero-fill ONE block only — d3 carries packed struct data (67),
		// not a real block count; using it would overwrite code at $86F2+.
		if (d1 == 0x00000640U && d2 == 0x0000FFFFU) {
			DWORD zeroBytes = blockSize;  // 1 block (512 bytes)
			if (zeroBytes > 0 && SCSI_IsLinearRamRange(a1, zeroBytes)) {
				for (i = 0; i < zeroBytes; i++) {
					Memory_WriteB(a1 + i, 0);
				}
			}
			snprintf(logLine, sizeof(logLine),
			         "SCSI_XFER_READ_CHAIN_END cmd=$%02X zeroed %u bytes at %08X",
			         cmd, (unsigned int)zeroBytes, (unsigned int)a1);
			SCSI_LogText(logLine);
			result = 0;
			break;
		}
		if (!SCSI_ResolveTransfer(lba, blocks, blockSize, &imageOffset, &transferBytes)) {
			result = 0xFFFFFFFF;
			break;
		}
		if (!SCSI_IsLinearRamRange(a1, transferBytes)) {
			result = 0xFFFFFFFF;
			break;
		}

		for (i = 0; i < transferBytes; i++) {
			Memory_WriteB(a1 + i, s_disk_image_buffer[4][imageOffset + i]);
		}
			snprintf(logLine, sizeof(logLine),
			         "SCSI_XFER_READ cmd=$%02X lba=%u blocks=%u blockSize=%u imgOff=0x%X first=%02X%02X%02X%02X",
			         cmd,
			         (unsigned int)lba,
			         (unsigned int)blocks,
			         (unsigned int)blockSize,
			         (unsigned int)imageOffset,
		         s_disk_image_buffer[4][imageOffset + 0],
		         s_disk_image_buffer[4][imageOffset + 1],
		         s_disk_image_buffer[4][imageOffset + 2],
		         s_disk_image_buffer[4][imageOffset + 3]);
		SCSI_LogText(logLine);
		result = 0;
		break;
	}

	case 0x22:  // _S_WRITE
	case 0x27: {  // _S_WRITEEXT
		DWORD lba;
		DWORD blocks;
		DWORD blockSize = SCSI_GetBlockSizeFromCode(d5);
		DWORD partBaseLba = 0;
		int usePackedLegacy = 0;
		DWORD imageOffset;
		DWORD transferBytes;

		lba = SCSI_GetTransferLBA(cmd, d2, d4);
		if (blockSize != 0 && s_scsi_partition_byte_offset != 0) {
			partBaseLba = (DWORD)((unsigned long long)s_scsi_partition_byte_offset /
			                     (unsigned long long)blockSize);
		}
		if (cmd == 0x22 &&
		    d1 == 0x00000640U &&
		    (d2 & 0xffff0000U) == 0 &&
		    (d2 & 0x0000ff00U) != 0 &&
		    (d2 & 0x0000ff00U) == (d1 & 0x0000ff00U)) {
			DWORD packedLba = d2 & 0x000000ffU;
			if (partBaseLba != 0) {
				packedLba += partBaseLba;
			}
			if (packedLba != lba) {
				snprintf(logLine, sizeof(logLine),
				         "SCSI_XFER_LBA_PACKED cmd=$%02X d1=%08X d2=%08X lba=%u->%u base=%u",
				         (unsigned int)cmd,
				         (unsigned int)d1,
				         (unsigned int)d2,
				         (unsigned int)lba,
				         (unsigned int)packedLba,
				         (unsigned int)partBaseLba);
				SCSI_LogText(logLine);
				lba = packedLba;
			}
			usePackedLegacy = 1;
		}
		blocks = d3;
		if (cmd == 0x22) {
			blocks &= 0xff;
			if (blocks == 0) {
				blocks = 256;
			}
			if (usePackedLegacy) {
				DWORD packedBlocks = d4 & 0x0000ffffU;
				if (packedBlocks == 0) {
					packedBlocks = 1;
				}
				snprintf(logLine, sizeof(logLine),
				         "SCSI_XFER_BLK_PACKED cmd=$%02X d4=%08X blocks=%u->%u",
				         (unsigned int)cmd,
				         (unsigned int)d4,
				         (unsigned int)blocks,
				         (unsigned int)packedBlocks);
				SCSI_LogText(logLine);
				blocks = packedBlocks;
			}
		}
		if (!SCSI_ResolveTransfer(lba, blocks, blockSize, &imageOffset, &transferBytes)) {
			result = 0xFFFFFFFF;
			break;
		}
		if (!SCSI_IsLinearRamRange(a1, transferBytes)) {
			result = 0xFFFFFFFF;
			break;
		}

		for (i = 0; i < transferBytes; i++) {
			s_disk_image_buffer[4][imageOffset + i] = Memory_ReadB(a1 + i);
		}
			snprintf(logLine, sizeof(logLine),
			         "SCSI_XFER_WRITE cmd=$%02X lba=%u blocks=%u blockSize=%u imgOff=0x%X",
			         cmd,
			         (unsigned int)lba,
			         (unsigned int)blocks,
			         (unsigned int)blockSize,
			         (unsigned int)imageOffset);
		SCSI_LogText(logLine);
		SASI_SetDirtyFlag(0);
		result = 0;
		break;
	}

	case 0x2f: {  // _S_STARTSTOP (boot-path quirk: used as data read on some images)
		DWORD rawLba = d2 & 0x00ffffffU;
		DWORD lba = rawLba;
		DWORD blocks = d4 & 0x0000ffffU;
		DWORD blockSize = SCSI_GetBlockSizeFromCode(d5);
		DWORD partBaseLba = 0;
		int lbaAdjusted = 0;
		DWORD imageOffset;
		DWORD transferBytes;

		if (blocks == 0) {
			blocks = d3 & 0x0000ffffU;
		}
		if (blocks == 0) {
			blocks = 1;
		}
		if (blockSize != 0 && s_scsi_partition_byte_offset != 0) {
			partBaseLba = (DWORD)((unsigned long long)s_scsi_partition_byte_offset /
			                     (unsigned long long)blockSize);
			// Some boot paths pass partition-relative LBA for cmd=$2F.
			// If so, lift it to absolute disk LBA before resolving.
			if (partBaseLba > 0 && lba < partBaseLba) {
				lba += partBaseLba;
				lbaAdjusted = 1;
			}
		}
		if (!SCSI_ResolveTransfer(lba, blocks, blockSize, &imageOffset, &transferBytes)) {
			result = 0xFFFFFFFF;
			break;
		}
		if (!SCSI_IsLinearRamRange(a1, transferBytes)) {
			result = 0xFFFFFFFF;
			break;
		}
		for (i = 0; i < transferBytes; i++) {
			Memory_WriteB(a1 + i, s_disk_image_buffer[4][imageOffset + i]);
		}
		snprintf(logLine, sizeof(logLine),
		         "SCSI_XFER_READ cmd=$%02X lba=%u raw=%u base=%u adj=%u blocks=%u blockSize=%u imgOff=0x%X a1=%08X d3=%08X d4=%08X m1121c=%02X%02X%02X%02X m123d2=%02X%02X%02X%02X",
		         (unsigned int)cmd,
		         (unsigned int)lba,
		         (unsigned int)rawLba,
		         (unsigned int)partBaseLba,
		         (unsigned int)lbaAdjusted,
		         (unsigned int)blocks,
		         (unsigned int)blockSize,
		         (unsigned int)imageOffset,
		         (unsigned int)a1,
		         (unsigned int)d3,
		         (unsigned int)d4,
		         (unsigned int)Memory_ReadB(0x0001121c),
		         (unsigned int)Memory_ReadB(0x0001121d),
		         (unsigned int)Memory_ReadB(0x0001121e),
		         (unsigned int)Memory_ReadB(0x0001121f),
		         (unsigned int)Memory_ReadB(0x000123d2),
		         (unsigned int)Memory_ReadB(0x000123d3),
		         (unsigned int)Memory_ReadB(0x000123d4),
		         (unsigned int)Memory_ReadB(0x000123d5));
		SCSI_LogText(logLine);
		result = 0;
		break;
	}

	case 0x23:  // _S_FORMAT
	case 0x2a:  // _S_MODESELECT
	case 0x2b:  // _S_REZEROUNIT
	case 0x2d:  // _S_SEEK
	case 0x30:  // _S_EJECT6MO1
	case 0x31:  // _S_REASSIGN
	case 0x32:  // _S_PAMEDIUM
	case 0x36:  // _b_dskini
	case 0x37:  // _b_format
	case 0x38:  // _b_badfmt
	case 0x39:  // _b_assign
		result = 0;
		break;

	case 0x24:  // _S_TESTUNIT
		result = (s_disk_image_buffer[4] != NULL && s_disk_image_buffer_size[4] > 0) ? 0 : 0xFFFFFFFF;
		break;

	case 0x25: {  // _S_READCAP
		DWORD blockSize = SCSI_GetImageBlockSize();
		DWORD dataOffset = SCSI_GetIocsDataOffset();
		DWORD dataSize = 0;
		DWORD totalBlocks;

		if (s_disk_image_buffer[4] == NULL || s_disk_image_buffer_size[4] <= 0) {
			result = 0xFFFFFFFF;
			break;
		}
		dataSize = (DWORD)s_disk_image_buffer_size[4];
		if (dataOffset >= dataSize) {
			result = 0xFFFFFFFF;
			break;
		}
		dataSize -= dataOffset;
		totalBlocks = dataSize / blockSize;
		if (totalBlocks == 0) {
			result = 0xFFFFFFFF;
			break;
		}
		if (!SCSI_IsLinearRamRange(a1, 8)) {
			result = 0xFFFFFFFF;
			break;
		}

		Memory_WriteB(a1 + 0, (BYTE)(((totalBlocks - 1) >> 24) & 0xff));
		Memory_WriteB(a1 + 1, (BYTE)(((totalBlocks - 1) >> 16) & 0xff));
		Memory_WriteB(a1 + 2, (BYTE)(((totalBlocks - 1) >> 8) & 0xff));
		Memory_WriteB(a1 + 3, (BYTE)((totalBlocks - 1) & 0xff));
		Memory_WriteB(a1 + 4, (BYTE)((blockSize >> 24) & 0xff));
		Memory_WriteB(a1 + 5, (BYTE)((blockSize >> 16) & 0xff));
		Memory_WriteB(a1 + 6, (BYTE)((blockSize >> 8) & 0xff));
		Memory_WriteB(a1 + 7, (BYTE)(blockSize & 0xff));
			snprintf(logLine, sizeof(logLine),
			         "SCSI_READCAP blocks=%u blockSize=%u dataOff=0x%X",
			         (unsigned int)totalBlocks,
			         (unsigned int)blockSize,
			         (unsigned int)dataOffset);
			SCSI_LogText(logLine);
			result = 0;
			break;
	}

	case 0x29: {  // _S_MODESENSE
		BYTE modeSense[12];
		DWORD len = d3 & 0xff;
		DWORD blockSize = SCSI_GetImageBlockSize();
		DWORD dataOffset = SCSI_GetIocsDataOffset();
		DWORD dataSize = 0;
		DWORD totalBlocks = 0;
		DWORD copyLen;

		memset(modeSense, 0, sizeof(modeSense));
		if (s_disk_image_buffer[4] != NULL) {
			dataSize = (DWORD)s_disk_image_buffer_size[4];
			if (dataOffset < dataSize) {
				dataSize -= dataOffset;
				totalBlocks = dataSize / blockSize;
			}
		}

		modeSense[0] = 0x0b;  // mode data length (excluding byte 0)
		modeSense[3] = 0x08;  // block descriptor length
		modeSense[5] = (BYTE)((totalBlocks >> 16) & 0xff);
		modeSense[6] = (BYTE)((totalBlocks >> 8) & 0xff);
		modeSense[7] = (BYTE)(totalBlocks & 0xff);
		modeSense[9] = (BYTE)((blockSize >> 16) & 0xff);
		modeSense[10] = (BYTE)((blockSize >> 8) & 0xff);
		modeSense[11] = (BYTE)(blockSize & 0xff);

		copyLen = (len < (DWORD)sizeof(modeSense)) ? len : (DWORD)sizeof(modeSense);
		if (!SCSI_IsLinearRamRange(a1, copyLen)) {
			result = 0xFFFFFFFF;
			break;
		}
		for (i = 0; i < copyLen; i++) {
			Memory_WriteB(a1 + i, modeSense[i]);
		}
		result = 0;
		break;
	}

	case 0x2c: {  // _S_REQUEST (REQUEST SENSE)
		DWORD len = d3 & 0xff;
		if (!SCSI_IsLinearRamRange(a1, len)) {
			result = 0xFFFFFFFF;
			break;
		}
		for (i = 0; i < len; i++) {
			Memory_WriteB(a1 + i, 0x00);
		}
		result = 0;
		break;
	}

	case 0xfc:  // observed on forced-boot path (vendor/private IOCS)
		// Treat as a successful no-op so the caller can continue probing.
		result = 0;
		break;

	case 0xae: {  // observed in forced-boot path before RAM callback jump
		// Human68k requests a continuation vector with d1=$0640.  Returning a
		// fixed address (for example $86A2) is fragile because it may land on
		// an RTE stub.  Use the current IOCS fn=$FF handler instead.
		if ((d1 & 0x0000ffffU) == 0x00000640U) {
			DWORD fnff = Memory_ReadD(0x000007FC) & 0x00ffffffU;
			if ((fnff & 1U) == 0U &&
			    fnff >= 0x00000400U &&
			    fnff < 0x00c00000U) {
				result = fnff;
			} else {
				result = 0;
			}
		} else {
			result = 0;
		}
		break;
	}

	default:
		result = 0xFFFFFFFF;
		snprintf(logLine, sizeof(logLine),
		         "SCSI_IOCS_UNHANDLED cmd=$%02X d2=%08X d3=%08X d4=%08X d5=%08X a1=%08X",
		         cmd,
		         (unsigned int)d2, (unsigned int)d3, (unsigned int)d4,
		         (unsigned int)d5, (unsigned int)a1);
		SCSI_LogText(logLine);
		break;
	}

	C68k_Set_DReg(&C68K, 0, result);
	snprintf(logLine, sizeof(logLine),
	         "SCSI_IOCS cmd=$%02X d2=%08X d3=%08X d4=%08X d5=%08X a1=%08X d0=%08X",
	         cmd, (unsigned int)d2, (unsigned int)d3, (unsigned int)d4,
	         (unsigned int)d5, (unsigned int)a1, (unsigned int)result);
	SCSI_LogText(logLine);
#endif
}


// -----------------------------------------------------------------------
//   I/O Read
//   - $EA0000-$EA1FFF: SCSI ROM (SCSIIPL) を返す
//   - $E9E000-$E9FFFF: SPC レジスタ範囲 → 安全なダミー値を返す
// -----------------------------------------------------------------------
BYTE FASTCALL SCSI_Read(DWORD adr)
{
	// SCSI ROM 読み取り:
	// - 外付けSCSI ROM ($EA0000-$EA1FFF)
	// - 内蔵SCSI IPL ウィンドウ ($FC0000-$FC1FFF)
	if ((adr >= 0x00ea0000 && adr < 0x00ea2000) ||
	    (adr >= 0x00fc0000 && adr < 0x00fc2000)) {
		return SCSIIPL[(adr ^ 1) & 0x1fff];
	}

	// SPC (MB89352) register window at $E96000.
	// Human68k's SCSI driver writes SCTL/BDID and reads back to verify the
	// chip exists.  Return the last written value for config registers so the
	// write-readback check passes.  For status registers return appropriate
	// fixed values.
	if (adr >= 0x00e96000 && adr < 0x00e98000) {
		BYTE offset = adr & 0x1f;
		BYTE result;
		switch (offset) {
		case 0x01:  // BDID - return last written value
		case 0x03:  // SCTL - return last written value
		case 0x05:  // SCMD - return last written value
			result = s_spc_regs[offset];
			break;
		case 0x09:  // INTS - interrupt sense
			result = s_spc_regs[0x09];
			break;
		case 0x0b:  // PSNS - phase sense (bus free)
			result = 0x00;
			break;
		case 0x0d:  // SSTS - SPC status (TC0 | DREG_EMPTY)
			result = 0x05;
			break;
		case 0x0f:  // SERR - no error
			result = 0x00;
			break;
		default:
			result = s_spc_regs[offset];
			break;
		}
		SCSI_LogSPCAccess("READ960", adr, result);
		return result;
	}

	// SPC レジスタ範囲 ($E9E000-$E9FFFF)
	// SPCレジスタはエミュレートしないため、安全なダミー値を返す
	// Alias: some SCSI ROM variants access SPC at $E9E000 range.
	// Use the same s_spc_regs[] state as the $E96000 window.
	if (adr >= 0x00e9e000 && adr < 0x00ea0000) {
		BYTE offset = adr & 0x1f;
		BYTE result;
		switch (offset) {
		case 0x01:  // BDID
		case 0x03:  // SCTL
		case 0x05:  // SCMD
			result = s_spc_regs[offset];
			break;
		case 0x09:  // INTS
			result = s_spc_regs[0x09];
			break;
		case 0x0b:  // PSNS
			result = 0x00;
			break;
		case 0x0d:  // SSTS
			result = 0x05;
			break;
		case 0x0f:  // SERR
			result = 0x00;
			break;
		default:
			result = s_spc_regs[offset];
			break;
		}
		SCSI_LogSPCAccess("READ", adr, result);
		return result;
	}

	return 0xff;
}


// -----------------------------------------------------------------------
//   I/O Write
//   - $E9F800: SCSI トラップアドレス (ブート/IOCS)
//   - $E9E000-$E9FFFF: SPC レジスタ範囲 → 安全に無視
// -----------------------------------------------------------------------
void FASTCALL SCSI_Write(DWORD adr, BYTE data)
{
	// SCSIトラップ:
	//  - 合成ROM経路: $E9F800
	//  - 実ROM互換経路: $E96020
	if (adr == 0x00e9f800 || adr == 0x00e96020) {
#if defined(HAVE_C68K)
		if (data != 0xff) {
			DWORD d2 = C68k_Get_DReg(&C68K, 2);
			DWORD d3 = C68k_Get_DReg(&C68K, 3);
			DWORD d4 = C68k_Get_DReg(&C68K, 4);
			DWORD d5 = C68k_Get_DReg(&C68K, 5);
			DWORD a1 = C68k_Get_AReg(&C68K, 1);
			if (s_last_iocs_sig_valid &&
			    s_last_iocs_sig_cmd == data &&
			    s_last_iocs_sig_d2 == d2 &&
			    s_last_iocs_sig_d3 == d3 &&
			    s_last_iocs_sig_d4 == d4 &&
			    s_last_iocs_sig_d5 == d5 &&
			    s_last_iocs_sig_a1 == a1 &&
			    s_last_iocs_sig_addr != adr) {
				char dupLog[128];
				snprintf(dupLog, sizeof(dupLog),
				         "SCSI_TRAP_DUP suppressed cmd=$%02X adr=$%06X prev=$%06X",
				         (unsigned int)data,
				         (unsigned int)adr,
				         (unsigned int)s_last_iocs_sig_addr);
				SCSI_LogText(dupLog);
				return;
			}
			s_last_iocs_sig_valid = 1;
			s_last_iocs_sig_addr = adr;
			s_last_iocs_sig_cmd = data;
			s_last_iocs_sig_d2 = d2;
			s_last_iocs_sig_d3 = d3;
			s_last_iocs_sig_d4 = d4;
			s_last_iocs_sig_d5 = d5;
			s_last_iocs_sig_a1 = a1;
		} else {
			s_last_iocs_sig_valid = 0;
		}
#endif
		SCSI_LogIO(adr, "TRAP", data);
		if (data == 0xff) {
			SCSI_HandleBoot();
		} else {
			SCSI_HandleIOCS(data);
		}
		return;
	}

	// デバイスドライバトラップ: $E9F802
	//  data=$02 → strategy (A5をC側で退避)
	//  data=$01 → interrupt (コマンド処理)
	//
	// Safety: ignore this trap unless the synthetic driver is actually linked
	// into the Human68k device chain. This prevents stray writes from driving
	// request-packet handling with invalid pointers.
	if (adr == 0x00e9f802) {
		if (!s_scsi_dev_linked) {
			return;
		}
#if defined(HAVE_C68K)
		if (data == 0x02) {
			s_scsi_dev_reqpkt = SCSI_Mask24(C68k_Get_AReg(&C68K, 5));
		} else if (data == 0x01) {
			SCSI_HandleDeviceCommand();
		}
#endif
		return;
	}

	// SPC register write at $E96000 page.
	// Store the value so readback checks pass, and handle commands.
	if (adr >= 0x00e96000 && adr < 0x00e98000) {
		BYTE offset = adr & 0x1f;
		SCSI_LogSPCAccess("WRITE960", adr, data);
		s_spc_regs[offset] = data;
		if (offset == 0x05) {  // SCMD - command register
			BYTE cmd = data & 0xe0;
			if (cmd == 0x20) {
				// SELECT command: pretend target responded immediately
				s_spc_regs[0x09] = 0x01;  // INTS: SEL/RESEL complete
			} else if (cmd == 0x40) {
				// RESET command: clear INTS, signal reset done
				s_spc_regs[0x09] = 0x04;  // INTS: command complete
			} else if (cmd == 0x00) {
				// BUS RELEASE
				s_spc_regs[0x09] = 0x04;  // INTS: command complete
			}
		} else if (offset == 0x09) {
			// Writing to INTS clears the bits (write-1-to-clear)
			s_spc_regs[0x09] &= ~data;
		}
		return;
	}

	// Alias: $E9E000 range SPC writes - same state as $E96000.
	if (adr >= 0x00e9e000 && adr < 0x00ea0000) {
		BYTE offset = adr & 0x1f;
		SCSI_LogSPCAccess("WRITE", adr, data);
		s_spc_regs[offset] = data;
		if (offset == 0x05) {
			BYTE cmd = data & 0xe0;
			if (cmd == 0x20) {
				s_spc_regs[0x09] = 0x01;
			} else if (cmd == 0x40) {
				s_spc_regs[0x09] = 0x04;
			} else if (cmd == 0x00) {
				s_spc_regs[0x09] = 0x04;
			}
		} else if (offset == 0x09) {
			s_spc_regs[0x09] &= ~data;
		}
		return;
	}
}


// -----------------------------------------------------------------------
//   ブートセクタ上の BPB から論理セクタサイズを推定
// -----------------------------------------------------------------------
static DWORD SCSI_DetectBootBlockSize(DWORD bootOffset)
{
	static const DWORD bpbOffsets[] = { 0x12, 0x11, 0x0E, 0x0B };
	BYTE* buf;
	DWORD size;
	DWORD i;

	if (s_disk_image_buffer[4] == NULL || s_disk_image_buffer_size[4] <= 0) {
		return 0;
	}
	buf = s_disk_image_buffer[4];
	size = (DWORD)s_disk_image_buffer_size[4];
	for (i = 0; i < (DWORD)(sizeof(bpbOffsets) / sizeof(bpbOffsets[0])); i++) {
		DWORD pos = bootOffset + bpbOffsets[i];
		WORD bytesPerSec;
		if (pos + 2 > size) {
			continue;
		}
		bytesPerSec = ((WORD)buf[pos] << 8) | (WORD)buf[pos + 1];
		if (bytesPerSec == 256 || bytesPerSec == 512 || bytesPerSec == 1024) {
			return (DWORD)bytesPerSec;
		}
	}
	return 0;
}


// -----------------------------------------------------------------------
//   パーティションブートセクタのバイトオフセットを取得
//   X68SCSI1 ヘッダ → パーティションテーブル → ブートセクタ位置
// -----------------------------------------------------------------------
static DWORD SCSI_FindPartitionBootOffset(void)
{
	BYTE* buf;
	long size;
	DWORD physSectorSize;
	DWORD partTableOffset;
	DWORD i;

	if (s_disk_image_buffer[4] == NULL || s_disk_image_buffer_size[4] < 0x1000) {
		return 0;
	}
	buf = s_disk_image_buffer[4];
	size = s_disk_image_buffer_size[4];

	// IOCS/boot pathと同じ判定を使い、raw/X68SCSI1で一貫させる
	physSectorSize = SCSI_GetImageBlockSize();
	if (physSectorSize != 256 && physSectorSize != 512 && physSectorSize != 1024) {
		physSectorSize = 512;
	}

	// パーティションテーブルは LBA 4 (physical sectors)
	partTableOffset = physSectorSize * 4;
	if (partTableOffset + 64 > (DWORD)size) {
		return 0;
	}

	// "X68K" シグネチャ確認
	if (memcmp(buf + partTableOffset, "X68K", 4) != 0) {
		return 0;
	}

	// パーティションエントリを検索 (テーブル内で "Human68k" を探す)
	for (i = partTableOffset + 4; i < partTableOffset + physSectorSize && i + 16 <= (DWORD)size; i += 2) {
		if (buf[i] == 'H' && memcmp(buf + i, "Human68k", 8) == 0) {
			// エントリ構造: type(8) + start(4,BE) + size(4,BE)
			DWORD startSec = ((DWORD)buf[i + 8] << 24) |
			                 ((DWORD)buf[i + 9] << 16) |
			                 ((DWORD)buf[i + 10] << 8) |
			                 ((DWORD)buf[i + 11]);
			// startSec は 1024バイト論理セクタ単位
			DWORD byteOffset = startSec * 1024;
			if (byteOffset < (DWORD)size) {
				char log[96];
				snprintf(log, sizeof(log),
				         "SCSI_DEV partBoot=0x%X startSec=%u",
				         (unsigned int)byteOffset, (unsigned int)startSec);
				SCSI_LogText(log);
				return byteOffset;
			}
		}
	}

	// フォールバック: LBA 64 (512バイトセクタ) = offset 0x8000
	if (0x8000 < (DWORD)size) {
		SCSI_LogText("SCSI_DEV partBoot fallback 0x8000");
		return 0x8000;
	}
	return 0;
}


// -----------------------------------------------------------------------
//   ディスクイメージからBPBを読み取る
//   outBpb: 36バイトのバッファ
//   outPartOffset: パーティション先頭のバイトオフセット
//   戻り値: 成功=1, 失敗=0
// -----------------------------------------------------------------------
static int SCSI_ReadBPBFromImage(BYTE* outBpb, DWORD* outPartOffset)
{
	BYTE* buf;
	long size;
	DWORD bootOffset;
	DWORD bpbOff = 0;
	BYTE rawBpb[36];
	WORD bytesPerSec = 0;

	if (s_disk_image_buffer[4] == NULL || s_disk_image_buffer_size[4] <= 0) {
		return 0;
	}
	buf = s_disk_image_buffer[4];
	size = s_disk_image_buffer_size[4];

	bootOffset = SCSI_FindPartitionBootOffset();
	if (bootOffset == 0 || bootOffset + 0x30 > (DWORD)size) {
		return 0;
	}

	// ブートセクタ先頭は BRA.W (0x60xx) であるべき
	if ((buf[bootOffset] & 0xF0) != 0x60) {
		SCSI_LogText("SCSI_DEV bootSec not BRA");
		return 0;
	}

	// Human68k BPB はブートセクタ内の以下のオフセットに存在しうる:
	//   $12: BRA.S (2B) + OEM 16B の場合
	//   $11: BRA.S (2B) + OEM 15B の場合
	//   $0E: BRA.W (4B) + OEM 10B の場合
	//   $0B: DOS互換 (JMP 3B + OEM 8B)
	{
		static const DWORD bpbOffsets[] = { 0x12, 0x11, 0x0E, 0x0B };
		int found = 0;
		int k;
		for (k = 0; k < 4; k++) {
			bpbOff = bootOffset + bpbOffsets[k];
			if (bpbOff + 36 > (DWORD)size) continue;
			bytesPerSec = ((WORD)buf[bpbOff] << 8) | (WORD)buf[bpbOff + 1];
			if (bytesPerSec == 256 || bytesPerSec == 512 || bytesPerSec == 1024) {
				found = 1;
				break;
			}
		}
		if (!found) {
			SCSI_LogText("SCSI_DEV BPB not found in bootSec");
			return 0;
		}
	}

	if (bpbOff + 36 > (DWORD)size) {
		return 0;
	}

	memcpy(rawBpb, buf + bpbOff, sizeof(rawBpb));
	memcpy(outBpb, rawBpb, sizeof(rawBpb));
	*outPartOffset = bootOffset;

	{
		char log[128];
		char raw[192];
		snprintf(raw, sizeof(raw),
		         "SCSI_DEV BPB raw off=0x%X: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
		         (unsigned int)bpbOff,
		         (unsigned int)rawBpb[0], (unsigned int)rawBpb[1], (unsigned int)rawBpb[2], (unsigned int)rawBpb[3],
		         (unsigned int)rawBpb[4], (unsigned int)rawBpb[5], (unsigned int)rawBpb[6], (unsigned int)rawBpb[7],
		         (unsigned int)rawBpb[8], (unsigned int)rawBpb[9], (unsigned int)rawBpb[10], (unsigned int)rawBpb[11],
		         (unsigned int)rawBpb[12], (unsigned int)rawBpb[13], (unsigned int)rawBpb[14], (unsigned int)rawBpb[15],
		         (unsigned int)rawBpb[16], (unsigned int)rawBpb[17], (unsigned int)rawBpb[18], (unsigned int)rawBpb[19]);
		SCSI_LogText(raw);
		snprintf(raw, sizeof(raw),
		         "SCSI_DEV BPB drv: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
		         (unsigned int)outBpb[0], (unsigned int)outBpb[1], (unsigned int)outBpb[2], (unsigned int)outBpb[3],
		         (unsigned int)outBpb[4], (unsigned int)outBpb[5], (unsigned int)outBpb[6], (unsigned int)outBpb[7],
		         (unsigned int)outBpb[8], (unsigned int)outBpb[9], (unsigned int)outBpb[10], (unsigned int)outBpb[11],
		         (unsigned int)outBpb[12], (unsigned int)outBpb[13], (unsigned int)outBpb[14], (unsigned int)outBpb[15],
		         (unsigned int)outBpb[16], (unsigned int)outBpb[17], (unsigned int)outBpb[18], (unsigned int)outBpb[19]);
		SCSI_LogText(raw);
		snprintf(log, sizeof(log),
		         "SCSI_DEV BPB secSize=%u secClus=%u b3=%02X b4=%02X b5=%02X rootEnt=%u media=$%02X b11=%02X",
		         (unsigned int)bytesPerSec,
		         (unsigned int)outBpb[2],
		         (unsigned int)outBpb[3],
		         (unsigned int)outBpb[4],
		         (unsigned int)outBpb[5],
		         (unsigned int)(((DWORD)outBpb[6] << 8) | outBpb[7]),
		         (unsigned int)outBpb[10],
		         (unsigned int)outBpb[11]);
		SCSI_LogText(log);
	}
	return 1;
}


// -----------------------------------------------------------------------
//   デバイスドライバコマンドハンドラ
//   s_scsi_dev_reqpkt に保存されたリクエストパケットを処理
//
//   リクエストパケット構造:
//     +$00: length (byte)
//     +$01: unit (byte)
//     +$02: command (byte)
//     +$03: status (word, BE) - ドライバが返す
//     +$0D: media / units (byte)
//     +$0E: transfer address / break address (long, BE)
//     +$12: count / BPB pointer (long, BE)
//     +$16: start sector (long, BE)
// -----------------------------------------------------------------------
static void SCSI_HandleDeviceCommand(void)
{
#if defined(HAVE_C68K)
	DWORD reqpkt = s_scsi_dev_reqpkt;
	BYTE pktLen;
	BYTE cmd;
	char logLine[192];

	reqpkt = SCSI_Mask24(reqpkt);
	if (reqpkt == 0) {
		return;
	}
	if (!SCSI_IsLinearRamRange(reqpkt, 6)) {
		SCSI_LogText("SCSI_DEV reqpkt range error");
		s_scsi_dev_reqpkt = 0;
		return;
	}
	pktLen = Memory_ReadB(reqpkt + 0);
	if (pktLen < 6) {
		pktLen = 6;
	}
	if (!SCSI_IsLinearRamRange(reqpkt, (DWORD)pktLen)) {
		SCSI_LogText("SCSI_DEV reqpkt len range error");
		s_scsi_dev_reqpkt = 0;
		return;
	}
	// A stale packet pointer can cause repeated processing if interrupt
	// is invoked without a preceding strategy call.
	s_scsi_dev_reqpkt = 0;

	cmd = Memory_ReadB(reqpkt + 2);

	snprintf(logLine, sizeof(logLine),
	         "SCSI_DEV cmd=%u reqpkt=$%08X len=%u unit=%u",
	         (unsigned int)cmd, (unsigned int)reqpkt,
	         (unsigned int)pktLen,
	         (unsigned int)Memory_ReadB(reqpkt + 1));
	SCSI_LogText(logLine);
	if (cmd == 0x57) {
		SCSI_LogDevicePacket(reqpkt, pktLen);
	}

	switch (cmd) {
	case 0: {  // INIT
		BYTE bpbData[36];
		DWORD partOffset = 0;
		DWORD breakAddrIn;
		DWORD workBase;
		DWORD breakAddrOut;
		DWORD i;
		WORD secSize;
		SCSI_LogDevicePacket(reqpkt, pktLen);

		breakAddrIn = ((DWORD)Memory_ReadB(reqpkt + 0x0E) << 24) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x0F) << 16) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x10) << 8) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x11));
		breakAddrIn = SCSI_Mask24(breakAddrIn);

		if (SCSI_ReadBPBFromImage(bpbData, &partOffset)) {
			BYTE fatCount;
			WORD reservedSec;
			WORD rootEnt;
			WORD secPerFatLE;
			DWORD rootDirBytes;
			secSize = ((WORD)bpbData[0] << 8) | (WORD)bpbData[1];
			s_scsi_partition_byte_offset = partOffset;
			s_scsi_sector_size = (secSize > 0) ? secSize : 1024;
			s_scsi_dev_absolute_sectors = -1;
			fatCount = bpbData[3];
			reservedSec = ((WORD)bpbData[4] << 8) | (WORD)bpbData[5];
			rootEnt = ((WORD)bpbData[6] << 8) | (WORD)bpbData[7];
			secPerFatLE = (WORD)bpbData[11] | ((WORD)bpbData[12] << 8);
			rootDirBytes = (DWORD)rootEnt * 32U;
			s_scsi_root_dir_start_sector =
			    (DWORD)reservedSec + ((DWORD)fatCount * (DWORD)secPerFatLE);
			s_scsi_root_dir_sector_count =
			    (s_scsi_sector_size != 0) ? ((rootDirBytes + s_scsi_sector_size - 1) / s_scsi_sector_size) : 0;

			// Use caller-provided break address as scratch/work area for BPB and
			// pointer table. Hard-coding low memory (e.g. $00000C00) corrupts
			// Human68k work areas and leads to boot-time crashes later.
			workBase = SCSI_Mask24((breakAddrIn + 0x1f) & ~0x1fU);
			breakAddrOut = workBase + 0x80;
			if (!SCSI_IsLinearRamRange(workBase, 0x80)) {
				workBase = SCSI_BPB_RAM_ADDR;
				breakAddrOut = breakAddrIn;
			}
			s_scsi_bpb_ram_addr = workBase;
			s_scsi_bpbptr_ram_addr = workBase + 0x40;

			// BPB/BPBポインタ領域を初期化（未初期化値がポインタとして
			// 解釈されると、カーネル側で異常アドレス参照が起きる）
			for (i = 0; i < 64; i++) {
				Memory_WriteB(s_scsi_bpb_ram_addr + i, 0x00);
				Memory_WriteB(s_scsi_bpbptr_ram_addr + i, 0x00);
			}

			// BPBをRAMに書き込み
			for (i = 0; i < 36; i++) {
				Memory_WriteB(s_scsi_bpb_ram_addr + i, bpbData[i]);
			}
			// hidden sectors (パーティション開始論理セクタ) をBPB+16に書込
			// ブートセクタのBPBに既に含まれているが、念のため上書き
			{
				DWORD hiddenSec = partOffset / s_scsi_sector_size;
				Memory_WriteB(s_scsi_bpb_ram_addr + 16, (BYTE)((hiddenSec >> 24) & 0xff));
				Memory_WriteB(s_scsi_bpb_ram_addr + 17, (BYTE)((hiddenSec >> 16) & 0xff));
				Memory_WriteB(s_scsi_bpb_ram_addr + 18, (BYTE)((hiddenSec >> 8) & 0xff));
				Memory_WriteB(s_scsi_bpb_ram_addr + 19, (BYTE)(hiddenSec & 0xff));
			}

			// BPB ポインタ配列
			// Human68k の一部経路で終端(-1)を早く参照している疑いがあるため、
			// [1] にも同じ BPB を複製して終端は [2] に置く。
			Memory_WriteB(s_scsi_bpbptr_ram_addr + 0, (BYTE)((s_scsi_bpb_ram_addr >> 24) & 0xff));
			Memory_WriteB(s_scsi_bpbptr_ram_addr + 1, (BYTE)((s_scsi_bpb_ram_addr >> 16) & 0xff));
			Memory_WriteB(s_scsi_bpbptr_ram_addr + 2, (BYTE)((s_scsi_bpb_ram_addr >> 8) & 0xff));
			Memory_WriteB(s_scsi_bpbptr_ram_addr + 3, (BYTE)(s_scsi_bpb_ram_addr & 0xff));
			Memory_WriteB(s_scsi_bpbptr_ram_addr + 4, (BYTE)((s_scsi_bpb_ram_addr >> 24) & 0xff));
			Memory_WriteB(s_scsi_bpbptr_ram_addr + 5, (BYTE)((s_scsi_bpb_ram_addr >> 16) & 0xff));
			Memory_WriteB(s_scsi_bpbptr_ram_addr + 6, (BYTE)((s_scsi_bpb_ram_addr >> 8) & 0xff));
			Memory_WriteB(s_scsi_bpbptr_ram_addr + 7, (BYTE)(s_scsi_bpb_ram_addr & 0xff));
			// 終端マーカー (-1)
			Memory_WriteB(s_scsi_bpbptr_ram_addr + 8, 0xff);
			Memory_WriteB(s_scsi_bpbptr_ram_addr + 9, 0xff);
			Memory_WriteB(s_scsi_bpbptr_ram_addr + 10, 0xff);
			Memory_WriteB(s_scsi_bpbptr_ram_addr + 11, 0xff);

			// Reserve the scratch area we just populated via break address.
			Memory_WriteB(reqpkt + 0x0E, (BYTE)((breakAddrOut >> 24) & 0xff));
			Memory_WriteB(reqpkt + 0x0F, (BYTE)((breakAddrOut >> 16) & 0xff));
			Memory_WriteB(reqpkt + 0x10, (BYTE)((breakAddrOut >> 8) & 0xff));
			Memory_WriteB(reqpkt + 0x11, (BYTE)(breakAddrOut & 0xff));

			// INIT +$12 は BPB ポインタ配列を返す。
			// [0] = BPB 先頭, [1] = -1(終端)
			// +$0D はユニット数。未設定だとカーネルが不定値(例: $F8)を
			// 参照して異常な初期化ループに入る。
			Memory_WriteB(reqpkt + 0x0D, 0x01);
			Memory_WriteB(reqpkt + 0x12, (BYTE)((s_scsi_bpbptr_ram_addr >> 24) & 0xff));
			Memory_WriteB(reqpkt + 0x13, (BYTE)((s_scsi_bpbptr_ram_addr >> 16) & 0xff));
			Memory_WriteB(reqpkt + 0x14, (BYTE)((s_scsi_bpbptr_ram_addr >> 8) & 0xff));
			Memory_WriteB(reqpkt + 0x15, (BYTE)(s_scsi_bpbptr_ram_addr & 0xff));
				SCSI_SetReqStatus(reqpkt, 1, 0x00);

			snprintf(logLine, sizeof(logLine),
			         "SCSI_DEV INIT ok partOff=0x%X secSize=%u hiddenSec=%u rootSec=%u rootCnt=%u brkIn=$%08X brkOut=$%08X bpb=$%08X",
			         (unsigned int)partOffset,
			         (unsigned int)s_scsi_sector_size,
			         (unsigned int)(partOffset / s_scsi_sector_size),
			         (unsigned int)s_scsi_root_dir_start_sector,
			         (unsigned int)s_scsi_root_dir_sector_count,
			         (unsigned int)breakAddrIn,
			         (unsigned int)breakAddrOut,
			         (unsigned int)s_scsi_bpb_ram_addr);
			SCSI_LogText(logLine);
			SCSI_LogDevicePacket(reqpkt, pktLen);
		} else {
			// BPB読取失敗
			Memory_WriteB(reqpkt + 0x0D, 0x00);
				SCSI_SetReqStatus(reqpkt, 0, 0x02);  // error: not ready
			SCSI_LogText("SCSI_DEV INIT failed (no BPB)");
			SCSI_LogDevicePacket(reqpkt, pktLen);
		}
		break;
	}

	case 1: {  // MEDIA CHECK
		// Human68k: +$0E >0 means changed, <0 means unchanged/unknown.
		Memory_WriteB(reqpkt + 0x0E, 0xff);  // -1 = not changed
		SCSI_SetReqStatus(reqpkt, 1, 0x00);
		break;
	}

	case 5: {  // DRIVE CONTROL/STATUS (X68000-specific block request)
		BYTE subcmd = Memory_ReadB(reqpkt + 0x0D);
		DWORD arg0 = ((DWORD)Memory_ReadB(reqpkt + 0x0E) << 24) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x0F) << 16) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x10) << 8) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x11));
		DWORD arg1 = ((DWORD)Memory_ReadB(reqpkt + 0x12) << 24) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x13) << 16) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x14) << 8) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x15));
		DWORD arg2 = ((DWORD)Memory_ReadB(reqpkt + 0x16) << 24) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x17) << 16) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x18) << 8) |
		             ((DWORD)Memory_ReadB(reqpkt + 0x19));
		DWORD arg0Mem = SCSI_Mask24(arg0);
		DWORD arg1Mem = SCSI_Mask24(arg1);
		DWORD arg2Mem = SCSI_Mask24(arg2);
		BYTE drvState = s_scsi_drvchk_state;
		char cmd5Log[128];

		// Human68k uses request command 5 on block devices as a drive
		// control/status packet. +$0D is an internal command compatible with
		// IOCS _B_DRVCHK (0,1,2,3,4,5,6,7,9), and the driver returns drive
		// state in +$0D.
		SCSI_LogDevicePacket(reqpkt, pktLen);
		snprintf(cmd5Log, sizeof(cmd5Log),
		         "SCSI_DEV cmd=5 drvchk sub=%u arg0=$%08X arg1=$%08X arg2=$%08X",
		         (unsigned int)subcmd, (unsigned int)arg0,
		         (unsigned int)arg1, (unsigned int)arg2);
		SCSI_LogText(cmd5Log);
		if (arg0 != 0 && SCSI_IsLinearRamRange(arg0Mem, 8)) {
			snprintf(cmd5Log, sizeof(cmd5Log),
			         "SCSI_DEV cmd=5 arg0[8]=%02X %02X %02X %02X %02X %02X %02X %02X",
			         (unsigned int)Memory_ReadB(arg0Mem + 0), (unsigned int)Memory_ReadB(arg0Mem + 1),
			         (unsigned int)Memory_ReadB(arg0Mem + 2), (unsigned int)Memory_ReadB(arg0Mem + 3),
			         (unsigned int)Memory_ReadB(arg0Mem + 4), (unsigned int)Memory_ReadB(arg0Mem + 5),
			         (unsigned int)Memory_ReadB(arg0Mem + 6), (unsigned int)Memory_ReadB(arg0Mem + 7));
			SCSI_LogText(cmd5Log);
		}
		if (subcmd == 9) {
			if (arg0 != 0 && SCSI_IsLinearRamRange(arg0Mem, 16)) {
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 sub=9 arg0[16]=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)Memory_ReadB(arg0Mem + 0), (unsigned int)Memory_ReadB(arg0Mem + 1),
				         (unsigned int)Memory_ReadB(arg0Mem + 2), (unsigned int)Memory_ReadB(arg0Mem + 3),
				         (unsigned int)Memory_ReadB(arg0Mem + 4), (unsigned int)Memory_ReadB(arg0Mem + 5),
				         (unsigned int)Memory_ReadB(arg0Mem + 6), (unsigned int)Memory_ReadB(arg0Mem + 7),
				         (unsigned int)Memory_ReadB(arg0Mem + 8), (unsigned int)Memory_ReadB(arg0Mem + 9),
				         (unsigned int)Memory_ReadB(arg0Mem + 10), (unsigned int)Memory_ReadB(arg0Mem + 11),
				         (unsigned int)Memory_ReadB(arg0Mem + 12), (unsigned int)Memory_ReadB(arg0Mem + 13),
				         (unsigned int)Memory_ReadB(arg0Mem + 14), (unsigned int)Memory_ReadB(arg0Mem + 15));
				SCSI_LogText(cmd5Log);
			}
			if (arg0 != 0 && SCSI_IsLinearRamRange(arg0Mem, 64)) {
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 sub=9 arg0[32..47]=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)Memory_ReadB(arg0Mem + 32), (unsigned int)Memory_ReadB(arg0Mem + 33),
				         (unsigned int)Memory_ReadB(arg0Mem + 34), (unsigned int)Memory_ReadB(arg0Mem + 35),
				         (unsigned int)Memory_ReadB(arg0Mem + 36), (unsigned int)Memory_ReadB(arg0Mem + 37),
				         (unsigned int)Memory_ReadB(arg0Mem + 38), (unsigned int)Memory_ReadB(arg0Mem + 39),
				         (unsigned int)Memory_ReadB(arg0Mem + 40), (unsigned int)Memory_ReadB(arg0Mem + 41),
				         (unsigned int)Memory_ReadB(arg0Mem + 42), (unsigned int)Memory_ReadB(arg0Mem + 43),
				         (unsigned int)Memory_ReadB(arg0Mem + 44), (unsigned int)Memory_ReadB(arg0Mem + 45),
				         (unsigned int)Memory_ReadB(arg0Mem + 46), (unsigned int)Memory_ReadB(arg0Mem + 47));
				SCSI_LogText(cmd5Log);
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 sub=9 arg0[48..63]=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)Memory_ReadB(arg0Mem + 48), (unsigned int)Memory_ReadB(arg0Mem + 49),
				         (unsigned int)Memory_ReadB(arg0Mem + 50), (unsigned int)Memory_ReadB(arg0Mem + 51),
				         (unsigned int)Memory_ReadB(arg0Mem + 52), (unsigned int)Memory_ReadB(arg0Mem + 53),
				         (unsigned int)Memory_ReadB(arg0Mem + 54), (unsigned int)Memory_ReadB(arg0Mem + 55),
				         (unsigned int)Memory_ReadB(arg0Mem + 56), (unsigned int)Memory_ReadB(arg0Mem + 57),
				         (unsigned int)Memory_ReadB(arg0Mem + 58), (unsigned int)Memory_ReadB(arg0Mem + 59),
				         (unsigned int)Memory_ReadB(arg0Mem + 60), (unsigned int)Memory_ReadB(arg0Mem + 61),
				         (unsigned int)Memory_ReadB(arg0Mem + 62), (unsigned int)Memory_ReadB(arg0Mem + 63));
				SCSI_LogText(cmd5Log);
			}
			if (SCSI_IsLinearRamRange(arg1Mem, 16)) {
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 sub=9 arg1[16] pre=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)Memory_ReadB(arg1Mem + 0), (unsigned int)Memory_ReadB(arg1Mem + 1),
				         (unsigned int)Memory_ReadB(arg1Mem + 2), (unsigned int)Memory_ReadB(arg1Mem + 3),
				         (unsigned int)Memory_ReadB(arg1Mem + 4), (unsigned int)Memory_ReadB(arg1Mem + 5),
				         (unsigned int)Memory_ReadB(arg1Mem + 6), (unsigned int)Memory_ReadB(arg1Mem + 7),
				         (unsigned int)Memory_ReadB(arg1Mem + 8), (unsigned int)Memory_ReadB(arg1Mem + 9),
				         (unsigned int)Memory_ReadB(arg1Mem + 10), (unsigned int)Memory_ReadB(arg1Mem + 11),
				         (unsigned int)Memory_ReadB(arg1Mem + 12), (unsigned int)Memory_ReadB(arg1Mem + 13),
				         (unsigned int)Memory_ReadB(arg1Mem + 14), (unsigned int)Memory_ReadB(arg1Mem + 15));
				SCSI_LogText(cmd5Log);
			}
			if (SCSI_IsLinearRamRange(arg2Mem, 16)) {
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 sub=9 arg2[16] pre=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)Memory_ReadB(arg2Mem + 0), (unsigned int)Memory_ReadB(arg2Mem + 1),
				         (unsigned int)Memory_ReadB(arg2Mem + 2), (unsigned int)Memory_ReadB(arg2Mem + 3),
				         (unsigned int)Memory_ReadB(arg2Mem + 4), (unsigned int)Memory_ReadB(arg2Mem + 5),
				         (unsigned int)Memory_ReadB(arg2Mem + 6), (unsigned int)Memory_ReadB(arg2Mem + 7),
				         (unsigned int)Memory_ReadB(arg2Mem + 8), (unsigned int)Memory_ReadB(arg2Mem + 9),
				         (unsigned int)Memory_ReadB(arg2Mem + 10), (unsigned int)Memory_ReadB(arg2Mem + 11),
				         (unsigned int)Memory_ReadB(arg2Mem + 12), (unsigned int)Memory_ReadB(arg2Mem + 13),
				         (unsigned int)Memory_ReadB(arg2Mem + 14), (unsigned int)Memory_ReadB(arg2Mem + 15));
				SCSI_LogText(cmd5Log);
			}
		}

		// Base state for a fixed SCSI HDD: media inserted, ready, writable.
		drvState |= 0x02;   // bit1: media inserted
		drvState &= (BYTE)~0x01; // bit0: misinserted
		drvState &= (BYTE)~0x04; // bit2: not-ready (clear = ready)
		drvState &= (BYTE)~0x08; // bit3: write-protect (clear = writable)
		if (s_disk_image_buffer[4] == NULL || s_disk_image_buffer_size[4] <= 0) {
			drvState &= (BYTE)~0x02;
			drvState |= 0x04;
		}

			// sub=9 is used both as a normal drive-state probe and as a
			// payload-bearing private/config call during CONFIG.SYS processing.
			// Always return drive-state in +$0D while preserving payload pointers.
			if (subcmd == 9) {
				if (drvState & 0x30) {
					drvState |= 0x40;
				} else {
					drvState &= (BYTE)~0x40;
				}
				s_scsi_drvchk_state = drvState;
				// Always return the current drive-state in +$0D for sub=9.
				// Some Human68k paths still read +$0D even when arg payloads are used.
				Memory_WriteB(reqpkt + 0x0D, drvState);
				if (arg0 == 0 && arg1 == 0 && arg2 == 0) {
					snprintf(cmd5Log, sizeof(cmd5Log),
					         "SCSI_DEV cmd=5 sub=9 probe -> state=$%02X",
					         (unsigned int)drvState);
					SCSI_LogText(cmd5Log);
				} else {
					snprintf(cmd5Log, sizeof(cmd5Log),
					         "SCSI_DEV cmd=5 sub=9 private/config -> OK(state=$%02X)",
					         (unsigned int)drvState);
					SCSI_LogText(cmd5Log);
				}
				SCSI_SetReqStatus(reqpkt, 1, 0x00);
			SCSI_LogDevicePacket(reqpkt, pktLen);
			break;
		}

		switch (subcmd) {
		case 0: // status check 1
		case 1: // eject
		case 9: // status check 2
			// Fixed media: report state, ignore eject.
			break;
		case 2: // set eject inhibit 1
			drvState |= 0x10;
			break;
		case 3: // clear eject inhibit 1
			drvState &= (BYTE)~0x10;
			break;
		case 4: // LED on
			drvState |= 0x80;
			break;
		case 5: // LED off
			drvState &= (BYTE)~0x80;
			break;
		case 6: // set eject inhibit 2
			drvState |= 0x20;
			break;
		case 7: // clear eject inhibit 2
			drvState &= (BYTE)~0x20;
			break;
			default:
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 drvchk sub=%u unsupported -> NOP success",
				         (unsigned int)subcmd);
				SCSI_LogText(cmd5Log);
				// Keep boot resilient: unknown subcommands are treated as
				// no-op success with the current drive-state result.
				break;
			}
			// bit6 reflects combined eject-inhibit state.
			if (drvState & 0x30) {
				drvState |= 0x40;
		} else {
			drvState &= (BYTE)~0x40;
		}
		s_scsi_drvchk_state = drvState;
		Memory_WriteB(reqpkt + 0x0D, drvState); // return drive state
		SCSI_SetReqStatus(reqpkt, 1, 0x00);
		if (subcmd == 9) {
			if (SCSI_IsLinearRamRange(arg1Mem, 16)) {
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 sub=9 arg1[16] post=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)Memory_ReadB(arg1Mem + 0), (unsigned int)Memory_ReadB(arg1Mem + 1),
				         (unsigned int)Memory_ReadB(arg1Mem + 2), (unsigned int)Memory_ReadB(arg1Mem + 3),
				         (unsigned int)Memory_ReadB(arg1Mem + 4), (unsigned int)Memory_ReadB(arg1Mem + 5),
				         (unsigned int)Memory_ReadB(arg1Mem + 6), (unsigned int)Memory_ReadB(arg1Mem + 7),
				         (unsigned int)Memory_ReadB(arg1Mem + 8), (unsigned int)Memory_ReadB(arg1Mem + 9),
				         (unsigned int)Memory_ReadB(arg1Mem + 10), (unsigned int)Memory_ReadB(arg1Mem + 11),
				         (unsigned int)Memory_ReadB(arg1Mem + 12), (unsigned int)Memory_ReadB(arg1Mem + 13),
				         (unsigned int)Memory_ReadB(arg1Mem + 14), (unsigned int)Memory_ReadB(arg1Mem + 15));
				SCSI_LogText(cmd5Log);
			}
			if (SCSI_IsLinearRamRange(arg2Mem, 16)) {
				snprintf(cmd5Log, sizeof(cmd5Log),
				         "SCSI_DEV cmd=5 sub=9 arg2[16] post=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)Memory_ReadB(arg2Mem + 0), (unsigned int)Memory_ReadB(arg2Mem + 1),
				         (unsigned int)Memory_ReadB(arg2Mem + 2), (unsigned int)Memory_ReadB(arg2Mem + 3),
				         (unsigned int)Memory_ReadB(arg2Mem + 4), (unsigned int)Memory_ReadB(arg2Mem + 5),
				         (unsigned int)Memory_ReadB(arg2Mem + 6), (unsigned int)Memory_ReadB(arg2Mem + 7),
				         (unsigned int)Memory_ReadB(arg2Mem + 8), (unsigned int)Memory_ReadB(arg2Mem + 9),
				         (unsigned int)Memory_ReadB(arg2Mem + 10), (unsigned int)Memory_ReadB(arg2Mem + 11),
				         (unsigned int)Memory_ReadB(arg2Mem + 12), (unsigned int)Memory_ReadB(arg2Mem + 13),
				         (unsigned int)Memory_ReadB(arg2Mem + 14), (unsigned int)Memory_ReadB(arg2Mem + 15));
				SCSI_LogText(cmd5Log);
			}
		}
		snprintf(cmd5Log, sizeof(cmd5Log),
		         "SCSI_DEV cmd=5 drvchk sub=%u -> state=$%02X",
		         (unsigned int)subcmd, (unsigned int)drvState);
		SCSI_LogText(cmd5Log);
		SCSI_LogDevicePacket(reqpkt, pktLen);
		break;
	}

	case 2: {  // BUILD BPB
		// BPBポインタを返す (INITで設定済)
		DWORD bpbAddr = s_scsi_bpb_ram_addr ? s_scsi_bpb_ram_addr : (DWORD)SCSI_BPB_RAM_ADDR;
		// Human68k command 2 returns BPB pointer in +$12.
		Memory_WriteB(reqpkt + 0x12, (BYTE)((bpbAddr >> 24) & 0xff));
		Memory_WriteB(reqpkt + 0x13, (BYTE)((bpbAddr >> 16) & 0xff));
		Memory_WriteB(reqpkt + 0x14, (BYTE)((bpbAddr >> 8) & 0xff));
		Memory_WriteB(reqpkt + 0x15, (BYTE)(bpbAddr & 0xff));
		SCSI_SetReqStatus(reqpkt, 1, 0x00);
		break;
	}

	case 4: {  // READ
		SCSI_LogDevicePacket(reqpkt, pktLen);
		SCSI_LogKernelQueueState("SCSI_DEV READ pre");
		DWORD bufAddr = ((DWORD)Memory_ReadB(reqpkt + 0x0E) << 24) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x0F) << 16) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x10) << 8) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x11));
		bufAddr = SCSI_Mask24(bufAddr);
		DWORD count   = ((DWORD)Memory_ReadB(reqpkt + 0x12) << 24) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x13) << 16) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x14) << 8) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x15));
		DWORD startSec = ((DWORD)Memory_ReadB(reqpkt + 0x16) << 24) |
		                 ((DWORD)Memory_ReadB(reqpkt + 0x17) << 16) |
		                 ((DWORD)Memory_ReadB(reqpkt + 0x18) << 8) |
		                 ((DWORD)Memory_ReadB(reqpkt + 0x19));
		DWORD secSize = s_scsi_sector_size;
		unsigned long long byteOffset;
		unsigned long long absByteOffset;
		unsigned long long relByteOffset = 0;
		unsigned long long byteCount;
		unsigned long long imgSize = 0;
		DWORD hiddenSec = 0;
		int useRelative = 0;
		DWORD i;

		absByteOffset = (unsigned long long)startSec * (unsigned long long)secSize;
		byteOffset = absByteOffset;
		if (secSize != 0 && s_scsi_partition_byte_offset != 0) {
			hiddenSec = s_scsi_partition_byte_offset / secSize;
			relByteOffset = (unsigned long long)s_scsi_partition_byte_offset + absByteOffset;
			if (s_scsi_dev_absolute_sectors == 1) {
				useRelative = 0;
			} else if (s_scsi_dev_absolute_sectors == 0) {
				useRelative = 1;
			} else {
				// Infer once from the first root-directory style access.
				// rootSec matches BPB-relative layout, rootSec+hidden matches
				// absolute layout.
				if (hiddenSec != 0 &&
				    startSec == s_scsi_root_dir_start_sector) {
					useRelative = 1;
					s_scsi_dev_absolute_sectors = 0;
					snprintf(logLine, sizeof(logLine),
					         "SCSI_DEV MODE infer=relative sec=%u root=%u hidden=%u",
					         (unsigned int)startSec,
					         (unsigned int)s_scsi_root_dir_start_sector,
					         (unsigned int)hiddenSec);
					SCSI_LogText(logLine);
				} else if (hiddenSec != 0 &&
				           startSec == (s_scsi_root_dir_start_sector + hiddenSec)) {
					useRelative = 0;
					s_scsi_dev_absolute_sectors = 1;
					snprintf(logLine, sizeof(logLine),
					         "SCSI_DEV MODE infer=absolute sec=%u root=%u hidden=%u",
					         (unsigned int)startSec,
					         (unsigned int)s_scsi_root_dir_start_sector,
					         (unsigned int)hiddenSec);
					SCSI_LogText(logLine);
				} else {
					// Conservative default for this driver path.
					useRelative = 1;
				}
			}
			byteOffset = useRelative ? relByteOffset : absByteOffset;
		}
		byteCount = (unsigned long long)count * (unsigned long long)secSize;
		if (s_disk_image_buffer[4] != NULL && s_disk_image_buffer_size[4] > 0) {
			imgSize = (unsigned long long)s_disk_image_buffer_size[4];
		}
		if (s_disk_image_buffer[4] != NULL &&
		    s_disk_image_buffer_size[4] > 0 &&
		    (byteOffset + byteCount > imgSize)) {
			if (useRelative &&
			    (absByteOffset + byteCount <= imgSize)) {
				byteOffset = absByteOffset;
				useRelative = 0;
				s_scsi_dev_absolute_sectors = 1;
				snprintf(logLine, sizeof(logLine),
				         "SCSI_DEV READ fallback absolute sec=%u off=0x%llX part=0x%X",
				         (unsigned int)startSec,
				         absByteOffset,
				         (unsigned int)s_scsi_partition_byte_offset);
				SCSI_LogText(logLine);
			} else if (!useRelative &&
			           relByteOffset != 0 &&
			           (relByteOffset + byteCount <= imgSize)) {
				byteOffset = relByteOffset;
				useRelative = 1;
				s_scsi_dev_absolute_sectors = 0;
				snprintf(logLine, sizeof(logLine),
				         "SCSI_DEV READ fallback relative sec=%u off=0x%llX hidden=%u part=0x%X",
				         (unsigned int)startSec,
				         relByteOffset,
				         (unsigned int)hiddenSec,
				         (unsigned int)s_scsi_partition_byte_offset);
				SCSI_LogText(logLine);
			} else if (absByteOffset + byteCount <= imgSize) {
				byteOffset = absByteOffset;
				useRelative = 0;
			}
		}

		snprintf(logLine, sizeof(logLine),
		         "SCSI_DEV READ sec=%u cnt=%u buf=$%08X off=0x%llX abs=0x%llX rel=0x%llX mode=%s hidden=%u part=0x%X bytes=%llu",
		         (unsigned int)startSec, (unsigned int)count,
		         (unsigned int)bufAddr,
		         byteOffset, absByteOffset, relByteOffset,
		         useRelative ? "rel" : "abs",
		         (unsigned int)hiddenSec,
		         (unsigned int)s_scsi_partition_byte_offset,
		         byteCount);
		SCSI_LogText(logLine);
		if (byteCount >= 16) {
			BYTE* src = &s_disk_image_buffer[4][(DWORD)byteOffset];
			unsigned long long alt512Offset =
			    (unsigned long long)startSec * 512ULL;
			snprintf(logLine, sizeof(logLine),
			         "SCSI_DEV READ src16=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			         (unsigned int)src[0], (unsigned int)src[1], (unsigned int)src[2], (unsigned int)src[3],
			         (unsigned int)src[4], (unsigned int)src[5], (unsigned int)src[6], (unsigned int)src[7],
			         (unsigned int)src[8], (unsigned int)src[9], (unsigned int)src[10], (unsigned int)src[11],
			         (unsigned int)src[12], (unsigned int)src[13], (unsigned int)src[14], (unsigned int)src[15]);
			SCSI_LogText(logLine);
			if (byteCount >= 64) {
				snprintf(logLine, sizeof(logLine),
				         "SCSI_DEV READ src32=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)src[0x20], (unsigned int)src[0x21], (unsigned int)src[0x22], (unsigned int)src[0x23],
				         (unsigned int)src[0x24], (unsigned int)src[0x25], (unsigned int)src[0x26], (unsigned int)src[0x27],
				         (unsigned int)src[0x28], (unsigned int)src[0x29], (unsigned int)src[0x2A], (unsigned int)src[0x2B],
				         (unsigned int)src[0x2C], (unsigned int)src[0x2D], (unsigned int)src[0x2E], (unsigned int)src[0x2F],
				         (unsigned int)src[0x30], (unsigned int)src[0x31], (unsigned int)src[0x32], (unsigned int)src[0x33],
				         (unsigned int)src[0x34], (unsigned int)src[0x35], (unsigned int)src[0x36], (unsigned int)src[0x37],
				         (unsigned int)src[0x38], (unsigned int)src[0x39], (unsigned int)src[0x3A], (unsigned int)src[0x3B],
				         (unsigned int)src[0x3C], (unsigned int)src[0x3D], (unsigned int)src[0x3E], (unsigned int)src[0x3F]);
				SCSI_LogText(logLine);
			}
			if (byteCount >= 0x20) {
				DWORD meta0be = ((DWORD)src[0x10] << 24) | ((DWORD)src[0x11] << 16) |
				               ((DWORD)src[0x12] << 8) | (DWORD)src[0x13];
				DWORD meta1be = ((DWORD)src[0x14] << 24) | ((DWORD)src[0x15] << 16) |
				               ((DWORD)src[0x16] << 8) | (DWORD)src[0x17];
				snprintf(logLine, sizeof(logLine),
				         "SCSI_DEV READ srcMeta=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X be0=%08X be1=%08X",
				         (unsigned int)src[0x10], (unsigned int)src[0x11], (unsigned int)src[0x12], (unsigned int)src[0x13],
				         (unsigned int)src[0x14], (unsigned int)src[0x15], (unsigned int)src[0x16], (unsigned int)src[0x17],
				         (unsigned int)src[0x18], (unsigned int)src[0x19], (unsigned int)src[0x1A], (unsigned int)src[0x1B],
				         (unsigned int)src[0x1C], (unsigned int)src[0x1D], (unsigned int)src[0x1E], (unsigned int)src[0x1F],
				         (unsigned int)meta0be, (unsigned int)meta1be);
				SCSI_LogText(logLine);
			}
			if (byteCount >= 0x210) {
				snprintf(logLine, sizeof(logLine),
				         "SCSI_DEV READ src+512=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)src[0x200], (unsigned int)src[0x201], (unsigned int)src[0x202], (unsigned int)src[0x203],
				         (unsigned int)src[0x204], (unsigned int)src[0x205], (unsigned int)src[0x206], (unsigned int)src[0x207],
				         (unsigned int)src[0x208], (unsigned int)src[0x209], (unsigned int)src[0x20A], (unsigned int)src[0x20B],
				         (unsigned int)src[0x20C], (unsigned int)src[0x20D], (unsigned int)src[0x20E], (unsigned int)src[0x20F]);
				SCSI_LogText(logLine);
			}
			if (byteCount >= 0x110) {
				snprintf(logLine, sizeof(logLine),
				         "SCSI_DEV READ src+256=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)src[0x100], (unsigned int)src[0x101], (unsigned int)src[0x102], (unsigned int)src[0x103],
				         (unsigned int)src[0x104], (unsigned int)src[0x105], (unsigned int)src[0x106], (unsigned int)src[0x107],
				         (unsigned int)src[0x108], (unsigned int)src[0x109], (unsigned int)src[0x10A], (unsigned int)src[0x10B],
				         (unsigned int)src[0x10C], (unsigned int)src[0x10D], (unsigned int)src[0x10E], (unsigned int)src[0x10F]);
				SCSI_LogText(logLine);
			}
			if (s_disk_image_buffer[4] != NULL &&
			    alt512Offset + 16ULL <= (unsigned long long)s_disk_image_buffer_size[4]) {
				BYTE* alt = &s_disk_image_buffer[4][(DWORD)alt512Offset];
				snprintf(logLine, sizeof(logLine),
				         "SCSI_DEV READ alt512=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				         (unsigned int)alt[0], (unsigned int)alt[1], (unsigned int)alt[2], (unsigned int)alt[3],
				         (unsigned int)alt[4], (unsigned int)alt[5], (unsigned int)alt[6], (unsigned int)alt[7],
				         (unsigned int)alt[8], (unsigned int)alt[9], (unsigned int)alt[10], (unsigned int)alt[11],
				         (unsigned int)alt[12], (unsigned int)alt[13], (unsigned int)alt[14], (unsigned int)alt[15]);
				SCSI_LogText(logLine);
			}
		}

		if (s_disk_image_buffer[4] == NULL ||
		    byteOffset + byteCount > (unsigned long long)s_disk_image_buffer_size[4] ||
		    byteCount == 0) {
			SCSI_SetReqStatus(reqpkt, 0, 0x02);  // error
			SCSI_LogText("SCSI_DEV READ error (range)");
			break;
		}

		if (!SCSI_IsLinearRamRange(bufAddr, (DWORD)byteCount)) {
			SCSI_SetReqStatus(reqpkt, 0, 0x02);
			SCSI_LogText("SCSI_DEV READ error (buf range)");
			break;
		}

		for (i = 0; i < (DWORD)byteCount; i++) {
			Memory_WriteB(bufAddr + i, s_disk_image_buffer[4][(DWORD)byteOffset + i]);
		}
		SCSI_NormalizeRootShortNames(bufAddr, startSec, count, secSize);
		SCSI_LogRootConfigEntry(bufAddr, startSec, count, secSize);

		SCSI_SetReqStatus(reqpkt, 1, 0x00);
		SCSI_LogKernelQueueState("SCSI_DEV READ post");
		SCSI_LogDevicePacket(reqpkt, pktLen);
		if (byteCount >= 16) {
			snprintf(logLine, sizeof(logLine),
			         "SCSI_DEV READ dst16=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			         (unsigned int)Memory_ReadB(bufAddr + 0),  (unsigned int)Memory_ReadB(bufAddr + 1),
			         (unsigned int)Memory_ReadB(bufAddr + 2),  (unsigned int)Memory_ReadB(bufAddr + 3),
			         (unsigned int)Memory_ReadB(bufAddr + 4),  (unsigned int)Memory_ReadB(bufAddr + 5),
			         (unsigned int)Memory_ReadB(bufAddr + 6),  (unsigned int)Memory_ReadB(bufAddr + 7),
			         (unsigned int)Memory_ReadB(bufAddr + 8),  (unsigned int)Memory_ReadB(bufAddr + 9),
			         (unsigned int)Memory_ReadB(bufAddr + 10), (unsigned int)Memory_ReadB(bufAddr + 11),
			         (unsigned int)Memory_ReadB(bufAddr + 12), (unsigned int)Memory_ReadB(bufAddr + 13),
			         (unsigned int)Memory_ReadB(bufAddr + 14), (unsigned int)Memory_ReadB(bufAddr + 15));
			SCSI_LogText(logLine);
		}
		if (byteCount >= 0x110) {
			snprintf(logLine, sizeof(logLine),
			         "SCSI_DEV READ dst+256=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			         (unsigned int)Memory_ReadB(bufAddr + 0x100), (unsigned int)Memory_ReadB(bufAddr + 0x101),
			         (unsigned int)Memory_ReadB(bufAddr + 0x102), (unsigned int)Memory_ReadB(bufAddr + 0x103),
			         (unsigned int)Memory_ReadB(bufAddr + 0x104), (unsigned int)Memory_ReadB(bufAddr + 0x105),
			         (unsigned int)Memory_ReadB(bufAddr + 0x106), (unsigned int)Memory_ReadB(bufAddr + 0x107),
			         (unsigned int)Memory_ReadB(bufAddr + 0x108), (unsigned int)Memory_ReadB(bufAddr + 0x109),
			         (unsigned int)Memory_ReadB(bufAddr + 0x10A), (unsigned int)Memory_ReadB(bufAddr + 0x10B),
			         (unsigned int)Memory_ReadB(bufAddr + 0x10C), (unsigned int)Memory_ReadB(bufAddr + 0x10D),
			         (unsigned int)Memory_ReadB(bufAddr + 0x10E), (unsigned int)Memory_ReadB(bufAddr + 0x10F));
			SCSI_LogText(logLine);
		}
		break;
	}

	case 8:   // WRITE
	case 9: { // WRITE WITH VERIFY
		DWORD bufAddr = ((DWORD)Memory_ReadB(reqpkt + 0x0E) << 24) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x0F) << 16) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x10) << 8) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x11));
		bufAddr = SCSI_Mask24(bufAddr);
		DWORD count   = ((DWORD)Memory_ReadB(reqpkt + 0x12) << 24) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x13) << 16) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x14) << 8) |
		                ((DWORD)Memory_ReadB(reqpkt + 0x15));
		DWORD startSec = ((DWORD)Memory_ReadB(reqpkt + 0x16) << 24) |
		                 ((DWORD)Memory_ReadB(reqpkt + 0x17) << 16) |
		                 ((DWORD)Memory_ReadB(reqpkt + 0x18) << 8) |
		                 ((DWORD)Memory_ReadB(reqpkt + 0x19));
		DWORD secSize = s_scsi_sector_size;
		unsigned long long byteOffset;
		unsigned long long absByteOffset;
		unsigned long long relByteOffset = 0;
		unsigned long long byteCount;
		unsigned long long imgSize = 0;
		DWORD hiddenSec = 0;
		int useRelative = 0;
		DWORD i;

		absByteOffset = (unsigned long long)startSec * (unsigned long long)secSize;
		byteOffset = absByteOffset;
		if (secSize != 0 && s_scsi_partition_byte_offset != 0) {
			hiddenSec = s_scsi_partition_byte_offset / secSize;
			relByteOffset = (unsigned long long)s_scsi_partition_byte_offset + absByteOffset;
			if (s_scsi_dev_absolute_sectors == 1) {
				useRelative = 0;
			} else if (s_scsi_dev_absolute_sectors == 0) {
				useRelative = 1;
			} else if (hiddenSec != 0 &&
			           startSec == s_scsi_root_dir_start_sector) {
				useRelative = 1;
			} else if (hiddenSec != 0 &&
			           startSec == (s_scsi_root_dir_start_sector + hiddenSec)) {
				useRelative = 0;
			} else {
				useRelative = 1;
			}
			byteOffset = useRelative ? relByteOffset : absByteOffset;
		}
		byteCount = (unsigned long long)count * (unsigned long long)secSize;
		if (s_disk_image_buffer[4] != NULL && s_disk_image_buffer_size[4] > 0) {
			imgSize = (unsigned long long)s_disk_image_buffer_size[4];
		}
		if (s_disk_image_buffer[4] != NULL &&
		    s_disk_image_buffer_size[4] > 0 &&
		    (byteOffset + byteCount > imgSize)) {
			if (useRelative &&
			    (absByteOffset + byteCount <= imgSize)) {
				byteOffset = absByteOffset;
				useRelative = 0;
				s_scsi_dev_absolute_sectors = 1;
				snprintf(logLine, sizeof(logLine),
				         "SCSI_DEV WRITE fallback absolute sec=%u off=0x%llX part=0x%X",
				         (unsigned int)startSec,
				         absByteOffset,
				         (unsigned int)s_scsi_partition_byte_offset);
				SCSI_LogText(logLine);
			} else if (!useRelative &&
			           relByteOffset != 0 &&
			           (relByteOffset + byteCount <= imgSize)) {
				byteOffset = relByteOffset;
				useRelative = 1;
				s_scsi_dev_absolute_sectors = 0;
				snprintf(logLine, sizeof(logLine),
				         "SCSI_DEV WRITE fallback relative sec=%u off=0x%llX hidden=%u part=0x%X",
				         (unsigned int)startSec,
				         relByteOffset,
				         (unsigned int)hiddenSec,
				         (unsigned int)s_scsi_partition_byte_offset);
				SCSI_LogText(logLine);
			} else if (absByteOffset + byteCount <= imgSize) {
				byteOffset = absByteOffset;
				useRelative = 0;
			}
		}
		snprintf(logLine, sizeof(logLine),
		         "SCSI_DEV WRITE sec=%u cnt=%u buf=$%08X off=0x%llX abs=0x%llX rel=0x%llX mode=%s hidden=%u part=0x%X bytes=%llu",
		         (unsigned int)startSec, (unsigned int)count,
		         (unsigned int)bufAddr,
		         byteOffset, absByteOffset, relByteOffset,
		         useRelative ? "rel" : "abs",
		         (unsigned int)hiddenSec,
		         (unsigned int)s_scsi_partition_byte_offset,
		         byteCount);
		SCSI_LogText(logLine);
		if (s_disk_image_buffer[4] != NULL &&
		    s_disk_image_buffer_size[4] > 0 &&
		    byteCount >= 16 &&
		    byteOffset + 16 <= (unsigned long long)s_disk_image_buffer_size[4]) {
			snprintf(logLine, sizeof(logLine),
			         "SCSI_DEV WRITE pre16=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			         (unsigned int)s_disk_image_buffer[4][(DWORD)byteOffset + 0],
			         (unsigned int)s_disk_image_buffer[4][(DWORD)byteOffset + 1],
			         (unsigned int)s_disk_image_buffer[4][(DWORD)byteOffset + 2],
			         (unsigned int)s_disk_image_buffer[4][(DWORD)byteOffset + 3],
			         (unsigned int)s_disk_image_buffer[4][(DWORD)byteOffset + 4],
			         (unsigned int)s_disk_image_buffer[4][(DWORD)byteOffset + 5],
			         (unsigned int)s_disk_image_buffer[4][(DWORD)byteOffset + 6],
			         (unsigned int)s_disk_image_buffer[4][(DWORD)byteOffset + 7],
			         (unsigned int)s_disk_image_buffer[4][(DWORD)byteOffset + 8],
			         (unsigned int)s_disk_image_buffer[4][(DWORD)byteOffset + 9],
			         (unsigned int)s_disk_image_buffer[4][(DWORD)byteOffset + 10],
			         (unsigned int)s_disk_image_buffer[4][(DWORD)byteOffset + 11],
			         (unsigned int)s_disk_image_buffer[4][(DWORD)byteOffset + 12],
			         (unsigned int)s_disk_image_buffer[4][(DWORD)byteOffset + 13],
			         (unsigned int)s_disk_image_buffer[4][(DWORD)byteOffset + 14],
			         (unsigned int)s_disk_image_buffer[4][(DWORD)byteOffset + 15]);
			SCSI_LogText(logLine);
		}

		if (s_disk_image_buffer[4] == NULL ||
		    byteOffset + byteCount > (unsigned long long)s_disk_image_buffer_size[4] ||
		    byteCount == 0 ||
		    !SCSI_IsLinearRamRange(bufAddr, (DWORD)byteCount)) {
			SCSI_SetReqStatus(reqpkt, 0, 0x02);
			break;
		}

		for (i = 0; i < (DWORD)byteCount; i++) {
			s_disk_image_buffer[4][(DWORD)byteOffset + i] = Memory_ReadB(bufAddr + i);
		}
		SASI_SetDirtyFlag(0);

		SCSI_SetReqStatus(reqpkt, 1, 0x00);
		break;
	}

	case 0x57: {  // Human68k extended parameter query (observed)
		SCSI_LogDevicePacket(reqpkt, Memory_ReadB(reqpkt + 0));

		// Unknown extension command.
		// Return success to keep Human68k probe paths progressing.
		SCSI_SetReqStatus(reqpkt, 1, 0x00);
		SCSI_LogText("SCSI_DEV cmd=0x57 unsupported (NOP success)");
		break;
	}

	default:
		// 未対応コマンドでも起動継続を優先し、成功扱いで返す。
		// 一部のHuman68k系ドライバは拡張コマンドを投げるため、
		// ここでエラーを返すと起動シーケンスが崩れる。
		SCSI_SetReqStatus(reqpkt, 1, 0x00);
		snprintf(logLine, sizeof(logLine),
		         "SCSI_DEV unsupported cmd=%u (NOP success)",
		         (unsigned int)cmd);
		SCSI_LogText(logLine);
		{
			DWORD dbgBuf = ((DWORD)Memory_ReadB(reqpkt + 0x0E) << 24) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x0F) << 16) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x10) << 8) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x11));
			DWORD dbgCnt = ((DWORD)Memory_ReadB(reqpkt + 0x12) << 24) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x13) << 16) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x14) << 8) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x15));
			DWORD dbgSec = ((DWORD)Memory_ReadB(reqpkt + 0x16) << 24) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x17) << 16) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x18) << 8) |
			               ((DWORD)Memory_ReadB(reqpkt + 0x19));
			snprintf(logLine, sizeof(logLine),
			         "SCSI_DEV unsupported detail buf=$%08X cnt=%u sec=%u",
			         (unsigned int)dbgBuf,
			         (unsigned int)dbgCnt,
			         (unsigned int)dbgSec);
			SCSI_LogText(logLine);
		}
		break;
	}
#endif
}

static void SCSI_LogDevicePacket(DWORD reqpkt, BYTE len)
{
	char line[256];
	size_t pos = 0;
	int i;
	int dumpLen = (len > 32) ? 32 : (int)len;
	reqpkt = SCSI_Mask24(reqpkt);

	if (!SCSI_IsLinearRamRange(reqpkt, (DWORD)dumpLen)) {
		SCSI_LogText("SCSI_DEV pkt dump skipped (range)");
		return;
	}

	pos += (size_t)snprintf(line + pos, sizeof(line) - pos,
	                        "SCSI_DEV pkt[%u]:",
	                        (unsigned int)dumpLen);
	for (i = 0; i < dumpLen && pos + 4 < sizeof(line); i++) {
		pos += (size_t)snprintf(line + pos, sizeof(line) - pos,
		                        " %02X", (unsigned int)Memory_ReadB(reqpkt + (DWORD)i));
	}
	SCSI_LogText(line);
}


// -----------------------------------------------------------------------
//   デバイスドライバチェインリンク
//   RAMにあるNULデバイスを探し、チェイン末尾に合成デバイスをリンク
// -----------------------------------------------------------------------
int SCSI_IsDeviceLinked(void)
{
	return s_scsi_dev_linked;
}

void SCSI_LinkDeviceDriver(void)
{
#if defined(HAVE_C68K)
	DWORD addr;
	DWORD devAddr = 0;
	DWORD nextPtr;
	DWORD chainLen = 0;
	char logLine[128];

	if (s_scsi_dev_linked) {
		return;
	}
	if (s_disk_image_buffer[4] == NULL || s_disk_image_buffer_size[4] <= 0) {
		return;
	}

	// NULデバイスヘッダを探す ("NUL     " at +$0E)
	// カーネルは $6800 付近にロードされる
	for (addr = 0x6800; addr < 0x20000; addr += 2) {
		if (Memory_ReadB(addr + 0x0E) == 'N' &&
		    Memory_ReadB(addr + 0x0F) == 'U' &&
		    Memory_ReadB(addr + 0x10) == 'L' &&
		    Memory_ReadB(addr + 0x11) == ' ' &&
		    Memory_ReadB(addr + 0x12) == ' ') {
			// 属性チェック: bit 15 set (character device)
			WORD attr = ((WORD)Memory_ReadB(addr + 4) << 8) |
			            (WORD)Memory_ReadB(addr + 5);
			if (attr & 0x8000) {
				devAddr = addr;
				break;
			}
		}
	}

	if (devAddr == 0) {
		return;  // NULデバイス未発見 - カーネル未初期化
	}

	snprintf(logLine, sizeof(logLine),
	         "SCSI_DEV NUL found at $%08X", (unsigned int)devAddr);
	SCSI_LogText(logLine);

	// チェインを辿って末尾を見つける
	addr = devAddr;
	while (chainLen < 64) {
		nextPtr = ((DWORD)Memory_ReadB(addr + 0) << 24) |
		          ((DWORD)Memory_ReadB(addr + 1) << 16) |
		          ((DWORD)Memory_ReadB(addr + 2) << 8) |
		          ((DWORD)Memory_ReadB(addr + 3));
		if (nextPtr == 0xFFFFFFFF || nextPtr == 0) {
			break;
		}
		// 既にリンク済みかチェック
		if ((nextPtr & 0x00FFFFFF) == (SCSI_SYNTH_DEVHDR_ADDR & 0x00FFFFFF)) {
			s_scsi_dev_linked = 1;
			SCSI_LogText("SCSI_DEV already linked");
			return;
		}
		addr = nextPtr & 0x00FFFFFF;
		chainLen++;
	}

	// チェイン末尾のnextポインタを合成デバイスヘッダへ向ける
	Memory_WriteB(addr + 0, (BYTE)((SCSI_SYNTH_DEVHDR_ADDR >> 24) & 0xff));
	Memory_WriteB(addr + 1, (BYTE)((SCSI_SYNTH_DEVHDR_ADDR >> 16) & 0xff));
	Memory_WriteB(addr + 2, (BYTE)((SCSI_SYNTH_DEVHDR_ADDR >> 8) & 0xff));
	Memory_WriteB(addr + 3, (BYTE)(SCSI_SYNTH_DEVHDR_ADDR & 0xff));

	s_scsi_dev_linked = 1;

	snprintf(logLine, sizeof(logLine),
	         "SCSI_DEV linked at chain end $%08X → $%08X (chain=%u)",
	         (unsigned int)addr, (unsigned int)SCSI_SYNTH_DEVHDR_ADDR,
	         (unsigned int)(chainLen + 1));
	SCSI_LogText(logLine);
#endif
}
