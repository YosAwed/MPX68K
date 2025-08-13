/* 
 * Copyright (c) 2003 NONAKA Kimihiro
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "common.h"
#include "winx68k.h"
//#include "winui.h"

#include "bg.h"
#include "crtc.h"
#include "gvram.h"
#include "mouse.h"
#include "palette.h"
#include "prop.h"
#include "status.h"
#include "tvram.h"
#include "joystick.h"
#include "keyboard.h"


BYTE    Debug_Text=1, Debug_Grp=1, Debug_Sp=1;

WORD *ScrBuf = 0;



int Draw_Opaque;
int FullScreenFlag = 0;
extern BYTE Draw_RedrawAllFlag;
BYTE Draw_DrawFlag = 1;

BYTE Draw_BitMask[800];
BYTE Draw_TextBitMask[800];

int winx = 0, winy = 0;
DWORD winh = 0, winw = 0;
DWORD root_width, root_height;
WORD FrameCount = 0;

WORD WinDraw_Pal16B, WinDraw_Pal16R, WinDraw_Pal16G;

DWORD WindowX = 0;
DWORD WindowY = 0;



void WinDraw_InitWindowSize(WORD width, WORD height)
{
	static BOOL inited = FALSE;

	if (!inited) {
		inited = TRUE;
	}

	winw = width;
	winh = height;

	if (root_width < winw)
		winx = (root_width - winw) / 2;
	else if (winx < 0)
		winx = 0;
	else if ((winx + winw) > root_width)
		winx = root_width - winw;
	if (root_height < winh)
		winy = (root_height - winh) / 2;
	else if (winy < 0)
		winy = 0;
	else if ((winy + winh) > root_height)
		winy = root_height - winh;
}

void WinDraw_ChangeSize(void)
{
	DWORD oldx = WindowX, oldy = WindowY;
	int dif;

	Mouse_ChangePos();

	switch (Config.WinStrech) {
	case 0:
		WindowX = TextDotX;
		WindowY = TextDotY;
		break;

	case 1:
		WindowX = 768;
		WindowY = 512;
		break;

	case 2:
		if (TextDotX <= 384)
			WindowX = TextDotX * 2;
		else
			WindowX = TextDotX;
		if (TextDotY <= 256)
			WindowY = TextDotY * 2;
		else
			WindowY = TextDotY;
		break;

	case 3:
		if (TextDotX <= 384)
			WindowX = TextDotX * 2;
		else
			WindowX = TextDotX;
		if (TextDotY <= 256)
			WindowY = TextDotY * 2;
		else
			WindowY = TextDotY;
		dif = WindowX - WindowY;
		if ((dif > -32) && (dif < 32)) {
			// ÀµÊý·Á¤Ë¶á¤¤²èÌÌ¤Ê¤é¡¢¤È¤·¤Æ¤ª¤³¤¦
			WindowX = (int)(WindowX * 1.25);
		}
		break;
	}

	if ((WindowX > 768) || (WindowX <= 0)) {
		if (oldx)
			WindowX = oldx;
		else
			WindowX = oldx = 768;
	}
	if ((WindowY > 512) || (WindowY <= 0)) {
		if (oldy)
			WindowY = oldy;
		else
			WindowY = oldy = 512;
	}

	if ((oldx == WindowX) && (oldy == WindowY))
		return;

	WinDraw_InitWindowSize((WORD)WindowX, (WORD)WindowY);
	StatBar_Show(Config.WindowFDDStat);
	Mouse_ChangePos();
}

//static int dispflag = 0;
void WinDraw_StartupScreen(void)
{
}

void WinDraw_CleanupScreen(void)
{
}

void WinDraw_ChangeMode(int flag)
{

	/* full screen mode(TRUE) <-> window mode(FALSE) */
	(void)flag;
}


int WinDraw_Init(void)
{
	int i, j;

	WindowX = 768;
	WindowY = 512;

	WinDraw_Pal16R = 0xf800;
	WinDraw_Pal16G = 0x07e0;
	WinDraw_Pal16B = 0x001f;

	ScrBuf = malloc(800 * 600 * 2);

	return TRUE;
}

