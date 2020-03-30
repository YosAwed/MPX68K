// JOYSTICK.C - joystick support for WinX68k

#include "common.h"
#include "prop.h"
#include "joystick.h"
#include "winui.h"
#include "keyboard.h"
#include <SDL.h>

#if defined(ANDROID) || TARGET_OS_IPHONE || defined(PSP)
#include "mouse.h"
#endif

#ifndef MAX_BUTTON
#define MAX_BUTTON 32
#endif

char joyname[2][MAX_PATH];
char joybtnname[2][MAX_BUTTON][MAX_PATH];
BYTE joybtnnum[2] = {0, 0};

BYTE joy[2];
BYTE JoyKeyState;
BYTE JoyKeyState0;
BYTE JoyKeyState1;
BYTE JoyState0[2];
BYTE JoyState1[2];

// This stores whether the buttons were down. This avoids key repeats.
BYTE JoyDownState0;
BYTE MouseDownState0;

// This stores whether the buttons were up. This avoids key repeats.
BYTE JoyUpState0;
BYTE MouseUpState0;

BYTE JoyPortData[2];


SDL_Joystick *sdl_joy;

void Joystick_Init(void)
{
	int i, nr_joys, nr_axes, nr_btns, nr_hats;

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

#if defined(ANDROID) || TARGET_OS_IPHONE
	Joystick_Vbtn_Update(WinUI_get_vkscale());
#endif

#ifndef PSP
	sdl_joy = 0;

	SDL_InitSubSystem(SDL_INIT_JOYSTICK);

	nr_joys = SDL_NumJoysticks();
	p6logd("joy num %d\n", nr_joys);
	for (i = 0; i < nr_joys; i++) {
		sdl_joy = SDL_JoystickOpen(i);
		if (sdl_joy) {
			nr_btns = SDL_JoystickNumButtons(sdl_joy);
			nr_axes = SDL_JoystickNumAxes(sdl_joy);
			nr_hats = SDL_JoystickNumHats(sdl_joy);

#if SDL_VERSION_ATLEAST(2, 0, 0)
			p6logd("Name: %s\n", SDL_JoystickNameForIndex(i));
#endif
			p6logd("# of Axes: %d\n", nr_axes);
			p6logd("# of Btns: %d\n", nr_btns);
			p6logd("# of Hats: %d\n", nr_hats);

			// skip accelerometer and keyboard
			if (nr_btns < 2 || (nr_axes < 2 && nr_hats == 0)) {
				Joystick_Cleanup();
				sdl_joy = 0;
			} else {
				break;
			}
		} else {
			p6logd("can't open joy %d\n", i);
		}
	}
#endif
}

void Joystick_Cleanup(void)
{
#ifndef PSP
#if SDL_VERSION_ATLEAST(2, 0, 0)
	if (SDL_JoystickGetAttached(sdl_joy)) {
		SDL_JoystickClose(sdl_joy);
	}
#else
	SDL_JoystickClose(sdl_joy);
#endif
#endif
}

