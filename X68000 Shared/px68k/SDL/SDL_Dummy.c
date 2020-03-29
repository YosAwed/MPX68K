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

// スレッド
#include <pthread.h>


#ifdef __APPLE__
#include <dispatch/dispatch.h>
#else
#include <semaphore.h>
#endif

struct rk_sema {
#ifdef __APPLE__
    dispatch_semaphore_t    sem;
#else
    sem_t                   sem;
#endif
};


static inline void
rk_sema_init(struct rk_sema *s, uint32_t value)
{
#ifdef __APPLE__
    dispatch_semaphore_t *sem = &s->sem;

    *sem = dispatch_semaphore_create(value);
#else
    sem_init(&s->sem, 0, value);
#endif
}

static inline void
rk_sema_wait(struct rk_sema *s)
{

#ifdef __APPLE__
    dispatch_semaphore_wait(s->sem, DISPATCH_TIME_FOREVER);
#else
    int r;

    do {
            r = sem_wait(&s->sem);
    } while (r == -1 && errno == EINTR);
#endif
}

static inline void
rk_sema_post(struct rk_sema *s)
{

#ifdef __APPLE__
    dispatch_semaphore_signal(s->sem);
#else
    sem_post(&s->sem);
#endif
}

static struct rk_sema  s_sdl_r_sema;
static struct rk_sema  s_sdl_w_sema;

void SDL_Quit()
{
    TRACE_FUNC;
}
static SDL_AudioSpec s_audio_spec;
static Uint8* s_sound_buffer;

void X68000_AudioCallBack(void* buffer, const unsigned int sample)
{

    if ( s_audio_spec.callback ) {
//        rk_sema_wait(&s_sdl_r_sema);
        //  void fill_audio(void *udata, Uint8 *stream, int len)
//        printf("Callback! %p %d\n", buffer, sample);
        int size = sample*2*2;
        
        s_audio_spec.callback(NULL, buffer, size);
        if ( s_sound_buffer ) {
//            printf("->Copy %p %d\n", old, s_sound_buffer-old );
            memcpy( buffer, s_sound_buffer, size);
        }
//        rk_sema_post(&s_sdl_w_sema);
    }
}

int SDL_OpenAudio(SDL_AudioSpec* desired, SDL_AudioSpec* obtained) {
    TRACE_FUNC;
    
    memcpy( &s_audio_spec, desired, sizeof(SDL_AudioSpec) );

    printf("AUDIO: samples %d\n", s_audio_spec.samples);
    printf("AUDIO: freq    %d\n", s_audio_spec.freq);

    rk_sema_init(&s_sdl_r_sema, 0);
    rk_sema_init(&s_sdl_w_sema, 0);

    return 0;
}

void SDL_PauseAudio(int pause_on)
{
    TRACE_FUNC;

}

void SDL_LockAudio(void)
{
//    TRACE_FUNC;
//    rk_sema_wait(&s_sdl_w_sema);

}


void SDL_UnlockAudio(void)
{
//    TRACE_FUNC;
//    rk_sema_post(&s_sdl_r_sema);
}

void SDL_CloseAudio(void)
{
    TRACE_FUNC;

}

void SDL_MixAudio(Uint8*       dst,
const Uint8* src,
Uint32       len,
int          volume) {

    s_sound_buffer = src;
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


static SDL_Surface* window_surface;
static SDL_PixelFormat  format;

SDL_Surface* SDL_CreateRGBSurface(Uint32 flags,
int    width,
int    height,
int    depth,
Uint32 Rmask,
Uint32 Gmask,
Uint32 Bmask,
Uint32 Amask){
    TRACE_FUNC;
    
    printf("SDL_CreateRGBSurface( flags:%d, width:%d, height:%d, depth:%d\n",
           flags, width, height, depth);
    SDL_Surface* s = malloc(sizeof(SDL_Surface));
    memset( s, 0x00, sizeof(SDL_Surface));
    
    format.BitsPerPixel = 32;
    format.BytesPerPixel = 3; // for MacOS Swift

    s->format = &format;
    s->flags = flags;
    s->w = width;
    s->h = height;
    int bufsize = format.BytesPerPixel * width * height;
    s->pixels = malloc(bufsize);
    memset( s->pixels, 0x00, sizeof(bufsize) );
    
    if ( window_surface == NULL) {
        window_surface = s;
    }
    
    return s;
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
//    TRACE_FUNC;
    return window_surface;
}


void SDL_JoystickClose(SDL_Joystick* joystick)
{
    TRACE_FUNC;

}

int SDL_PollEvent(SDL_Event* event)
{
    return 0;
}

int SDL_UpdateWindowSurface(SDL_Window* window)
{
 //   TRACE_FUNC;
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
