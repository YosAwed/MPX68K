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

extern signed char MouseX;
extern signed char MouseY;
extern BYTE MouseSt;

#endif
