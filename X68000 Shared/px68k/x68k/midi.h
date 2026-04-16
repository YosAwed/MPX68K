#ifndef _winx68k_midi
#define _winx68k_midi

#include "common.h"

typedef struct {
	int enabled;
	int hasOutput;
	int module;
	int ctrl;
	int pos;
	int sysCount;
	int exclusiveWait;
	int regHigh;
	int playing;
	int vector;
	int intEnable;
	int intVect;
	int intFlag;
	DWORD buffered;
	long bufTimer;
	BYTE r05;
	DWORD gTimerMax;
	DWORD mTimerMax;
	long gTimerVal;
	long mTimerVal;
	int txFull;
	int delayWrite;
	int delayRead;
	int delayCount;
	long swiftBufferSize;
} MIDIMonitorState;

void MIDI_Init(void);
void MIDI_Cleanup(void);
void MIDI_Reset(void);
BYTE FASTCALL MIDI_Read(DWORD adr);
void FASTCALL MIDI_Write(DWORD adr, BYTE data);
void MIDI_SetModule(void);
void FASTCALL MIDI_Timer(DWORD clk);
int MIDI_SetMimpiMap(char *filename);
int MIDI_EnableMimpiDef(int enable);
void MIDI_DelayOut(unsigned int delay);
void MIDI_GetMonitorState(MIDIMonitorState* state);

#endif
