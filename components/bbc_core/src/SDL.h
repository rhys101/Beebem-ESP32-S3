#pragma once

/* The original handheld port includes both <SDL.h> and "sdl.h". Those names
 * are the same file on the default macOS filesystem, so this header provides
 * the small combined SDL/platform surface the portable core actually uses. */
#include <stdint.h>
#include <stdio.h>

typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;

typedef struct SDL_PixelFormat {
    Uint8 BytesPerPixel;
} SDL_PixelFormat;

typedef struct SDL_Surface {
    int w;
    int h;
    int pitch;
    void *pixels;
    SDL_PixelFormat *format;
} SDL_Surface;

typedef struct SDL_Joystick SDL_Joystick;
typedef struct SDL_Rect {
    int16_t x;
    int16_t y;
    uint16_t w;
    uint16_t h;
} SDL_Rect;

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SDL_BYTEORDER SDL_BIG_ENDIAN
#else
#define SDL_BYTEORDER SDL_LIL_ENDIAN
#endif

#define SDL_MUSTLOCK(surface) 0

static inline int SDL_LockSurface(SDL_Surface *) { return 0; }
static inline void SDL_UnlockSurface(SDL_Surface *) {}
static inline Uint32 SDL_MapRGB(SDL_PixelFormat *, Uint8 r, Uint8 g, Uint8 b)
{
    return (r ? 1u : 0u) | (g ? 2u : 0u) | (b ? 4u : 0u);
}
static inline int SDL_BlitSurface(SDL_Surface *, const SDL_Rect *, SDL_Surface *, SDL_Rect *) { return 0; }
static inline void SDL_UpdateRect(SDL_Surface *, int, int, unsigned, unsigned) {}

extern SDL_Surface *frame_buffer_p;
extern SDL_Surface *rgb_surface;

unsigned int SDL_GetTicks(void);
void SDL_Delay(unsigned int ticks);
void SDL_Sleep(unsigned int ticks);
unsigned long HowManyBytesLeftInSDLSoundBuffer(void);
