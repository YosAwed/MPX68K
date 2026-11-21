// DC blocking filter for the mixed OPM + ADPCM output.
//
// The real X68000 couples its audio output through a capacitor, so no DC
// reaches the speakers. The MSM6258 decoder accumulates rounding error and
// can stay off-centre (drivers that stream silence keep it there, and PCM8A
// restarts can shift it), so the emulated mix carries a DC offset that the
// hardware never outputs. A first-order high-pass at about 10 Hz removes it
// without touching the audible band.
//
//   y[n] = x[n] - x[n-1] + r * y[n-1],   r = exp(-2 * pi * fc / fs)
#ifndef _winx68k_dc_block
#define _winx68k_dc_block

#include <math.h>

#define DC_BLOCK_CUTOFF_HZ 10.0

typedef struct {
	float r;
	float x1[2];
	float y1[2];
} DCBlock;

static inline void DCBlock_Init(DCBlock *f, unsigned long sampleRate)
{
	f->r = (sampleRate > 0)
		? (float)exp(-2.0 * M_PI * DC_BLOCK_CUTOFF_HZ / (double)sampleRate)
		: 1.0f;
	f->x1[0] = f->x1[1] = 0.0f;
	f->y1[0] = f->y1[1] = 0.0f;
}

// ch: 0 = left, 1 = right. Returns the filtered sample, clipped to 16 bits.
static inline short DCBlock_Process(DCBlock *f, int ch, int x)
{
	float y = (float)x - f->x1[ch] + f->r * f->y1[ch];
	f->x1[ch] = (float)x;
	f->y1[ch] = y;
	if (y > 32767.0f) return 32767;
	if (y < -32768.0f) return -32768;
	return (short)lrintf(y);
}

#endif