void
WinDraw_Cleanup(void)
{
    free( ScrBuf );
    ScrBuf = 0;
}

void
WinDraw_Redraw(void)
{

	TVRAM_SetAllDirty();
}


void FASTCALL WinDraw_Draw(unsigned char* data)
{
	static int oldtextx = -1, oldtexty = -1;

	if (oldtextx != TextDotX) {
		oldtextx = TextDotX;
		p6logd("TextDotX: %d\n", TextDotX);
	}
	if (oldtexty != TextDotY) {
		oldtexty = TextDotY;
		p6logd("TextDotY: %d\n", TextDotY);
	}




//    const int Bpp = 3;

    WORD* src = ScrBuf;
    BYTE* dst = data;

    for (int y = 0; y < TextDotY; y++) {
        src = ScrBuf + SCREEN_WIDTH * y;

        for (int x = 0; x < TextDotX; x++) {
            *dst++ /*R*/= (*src   & 0xf800)>>8; // R
            *dst++ /*G*/= (*src   & 0x07e0)>>3; // G
            *dst++ /*B*/= (*src++ & 0x001f)<<3; // B
            *dst++ /*A*/= 0xff;
        }
    }
	
	FrameCount++;
    if (!Draw_DrawFlag/* && is_installed_idle_process()*/) {
		return;
    }

    Draw_DrawFlag = 0;

}


#define WD_MEMCPY(src) memcpy(&ScrBuf[adr], (src), TextDotX * 2)

#define WD_LOOP(start, end, sub)			\
{ 							\
	for (i = (start); i < (end); i++, adr++) {	\
		sub();					\
	}						\
}

#define WD_SUB(SUFFIX, src)			\
{						\
	w = (src);				\
	if (w != 0)				\
		ScrBuf##SUFFIX[adr] = w;	\
}


INLINE void WinDraw_DrawGrpLine(int opaq)
{
#define _DGL_SUB(SUFFIX) WD_SUB(SUFFIX, (Grp_DoubleBuffer ? Grp_LineBuf_Active[i] : Grp_LineBuf[i]))

	DWORD adr = VLINE*FULLSCREEN_WIDTH;
	WORD w;
	int i;

	if (opaq) {
		if (Grp_DoubleBuffer) {
			WD_MEMCPY(Grp_LineBuf_Active);
		} else {
			WD_MEMCPY(Grp_LineBuf);
		}
	} else {
		WD_LOOP(0,  TextDotX, _DGL_SUB);
	}
}

INLINE void WinDraw_DrawGrpLineNonSP(int opaq)
{
#define _DGL_NSP_SUB(SUFFIX) WD_SUB(SUFFIX, (Grp_DoubleBuffer ? Grp_LineBufSP2_Active[i] : Grp_LineBufSP2[i]))

	DWORD adr = VLINE*FULLSCREEN_WIDTH;
	WORD w;
	int i;

	if (opaq) {
		if (Grp_DoubleBuffer) {
			WD_MEMCPY(Grp_LineBufSP2_Active);
		} else {
			WD_MEMCPY(Grp_LineBufSP2);
		}
	} else {
		WD_LOOP(0,  TextDotX, _DGL_NSP_SUB);
	}
}

INLINE void WinDraw_DrawTextLine(int opaq, int td)
{
#define _DTL_SUB2(SUFFIX) WD_SUB(SUFFIX, (BG_DoubleBuffer ? BG_LineBuf_Active[i] : BG_LineBuf[i]))
#define _DTL_SUB(SUFFIX)		\
{					\
	if (Text_TrFlag[i] & 1) {	\
		_DTL_SUB2(SUFFIX);	\
	}				\
}	

	DWORD adr = VLINE*FULLSCREEN_WIDTH;
	WORD w;
	int i;

	if (opaq) {
		if (BG_DoubleBuffer) {
			WD_MEMCPY(&BG_LineBuf_Active[16]);
		} else {
			WD_MEMCPY(&BG_LineBuf[16]);
		}
	} else {
		if (td) {
			WD_LOOP(16, TextDotX + 16, _DTL_SUB);
		} else {
			WD_LOOP(16, TextDotX + 16, _DTL_SUB2);
		}
	}
}

