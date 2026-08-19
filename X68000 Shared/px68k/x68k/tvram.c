// ---------------------------------------------------------------------------------------
//  TVRAM.C - Text VRAM
//  ToDo : 透明色処理とか色々
// ---------------------------------------------------------------------------------------

#include	"common.h"
#include	"winx68k.h"
#include	"windraw.h"
#include	"bg.h"
#include	"crtc.h"
#include	"palette.h"
#include	"m68000.h"
#include	"tvram.h"

	BYTE	TVRAM[0x80000];
	BYTE	TextDrawWork[1024*1024];
	BYTE	TextDirtyLine[1024];

	BYTE	TextDrawPattern[2048*4];

//	WORD	Text_LineBuf[1024];	// →BGのを使うように変更
	BYTE	Text_TrFlag[SCRBUF_STRIDE + 16];

INLINE void TVRAM_WriteByteMask(DWORD adr, BYTE data);

// -----------------------------------------------------------------------
//   全部書き換え〜
// -----------------------------------------------------------------------
void TVRAM_SetAllDirty(void)
{
	memset(TextDirtyLine, 1, 1024);
}


// -----------------------------------------------------------------------
//   初期化
// -----------------------------------------------------------------------
void TVRAM_Init(void)
{
	int i, j, bit;
	ZeroMemory(TVRAM, 0x80000);
	ZeroMemory(TextDrawWork, 1024*1024);
	TVRAM_SetAllDirty();

	ZeroMemory(TextDrawPattern, 2048*4);		// パターンテーブル初期化
	for (i=0; i<256; i++)
	{
		for (j=0, bit=0x80; j<8; j++, bit>>=1)
		{
			if (i&bit) {
				TextDrawPattern[i*8+j     ] = 1;
				TextDrawPattern[i*8+j+2048] = 2;
				TextDrawPattern[i*8+j+4096] = 4;
				TextDrawPattern[i*8+j+6144] = 8;
			}
		}
	}
}


// -----------------------------------------------------------------------
//   撤収
// -----------------------------------------------------------------------
void TVRAM_Cleanup(void)
{
}


// -----------------------------------------------------------------------
//   読むなり
// -----------------------------------------------------------------------
BYTE FASTCALL TVRAM_Read(DWORD adr)
{
	adr &= 0x7ffff;
	adr ^= 1;
	return TVRAM[adr];
}


// -----------------------------------------------------------------------
//   1ばいと書くなり
// -----------------------------------------------------------------------
INLINE void TVRAM_WriteByte(DWORD adr, BYTE data)
{
	if (TVRAM[adr]!=data)
	{
		TextDirtyLine[(((adr&0x1ffff)/128)-TextScrollY)&1023] = 1;
		TVRAM[adr] = data;
	}
}


// -----------------------------------------------------------------------
//   ますく付きで書くなり
// -----------------------------------------------------------------------
INLINE void TVRAM_WriteByteMask(DWORD adr, BYTE data)
{
	data = (TVRAM[adr] & CRTC_Regs[0x2e + ((adr^1) & 1)]) | (data & (~CRTC_Regs[0x2e + ((adr ^ 1) & 1)]));
	if (TVRAM[adr] != data)
	{
		TextDirtyLine[(((adr&0x1ffff)/128)-TextScrollY)&1023] = 1;
		TVRAM[adr] = data;
	}
}


