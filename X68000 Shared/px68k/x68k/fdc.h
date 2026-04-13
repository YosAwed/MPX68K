#ifndef _winx68k_fdc
#define _winx68k_fdc

#include "common.h"

typedef struct {
	int cmd;
	int cyl;
	int drv;
	int ready;
	int ctrl;
	int wexec;
	int rdptr;
	int wrptr;
	int rdnum;
	int wrnum;
	int bufnum;
	int st0;
	int st1;
	int st2;
} FDCMonitorState;

void FDC_Init(void);
BYTE FASTCALL FDC_Read(DWORD adr);
void FASTCALL FDC_Write(DWORD adr, BYTE data);
short FDC_Flush(void);
void FDC_EPhaseEnd(void);
void FDC_SetForceReady(int n);
int FDC_IsDataReady(void);
void FDC_ClearPendingState(void);
void FDC_GetMonitorState(FDCMonitorState* state);

#endif //_winx68k_fdc