INLINE void WinDraw_DrawTextLineTR(int opaq)
{
#define _DTL_TR_SUB(SUFFIX)			   \
{						   \
	w = (Grp_DoubleBuffer ? Grp_LineBufSP_Active[i - 16] : Grp_LineBufSP[i - 16]);		   \
	if (w != 0) {				   \
		w &= Pal_HalfMask;		   \
		v = (BG_DoubleBuffer ? BG_LineBuf_Active[i] : BG_LineBuf[i]);		   \
		if (v & Ibit)			   \
			w += Pal_Ix2;		   \
		v &= Pal_HalfMask;		   \
		v += w;				   \
		v >>= 1;			   \
	} else {				   \
		if (Text_TrFlag[i] & 1)		   \
			v = (BG_DoubleBuffer ? BG_LineBuf_Active[i] : BG_LineBuf[i]);	   \
		else				   \
			v = 0;			   \
	}					   \
	ScrBuf##SUFFIX[adr] = (WORD)v;		   \
}

#define _DTL_TR_SUB2(SUFFIX)			   \
{						   \
	if (Text_TrFlag[i] & 1) {		   \
		w = (Grp_DoubleBuffer ? Grp_LineBufSP_Active[i - 16] : Grp_LineBufSP[i - 16]);	   \
		v = (BG_DoubleBuffer ? BG_LineBuf_Active[i] : BG_LineBuf[i]);		   \
						   \
		if (v != 0) {			   \
			if (w != 0) {			\
				w &= Pal_HalfMask;	\
				if (v & Ibit)		\
					w += Pal_Ix2;	\
				v &= Pal_HalfMask;	\
				v += w;			\
				v >>= 1;		\
			}				\
			ScrBuf##SUFFIX[adr] = (WORD)v;	\
		}					\
	}						\
}

	DWORD adr = VLINE*FULLSCREEN_WIDTH;
	DWORD v;
	WORD w;
	int i;

	if (opaq) {
		WD_LOOP(16, TextDotX + 16, _DTL_TR_SUB);
	} else {
		WD_LOOP(16, TextDotX + 16, _DTL_TR_SUB2);
	}
}

INLINE void WinDraw_DrawBGLine(int opaq, int td)
{
#define _DBL_SUB2(SUFFIX) WD_SUB(SUFFIX, (BG_DoubleBuffer ? BG_LineBuf_Active[i] : BG_LineBuf[i]))
#define _DBL_SUB(SUFFIX)			 \
{						 \
	if (Text_TrFlag[i] & 2) {		 \
		_DBL_SUB2(SUFFIX); \
	} \
}

	DWORD adr = VLINE*FULLSCREEN_WIDTH;
	WORD w;
	int i;

#if 0 // debug for segv
	static int log_start = 0;

	if (TextDotX == 128 && TextDotY == 128) {
		log_start = 1;
	}
	if (log_start) {
		printf("opaq/td: %d/%d VLINE: %d, TextDotX: %d\n", opaq, td, VLINE, TextDotX);
	}
#endif

	if (opaq) {
		if (BG_DoubleBuffer) {
			WD_MEMCPY(&BG_LineBuf_Active[16]);
		} else {
			WD_MEMCPY(&BG_LineBuf[16]);
		}
	} else {
		if (td) {
			WD_LOOP(16, TextDotX + 16, _DBL_SUB);
		} else {
			WD_LOOP(16, TextDotX + 16, _DBL_SUB2);
		}
	}
}

