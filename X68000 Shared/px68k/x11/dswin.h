#ifndef dswin_h__
#define dswin_h__

#include "common.h"

typedef struct {
	unsigned long ratebase;
	long preCounter;
	long bufferBytes;
	long dataBytes;
	long freeBytes;
	long readOffset;
	long writeOffset;
	unsigned int lastCallbackBytes;
	unsigned int refillCount;
	unsigned int directCallback;
} DSoundMonitorState;

int DSound_Init(unsigned long rate, unsigned long length);
int DSound_Cleanup(void);

void DSound_Play(void);
void DSound_Stop(void);
void FASTCALL DSound_Send0(long clock);

void DS_SetVolumeOPM(long vol);
void DS_SetVolumeADPCM(long vol);
void DS_SetVolumeMercury(long vol);
void DSound_GetMonitorState(DSoundMonitorState* state);

#endif /* dswin_h__ */
