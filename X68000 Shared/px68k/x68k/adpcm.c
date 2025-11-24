// ---------------------------------------------------------------------------------------
//  ADPCM.C - ADPCM (OKI MSM6258V)
//    な〜んか、X68Sound.dllに比べてカシャカシャした音になるんだよなぁ……
//    DSoundのクセってのもあるけど、それだけじゃなさそうな気もする
// ---------------------------------------------------------------------------------------

#include <math.h>

#include "common.h"
#include "prop.h"
#include "pia.h"
#include "adpcm.h"
#include "dmac.h"
#include "adpcm_optimized.h"

// Lightweight debug counters for ADPCM I/O tracing
static int adpcm_cmd_log_count = 0;
static int adpcm_data_log_count = 0;

// Forward declarations for optimized functions
#if ADPCM_ENABLE_OPTIMIZATIONS
static void ADPCM_InitTable_Optimized(void);
static void ADPCM_WriteOne_Optimized(int val);
#endif

#define ADPCM_BufSize      96000
#define ADPCMMAX           2047
#define ADPCMMIN          -2048
#define FM_IPSCALE         256L

#define OVERSAMPLEMUL      2

// Optional: use XEiJ-style PCM repeat / interval timer instead of legacy PreUpdate logic
// 0 = keep existing behavior, 1 = drive DMA channel 3 with a dedicated PCM ticker
// 現状は安全のためデフォルトで無効化しておく
#define ADPCM_USE_PCM_TICKER 0

#define INTERPOLATE(y, x)	\
	(((((((-y[0]+3*y[1]-3*y[2]+y[3]) * x + FM_IPSCALE/2) / FM_IPSCALE \
	+ 3 * (y[0]-2*y[1]+y[2])) * x + FM_IPSCALE/2) / FM_IPSCALE \
	- 2*y[0]-3*y[1]+6*y[2]-y[3]) * x + 3*FM_IPSCALE) / (6*FM_IPSCALE) + y[1])

static int ADPCM_VolumeShift = 65536;
static const int index_shift[16] = {
	-1*16, -1*16, -1*16, -1*16, 2*16, 4*16, 6*16, 8*16,
	-1*16, -1*16, -1*16, -1*16, 2*16, 4*16, 6*16, 8*16 };
static const int ADPCM_Clocks[8] = {
	93750, 125000, 187500, 125000, 46875, 62500, 93750, 62500 };
static int dif_table[49*16];
static signed short ADPCM_BufR[ADPCM_BufSize];
static signed short ADPCM_BufL[ADPCM_BufSize];

// -----------------------------------------------------------------------
//   PCM timing (XEiJ-style, 62500Hz base)
//   PCM_SAMPLE_REPEAT 相当のテーブルと、専用タイマ状態
// -----------------------------------------------------------------------
static const int ADPCM_PCM_SAMPLE_FREQ = 62500;
// 8MHz/4MHz×分周比の組み合わせに対応する繰り返し回数
//   XEiJ.ADPCM.PCM_SAMPLE_REPEAT の先頭 8 要素に対応
static const int ADPCM_PCM_SAMPLE_REPEAT[8] = {
	8, 6, 4, 6,   // 8MHz / 1024, 768, 512, 768
	16, 12, 8, 12 // 4MHz / 1024, 768, 512, 768
};

// 現在のクロックモードに対する繰り返し回数と、1 データ(2 サンプル)毎の間隔
// 間隔は「10MHz 相当クロック」単位とし、PreUpdate に渡される clock をそのまま使う
static DWORD ADPCM_pcmRepeat = 8;
static DWORD ADPCM_pcmIntervalCycles = 0;
static DWORD ADPCM_pcmAcc = 0;

static long ADPCM_WrPtr = 0;
static long ADPCM_RdPtr = 0;
static DWORD ADPCM_SampleRate = 44100*12;
       DWORD ADPCM_ClockRate = 7800*12;
static DWORD ADPCM_Count = 0;
static int ADPCM_Step = 0;
static int ADPCM_Out = 0;
static BYTE ADPCM_Playing = 0;
       BYTE ADPCM_Clock = 0;
static int ADPCM_PreCounter = 0;
static int ADPCM_DifBuf = 0;


