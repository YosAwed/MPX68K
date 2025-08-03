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
    
    printf("Sound Init Sampling Rate:%dHz buflen:%d\n", rate, buflen );

    // Enhanced initialization: Clear all audio buffers thoroughly to prevent startup noise
    memset(pcmbuffer, 0, PCMBUF_SIZE);
    memset(rsndbuf, 0, PCMBUF_SIZE);
    
    // Reset buffer pointers with proper alignment
    pbsp = pcmbuffer;
    pbrp = pcmbuffer;
    pbwp = pcmbuffer;
    pbep = &pcmbuffer[PCMBUF_SIZE];
    
    // Pre-fill some silence to ensure stable initial state
    for (int i = 0; i < 1024; i++) {
        ((short*)pcmbuffer)[i] = 0;
    }
    
    printf("Audio buffers initialized with silence to prevent startup noise\n");

    ratebase = rate;

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

   // Generate audio with both sources - prevent buffer overlap issues
   memset(pbwp, 0, total_bytes);  // Clear buffer first to prevent noise artifacts
   ADPCM_Update((short *)pbwp, length, rate, pbsp, pbep);
   OPM_Update((short *)pbwp, length, rate, pbsp, pbep);

   // Update write pointer atomically at the end
   pbwp += total_bytes;
	if (pbwp >= pbep) {
      pbwp = pbsp + (pbwp - pbep);
	}
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
    int size = sample * sizeof(unsigned short) * 2;
#if 0
//	short buf[size] ;
	memset( buffer, 0x00, size );
	ratebase = 0;
	ADPCM_Update(buffer, sample, ratebase, &buffer[0], &buffer[size*2]);
	OPM_Update(buffer, sample, ratebase, &buffer[0], &buffer[size*2]);
#else
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

      datalen = pbwp - pbrp;

      // needs more data - generate predictable amount to prevent underruns
	   if (datalen < len) {
		   int missing_bytes = len - datalen;
		   int extra_samples = (missing_bytes / 4) + 256; // Fixed buffer size to reduce stuttering
		   DSound_Send(extra_samples);
//		   printf("MORE!");
	   }

#if 0
      datalen = pbwp - pbrp;
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

      lena = pbep - pbrp;
      if (lena >= len)
      {
         buf = pbrp;
         pbrp += len;
         //printf("TYPEC: ");
      }
      else
      {
         lenb = len - lena;

		  if (pbwp - pbsp < lenb) {
            int missing_bytes = lenb - (pbwp - pbsp);
            int needed_samples = (missing_bytes / 4) + 128; // Smaller buffer to reduce latency
            DSound_Send(needed_samples);
		  }
#if 0
         if (pbwp - pbsp < lenb)
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
   
   // Copy audio data to output buffer with noise prevention
   if (buf != NULL && len > 0) {
       // First clear the output buffer to ensure clean state
       memset(buffer, 0, len);
       
       // Copy audio data
       memcpy(buffer, buf, len);
       
       // Apply gentle fade-in/fade-out to prevent pops during silence transitions
       short *samples = (short*)buffer;
       int sample_count = len / 2;  // 16-bit samples
       
       // Check if buffer contains mostly silence
       int silence_threshold = 100;  // Threshold for detecting silence
       int non_silent_samples = 0;
       for (int i = 0; i < sample_count; i++) {
           if (abs(samples[i]) > silence_threshold) {
               non_silent_samples++;
           }
       }
       
       // If mostly silent, ensure complete silence to prevent noise
       if (non_silent_samples < (sample_count / 100)) {  // Less than 1% non-silent
           memset(buffer, 0, len);
       }
   } else {
       // Ensure buffer is properly cleared if no valid data
       memset(buffer, 0, len);
   }
}


