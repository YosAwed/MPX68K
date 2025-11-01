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
#include "scc.h"
#include <sys/time.h>
#include <math.h>

float	MouseDX = 0;
float	MouseDY = 0;
BYTE	MouseStat = 0;
BYTE	MouseSW = 1;

POINT	CursorPos;
int	mousex = 0, mousey = 0;

// Button state queue to preserve rapid press/release transitions for VS.X
#define BTNQ_CAP 32
static BYTE ButtonStQueue[BTNQ_CAP];
static int BtnQHead = 0, BtnQTail = 0, BtnQCount = 0;

static void BtnQ_Enq(BYTE st)
{
    if (BtnQCount >= BTNQ_CAP) {
        // drop oldest
        BtnQHead = (BtnQHead + 1) % BTNQ_CAP;
        BtnQCount--;
    }
    ButtonStQueue[BtnQTail] = st;
    BtnQTail = (BtnQTail + 1) % BTNQ_CAP;
    BtnQCount++;
}

static int BtnQ_Deq(BYTE* st)
{
    if (BtnQCount == 0) return 0;
    *st = ButtonStQueue[BtnQHead];
    BtnQHead = (BtnQHead + 1) % BTNQ_CAP;
    BtnQCount--;
    return 1;
}

// 修正: 重複送信防止のための変数を追加
static signed char LastMouseX = 0;
static signed char LastMouseY = 0;
static BYTE LastMouseSt = 0;   // last sent state
static BYTE MouseStatPrev = 0; // last observed in Mouse_Event for change detection
static int MouseDataSendCount = 0;  // 送信回数をカウント

// ダブルクリック実行中フラグ（現在ロジックでは未使用。互換のため残置）
static int DoubleClickInProgress = 0;
static int g_mouse_edgelog = 0; // env SCC_MOUSE_EDGELOG=1

// 互換モード: オリジナルpx68kに合わせて簡素化した動作に切り替える
static int g_mouse_compat_mode = 0;

// Forward decl for edge logging timestamp
static double GetCurrentTimeMs(void);


void Mouse_Init(void)
{
	if (Config.JoyOrMouse) {
		Mouse_StartCapture(1);
	}
	
	// 修正: 初期化時に前回値もリセット
	LastMouseX = 0;
	LastMouseY = 0;
	LastMouseSt = 0;
    MouseStatPrev = 0;
    MouseDataSendCount = 0;
    BtnQHead = BtnQTail = BtnQCount = 0;

    const char* edgeEnv = getenv("SCC_MOUSE_EDGELOG");
    if (edgeEnv && (edgeEnv[0]=='1'||edgeEnv[0]=='t'||edgeEnv[0]=='T'||edgeEnv[0]=='y'||edgeEnv[0]=='Y')) {
        g_mouse_edgelog = 1;
        printf("[mouse.c] edge log enabled\n");
    }
}


// ----------------------------------
//	Mouse Event Occured
// ----------------------------------
// Reduce verbose logging by default
// #define MOUSE_DEBUG 1
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
            // Latch only on actual edge
            if (MouseStat != MouseStatPrev) {
                if (g_mouse_edgelog) {
                    double t = GetCurrentTimeMs();
                    printf("[mouse.c] EDGE t=%.3f st=0x%02X\n", t, MouseStat);
                }
                SCC_LatchMouseStatus(MouseStat, 0, 0);
                if (!g_mouse_compat_mode) { BtnQ_Enq(MouseStat); }
                MouseStatPrev = MouseStat;
            }
            break;
        case 2:	// right button
            if (dx != 0)
                MouseStat |= 2;
            else
                MouseStat &= 0xfd;
            // Latch only on actual edge
            if (MouseStat != MouseStatPrev) {
                if (g_mouse_edgelog) {
                    double t = GetCurrentTimeMs();
                    printf("[mouse.c] EDGE t=%.3f st=0x%02X\n", t, MouseStat);
                }
                SCC_LatchMouseStatus(MouseStat, 0, 0);
                if (!g_mouse_compat_mode) { BtnQ_Enq(MouseStat); }
                MouseStatPrev = MouseStat;
            }
            break;
		default:
			break;
		}

        // 旧ロジックに合わせ、ボタン状態は即時にMouseStatへ反映のみ（キューなし）
#ifdef MOUSE_DEBUG
        printf("[mouse.c] Event param=%d dx=%.3f dy=%.3f -> MouseDX=%.3f MouseDY=%.3f MouseStat=0x%02X\n",
               param, dx, dy, MouseDX, MouseDY, MouseStat);
#endif
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
    // 旧ロジックに合わせた単純送出（必要最低限の重複抑止のみ）
	POINT pt;
	int x, y;

	if (MouseSW) {

	// 互換モードではキューを使わず、その時点の状態をそのままサンプルする
	if (g_mouse_compat_mode) {
		x = (int)MouseDX;
		y = (int)MouseDY;
		MouseDX = 0.0f;
		MouseDY = 0.0f;
		MouseSt = MouseStat;
	} else {
		// If there is a queued button state, emit it as a button-only packet
		BYTE queued;
		int buttonOnly = BtnQ_Deq(&queued);
		if (buttonOnly) {
			x = 0; y = 0;
			MouseSt = queued;
		} else {
			// Round to nearest so small movements materialize without needing accumulator
			x = (int)lrintf(MouseDX);
			y = (int)lrintf(MouseDY);

			// Clear accumulators to avoid residual drift/inertia
			MouseDX = 0.0f;
			MouseDY = 0.0f;
			// 現在のボタン状態をそのまま送る
			MouseSt = MouseStat;
		}
	}

        // ダブルクリック抑制中は移動量のみ無効化（ボタンは送る）
        if (DoubleClickInProgress && (x != 0 || y != 0)) {
            x = 0;
            y = 0;
        }

        // 変化がなければ送らない（移動0かつボタン状態同一）
        // 互換モードではオリジナル準拠として抑制しない（常に現在値を報告）
        if (!g_mouse_compat_mode) {
            if (x == 0 && y == 0 && MouseSt == LastMouseSt) {
                MouseX = 0;
                MouseY = 0;
                return;
            }
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

#ifdef MOUSE_DEBUG
        printf("[mouse.c] SetData x=%d y=%d -> MouseX=%d MouseY=%d MouseSt=0x%02X (sent=%d)\n",
               x, y, MouseX, MouseY, MouseSt, MouseDataSendCount);
#endif

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

void Mouse_SetCompatMode(int enable)
{
    g_mouse_compat_mode = (enable ? 1 : 0);
    // トグル時は状態をクリアして履歴に引きずられないようにする
    BtnQHead = BtnQTail = BtnQCount = 0;
    LastMouseX = 0;
    LastMouseY = 0;
    LastMouseSt = 0;
    MouseStatPrev = MouseStat;
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
    MouseStatPrev = 0;
    BtnQHead = BtnQTail = BtnQCount = 0;
    // Also clear SCC-visible queue
    MouseX = 0;
    MouseY = 0;
}

// ダブルクリック抑制フラグの制御（移動量のみ抑止）
void Mouse_SetDoubleClickInProgress(int flag)
{
    DoubleClickInProgress = flag;
}
