#ifndef winx68k_joy_h
#define winx68k_joy_h

#include "common.h"

#define	JOY_UP		0x01
#define	JOY_DOWN	0x02
#define	JOY_LEFT	0x04
#define	JOY_RIGHT	0x08
#define	JOY_TRG2	0x20
#define	JOY_TRG1	0x40

#define	JOY_TRG5	0x01
#define	JOY_TRG4	0x02
#define	JOY_TRG3	0x04
#define	JOY_TRG7	0x08
#define	JOY_TRG8	0x20
#define	JOY_TRG6	0x40

void Joystick_Init(void);
void Joystick_Cleanup(void);
BYTE FASTCALL Joystick_Read(BYTE num);
void FASTCALL Joystick_Write(BYTE num, BYTE data);
void FASTCALL Joystick_Update(void);

extern BYTE joy[2];
extern BYTE JoyKeyState;
extern BYTE JoyKeyState0;
extern BYTE JoyKeyState1;
extern BYTE JoyState0[2];
extern BYTE JoyState1[2];
extern BYTE JoyPortData[2];

#endif