// -----------------------------------------------------------------------
//   書くなり
// -----------------------------------------------------------------------
void FASTCALL TVRAM_Write(DWORD adr, BYTE data)
{
	adr &= 0x7ffff;
	adr ^= 1;
	if (CRTC_Regs[0x2a]&1)			// 同時アクセス
	{
		adr &= 0x1ffff;
		if (CRTC_Regs[0x2a]&2)		// Text Mask
		{
			if (CRTC_Regs[0x2b]&0x10) TVRAM_WriteByteMask(adr        , data);
			if (CRTC_Regs[0x2b]&0x20) TVRAM_WriteByteMask(adr+0x20000, data);
			if (CRTC_Regs[0x2b]&0x40) TVRAM_WriteByteMask(adr+0x40000, data);
			if (CRTC_Regs[0x2b]&0x80) TVRAM_WriteByteMask(adr+0x60000, data);
		}
		else
		{
			if (CRTC_Regs[0x2b]&0x10) TVRAM_WriteByte(adr        , data);
			if (CRTC_Regs[0x2b]&0x20) TVRAM_WriteByte(adr+0x20000, data);
			if (CRTC_Regs[0x2b]&0x40) TVRAM_WriteByte(adr+0x40000, data);
			if (CRTC_Regs[0x2b]&0x80) TVRAM_WriteByte(adr+0x60000, data);
		}
	}
	else					// シングルアクセス
	{
		if (CRTC_Regs[0x2a]&2)		// Text Mask
		{
			TVRAM_WriteByteMask(adr, data);
		}
		else
		{
			TVRAM_WriteByte(adr, data);
		}
	}
#ifdef USE_ASM
	_asm {
		push	edi
		push	esi

		mov	eax, adr
		mov	esi, eax
		and	esi, 01ffffh		; TVRAM Adr
		mov	edi, eax
		and	edi, 01ff80h		; 下位7bitマスク
		shl	edi, 3
		and	eax, 07fh
		xor	al, 1
		shl	eax, 3
		add	edi, eax		; edi = workadr

		xor	eax, eax

		mov	al, byte ptr TVRAM[esi+60000h]
		mov	ecx, dword ptr (TextDrawPattern+6144)[eax*8]
		mov	edx, dword ptr (TextDrawPattern+6144)[eax*8+4]
		mov	al, byte ptr TVRAM[esi+40000h]
		or	ecx, dword ptr (TextDrawPattern+4096)[eax*8]
		or	edx, dword ptr (TextDrawPattern+4096)[eax*8+4]
		mov	al, byte ptr TVRAM[esi+20000h]
		or	ecx, dword ptr (TextDrawPattern+2048)[eax*8]
		or	edx, dword ptr (TextDrawPattern+2048)[eax*8+4]
		mov	al, byte ptr TVRAM[esi]
		or	ecx, dword ptr TextDrawPattern[eax*8]
		or	edx, dword ptr TextDrawPattern[eax*8+4]
		mov	dword ptr TextDrawWork[edi], ecx
		mov	dword ptr (TextDrawWork+4)[edi], edx

		pop	esi
		pop	edi
	}
#elif defined(USE_GAS) && defined(__i386__)
	asm (
		"mov	%0, %%eax;"
		"mov	%%eax, %%esi;"
		"and	$0x1ffff, %%esi;"	/* TVRAM Adr */
		"mov	%%eax, %%edi;"
		"and	$0x1ff80, %%edi;"	/* 下位7bitマスク */
		"shl	$3, %%edi;"
		"and	$0x7f, %%eax;"
		"xor	$1, %%al;"
		"shl	$3, %%eax;"
		"add	%%eax, %%edi;"		/* edi = workadr */

		"xor	%%eax, %%eax;"

		"mov	TVRAM + 0x60000(%%esi), %%al;"
		"mov	TextDrawPattern + 6144(, %%eax, 8), %%ecx;"
		"mov	TextDrawPattern + 6144 + 4(, %%eax, 8), %%edx;"
		"mov	TVRAM + 0x40000(%%esi), %%al;"
		"or	TextDrawPattern + 4096(, %%eax, 8), %%ecx;"
		"or	TextDrawPattern + 4096 + 4(, %%eax, 8), %%edx;"
		"mov	TVRAM + 0x20000(%%esi), %%al;"
		"or	TextDrawPattern + 2048(, %%eax, 8), %%ecx;"
		"or	TextDrawPattern + 2048 + 4(, %%eax, 8), %%edx;"
		"mov	TVRAM(%%esi), %%al;"
		"or	TextDrawPattern(, %%eax, 8), %%ecx;"
		"or	TextDrawPattern + 4(, %%eax, 8), %%edx;"
		"mov	%%ecx, TextDrawWork(%%edi);"
		"mov	%%edx, TextDrawWork + 4(%%edi);"
	: /* output: nothing */
	: "m" (adr)
	: "ax", "cx", "dx", "si", "di", "memory");
#else /* !USE_ASM && !(USE_GAS && __i386__) */
	{
		DWORD *ptr = (DWORD *)TextDrawPattern;
		DWORD tvram_addr = adr & 0x1ffff;
		DWORD workadr = ((adr & 0x1ff80) + ((adr ^ 1) & 0x7f)) << 3;
		DWORD t0, t1;
		BYTE pat;

		pat = TVRAM[tvram_addr + 0x60000];
		t0 = ptr[(pat * 2) + 1536];
		t1 = ptr[(pat * 2 + 1) + 1536];

		pat = TVRAM[tvram_addr + 0x40000];
		t0 |= ptr[(pat * 2) + 1024];
		t1 |= ptr[(pat * 2 + 1) + 1024];

		pat = TVRAM[tvram_addr + 0x20000];
		t0 |= ptr[(pat * 2) + 512];
		t1 |= ptr[(pat * 2 + 1) + 512];

		pat = TVRAM[tvram_addr];
		t0 |= ptr[(pat * 2)];
		t1 |= ptr[(pat * 2 + 1)];

		*((DWORD *)&TextDrawWork[workadr]) = t0;
		*(((DWORD *)(&TextDrawWork[workadr])) + 1) = t1;
	}
#endif	/* USE_ASM */
}


