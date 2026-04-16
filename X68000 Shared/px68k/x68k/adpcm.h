#ifndef _winx68k_adpcm_h
#define _winx68k_adpcm_h

extern BYTE ADPCM_Clock;
extern DWORD ADPCM_ClockRate;

typedef struct {
	long writePtr;
	long readPtr;
	long bufferSize;
	DWORD sampleRate;
	DWORD clockRate;
	DWORD count;
	long long preCounter;
	int step;
	int output;
	int playing;
	int dmaReady;
	int diffBuffer;
	int pan;
	int volumeShift;
	int oldLeft;
	int oldRight;
} ADPCMMonitorState;

void FASTCALL ADPCM_PreUpdate(DWORD clock);
void FASTCALL ADPCM_Update(signed short *buffer, DWORD length, int rate, BYTE *pbsp, BYTE *pbep);

void FASTCALL ADPCM_Write(DWORD adr, BYTE data);
BYTE FASTCALL ADPCM_Read(DWORD adr);

void ADPCM_SetVolume(BYTE vol);
void ADPCM_SetPan(int n);
void ADPCM_SetClock(int n);

void ADPCM_Init(DWORD samplerate);
int ADPCM_IsReady(void);
void ADPCM_GetMonitorState(ADPCMMonitorState* state);

#endif
