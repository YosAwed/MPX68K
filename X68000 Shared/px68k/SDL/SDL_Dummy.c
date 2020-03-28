//
//  SDL_Dummy.c
//  X68000
//
//  Created by GOROman on 2020/03/29.
//  Copyright © 2020 GOROman. All rights reserved.
//

#include "SDL.h"
#include "SDL_video.h"
#include "SDL_Dummy.h"

#define TRACE_FUNC printf("DUMMY: %s\n", __func__ )

void SDL_Quit()
{
    TRACE_FUNC;
}

int SDL_OpenAudio(SDL_AudioSpec* desired,
SDL_AudioSpec* obtained) {
    TRACE_FUNC;

    return 0;
}

void SDL_PauseAudio(int pause_on)
{
    TRACE_FUNC;

}

void SDL_LockAudio(void)
{
    TRACE_FUNC;

}

void SDL_CloseAudio(void)
{
    TRACE_FUNC;

}

void SDL_UnlockAudio(void)
{
    TRACE_FUNC;
}

void SDL_MixAudio(Uint8*       dst,
const Uint8* src,
Uint32       len,
int          volume) {
    
    TRACE_FUNC;
}




int SDL_InitSubSystem(Uint32 flags)
{
    TRACE_FUNC;
    return 0;
}

int SDL_Init(Uint32 flags)
{
    TRACE_FUNC;
    return 0;
}

/*
 Undefined symbol: _SDL_CreateRGBSurface
 Undefined symbol: _SDL_FillRect
 Undefined symbol: _SDL_GetWindowSurface
 Undefined symbol: _SDL_JoystickClose
 Undefined symbol: _SDL_JoystickGetAttached
 Undefined symbol: _SDL_JoystickGetAxis
 
 Undefined symbol: _SDL_JoystickGetButton
 Undefined symbol: _SDL_JoystickGetHat
 
 Undefined symbol: _SDL_JoystickNameForIndex
 
 Undefined symbol: _SDL_JoystickNumAxes
 
 Undefined symbol: _SDL_JoystickNumButtons
 Undefined symbol: _SDL_JoystickNumHats

 Undefined symbol: _SDL_JoystickOpen
 Undefined symbol: _SDL_JoystickUpdate
 Undefined symbol: _SDL_NumJoysticks
 
 Undefined symbol: _SDL_PollEvent
 Undefined symbol: _SDL_UpdateWindowSurface
 Undefined symbol: _SDL_UpperBlit
 Undefined symbol: _sdl_window
 */

static SDL_Surface surface;

SDL_Surface* SDL_CreateRGBSurface(Uint32 flags,
int    width,
int    height,
int    depth,
Uint32 Rmask,
Uint32 Gmask,
Uint32 Bmask,
Uint32 Amask){
    TRACE_FUNC;
    return &surface;
}

int SDL_FillRect(SDL_Surface*    dst,
const SDL_Rect* rect,
Uint32          color)
{
    TRACE_FUNC;
    return 0;
}

SDL_Surface* SDL_GetWindowSurface(SDL_Window* window)
{
    TRACE_FUNC;
    return NULL;
}


void SDL_JoystickClose(SDL_Joystick* joystick)
{
    TRACE_FUNC;

}

int SDL_PollEvent(SDL_Event* event)
{
    TRACE_FUNC;


    return 0;
}

int SDL_UpdateWindowSurface(SDL_Window* window)
{
    TRACE_FUNC;
    return 0;
}

//SDL_UpperBlit
int SDL_BlitSurface(SDL_Surface*    src,
const SDL_Rect* srcrect,
SDL_Surface*    dst,
SDL_Rect*       dstrect)
{
    TRACE_FUNC;
    return 0;
}

SDL_bool SDL_JoystickGetAttached(SDL_Joystick * joystick)
{
    TRACE_FUNC;
    return 0;
}

 
Sint16 SDL_JoystickGetAxis(SDL_Joystick * joystick, int axis)
{
    TRACE_FUNC;
    return 0;
}

Uint8 SDL_JoystickGetButton(SDL_Joystick * joystick, int button)
{
    TRACE_FUNC;
    return 0;
}

Uint8 SDL_JoystickGetHat(SDL_Joystick * joystick, int hat)
{
    TRACE_FUNC;
    return 0;
}

const char * SDL_JoystickNameForIndex(int device_index)
{
    TRACE_FUNC;
    return "JOY";
}

int SDL_JoystickNumAxes(SDL_Joystick * joystick) {
    TRACE_FUNC;
    return 0;
}


int SDL_JoystickNumHats(SDL_Joystick * joystick)
{
    TRACE_FUNC;
    return 0;
}

int SDL_JoystickNumButtons(SDL_Joystick * joystick)
{
    TRACE_FUNC;
    return 0;
}

SDL_Joystick *SDL_JoystickOpen(int device_index)
{
    TRACE_FUNC;
    return NULL;
}

void SDL_JoystickUpdate(void)
{
    TRACE_FUNC;


}

int SDL_NumJoysticks(void)
{
    TRACE_FUNC;
    return 0;
}



//sdl_window = SDL_CreateWindow(APPNAME" SDL", 0, 0, FULLSCREEN_WIDTH, FULLSCREEN_HEIGHT, SDL_WINDOW_SHOWN);
SDL_Window sdl_window;

SDL_Window* SDL_CreateWindow(const char* title,
int         x,
int         y,
int         w,
int         h,
Uint32      flags)

{
    TRACE_FUNC;
    return &sdl_window;
}