BYTE FASTCALL Joystick_Read(BYTE num)
{
	BYTE joynum = num;
	BYTE ret0 = 0xff, ret1 = 0xff, ret;

	if (Config.JoySwap) joynum ^= 1;
	if (joy[num]) {
		ret0 = JoyState0[num];
		ret1 = JoyState1[num];
	}

	if (Config.JoyKey)
	{
		if ((Config.JoyKeyJoy2)&&(num==1))
			ret0 ^= JoyKeyState;
		else if ((!Config.JoyKeyJoy2)&&(num==0))
			ret0 ^= JoyKeyState;
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

#ifdef PSP
void FASTCALL Joystick_Update(int is_menu)
#else
void FASTCALL Joystick_Update(int is_menu, SDL_Keycode key)
#endif
{
	BYTE ret0 = 0xff, ret1 = 0xff;
	BYTE mret0 = 0xff, mret1 = 0xff;
	int num = 0; //xxx only joy1
	static BYTE pre_ret0 = 0xff, pre_mret0 = 0xff;
#if defined(PSP)
	static DWORD button_down = 0;
	DWORD button_changing;

	SceCtrlData psppad;
	sceCtrlPeekBufferPositive(&psppad, 1);

	if (is_menu || !Config.JoyOrMouse || Keyboard_IsSwKeyboard()) {
		if (psppad.Buttons & PSP_CTRL_LEFT) {
			ret0 ^= JOY_LEFT;
		}
		if (psppad.Buttons & PSP_CTRL_RIGHT) {
			ret0 ^= JOY_RIGHT;
		}
		if (psppad.Buttons & PSP_CTRL_UP) {
			ret0 ^= JOY_UP;
		}
		if (psppad.Buttons & PSP_CTRL_DOWN) {
			ret0 ^= JOY_DOWN;
		}
		if (psppad.Buttons & PSP_CTRL_CIRCLE) {
			ret0 ^= JOY_TRG1;
		}
		if (psppad.Buttons & PSP_CTRL_CROSS) {
			ret0 ^= JOY_TRG2;
		}
	} else {
		if (psppad.Buttons & PSP_CTRL_CIRCLE) {
			mret0 ^= JOY_TRG1;
		}
		if (psppad.Buttons & PSP_CTRL_CROSS) {
			mret0 ^= JOY_TRG2;
		}
	}

	JoyDownState0 = ~(ret0 ^ pre_ret0) | ret0;
	JoyUpState0 = (ret0 ^ pre_ret0) & ret0;
	pre_ret0 = ret0;

	MouseDownState0 = ~(mret0 ^ pre_mret0) | mret0;
	MouseUpState0 = (mret0 ^ pre_mret0) & mret0;
	pre_mret0 = mret0;

	// up the bits which changed the states
	button_changing = psppad.Buttons ^ button_down;
	// first down = changing the state & down now
	JoyDownStatePSP = button_changing & psppad.Buttons;
	// invert the bits which changed the states
	button_down ^= button_changing;

	// save the states of Analog pad
	JoyAnaPadX = psppad.Lx;
	JoyAnaPadY = psppad.Ly;

#else //defined(PSP)
	signed int x, y;
	UINT8 hat;
#if defined(ANDROID) || TARGET_OS_IPHONE
	SDL_Finger *finger;
	SDL_FingerID fid;
	float fx, fy;
	int i, j;
	float scale, asb_x, asb_y; // play x, play y of a button

	// all active buttons are set to off
	for (i = 0; i < VBTN_MAX; i++) {
		if (vbtn_state[i] != VBTN_NOUSE) {
			vbtn_state[i] = VBTN_OFF;
		}
	}

	if (touchId == -1)
		goto skip_vpad;

	// A play of the button changes size according to scale.
	scale = WinUI_get_vkscale();
	asb_x = (float)20 * scale / 800.0;
	asb_y = (float)20 * scale / 600.0;

	// set the button on, only which is pushed just now
	for (i = 0; i < FINGER_MAX; i++) {
		finger = SDL_GetTouchFinger(touchId, i);
		if (!finger)
			continue;

		fx = finger->x;
		fy = finger->y;

		//p6logd("id: %d x: %f y: %f", i, fx, fy);

		for (j = 0; j < VBTN_MAX; j++) {
			if (vbtn_state[j] == VBTN_NOUSE)
				continue;

			if (vbtn_rect[j].x - asb_x > fx)
				continue;
			if (vbtn_rect[j].x2 + asb_x < fx)
				continue;
			if (vbtn_rect[j].y - asb_y > fy)
				continue;
			if (vbtn_rect[j].y2 + asb_y < fy)
				continue;

			vbtn_state[j] = VBTN_ON;
			// The buttons don't overlap.
			break;
		}
	}

	if (need_Vpad()) {
		if (vbtn_state[0] == VBTN_ON) {
			ret0 ^= JOY_LEFT;
		}
		if (vbtn_state[1] == VBTN_ON) {
			ret0 ^= JOY_RIGHT;
		}
		if (vbtn_state[2] == VBTN_ON) {
			ret0 ^= JOY_UP;
		}
		if (vbtn_state[3] == VBTN_ON) {
			ret0 ^= JOY_DOWN;
		}
		if (vbtn_state[4] == VBTN_ON) {
			ret0 ^= (Config.VbtnSwap == 0)? JOY_TRG1 : JOY_TRG2;
		}
		if (vbtn_state[5] == VBTN_ON) {
			ret0 ^= (Config.VbtnSwap == 0)? JOY_TRG2 : JOY_TRG1;
		}
	} else if (Config.JoyOrMouse) {
		if (vbtn_state[4] == VBTN_ON) {
			mret0 ^= (Config.VbtnSwap == 0)? JOY_TRG1 : JOY_TRG2;
		}
		if (vbtn_state[5] == VBTN_ON) {
			mret0 ^= (Config.VbtnSwap == 0)? JOY_TRG2 : JOY_TRG1;
		}
	}

skip_vpad:

#endif //defined(ANDROID) || TARGET_OS_IPHONE

	// Hardware Joystick
	if (sdl_joy) {
		SDL_JoystickUpdate();
		x = SDL_JoystickGetAxis(sdl_joy, Config.HwJoyAxis[0]);
		y = SDL_JoystickGetAxis(sdl_joy, Config.HwJoyAxis[1]);

		if (x < -JOYAXISPLAY) {
			ret0 ^= JOY_LEFT;
		}
		if (x > JOYAXISPLAY) {
			ret0 ^= JOY_RIGHT;
		}
		if (y < -JOYAXISPLAY) {
			ret0 ^= JOY_UP;
		}
		if (y > JOYAXISPLAY) {
			ret0 ^= JOY_DOWN;
		}

		hat = SDL_JoystickGetHat(sdl_joy, Config.HwJoyHat);

		if (hat) {
			switch (hat) {
			case SDL_HAT_RIGHTUP:
				ret0 ^= JOY_RIGHT;
			case SDL_HAT_UP:
				ret0 ^= JOY_UP;
				break;
			case SDL_HAT_RIGHTDOWN:
				ret0 ^= JOY_DOWN;
			case SDL_HAT_RIGHT:
				ret0 ^= JOY_RIGHT;
				break;
			case SDL_HAT_LEFTUP:
				ret0 ^= JOY_UP;
			case SDL_HAT_LEFT:
				ret0 ^= JOY_LEFT;
				break;
			case SDL_HAT_LEFTDOWN:
				ret0 ^= JOY_LEFT;
			case SDL_HAT_DOWN:
				ret0 ^= JOY_DOWN;
				break;
			}
		}

		if (SDL_JoystickGetButton(sdl_joy, Config.HwJoyBtn[0])) {
			ret0 ^= JOY_TRG1;
		}
		if (SDL_JoystickGetButton(sdl_joy, Config.HwJoyBtn[1])) {
			ret0 ^= JOY_TRG2;
		}
		if (SDL_JoystickGetButton(sdl_joy, Config.HwJoyBtn[2])) {
			ret1 ^= JOY_TRG3;
		}
		if (SDL_JoystickGetButton(sdl_joy, Config.HwJoyBtn[3])) {
			ret1 ^= JOY_TRG4;
		}
		if (SDL_JoystickGetButton(sdl_joy, Config.HwJoyBtn[4])) {
			ret1 ^= JOY_TRG5;
		}
		if (SDL_JoystickGetButton(sdl_joy, Config.HwJoyBtn[5])) {
			ret1 ^= JOY_TRG6;
		}
		if (SDL_JoystickGetButton(sdl_joy, Config.HwJoyBtn[6])) {
			ret1 ^= JOY_TRG7;
		}
		if (SDL_JoystickGetButton(sdl_joy, Config.HwJoyBtn[7])) {
			ret1 ^= JOY_TRG8;
		}
	}

	// scan keycode for menu UI
	if (key != SDLK_UNKNOWN) {
		switch (key) {
		case SDLK_UP :
			ret0 ^= JOY_UP;
			break;
		case SDLK_DOWN:
			ret0 ^= JOY_DOWN;
			break;
		case SDLK_LEFT:
			ret0 ^= JOY_LEFT;
			break;
		case SDLK_RIGHT:
			ret0 ^= JOY_RIGHT;
			break;
		case SDLK_RETURN:
			ret0 ^= JOY_TRG1;
			break;
		case SDLK_ESCAPE:
			ret0 ^= JOY_TRG2;
			break;
		}
	}

	JoyDownState0 = ~(ret0 ^ pre_ret0) | ret0;
	JoyUpState0 = (ret0 ^ pre_ret0) & ret0;
	pre_ret0 = ret0;

	MouseDownState0 = ~(mret0 ^ pre_mret0) | mret0;
	MouseUpState0 = (mret0 ^ pre_mret0) & mret0;
	pre_mret0 = mret0;

#endif //defined(PSP)
	// disable Joystick when software keyboard is active
	if (!is_menu && !Keyboard_IsSwKeyboard()) {
        JoyState0[num] = s_JoyData[num];//ret0
        JoyState1[num] = ret1;
	}

#if defined(USE_OGLES11) || defined(PSP)
	// update the states of the mouse buttons
	if (!(MouseDownState0 & JOY_TRG1) | (MouseUpState0 & JOY_TRG1)) {
		printf("mouse btn1 event\n");
		Mouse_Event(1, (MouseUpState0 & JOY_TRG1)? 0 : 1.0, 0);
	}
	if (!(MouseDownState0 & JOY_TRG2) | (MouseUpState0 & JOY_TRG2)) {
		printf("mouse btn2 event\n");
		Mouse_Event(2, (MouseUpState0 & JOY_TRG2)? 0 : 1.0, 0);
	}
#ifdef PSP
	mouse_update_psp(psppad);
#endif

#endif
}

BYTE get_joy_downstate(void)
{
	return JoyDownState0;
}
void reset_joy_downstate(void)
{
	JoyDownState0 = 0xff;
}
BYTE get_joy_upstate(void)
{
	return JoyUpState0;
}
void reset_joy_upstate(void)
{
	JoyUpState0 = 0x00;
}

#ifdef PSP
DWORD Joystick_get_downstate_psp(DWORD ctrl_bit)
{
	return (JoyDownStatePSP & ctrl_bit);
}	  
void Joystick_reset_downstate_psp(DWORD ctrl_bit)
{
	JoyDownStatePSP ^= ctrl_bit;
}	  

void Joystatic_reset_anapad_psp(void)
{
	JoyAnaPadX = JoyAnaPadY = 128;
}

void _get_anapad_sub(BYTE av, int *v, int max)
{
	// accelerate accroding to an angle of the stick
	if (av > 255 / 2 + 32) {
		(*v)++; // move a little bit
		if (av > 255 - 5) {
			(*v)++; // accelerate
			if (av == 255) {
				*v += 2; // more accelerate
			}
		}
		if (*v > max) {
			*v = max;
		}
	}
	if (av < 255 / 2 - 32) {
		(*v)--;
		if (av < 5) {
			(*v)--;
			if (av == 0) {
				*v -= 2;
			}
		}
		if (*v < 0) {
			*v = 0;
		}
	}
}

void Joystick_mv_anapad_psp(void)
{
	_get_anapad_sub(JoyAnaPadX, &kbd_x, 450);
	_get_anapad_sub(JoyAnaPadY, &kbd_y, 250);

}
static void mouse_update_psp(SceCtrlData psppad)
{
	int x = 128, y = 128; // origin is 128

	_get_anapad_sub(JoyAnaPadX, &x, 255);
	_get_anapad_sub(JoyAnaPadY, &y, 255);
	
	x -= 128; //x,y:(-4..4)
	y -= 128;

	if (psppad.Buttons & PSP_CTRL_LEFT) {
		x--;
	}
	if (psppad.Buttons & PSP_CTRL_RIGHT) {
		x++;
	}
	if (psppad.Buttons & PSP_CTRL_UP) {
		y--;
	}
	if (psppad.Buttons & PSP_CTRL_DOWN) {
		y++;
	}

	if (x != 0 || y != 0) {
		printf("mouse x: %d y: %d", x, y);
		Mouse_Event(0, (float)x * 0.1 * Config.MouseSpeed, (float)y * 0.1 * Config.MouseSpeed);
	}
}
#endif

