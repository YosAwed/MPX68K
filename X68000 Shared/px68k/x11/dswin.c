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

#include    "windows.h"
#include    "common.h"
#include    "dswin.h"
#include    "prop.h"
#include    "adpcm.h"
//#include    "mercury.h"
#include    "fmg_wrap.h"

// Use a direct mixing path from the audio callback
// instead of the legacy DSound ring buffer.
#define DSOUND_USE_DIRECT_CALLBACK 1

#define PCMBUF_SIZE 2*2*48000
BYTE pcmbuffer[PCMBUF_SIZE];
BYTE *pcmbufp = pcmbuffer;
BYTE *pbsp = pcmbuffer;
BYTE *pbrp = pcmbuffer, *pbwp = pcmbuffer;
BYTE *pbep = &pcmbuffer[PCMBUF_SIZE];
DWORD ratebase = 44100;
long DSound_PreCounter = 0;
BYTE rsndbuf[PCMBUF_SIZE];


void audio_callback(void *buffer, int len);


int DSound_Init(unsigned long rate, unsigned long buflen)
{
//    DWORD samples;
    
    printf("Sound Init Sampling Rate:%luHz buflen:%lu\n", rate, buflen );

    // Fix: Initialize audio buffers to silence to prevent noise
    memset(pcmbuffer, 0, PCMBUF_SIZE);
    memset(rsndbuf, 0, PCMBUF_SIZE);
    
    // Reset buffer pointers
    pbsp = pcmbuffer;
    pbrp = pcmbuffer;
    pbwp = pcmbuffer;
    pbep = &pcmbuffer[PCMBUF_SIZE];

    ratebase = (DWORD)rate;

    return TRUE;
}

void
DSound_Play(void)
{
	ADPCM_SetVolume((BYTE)Config.PCM_VOL);
	OPM_SetVolume((BYTE)Config.OPM_VOL);
}

void
DSound_Stop(void)
{
	ADPCM_SetVolume(0);
	OPM_SetVolume(0);
}

int
DSound_Cleanup(void)
{
    return TRUE;
}

static void sound_send(int length)
{
    // In direct-callback mode, we generate audio
    // exclusively from X68000_AudioCallBack().
#if DSOUND_USE_DIRECT_CALLBACK
    (void)length;
    return;
#else
    int rate = ratebase;
	if ( ratebase == 22050 ) {
		rate = 0;   // 0にしないとおかしい！
	}

   // Fix: More defensive approach to prevent race conditions
   BYTE *write_start = pbwp;
   int total_bytes = length * sizeof(WORD) * 2;
   
   // Calculate if the write would wrap around the buffer
   BYTE *write_end = write_start + total_bytes;
   if (write_end > pbep) {
       // Handle wrap-around case - clear in two parts
       int first_part = pbep - write_start;
       int second_part = total_bytes - first_part;
       
       // Clear first part
       memset(write_start, 0, first_part);
       // Clear second part (from start of buffer)
       memset(pbsp, 0, second_part);
   } else {
       // Simple case - clear continuous area
       memset(write_start, 0, total_bytes);
   }

   // Generate audio with both sources
   ADPCM_Update((short *)pbwp, length, rate, pbsp, pbep);
   OPM_Update((short *)pbwp, length, rate, pbsp, pbep);

   // Update write pointer atomically at the end
   pbwp += total_bytes;
	if (pbwp >= pbep) {
      pbwp = pbsp + (pbwp - pbep);
	}
 #endif
}

void FASTCALL DSound_Send0(long clock)
{
    int length = 0;
    int rate;


#if 1
	DSound_PreCounter += (ratebase * clock);
    while (DSound_PreCounter >= 10000000L)
   {
        length++;
        DSound_PreCounter -= 10000000L;
    }

    if (length == 0)
        return;
#else
#endif
//	printf("%d %d\n", length, DSound_PreCounter);
    sound_send(length);
}

static void FASTCALL DSound_Send(int length)
{
    sound_send(length);
}

