#include "SDL.h"
#include "beebconfig_data.h"
#include "beebwin.h"
#include "messagebox.h"
#include "uefstate.h"

#include <stdio.h>

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void sync(void) {}
#else
#include <chrono>
#include <thread>
#endif

BeebConfig config{};
BeebWin *mainWin = nullptr;
SDL_Surface *frame_buffer_p = nullptr;
SDL_Surface *rgb_surface = nullptr;
int DumpAfterEach = 0;

unsigned int SDL_GetTicks(void)
{
#ifdef ESP_PLATFORM
    return static_cast<unsigned int>(esp_timer_get_time() / 1000);
#else
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return static_cast<unsigned int>(duration_cast<milliseconds>(steady_clock::now() - start).count());
#endif
}

void SDL_Delay(unsigned int ticks)
{
#ifdef ESP_PLATFORM
    vTaskDelay(pdMS_TO_TICKS(ticks));
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
#endif
}

void SDL_Sleep(unsigned int ticks) { SDL_Delay(ticks); }

unsigned long HowManyBytesLeftInSDLSoundBuffer(void) { return 0; }

void fput16(unsigned int value, FILE *file)
{
    fputc(value & 0xff, file);
    fputc((value >> 8) & 0xff, file);
}

void fput32(unsigned int value, FILE *file)
{
    fput16(value & 0xffff, file);
    fput16(value >> 16, file);
}

unsigned int fget16(FILE *file)
{
    const unsigned int lo = static_cast<unsigned int>(fgetc(file)) & 0xff;
    const unsigned int hi = static_cast<unsigned int>(fgetc(file)) & 0xff;
    return lo | (hi << 8);
}

unsigned int fget32(FILE *file)
{
    const unsigned int lo = fget16(file);
    const unsigned int hi = fget16(file);
    return lo | (hi << 16);
}

extern "C" int EG_MessageBox(SDL_Surface *, int, const char *title, const char *text,
                              const char *, const char *, const char *, const char *, int)
{
    fprintf(stderr, "BeebEm: %s: %s\n", title ? title : "message", text ? text : "");
    return 1;
}