// -----------------------------------------------------------------------
//   らすたこぴー時のあっぷでーと
// -----------------------------------------------------------------------
void FASTCALL TVRAM_RCUpdate(void)
{
	DWORD adr = ((DWORD)CRTC_Regs[0x2d]<<9);

#ifdef USE_ASM
	_asm
	{
		push	edi
		push	esi
		mov	esi, adr
		mov	edi, esi
		shl	edi, 3
		mov	esi, adr
		mov	cx, 512
		xor	eax, eax
	rcu_mainloop:
		xor	esi, 1
		mov	al, byte ptr TVRAM[esi+60000h]
		mov	ebx, dword ptr (TextDrawPattern+6144)[eax*8]
		mov	edx, dword ptr (TextDrawPattern+6144)[eax*8+4]
		mov	al, byte ptr TVRAM[esi+40000h]
		or	ebx, dword ptr (TextDrawPattern+4096)[eax*8]
		or	edx, dword ptr (TextDrawPattern+4096)[eax*8+4]
		mov	al, byte ptr TVRAM[esi+20000h]
		or	ebx, dword ptr (TextDrawPattern+2048)[eax*8]
		or	edx, dword ptr (TextDrawPattern+2048)[eax*8+4]
		mov	al, byte ptr TVRAM[esi]
		or	ebx, dword ptr TextDrawPattern[eax*8]
		or	edx, dword ptr TextDrawPattern[eax*8+4]
		mov	dword ptr TextDrawWork[edi], ebx
		add	edi, 4
		mov	dword ptr TextDrawWork[edi], edx
		add	edi, 4
		xor	esi, 1
		inc	esi
		dec	cx
		jnz	rcu_mainloop
//		loop	rcu_mainloop
		pop	esi
		pop	edi
	}
#elif defined(USE_GAS) && defined(__i386__)
	asm (
		"mov	%0, %%esi;"
		"mov	%%esi, %%edi;"
		"shl	$3, %%edi;"
		"mov	%0, %%esi;"
		"mov	$512, %%cx;"
		"xor	%%eax, %%eax;"
	".rcu_mainloop:"
		"xor	$1, %%esi;"
		"mov	TVRAM + 0x60000(%%esi), %%al;"
		"mov	TextDrawPattern + 6144(, %%eax, 8), %%ebx;"
		"mov	TextDrawPattern + 6144 + 4(, %%eax, 8), %%edx;"
		"mov	TVRAM + 0x40000(%%esi), %%al;"
		"or	TextDrawPattern + 4096(, %%eax, 8), %%ebx;"
		"or	TextDrawPattern + 4096 + 4(, %%eax, 8), %%edx;"
		"mov	TVRAM + 0x20000(%%esi), %%al;"
		"or	TextDrawPattern + 2048(, %%eax, 8), %%ebx;"
		"or	TextDrawPattern + 2048 + 4(, %%eax, 8), %%edx;"
		"mov	TVRAM(%%esi), %%al;"
		"or	TextDrawPattern(, %%eax, 8), %%ebx;"
		"or	TextDrawPattern + 4(, %%eax, 8), %%edx;"
		"mov	%%ebx, TextDrawWork(%%edi);"
		"add	$4, %%edi;"
		"mov	%%edx, TextDrawWork(%%edi);"
		"add	$4, %%edi;"
		"xor	$1, %%esi;"
		"inc	%%esi;"
		"loop	.rcu_mainloop;"
	: /* output: nothing */
	: "m" (adr)
	: "ax", "bx", "cx", "dx", "si", "di", "memory");
#else /* !USE_ASM && !(USE_GAS && __i386__) */
	/* XXX: BUG */
	DWORD *ptr = (DWORD *)TextDrawPattern;
	DWORD *wptr = (DWORD *)(TextDrawWork + (adr << 3));
	DWORD t0, t1;
	DWORD tadr;
	BYTE pat;
	int i;

	for (i = 0; i < 512; i++, adr++) {
		tadr = adr ^ 1;

		pat = TVRAM[tadr + 0x60000];
		t0 = ptr[(pat * 2) + 1536];
		t1 = ptr[(pat * 2 + 1) + 1536];

		pat = TVRAM[tadr + 0x40000];
		t0 |= ptr[(pat * 2) + 1024];
		t1 |= ptr[(pat * 2 + 1) + 1024];

		pat = TVRAM[tadr + 0x20000];
		t0 |= ptr[(pat * 2) + 512];
		t1 |= ptr[(pat * 2 + 1) + 512];

		pat = TVRAM[tadr];
		t0 |= ptr[(pat * 2)];
		t1 |= ptr[(pat * 2 + 1)];

		*wptr++ = t0;
		*wptr++ = t1;
	}
#endif	/* USE_ASM */
}