INLINE void WinDraw_DrawBGLineTR(int opaq)
{

#define _DBL_TR_SUB3()			\
{					\
	if (w != 0) {			\
		w &= Pal_HalfMask;	\
		if (v & Ibit)		\
			w += Pal_Ix2;	\
		v &= Pal_HalfMask;	\
		v += w;			\
		v >>= 1;		\
	}				\
}

#define _DBL_TR_SUB(SUFFIX) \
{					\
	w = (Grp_DoubleBuffer ? Grp_LineBufSP_Active[i - 16] : Grp_LineBufSP[i - 16]);	\
	v = (BG_DoubleBuffer ? BG_LineBuf_Active[i] : BG_LineBuf[i]);		\
					\
	_DBL_TR_SUB3()			\
	ScrBuf##SUFFIX[adr] = (WORD)v;	\
}

#define _DBL_TR_SUB2(SUFFIX) \
{							\
	if (Text_TrFlag[i] & 2) {  			\
		w = (Grp_DoubleBuffer ? Grp_LineBufSP_Active[i - 16] : Grp_LineBufSP[i - 16]);		\
		v = (BG_DoubleBuffer ? BG_LineBuf_Active[i] : BG_LineBuf[i]);			\
							\
		if (v != 0) {				\
			_DBL_TR_SUB3()			\
			ScrBuf##SUFFIX[adr] = (WORD)v;	\
		}					\
	}						\
}

	DWORD adr = VLINE*FULLSCREEN_WIDTH;
	DWORD v;
	WORD w;
	int i;

	if (opaq) {
		WD_LOOP(16, TextDotX + 16, _DBL_TR_SUB);
	} else {
		WD_LOOP(16, TextDotX + 16, _DBL_TR_SUB2);
	}

}

INLINE void WinDraw_DrawPriLine(void)
{
#define _DPL_SUB(SUFFIX) WD_SUB(SUFFIX, (Grp_DoubleBuffer ? Grp_LineBufSP_Active[i] : Grp_LineBufSP[i]))

	DWORD adr = VLINE*FULLSCREEN_WIDTH;
	WORD w;
	int i;

	WD_LOOP(0, TextDotX, _DPL_SUB);
}