void X68000_AudioCallBack(void* buffer, const unsigned int sample)
{
#if DSOUND_USE_DIRECT_CALLBACK
    // Direct mixing path: generate ADPCM and OPM into separate
    // temporary buffers, then mix into the host buffer. This
    // prevents OPM_Update() from overwriting ADPCM-only regions.

    // Safety limit for temporary buffers
    enum { DSOUND_MAX_FRAMES = 4096 };
    static short adpcmBuf[DSOUND_MAX_FRAMES * 2];
    static short opmBuf[DSOUND_MAX_FRAMES * 2];

    unsigned int frames = sample;
    if (frames > DSOUND_MAX_FRAMES) {
        frames = DSOUND_MAX_FRAMES;
    }

    unsigned int bytesPerFrame = sizeof(short) * 2; // stereo
    unsigned int bufBytes = frames * bytesPerFrame;

    // Clear temporary buffers
    memset(adpcmBuf, 0x00, bufBytes);
    memset(opmBuf, 0x00, bufBytes);

    // PSP 以外は rate == 0 を渡すのが元の実装
    int rate = 0;

    // Generate ADPCM into temporary buffer
    ADPCM_Update(adpcmBuf, frames, rate,
                 (BYTE *)adpcmBuf, ((BYTE *)adpcmBuf) + bufBytes);

    // Generate OPM into temporary buffer
    OPM_Update(opmBuf, frames, rate,
               (BYTE *)opmBuf, ((BYTE *)opmBuf) + bufBytes);

    // Mix into host buffer with 16-bit clipping
    short *out = (short *)buffer;
    unsigned int totalSamples = frames * 2; // stereo samples
    for (unsigned int i = 0; i < totalSamples; i++) {
        int v = (int)adpcmBuf[i] + (int)opmBuf[i];
        if (v > 32767) v = 32767;
        else if (v < -32768) v = -32768;
        out[i] = (short)v;
    }

    // If sample > frames (rare), zero the tail to avoid garbage
    if (sample > frames) {
        unsigned int tailSamples = (sample - frames) * 2;
        memset(out + totalSamples, 0x00, tailSamples * sizeof(short));
    }
#else
    int size = sample * sizeof(unsigned short) * 2;
    audio_callback(buffer, size);
#endif
}


void audio_callback(void *buffer, int len)
{
   int lena, lenb, datalen, rate;
   BYTE *buf;

cb_start:
   if (pbrp <= pbwp)
   {
      // pcmbuffer
      // +---------+-------------+----------+
      // |         |/////////////|          |
      // +---------+-------------+----------+
      // A         A<--datalen-->A          A
      // |         |             |          |
      // pbsp     pbrp          pbwp       pbep

      datalen = (int)(pbwp - pbrp);  // pcmbufferサイズ(192k)内なのでintへ明示キャスト

      // needs more data - generate extra to prevent underruns
	   if (datalen < len) {
		   int extra_samples = ((len - datalen) / 4) + 512; // Generate extra samples
		   DSound_Send(extra_samples);
//		   printf("MORE!");
	   }

#if 0
      datalen = (int)(pbwp - pbrp);  // pcmbufferサイズ(192k)内なのでintへ明示キャスト
//	   printf("%d\n",datalen);
	   if (datalen < len) {
         printf("xxxxx not enough sound data: %5d/%5d xxxxx\n",datalen, len);
	   }
#endif

      // change to TYPEC or TYPED
	   if (pbrp > pbwp) {
         goto cb_start;
	   }
	   
      buf = pbrp;
      pbrp += len;
      //printf("TYPEA: ");
   }
   else
   {
      // pcmbuffer
      // +---------+-------------+----------+
      // |/////////|             |//////////|
      // +------+--+-------------+----------+
      // <-lenb->  A             <---lena--->
      // A         |             A          A
      // |         |             |          |
      // pbsp     pbwp          pbrp       pbep

      lena = (int)(pbep - pbrp);
      if (lena >= len)
      {
         buf = pbrp;
         pbrp += len;
         //printf("TYPEC: ");
      }
      else
      {
         lenb = len - lena;

         int available = (int)(pbwp - pbsp);
         if (available < lenb) {
            int needed_samples = ((lenb - available) / 4) + 256; // Generate extra samples  
            DSound_Send(needed_samples);
         }
#if 0
         if (available < lenb)
            printf("xxxxx not enough sound data xxxxx\n");
#endif
         memcpy(rsndbuf, pbrp, lena);
         memcpy(&rsndbuf[lena], pbsp, lenb);
         buf = rsndbuf;
         pbrp = pbsp + lenb;
         //printf("TYPED: ");
      }
   }
//    printf("SND:%p %p %d\n", buffer, buf, len);
   
   // Copy audio data to output buffer (no additional silence gating)
   memcpy(buffer, buf, len);
}
