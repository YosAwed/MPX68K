#ifndef _winx68k_scc
#define _winx68k_scc

#include "common.h"

void SCC_IntCheck(void);
void SCC_Init(void);
BYTE FASTCALL SCC_Read(DWORD adr);
void FASTCALL SCC_Write(DWORD adr, BYTE data);

// Enhanced serial bridge (C signatures)
int SCC_SetMode(int mode, const char* config);
void SCC_CloseSerial(void);
const char* SCC_GetSlavePath(void);

// Compatibility toggle: emulate original px68k mouse SCC behavior
void SCC_SetCompatMode(int enable);
int SCC_GetCompatMode(void);

// Latch mouse status edges for robust delivery on RTS rising
void SCC_LatchMouseStatus(unsigned char st, signed char x, signed char y);

extern signed char MouseX;
extern signed char MouseY;
extern BYTE MouseSt;

#endif
