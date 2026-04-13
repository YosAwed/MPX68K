#ifndef _winx68k_irqh
#define _winx68k_irqh

#include "common.h"

void IRQH_Init(void);
DWORD FASTCALL IRQH_DefaultVector(BYTE irq);
void IRQH_IRQCallBack(BYTE irq);
void IRQH_Int(BYTE irq, void* handler);

extern BYTE IRQH_IRQ[8];

#endif
