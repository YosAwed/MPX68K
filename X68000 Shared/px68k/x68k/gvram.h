#ifndef _winx68k_gvram
#define _winx68k_gvram

#include "common.h"

extern	BYTE	GVRAM[0x80000];
extern	WORD	Grp_LineBuf[1024];
extern	WORD	Grp_LineBufSP[1024];
extern	WORD	Grp_LineBufSP2[1024];

// ダブルバッファリング用の変数とアクセサ
extern	WORD	*Grp_LineBuf_Active;	// 表示用アクティブバッファ
extern	WORD	*Grp_LineBuf_Draw;		// 描画用バックバッファ
extern	WORD	*Grp_LineBufSP_Active;	// SP用表示用アクティブバッファ
extern	WORD	*Grp_LineBufSP_Draw;	// SP用描画用バックバッファ
extern	WORD	*Grp_LineBufSP2_Active;	// SP2用表示用アクティブバッファ  
extern	WORD	*Grp_LineBufSP2_Draw;	// SP2用描画用バックバッファ
extern	int		Grp_DoubleBuffer;		// ダブルバッファリング有効フラグ

void GVRAM_Init(void);

void FASTCALL GVRAM_FastClear(void);

BYTE FASTCALL GVRAM_Read(DWORD adr);
void FASTCALL GVRAM_Write(DWORD adr, BYTE data);

void Grp_DrawLine16(void);
void FASTCALL Grp_DrawLine8(int page, int opaq);
void FASTCALL Grp_DrawLine4(DWORD page, int opaq);
void FASTCALL Grp_DrawLine4h(void);
void FASTCALL Grp_DrawLine16SP(void);
void FASTCALL Grp_DrawLine8SP(int page/*, int opaq*/);
void FASTCALL Grp_DrawLine4SP(DWORD page/*, int opaq*/);
void FASTCALL Grp_DrawLine4hSP(void);
void FASTCALL Grp_DrawLine8TR(int page, int opaq);
void FASTCALL Grp_DrawLine8TR_GT(int page, int opaq);
void FASTCALL Grp_DrawLine4TR(DWORD page, int opaq);

// ダブルバッファリング管理関数
void FASTCALL Grp_SwapBuffers(void);
void FASTCALL Grp_SetDoubleBuffer(int enable);

#endif

