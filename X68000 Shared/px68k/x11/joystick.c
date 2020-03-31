// JOYSTICK.C - joystick support for WinX68k

#include "common.h"
#include "prop.h"
#include "joystick.h"
#include "winui.h"
#include "keyboard.h"


BYTE joy[2];
BYTE JoyKeyState;
BYTE JoyKeyState0;
BYTE JoyKeyState1;
BYTE JoyState0[2];
BYTE JoyState1[2];


BYTE JoyPortData[2];



void Joystick_Init(void)
{

	joy[0] = 1;  // active only one
	joy[1] = 0;
	JoyKeyState = 0;
	JoyKeyState0 = 0;
	JoyKeyState1 = 0;
	JoyState0[0] = 0xff;
	JoyState0[1] = 0xff;
	JoyState1[0] = 0xff;
	JoyState1[1] = 0xff;
	JoyPortData[0] = 0;
	JoyPortData[1] = 0;


}

void Joystick_Cleanup(void)
{
}

BYTE FASTCALL Joystick_Read(BYTE num)
{
	BYTE joynum = num;
	BYTE ret0 = 0xff, ret1 = 0xff, ret;

	if (joy[num]) {
		ret0 = JoyState0[num];
		ret1 = JoyState1[num];
	}

	ret = ((~JoyPortData[num])&ret0)|(JoyPortData[num]&ret1);

	return ret;
}


void FASTCALL Joystick_Write(BYTE num, BYTE data)
{
    printf("Joystick %d:%02x\n", num, data);
	if ( (num==0)||(num==1) ) JoyPortData[num] = data;
}

static BYTE s_JoyData[2] = { 0xff, 0xff };
void X68000_Joystick_Set(BYTE num, BYTE data)
{
    if ( (num==0)||(num==1) ) s_JoyData[num] = ~data;   // Neg. Logic
}

void FASTCALL Joystick_Update()
{
    for ( int i=0; i<2; ++i ) {
        JoyState0[i] = s_JoyData[i];
        JoyState1[i] = 0xff;    // for MD Pad 6B
    }
}