// -----------------------------------------------------------------------
//   1ライン描画（C言語版・ダブルバッファ対応）
// -----------------------------------------------------------------------
void FASTCALL Text_DrawLine_C(int opaq)
{
	WORD *target_buf;
	BYTE *tr_flag;
	DWORD line, scroll_x, scroll_y;
	int i, x, y;
	WORD color;
	BYTE pattern;
	
	// ダブルバッファ対応：適切なバッファを選択
	if (BG_DoubleBuffer) {
		target_buf = BG_LineBuf_Draw;  // 描画用バッファに書き込み
	} else {
		target_buf = BG_LineBuf;       // 元のバッファに書き込み
	}
	
	tr_flag = Text_TrFlag;
	line = VLINE;
	
	// インターレース処理
	line *= CRTC_VramRowStep;
	
	scroll_y = (line + TextScrollY) & 1023;
	scroll_x = TextScrollX & 1023;
	
	y = scroll_y << 10;
	x = scroll_x;
	
	if (opaq) {
		// 不透明描画：全ピクセルを描画
		for (i = 0; i < TextDotX; i++) {
			pattern = TextDrawWork[y + ((x + i) & 1023)];
			tr_flag[i + 16] = (pattern & 15) ? 1 : 0;
			color = TextPal[pattern & 15];
			target_buf[i + 16] = color;
		}
	} else {
		// 透明描画：透明でないピクセルのみ描画
		for (i = 0; i < TextDotX; i++) {
			pattern = TextDrawWork[y + ((x + i) & 1023)];
			if (pattern & 15) {
				tr_flag[i + 16] = 1;
				color = TextPal[pattern & 15];
				target_buf[i + 16] = color;
			} else {
				tr_flag[i + 16] = 0;
			}
		}
	}
}
