#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    BBC_FRAME_WIDTH = 640,
    BBC_FRAME_HEIGHT = 256,
};

typedef struct {
    const uint8_t *os_rom;
    size_t os_rom_size;
    const uint8_t *basic_rom;
    size_t basic_rom_size;
    const uint8_t *dfs_rom;
    size_t dfs_rom_size;
    const uint8_t *teletext_font;
    size_t teletext_font_size;
} bbc_roms_t;

typedef void (*bbc_core_sound_callback_t)(uint8_t value, void *context);

/* Initialise a BBC Model B with a stable 8-bit, RGB-bit-order framebuffer. */
bool bbc_core_init(const bbc_roms_t *roms, uint8_t *framebuffer_a,
                   uint8_t *framebuffer_b, size_t pitch);

/* Install a single-sided DFS SSD image in drive 0 (the data is copied). */
bool bbc_core_mount_ssd(const uint8_t *image, size_t image_size);
/* Mount a mutable SSD whose completed 8271 track writes are flushed to path. */
bool bbc_core_mount_ssd_writable(const uint8_t *image, size_t image_size,
                                 const char *path);

/* Hold SHIFT across a hardware-style BREAK long enough to trigger !BOOT. */
void bbc_core_shift_break(void);

/* Execute a small CPU batch. Returns the number of newly completed fields. */
unsigned bbc_core_run_batch(void);

/* Reset the CPU and peripherals while retaining the installed ROMs. */
void bbc_core_reset(void);

/* BBC keyboard matrix access (row/column are BeebEm matrix coordinates). */
void bbc_core_key_down(int row, int column);
void bbc_core_key_up(int row, int column);
void bbc_core_set_joystick(uint16_t x, uint16_t y);
void bbc_core_set_joystick_button(bool down);

/* Route SN76489 register writes to the platform audio renderer. */
void bbc_core_set_sound_callback(bbc_core_sound_callback_t callback, void *context);
void bbc_core_sound_write(uint8_t value);

unsigned bbc_core_frame_count(void);
uint64_t bbc_core_cycle_count(void);
const uint8_t *bbc_core_framebuffer(void);

#ifdef __cplusplus
}
#endif
