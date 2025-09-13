/* 
 * Copyright (c) 2003,2008 NONAKA Kimihiro
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
#include "prop.h"
#include "scc.h"
#include "crtc.h"
#include "mouse.h"
#include <sys/time.h>

float	MouseDX = 0;
float	MouseDY = 0;
BYTE	MouseStat = 0;
BYTE	MouseSW = 1;

POINT	CursorPos;
int	mousex = 0, mousey = 0;

// 修正: 重複送信防止のための変数を追加
static signed char LastMouseX = 0;
static signed char LastMouseY = 0;
static BYTE LastMouseSt = 0;
static int MouseDataSendCount = 0;  // 送信回数をカウント


void Mouse_Init(void)
{
	if (Config.JoyOrMouse) {
		Mouse_StartCapture(1);
	}
	
	// 修正: 初期化時に前回値もリセット
	LastMouseX = 0;
	LastMouseY = 0;
	LastMouseSt = 0;
	MouseDataSendCount = 0;
}


// ----------------------------------
//	Mouse Event Occured
// ----------------------------------
void Mouse_Event(int param, float dx, float dy)
{
//	printf("Mouse( %d, %f %f )\n", param, dx, dy);

	if (MouseSW) {
		switch (param) {
        case 0:	// mouse move
            // Invert Y here so positive dy means up on X68 side
            MouseDX += dx;
            MouseDY -= dy;
			break;
		case 1:	// left button
			if (dx != 0)
				MouseStat |= 1;
			else
				MouseStat &= 0xfe;
			break;
		case 2:	// right button
			if (dx != 0)
				MouseStat |= 2;
			else
				MouseStat &= 0xfd;
			break;
		default:
			break;
		}
	}
}


// ----------------------------------
//	Mouse Data send to SCC
// ----------------------------------
static double GetCurrentTimeMs(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000.0) + (tv.tv_usec / 1000.0);
}

void Mouse_SetData(void)
{
    // 修正: @GOROmanコメントを削除し、重複防止機能付きで有効化
	POINT pt;
	int x, y;

	if (MouseSW) {

		x = (int)MouseDX;
		y = (int)MouseDY;

		MouseDX = MouseDY = 0;

		MouseSt = MouseStat;

		// 修正: ドラッグ対応 - 移動量があるか、ボタン状態変化があれば送信
		if (x == 0 && y == 0 && MouseSt == LastMouseSt) {
			// 移動量もボタン状態変化もない場合はスキップ
			return;
		}

		if (x > 127) {
			MouseSt |= 0x10;
			MouseX = 127;
		} else if (x < -128) {
			MouseSt |= 0x20;
			MouseX = -128;
		} else {
			MouseX = (signed char)x;
		}

		if (y > 127) {
			MouseSt |= 0x40;
			MouseY = 127;
		} else if (y < -128) {
			MouseSt |= 0x80;
			MouseY = -128;
		} else {
			MouseY = (signed char)y;
		}

		// 修正: 前回の値を記録
		LastMouseX = MouseX;
		LastMouseY = MouseY;
		LastMouseSt = MouseSt;
		MouseDataSendCount++;

		// タイミングログ（ボタン状態変化時のみ）
		static double lastButtonTime = 0.0;
		static BYTE lastButtonState = 0;
		if ((MouseSt & 0x03) != (lastButtonState & 0x03)) {
		    double currentTime = GetCurrentTimeMs();
		    if (lastButtonTime > 0) {
		        double interval = currentTime - lastButtonTime;
		        printf("Mouse: Button %02X→%02X, interval=%.1fms, X=%d, Y=%d\n",
		               lastButtonState, MouseSt, interval, MouseX, MouseY);
		    } else {
		        printf("Mouse: Button %02X (first), X=%d, Y=%d\n", MouseSt, MouseX, MouseY);
		    }
		    lastButtonTime = currentTime;
		    lastButtonState = MouseSt;
		}

	} else {
		MouseSt = 0;
		MouseX = 0;
		MouseY = 0;
		
		// 修正: 無効時も前回値をリセット
		LastMouseX = 0;
		LastMouseY = 0;
		LastMouseSt = 0;
	}
}


// ----------------------------------
//	Start Capture
// ----------------------------------
void Mouse_StartCapture(int flag)
{
	if (flag && !MouseSW) {
		MouseSW = 1;
		// 修正: キャプチャ開始時に状態をリセット
		MouseDataSendCount = 0;
		LastMouseX = 0;
		LastMouseY = 0;
		LastMouseSt = 0;
	} else 	if (!flag && MouseSW) {
		MouseSW = 0;
		// 修正: キャプチャ終了時も状態をリセット
		MouseDataSendCount = 0;
		LastMouseX = 0;
		LastMouseY = 0;
		LastMouseSt = 0;
	}
}

void Mouse_ChangePos(void)
{
#if 0
	if (MouseSW) {
		POINT pt;

		getmaincenter(window, &pt);
		gdk_window_set_pointer(window->window, pt.x, pt.y);
	}
#endif
}

// 外部から呼び出して、蓄積状態をクリア
void Mouse_ResetState(void)
{
    MouseDX = 0.0f;
    MouseDY = 0.0f;
    MouseStat = 0;
    LastMouseX = 0;
    LastMouseY = 0;
    LastMouseSt = 0;
    // Also clear SCC-visible queue
    MouseX = 0;
    MouseY = 0;
}

