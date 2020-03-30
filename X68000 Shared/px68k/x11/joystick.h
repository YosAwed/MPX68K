#ifndef winx68k_joy_h
#define winx68k_joy_h

#include "common.h"
#include <SDL.h>
#if SDL_VERSION_ATLEAST(2, 0, 0)
#include <SDL_keycode.h>
#endif
#include <SDL_joystick.h>

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

#ifndef PSP
#define JOYAXISPLAY 2048
#endif

#if defined(ANDROID) || TARGET_OS_IPHONE
#define VBTN_ON 2
#define VBTN_OFF 1

typedef struct _vbtn_points {
	float x;
	float y;
} VBTN_POINTS;
#endif

#define need_Vpad() (is_menu || Keyboard_IsSwKeyboard() || (!Config.JoyOrMouse && !sdl_joy))

void Joystick_Init(void);
void Joystick_Cleanup(void);
BYTE FASTCALL Joystick_Read(BYTE num);
void FASTCALL Joystick_Write(BYTE num, BYTE data);
#if !SDL_VERSION_ATLEAST(2, 0, 0)
typedef signed int SDL_Keycode;
#endif
void FASTCALL Joystick_Update(int is_menu, SDL_Keycode key);

BYTE get_joy_downstate(void);
void reset_joy_downstate(void);
BYTE get_joy_upstate(void);
void reset_joy_upstate(void);

extern BYTE JoyKeyState;
#ifndef PSP
extern SDL_Joystick *sdl_joy;
#endif

#endif
