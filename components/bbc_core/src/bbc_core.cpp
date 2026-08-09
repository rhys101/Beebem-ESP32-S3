#include "bbc_core.h"

#include "6502core.h"
#include "atodconv.h"
#include "beebmem.h"
#include "beebwin.h"
#include "disc8271.h"
#include "SDL.h"
#include "sysvia.h"
#include "uservia.h"
#include "video.h"

#include <string.h>

namespace {
SDL_PixelFormat pixel_format{1};
SDL_Surface frame_surface{};
unsigned previous_frame_count = 0;
unsigned shift_release_frame = 0;
uint8_t *framebuffers[2]{};
unsigned render_index = 0;
const uint8_t *completed_framebuffer = nullptr;
bbc_core_sound_callback_t sound_callback = nullptr;
void *sound_callback_context = nullptr;

void frame_complete(void *)
{
    __atomic_store_n(&completed_framebuffer, framebuffers[render_index], __ATOMIC_RELEASE);
    render_index ^= 1;
    frame_surface.pixels = framebuffers[render_index];
    memset(frame_surface.pixels, 0, frame_surface.pitch * BBC_FRAME_HEIGHT);
}
}

bool bbc_core_init(const bbc_roms_t *roms, uint8_t *framebuffer_a,
                   uint8_t *framebuffer_b, size_t pitch)
{
    if (roms == nullptr || framebuffer_a == nullptr || framebuffer_b == nullptr ||
        pitch < BBC_FRAME_WIDTH ||
        roms->os_rom_size != 16384 || roms->basic_rom_size != 16384 ||
        roms->dfs_rom_size != 16384) {
        return false;
    }

    framebuffers[0] = framebuffer_a;
    framebuffers[1] = framebuffer_b;
    render_index = 0;
    __atomic_store_n(&completed_framebuffer, framebuffer_a, __ATOMIC_RELEASE);
    frame_surface = {BBC_FRAME_WIDTH, BBC_FRAME_HEIGHT, static_cast<int>(pitch), framebuffer_a,
                     &pixel_format};
    frame_buffer_p = &frame_surface;
    rgb_surface = nullptr;
    memset(framebuffer_a, 0, pitch * BBC_FRAME_HEIGHT);
    memset(framebuffer_b, 0, pitch * BBC_FRAME_HEIGHT);

    if (mainWin == nullptr) {
        mainWin = new BeebWin();
    }
    mainWin->FindPaletteColors();

    BeebMemInitFromBuffers(roms->os_rom, roms->basic_rom, roms->dfs_rom);
    VideoSetTeletextFont(roms->teletext_font, static_cast<unsigned int>(roms->teletext_font_size));
    Init6502core();
    SysVIAReset();
    UserVIAReset();
    AtoDInit();
    // The RG350 BeebEm port normally initializes this through BeebConfig.
    // Our headless platform shim has no configuration loader, so make the
    // BBC graphics raster discard its 32-line vertical-blank interval here.
    // Leaving it at the zero-initialized value shifts bitmap modes down and
    // clips their bottom 32 lines; Mode 7 uses a separate drawing path.
    Video_SetVHoldNormal();
    VideoInit();
    VideoSetFrameCompleteCallback(frame_complete, nullptr);
    Disc8271_reset();
    previous_frame_count = FrameCount;
    return true;
}

void bbc_core_reset(void)
{
    Init6502core();
    SysVIAReset();
    UserVIAReset();
    AtoDReset();
    VideoInit();
    Disc8271_reset();
    previous_frame_count = FrameCount;
}

unsigned bbc_core_run_batch(void)
{
    Exec6502Instruction();
    const unsigned now = FrameCount;
    if (shift_release_frame != 0 && now >= shift_release_frame) {
        BeebKeyUp(0, 0);
        shift_release_frame = 0;
    }
    const unsigned completed = now - previous_frame_count;
    previous_frame_count = now;
    return completed;
}

bool bbc_core_mount_ssd(const uint8_t *image, size_t image_size)
{
    return LoadSimpleDiscImageFromBuffer(image, static_cast<unsigned int>(image_size), 0, 0) != 0;
}

bool bbc_core_mount_ssd_writable(const uint8_t *image, size_t image_size,
                                 const char *path)
{
    if (!bbc_core_mount_ssd(image, image_size)) return false;
    return Disc8271_set_writable_path(0, path) != 0;
}

void bbc_core_shift_break(void)
{
    BeebKeyDown(0, 0);
    Init6502core();
    Disc8271_reset();
    shift_release_frame = FrameCount + 25;
}

void bbc_core_key_down(int row, int column) { BeebKeyDown(row, column); }
void bbc_core_key_up(int row, int column) { BeebKeyUp(row, column); }
void bbc_core_set_joystick(uint16_t x, uint16_t y)
{
    JoystickEnabled = 1;
    JoystickX = x;
    JoystickY = y;
}
void bbc_core_set_joystick_button(bool down) { JoystickButton = down ? 1 : 0; }
void bbc_core_set_sound_callback(bbc_core_sound_callback_t callback, void *context)
{
    sound_callback_context = context;
    __atomic_store_n(&sound_callback, callback, __ATOMIC_RELEASE);
}
void bbc_core_sound_write(uint8_t value)
{
    bbc_core_sound_callback_t callback =
        __atomic_load_n(&sound_callback, __ATOMIC_ACQUIRE);
    if (callback != nullptr) callback(value, sound_callback_context);
}
unsigned bbc_core_frame_count(void) { return __atomic_load_n(&FrameCount, __ATOMIC_ACQUIRE); }
uint64_t bbc_core_cycle_count(void) { return static_cast<uint32_t>(TotalCycles); }
const uint8_t *bbc_core_framebuffer(void)
{
    return __atomic_load_n(&completed_framebuffer, __ATOMIC_ACQUIRE);
}