static int ADPCM_Pan = 0x00;
static int OldR = 0, OldL = 0;
static int Outs[8];
static int OutsIp[4];
static int OutsIpR[4];
static int OutsIpL[4];

// DC blocking filter state (1st order high-pass)
static double dc_filter_x1 = 0.0;  // Previous input
static double dc_filter_y1 = 0.0;  // Previous output
static const double DC_FILTER_ALPHA = 0.995;  // High-pass cutoff ~3.5Hz at 22kHz

// -----------------------------------------------------------------------
//   helper: 原発振周波数と分周比に応じて PCM リピートと間隔を更新
// -----------------------------------------------------------------------
static void ADPCM_UpdateRepeatInterval(void)
{
	int idx = ADPCM_Clock & 0x07;
	if (idx < 0 || idx >= 8) {
		idx = 0;
	}

	ADPCM_pcmRepeat = (DWORD)ADPCM_PCM_SAMPLE_REPEAT[idx];

	// XEiJ では:
	//   pcmInterval (TMR_FREQ 単位) = (1 / 62500Hz) * 2 * pcmRepeat
	//   DMAC は 10MHz 相当なので、1 データ毎の DMA 要求間隔は
	//     10^7 / 62500 * 2 * repeat = 320 * repeat [cycle]
	ADPCM_pcmIntervalCycles = (DWORD)(320 * ADPCM_pcmRepeat);

	// 専用タイマは ADPCM_Init でリセットされるのでここでは蓄積値は触らない
}

int ADPCM_IsReady(void)
{
	return 1;
}


