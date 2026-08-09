#pragma once

#include "SDL.h"

#define EG_MESSAGEBOX_STOP 0
#define EG_MESSAGEBOX_QUESTION 1
#define EG_MESSAGEBOX_INFORMATION 2

#ifdef __cplusplus
extern "C" {
#endif
int EG_MessageBox(SDL_Surface *surface, int type, const char *title, const char *text,
                  const char *button1, const char *button2, const char *button3,
                  const char *button4, int has_focus);
#ifdef __cplusplus
}
#endif
