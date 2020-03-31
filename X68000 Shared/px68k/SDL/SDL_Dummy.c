#include "SDL.h"
#include "SDL_video.h"
#include "SDL_Dummy.h"

#define TRACE_FUNC printf("DUMMY: %s\n", __func__ )

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