// -----------------------------------------------------------------------
//   てーぶる初期化
// -----------------------------------------------------------------------
static void ADPCM_InitTable(void)
{
#if ADPCM_ENABLE_OPTIMIZATIONS && ADPCM_OPTIMIZATION_LEVEL >= 1
	ADPCM_InitTable_Optimized();
#endif

	int step, n;
	double val;
	static int bit[16][4] =
	{
		{ 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
		{ 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
		{-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
		{-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
	};

	for (step=0; step<=48; step++) {
		val = floor(16.0 * pow ((double)1.1, (double)step));
		for (n=0; n<16; n++) {
			dif_table[step*16+n] = bit[n][0] *
			   (int)(val   * bit[n][1] +
				 val/2 * bit[n][2] +
				 val/4 * bit[n][3] +
				 val/8);
		}
	}
}


#define LimitMix(val) { \
	if ( val > 0x7fff )      val = 0x7fff; \
	else if ( val < -0x8000 ) val = -0x8000; \
}

// -----------------------------------------------------------------------
//   MPUクロック経過分だけバッファにデータを溜めておく
// -----------------------------------------------------------------------
void FASTCALL ADPCM_PreUpdate(DWORD clock)
{
	/*if (!ADPCM_Playing) return;*/
#if ADPCM_USE_PCM_TICKER
	// XEiJ の sndPcmTicker 相当:
	// clock は「今回のラインで経過した 10MHz クロック数」(Δt) として扱い、
	// ADPCM_pcmIntervalCycles ごとに DMA チャンネル 3 にデータを要求する
	if (!ADPCM_Playing || ADPCM_pcmIntervalCycles == 0) {
		return;
	}

	ADPCM_pcmAcc += clock;
	while (ADPCM_pcmAcc >= ADPCM_pcmIntervalCycles) {
		ADPCM_pcmAcc -= ADPCM_pcmIntervalCycles;
		DMA_Exec(3);
	}
#else
	ADPCM_PreCounter += ((ADPCM_ClockRate/24)*clock);
	while ( ADPCM_PreCounter>=10000000L ) {		// ↓ データの送りすぎ防止（A-JAX）。200サンプリングくらいまでは許そう…。
		ADPCM_DifBuf -= ( (ADPCM_SampleRate*400)/ADPCM_ClockRate );
		if ( ADPCM_DifBuf<=0 ) {
			ADPCM_DifBuf = 0;
			DMA_Exec(3);
		}
		ADPCM_PreCounter -= 10000000L;
	}
#endif
}


// -----------------------------------------------------------------------
//   DSoundが指定してくる分だけバッファにデータを書き出す
// -----------------------------------------------------------------------
void FASTCALL ADPCM_Update(signed short *buffer, DWORD length, int rate, BYTE *pbsp, BYTE *pbep)
{
	int outs;
	signed int outl, outr;
	
	// Debug: track output amplitude for specific clock modes (e.g. Phalanx logo)
	static int adpcm_dbg_prints = 0;
	int local_minL =  32767;
	int local_maxL = -32768;
	int local_minR =  32767;
	int local_maxR = -32768;
	int sample_count = 0;

	if ( length<=0 ) return;

	while ( length ) {
		if (buffer >= (signed short *)pbep) {
			buffer = (signed short *)pbsp;
		}
		int tmpl, tmpr;

		if ( (ADPCM_WrPtr==ADPCM_RdPtr)&&(!(DMA[3].CCR&0x40)) ) DMA_Exec(3);
		if ( ADPCM_WrPtr!=ADPCM_RdPtr ) {
			OldR = outr = ADPCM_BufL[ADPCM_RdPtr];
			OldL = outl = ADPCM_BufR[ADPCM_RdPtr];
			ADPCM_RdPtr++;
			if ( ADPCM_RdPtr>=ADPCM_BufSize ) ADPCM_RdPtr = 0;
		} else {
			// Fix: Output silence when no audio data is available
			// to prevent periodic noise from old audio data
			if ( !ADPCM_Playing ) {
				outr = 0;
				outl = 0;
				OldR = 0;
				OldL = 0;
			} else {
				outr = OldR;
				outl = OldL;
			}
		}

		if ( Config.Sound_LPF ) {
			outr = (int)(outr*40*ADPCM_VolumeShift);
			outs = (outr + Outs[3]*2 + Outs[2] + Outs[1]*157 - Outs[0]*61) >> 8;
			Outs[2] = Outs[3];
			Outs[3] = outr;
			Outs[0] = Outs[1];
			Outs[1] = outs;
		} else {
			outs = (int)(outr*ADPCM_VolumeShift);
		}

		OutsIpR[0] = OutsIpR[1];
		OutsIpR[1] = OutsIpR[2];
		OutsIpR[2] = OutsIpR[3];
		OutsIpR[3] = outs;

		if ( Config.Sound_LPF ) {
			outl = (int)(outl*40*ADPCM_VolumeShift);
			outs = (outl + Outs[7]*2 + Outs[6] + Outs[5]*157 - Outs[4]*61) >> 8;
			Outs[6] = Outs[7];
			Outs[7] = outl;
			Outs[4] = Outs[5];
			Outs[5] = outs;
		} else {
			outs = (int)(outl*ADPCM_VolumeShift);
		}

		OutsIpL[0] = OutsIpL[1];
		OutsIpL[1] = OutsIpL[2];
		OutsIpL[2] = OutsIpL[3];
		OutsIpL[3] = outs;

#if 1
		tmpr = INTERPOLATE(OutsIpR, 0);
		if ( tmpr>32767 ) tmpr = 32767; else if ( tmpr<(-32768) ) tmpr = -32768;
		if (tmpr < local_minR) local_minR = tmpr;
		if (tmpr > local_maxR) local_maxR = tmpr;
		*(buffer++) = (short)tmpr;
		tmpl = INTERPOLATE(OutsIpL, 0);
		if ( tmpl>32767 ) tmpl = 32767; else if ( tmpl<(-32768) ) tmpl = -32768;
		if (tmpl < local_minL) local_minL = tmpl;
		if (tmpl > local_maxL) local_maxL = tmpl;
		*(buffer++) = (short)tmpl;
		sample_count++;
		// PSP以外はrateは0
		if (rate == 22050) {
			if (buffer >= (signed short *)pbep) {
				buffer = (signed short *)pbsp;
			}
			*(buffer++) = (short)tmpr;
			*(buffer++) = (short)tmpl;
		} else if (rate == 11025) {
			if (buffer >= (signed short *)pbep) {
				buffer = (signed short *)pbsp;
			}
			*(buffer++) = (short)tmpr;
			*(buffer++) = (short)tmpl;
			if (buffer >= (signed short *)pbep) {
				buffer = (signed short *)pbsp;
			}
			*(buffer++) = (short)tmpr;
			*(buffer++) = (short)tmpl;
			if (buffer >= (signed short *)pbep) {
				buffer = (signed short *)pbsp;
			}
			*(buffer++) = (short)tmpr;
			*(buffer++) = (short)tmpl;
		}
#else
		*(buffer++) = (short)OutsIpR[3];
		*(buffer++) = (short)OutsIpL[3];
#endif

		length--;
	}

	ADPCM_DifBuf = (int)(ADPCM_WrPtr-ADPCM_RdPtr);
	if ( ADPCM_DifBuf<0 ) ADPCM_DifBuf += ADPCM_BufSize;

	// Print debug info for the first few calls while PCM clock mode == 0
	if (ADPCM_Clock == 0 && adpcm_dbg_prints < 16 && sample_count > 0) {
		printf("ADPCM DBG: clk=0 playing=%d samples=%d L[min=%d max=%d] R[min=%d max=%d] Wr=%ld Rd=%ld Dif=%d\n",
		       ADPCM_Playing, sample_count, local_minL, local_maxL, local_minR, local_maxR,
		       ADPCM_WrPtr, ADPCM_RdPtr, ADPCM_DifBuf);
		adpcm_dbg_prints++;
	}
}


// -----------------------------------------------------------------------
//   1nibble（4bit）をデコード
// -----------------------------------------------------------------------
INLINE void ADPCM_WriteOne(int val)
{
#if ADPCM_ENABLE_OPTIMIZATIONS && ADPCM_OPTIMIZATION_LEVEL >= 1
	ADPCM_WriteOne_Optimized(val);
	return;
#endif

	ADPCM_Out += dif_table[ADPCM_Step+val];
	if ( ADPCM_Out>ADPCMMAX ) ADPCM_Out = ADPCMMAX; else if ( ADPCM_Out<ADPCMMIN ) ADPCM_Out = ADPCMMIN;
	
	ADPCM_Step += index_shift[val];
	if ( ADPCM_Step>(48*16) ) ADPCM_Step = (48*16); else if ( ADPCM_Step<0 ) ADPCM_Step = 0;

	if ( OutsIp[0]==-1 ) {
		OutsIp[0] =
		OutsIp[1] =
		OutsIp[2] =
		OutsIp[3] = ADPCM_Out;
	} else {
		OutsIp[0] = OutsIp[1];
		OutsIp[1] = OutsIp[2];
		OutsIp[2] = OutsIp[3];
		OutsIp[3] = ADPCM_Out;
	}

	while ( ADPCM_SampleRate>ADPCM_Count ) {
		if ( ADPCM_Playing ) {
			int ratio = (((ADPCM_Count/100)*FM_IPSCALE)/(ADPCM_SampleRate/100));
			int tmp = INTERPOLATE(OutsIp, ratio);
			if ( tmp>ADPCMMAX ) tmp = ADPCMMAX; else if ( tmp<ADPCMMIN ) tmp = ADPCMMIN;
			if ( !(ADPCM_Pan&1) )
				ADPCM_BufR[ADPCM_WrPtr] = (short)tmp;
			else
				ADPCM_BufR[ADPCM_WrPtr] = 0;
			if ( !(ADPCM_Pan&2) )
				ADPCM_BufL[ADPCM_WrPtr++] = (short)tmp;
			else
				ADPCM_BufL[ADPCM_WrPtr++] = 0;
			if ( ADPCM_WrPtr>=ADPCM_BufSize ) ADPCM_WrPtr = 0;
		}
		ADPCM_Count += ADPCM_ClockRate;
	}
	ADPCM_Count -= ADPCM_SampleRate;
}


// -----------------------------------------------------------------------
//   I/O Write
// -----------------------------------------------------------------------
void FASTCALL ADPCM_Write(DWORD adr, BYTE data)
{
	if ( adr==0xe92001 ) {
		// Trace ADPCM command writes (start/stop)
		if (adpcm_cmd_log_count < 256) {
			printf("ADPCM CMD #%3d: data=%02X playing=%d clock=%u pan=%02X\n",
			       adpcm_cmd_log_count++, data, ADPCM_Playing, (unsigned)ADPCM_Clock, (unsigned)ADPCM_Pan);
		}
		if ( data&1 ) {
			ADPCM_Playing = 0;
			// Original behavior: only clear last outputs to avoid clicks
			OldL = OldR = 0;
		} else if ( data&2 ) {
			if ( !ADPCM_Playing ) {
				ADPCM_Step = 0;
				ADPCM_Out = 0;
				// Fix: Initialize with silence instead of -2 to prevent startup noise
				OldL = OldR = 0;
				ADPCM_Playing = 1;
			}
			OutsIp[0] = OutsIp[1] = OutsIp[2] = OutsIp[3] = -1;
		}
	} else if ( adr==0xe92003 ) {
		// Trace ADPCM data writes (first few only, to avoid flooding)
		if (adpcm_data_log_count < 512) {
			printf("ADPCM DAT #%3d: data=%02X playing=%d\n",
			       adpcm_data_log_count++, data, ADPCM_Playing);
		}
		if ( ADPCM_Playing ) {
			ADPCM_WriteOne((int)(data&15));
			ADPCM_WriteOne((int)((data>>4)&15));
		}
	}
}


// -----------------------------------------------------------------------
//   I/O Read（ステータスチェック）
// -----------------------------------------------------------------------
BYTE FASTCALL ADPCM_Read(DWORD adr)
{
	if ( adr==0xe92001 )
		return ((ADPCM_Playing)?0xc0:0x40);
	else
		return 0x00;
}


// -----------------------------------------------------------------------
//   ぼりゅーむ
// -----------------------------------------------------------------------
void ADPCM_SetVolume(BYTE vol)
{
	if ( vol>16 ) vol=16;
//	if ( vol<0  ) vol=0;

	if ( vol )
		ADPCM_VolumeShift = (int)((double)16/pow(1.189207115, (16-vol)));
	else
		ADPCM_VolumeShift = 0;		// Mute
}


// -----------------------------------------------------------------------
//   Panning
// -----------------------------------------------------------------------
void ADPCM_SetPan(int n)
{
	if ( (ADPCM_Pan&0x0c)!=(n&0x0c) ) {
		ADPCM_Count = 0;
		ADPCM_Clock = (ADPCM_Clock&4)|((n>>2)&3);
		ADPCM_ClockRate = ADPCM_Clocks[ADPCM_Clock];
		ADPCM_UpdateRepeatInterval();
	}
	ADPCM_Pan = n;
}


// -----------------------------------------------------------------------
//   Clock
// -----------------------------------------------------------------------
void ADPCM_SetClock(int n)
{
	if ( (ADPCM_Clock&4)!=n ) {
		ADPCM_Count = 0;
		ADPCM_Clock = n|((ADPCM_Pan>>2)&3);
		ADPCM_ClockRate = ADPCM_Clocks[ADPCM_Clock];
		ADPCM_UpdateRepeatInterval();
	}
}


// -----------------------------------------------------------------------
//   初期化
// -----------------------------------------------------------------------
void ADPCM_Init(DWORD samplerate)
{
	ADPCM_WrPtr = 0;
	ADPCM_RdPtr = 0;
	ADPCM_Out = 0;
	ADPCM_Step = 0;
	ADPCM_Playing = 0;
	ADPCM_SampleRate = (samplerate*12);
	ADPCM_PreCounter = 0;
	ADPCM_pcmAcc = 0;
	memset(Outs, 0, sizeof(Outs));
	OutsIp[0] = OutsIp[1] = OutsIp[2] = OutsIp[3] = -1;
	OutsIpR[0] = OutsIpR[1] = OutsIpR[2] = OutsIpR[3] = 0;
	OutsIpL[0] = OutsIpL[1] = OutsIpL[2] = OutsIpL[3] = 0;
	OldL = OldR = 0;

	ADPCM_SetPan(0x0b);
	ADPCM_UpdateRepeatInterval();
	ADPCM_InitTable();
}

// Include optimized implementations
#if ADPCM_ENABLE_OPTIMIZATIONS
#include "adpcm_optimized_safe.c"
#endif