void WinDraw_DrawLine(void)
{
	int opaq, ton=0, gon=0, bgon=0, tron=0, pron=0, tdrawed=0;
//@    printf("%d\n", VLINE);
    if((signed int)VLINE<0 )return; //@GOROman
    
	if (!TextDirtyLine[VLINE]) return;
	TextDirtyLine[VLINE] = 0;
	Draw_DrawFlag = 1;


	if (Debug_Grp)
	{
	switch(VCReg0[1]&3)
	{
	case 0:					// 16 colors
		if (VCReg0[1]&4)		// 1024dot
		{
			if (VCReg2[1]&0x10)
			{
				if ( (VCReg2[0]&0x14)==0x14 )
				{
					Grp_DrawLine4hSP();
					pron = tron = 1;
				}
				else
				{
					Grp_DrawLine4h();
					gon=1;
				}
			}
		}
		else				// 512dot
		{
			if ( (VCReg2[0]&0x10)&&(VCReg2[1]&1) )
			{
				Grp_DrawLine4SP((VCReg1[1]   )&3/*, 1*/);			// È¾Æ©ÌÀ¤Î²¼½àÈ÷
				pron = tron = 1;
			}
			opaq = 1;
			if (VCReg2[1]&8)
			{
				Grp_DrawLine4((VCReg1[1]>>6)&3, 1);
				opaq = 0;
				gon=1;
			}
			if (VCReg2[1]&4)
			{
				Grp_DrawLine4((VCReg1[1]>>4)&3, opaq);
				opaq = 0;
				gon=1;
			}
			if (VCReg2[1]&2)
			{
				if ( ((VCReg2[0]&0x1e)==0x1e)&&(tron) )
					Grp_DrawLine4TR((VCReg1[1]>>2)&3, opaq);
				else
					Grp_DrawLine4((VCReg1[1]>>2)&3, opaq);
				opaq = 0;
				gon=1;
			}
			if (VCReg2[1]&1)
			{
//				if ( (VCReg2[0]&0x1e)==0x1e )
//				{
//					Grp_DrawLine4SP((VCReg1[1]   )&3, opaq);
//					tron = pron = 1;
//				}
//				else
				if ( (VCReg2[0]&0x14)!=0x14 )
				{
					Grp_DrawLine4((VCReg1[1]   )&3, opaq);
					gon=1;
				}
			}
		}
		break;
	case 1:	
	case 2:	
		opaq = 1;		// 256 colors
		if ( (VCReg1[1]&3) <= ((VCReg1[1]>>4)&3) )	// Æ±¤¸ÃÍ¤Î»þ¤Ï¡¢GRP0¤¬Í¥Àè¡Ê¥É¥é¥¹¥Ô¡Ë
		{
			if ( (VCReg2[0]&0x10)&&(VCReg2[1]&1) )
			{
				Grp_DrawLine8SP(0);			// È¾Æ©ÌÀ¤Î²¼½àÈ÷
				tron = pron = 1;
			}
			if (VCReg2[1]&4)
			{
				if ( ((VCReg2[0]&0x1e)==0x1e)&&(tron) )
					Grp_DrawLine8TR(1, 1);
				else
					Grp_DrawLine8(1, 1);
				opaq = 0;
				gon=1;
			}
			if (VCReg2[1]&1)
			{
				if ( (VCReg2[0]&0x14)!=0x14 )
				{
					Grp_DrawLine8(0, opaq);
					gon=1;
				}
			}
		}
		else
		{
			if ( (VCReg2[0]&0x10)&&(VCReg2[1]&1) )
			{
				Grp_DrawLine8SP(1);			// È¾Æ©ÌÀ¤Î²¼½àÈ÷
				tron = pron = 1;
			}
			if (VCReg2[1]&4)
			{
				if ( ((VCReg2[0]&0x1e)==0x1e)&&(tron) )
					Grp_DrawLine8TR(0, 1);
				else
					Grp_DrawLine8(0, 1);
				opaq = 0;
				gon=1;
			}
			if (VCReg2[1]&1)
			{
				if ( (VCReg2[0]&0x14)!=0x14 )
				{
					Grp_DrawLine8(1, opaq);
					gon=1;
				}
			}
		}
		break;
	case 3:					// 65536 colors
		if (VCReg2[1]&15)
		{
			if ( (VCReg2[0]&0x14)==0x14 )
			{
				Grp_DrawLine16SP();
				tron = pron = 1;
			}
			else
			{
				Grp_DrawLine16();
				gon=1;
			}
		}
		break;
	}
	}


//	if ( ( ((VCReg1[0]&0x30)>>4) < (VCReg1[0]&0x03) ) && (gon) )
//		gdrawed = 1;				// Grp¤è¤êBG¤ÎÊý¤¬¾å

	if ( ((VCReg1[0]&0x30)>>2) < (VCReg1[0]&0x0c) )
	{						// BG¤ÎÊý¤¬¾å
		if ((VCReg2[1]&0x20)&&(Debug_Text))
		{
			Text_DrawLine_C(1);
			ton = 1;
		}
		else
			ZeroMemory(Text_TrFlag, TextDotX+16);

		if ((VCReg2[1]&0x40)&&(BG_Regs[8]&2)&&(!(BG_Regs[0x11]&2))&&(Debug_Sp))
		{
			int s1, s2;
			s1 = (((BG_Regs[0x11]  &4)?2:1)-((BG_Regs[0x11]  &16)?1:0));
			s2 = (((CRTC_Regs[0x29]&4)?2:1)-((CRTC_Regs[0x29]&16)?1:0));
			VLINEBG = VLINE;
			VLINEBG <<= s1;
			VLINEBG >>= s2;
			if ( !(BG_Regs[0x11]&16) ) VLINEBG -= ((BG_Regs[0x0f]>>s1)-(CRTC_Regs[0x0d]>>s2));
			BG_DrawLine(!ton, 0);
			BG_SwapBuffers(); // フレーム描画完了後にバッファスワップ
			Grp_SwapBuffers(); // グラフィックプレーン用バッファスワップ
			bgon = 1;
		}
	}
	else
	{						// Text¤ÎÊý¤¬¾å
		if ((VCReg2[1]&0x40)&&(BG_Regs[8]&2)&&(!(BG_Regs[0x11]&2))&&(Debug_Sp))
		{
			int s1, s2;
			s1 = (((BG_Regs[0x11]  &4)?2:1)-((BG_Regs[0x11]  &16)?1:0));
			s2 = (((CRTC_Regs[0x29]&4)?2:1)-((CRTC_Regs[0x29]&16)?1:0));
			VLINEBG = VLINE;
			VLINEBG <<= s1;
			VLINEBG >>= s2;
			if ( !(BG_Regs[0x11]&16) ) VLINEBG -= ((BG_Regs[0x0f]>>s1)-(CRTC_Regs[0x0d]>>s2));
			ZeroMemory(Text_TrFlag, TextDotX+16);
			BG_DrawLine(1, 1);
			BG_SwapBuffers(); // フレーム描画完了後にバッファスワップ
			Grp_SwapBuffers(); // グラフィックプレーン用バッファスワップ
			bgon = 1;
		}
		else
		{
			if ((VCReg2[1]&0x20)&&(Debug_Text))
			{
				int i;
				for (i = 16; i < TextDotX + 16; ++i) {
					if (BG_DoubleBuffer) {
						BG_LineBuf_Draw[i] = TextPal[0];  // 次フレーム用描画バッファをクリア
					} else {
						BG_LineBuf[i] = TextPal[0];
					}
				}
			} else {		// 20010120 ¡Êàèàá¿§¡Ë
				if (BG_DoubleBuffer) {
					memset(&BG_LineBuf_Draw[16], 0, TextDotX * 2);  // 次フレーム用描画バッファをクリア
				} else {
					bzero(&BG_LineBuf[16], TextDotX * 2);
				}
			}
			ZeroMemory(Text_TrFlag, TextDotX+16);
			bgon = 1;
		}

		if ((VCReg2[1]&0x20)&&(Debug_Text))
		{
			Text_DrawLine_C(!bgon);
			ton = 1;
		}
	}


	opaq = 1;


#if 0
					// Pri = 3¡Ê°ãÈ¿¡Ë¤ËÀßÄê¤µ¤ì¤Æ¤¤¤ë²èÌÌ¤òÉ½¼¨
		if ( ((VCReg1[0]&0x30)==0x30)&&(bgon) )
		{
			if ( ((VCReg2[0]&0x5d)==0x1d)&&((VCReg1[0]&0x03)!=0x03)&&(tron) )
			{
				if ( (VCReg1[0]&3)<((VCReg1[0]>>2)&3) )
				{
					WinDraw_DrawBGLineTR(opaq);
					tdrawed = 1;
					opaq = 0;
				}
			}
			else
			{
				WinDraw_DrawBGLine(opaq, /*tdrawed*/0);
				tdrawed = 1;
				opaq = 0;
			}
		}
		if ( ((VCReg1[0]&0x0c)==0x0c)&&(ton) )
		{
			if ( ((VCReg2[0]&0x5d)==0x1d)&&((VCReg1[0]&0x03)!=0x0c)&&(tron) )
				WinDraw_DrawTextLineTR(opaq);
			else
				WinDraw_DrawTextLine(opaq, /*tdrawed*/((VCReg1[0]&0x30)==0x30));
			opaq = 0;
			tdrawed = 1;
		}
#endif
					// Pri = 2 or 3¡ÊºÇ²¼°Ì¡Ë¤ËÀßÄê¤µ¤ì¤Æ¤¤¤ë²èÌÌ¤òÉ½¼¨
					// ¥×¥é¥¤¥ª¥ê¥Æ¥£¤¬Æ±¤¸¾ì¹ç¤Ï¡¢GRP<SP<TEXT¡©¡Ê¥É¥é¥¹¥Ô¡¢ÅíÅÁ¡¢YsIIIÅù¡Ë

					// Grp¤è¤êText¤¬¾å¤Ë¤¢¤ë¾ì¹ç¤ËText¤È¤ÎÈ¾Æ©ÌÀ¤ò¹Ô¤¦¤È¡¢SP¤Î¥×¥é¥¤¥ª¥ê¥Æ¥£¤â
					// Text¤Ë°ú¤­¤º¤é¤ì¤ë¡©¡Ê¤Ä¤Þ¤ê¡¢Grp¤è¤ê²¼¤Ë¤¢¤Ã¤Æ¤âSP¤¬É½¼¨¤µ¤ì¤ë¡©¡Ë

					// KnightArms¤È¤«¤ò¸«¤ë¤È¡¢È¾Æ©ÌÀ¤Î¥Ù¡¼¥¹¥×¥ì¡¼¥ó¤Ï°ìÈÖ¾å¤Ë¤Ê¤ë¤ß¤¿¤¤¡Ä¡£

		if ( (VCReg1[0]&0x02) )
		{
			if (gon)
			{
				WinDraw_DrawGrpLine(opaq);
				opaq = 0;
			}
			if (tron)
			{
				WinDraw_DrawGrpLineNonSP(opaq);
				opaq = 0;
			}
		}
		if ( (VCReg1[0]&0x20)&&(bgon) )
		{
			if ( ((VCReg2[0]&0x5d)==0x1d)&&((VCReg1[0]&0x03)!=0x02)&&(tron) )
			{
				if ( (VCReg1[0]&3)<((VCReg1[0]>>2)&3) )
				{
					WinDraw_DrawBGLineTR(opaq);
					tdrawed = 1;
					opaq = 0;
				}
			}
			else
			{
				WinDraw_DrawBGLine(opaq, /*0*/tdrawed);
				tdrawed = 1;
				opaq = 0;
			}
		}
		if ( (VCReg1[0]&0x08)&&(ton) )
		{
			if ( ((VCReg2[0]&0x5d)==0x1d)&&((VCReg1[0]&0x03)!=0x02)&&(tron) )
				WinDraw_DrawTextLineTR(opaq);
			else
				WinDraw_DrawTextLine(opaq, tdrawed/*((VCReg1[0]&0x30)>=0x20)*/);
			opaq = 0;
			tdrawed = 1;
		}

					// Pri = 1¡Ê2ÈÖÌÜ¡Ë¤ËÀßÄê¤µ¤ì¤Æ¤¤¤ë²èÌÌ¤òÉ½¼¨
		if ( ((VCReg1[0]&0x03)==0x01)&&(gon) )
		{
			WinDraw_DrawGrpLine(opaq);
			opaq = 0;
		}
		if ( ((VCReg1[0]&0x30)==0x10)&&(bgon) )
		{
			if ( ((VCReg2[0]&0x5d)==0x1d)&&(!(VCReg1[0]&0x03))&&(tron) )
			{
				if ( (VCReg1[0]&3)<((VCReg1[0]>>2)&3) )
				{
					WinDraw_DrawBGLineTR(opaq);
					tdrawed = 1;
					opaq = 0;
				}
			}
			else
			{
				WinDraw_DrawBGLine(opaq, ((VCReg1[0]&0xc)==0x8));
				tdrawed = 1;
				opaq = 0;
			}
		}
		if ( ((VCReg1[0]&0x0c)==0x04) && ((VCReg2[0]&0x5d)==0x1d) && (VCReg1[0]&0x03) && (((VCReg1[0]>>4)&3)>(VCReg1[0]&3)) && (bgon) && (tron) )
		{
			WinDraw_DrawBGLineTR(opaq);
			tdrawed = 1;
			opaq = 0;
			if (tron)
			{
				WinDraw_DrawGrpLineNonSP(opaq);
			}
		}
		else if ( ((VCReg1[0]&0x03)==0x01)&&(tron)&&(gon)&&(VCReg2[0]&0x10) )
		{
			WinDraw_DrawGrpLineNonSP(opaq);
			opaq = 0;
		}
		if ( ((VCReg1[0]&0x0c)==0x04)&&(ton) )
		{
			if ( ((VCReg2[0]&0x5d)==0x1d)&&(!(VCReg1[0]&0x03))&&(tron) )
				WinDraw_DrawTextLineTR(opaq);
			else
				WinDraw_DrawTextLine(opaq, ((VCReg1[0]&0x30)>=0x10));
			opaq = 0;
			tdrawed = 1;
		}

					// Pri = 0¡ÊºÇÍ¥Àè¡Ë¤ËÀßÄê¤µ¤ì¤Æ¤¤¤ë²èÌÌ¤òÉ½¼¨
		if ( (!(VCReg1[0]&0x03))&&(gon) )
		{
			WinDraw_DrawGrpLine(opaq);
			opaq = 0;
		}
		if ( (!(VCReg1[0]&0x30))&&(bgon) )
		{
			WinDraw_DrawBGLine(opaq, /*tdrawed*/((VCReg1[0]&0xc)>=0x4));
			tdrawed = 1;
			opaq = 0;
		}
		if ( (!(VCReg1[0]&0x0c)) && ((VCReg2[0]&0x5d)==0x1d) && (((VCReg1[0]>>4)&3)>(VCReg1[0]&3)) && (bgon) && (tron) )
		{
			WinDraw_DrawBGLineTR(opaq);
			tdrawed = 1;
			opaq = 0;
			if (tron)
			{
				WinDraw_DrawGrpLineNonSP(opaq);
			}
		}
		else if ( (!(VCReg1[0]&0x03))&&(tron)&&(VCReg2[0]&0x10) )
		{
			WinDraw_DrawGrpLineNonSP(opaq);
			opaq = 0;
		}
		if ( (!(VCReg1[0]&0x0c))&&(ton) )
		{
			WinDraw_DrawTextLine(opaq, 1);
			tdrawed = 1;
			opaq = 0;
		}

					// ÆÃ¼ì¥×¥é¥¤¥ª¥ê¥Æ¥£»þ¤Î¥°¥é¥Õ¥£¥Ã¥¯
		if ( ((VCReg2[0]&0x5c)==0x14)&&(pron) )	// ÆÃ¼ìPri»þ¤Ï¡¢ÂÐ¾Ý¥×¥ì¡¼¥ó¥Ó¥Ã¥È¤Ï°ÕÌ£¤¬Ìµ¤¤¤é¤·¤¤¡Ê¤Ä¤¤¤ó¤Ó¡¼¡Ë
		{
			WinDraw_DrawPriLine();
		}
		else if ( ((VCReg2[0]&0x5d)==0x1c)&&(tron) )	// È¾Æ©ÌÀ»þ¤ËÁ´¤Æ¤¬Æ©ÌÀ¤Ê¥É¥Ã¥È¤ò¥Ï¡¼¥Õ¥«¥é¡¼¤ÇËä¤á¤ë
		{						// ¡ÊAQUALES¡Ë

#define _DL_SUB(SUFFIX) \
{								\
	w = (Grp_DoubleBuffer ? Grp_LineBufSP_Active[i] : Grp_LineBufSP[i]);					\
	if (w != 0 && (ScrBuf##SUFFIX[adr] & 0xffff) == 0)	\
		ScrBuf##SUFFIX[adr] = (w & Pal_HalfMask) >> 1;	\
}

			DWORD adr = VLINE*FULLSCREEN_WIDTH;
			WORD w;
			int i;

			WD_LOOP(0, TextDotX, _DL_SUB);
		}


	if (opaq)
	{
		DWORD adr = VLINE*FULLSCREEN_WIDTH;
		bzero(&ScrBuf[adr], TextDotX * 2);
	}
}


