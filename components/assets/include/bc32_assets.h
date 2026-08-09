#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "bbc_core.h"

#ifdef __cplusplus
extern "C" {
#endif

const bbc_roms_t *bc32_assets_roms(void);

typedef enum {
    BC32_DISC_CHUCKIE_EGG,
    BC32_DISC_PLANETOID,
    BC32_DISC_HOPPER,
    BC32_DISC_ARCADIANS,
    BC32_DISC_REPTON,
    BC32_DISC_THRUST,
    BC32_DISC_ZALAGA,
    // Preserve removed-game slots so existing writable /storage/dNN.ssd files
    // can never be mistaken for a newly added title of the same size.
    BC32_DISC_UNUSED_7,
    BC32_DISC_DAREDEVIL_DENNIS,
    BC32_DISC_UNUSED_9,
    BC32_DISC_FRAK,
    BC32_DISC_REPTON3,
    BC32_DISC_REPTON2,
    BC32_DISC_SNAPPER,
    BC32_DISC_KILLER_GORILLA,
    BC32_DISC_MR_EE,
    BC32_DISC_FLAPPY_BIRD,
    BC32_DISC_PAINTER,
    BC32_DISC_UNUSED_MONSTERS,
    BC32_DISC_SUPER_BREAKOUT,
    BC32_DISC_BBC_TETRIS,
    BC32_DISC_CITADEL,
    BC32_DISC_ELITE,
    BC32_DISC_COUNT,
} bc32_disc_id_t;

const uint8_t *bc32_assets_disc(bc32_disc_id_t id, size_t *size);
const uint8_t *bc32_assets_font(size_t *size);
// Expands the selected 3-bit RLE screenshot into a 640x256 framebuffer.
bool bc32_assets_decode_screenshot(bc32_disc_id_t id, uint8_t *destination,
                                   size_t destination_size);

#ifdef __cplusplus
}
#endif
