#include "bc32_assets.h"

extern const uint8_t os12_start[] asm("_binary_os12_rom_start");
extern const uint8_t os12_end[] asm("_binary_os12_rom_end");
extern const uint8_t basic2_start[] asm("_binary_basic2_rom_start");
extern const uint8_t basic2_end[] asm("_binary_basic2_rom_end");
extern const uint8_t dnfs_start[] asm("_binary_dnfs_rom_start");
extern const uint8_t dnfs_end[] asm("_binary_dnfs_rom_end");
extern const uint8_t teletext_start[] asm("_binary_teletext_fnt_start");
extern const uint8_t teletext_end[] asm("_binary_teletext_fnt_end");
extern const uint8_t chuckie_start[] asm("_binary_chuckie_egg_ssd_start");
extern const uint8_t chuckie_end[] asm("_binary_chuckie_egg_ssd_end");
extern const uint8_t planetoid_start[] asm("_binary_planetoid_ssd_start");
extern const uint8_t planetoid_end[] asm("_binary_planetoid_ssd_end");
extern const uint8_t hopper_start[] asm("_binary_hopper_ssd_start");
extern const uint8_t hopper_end[] asm("_binary_hopper_ssd_end");
extern const uint8_t arcadians_start[] asm("_binary_arcadians_ssd_start");
extern const uint8_t arcadians_end[] asm("_binary_arcadians_ssd_end");
extern const uint8_t repton_start[] asm("_binary_repton_ssd_start");
extern const uint8_t repton_end[] asm("_binary_repton_ssd_end");
extern const uint8_t thrust_start[] asm("_binary_thrust_ssd_start");
extern const uint8_t thrust_end[] asm("_binary_thrust_ssd_end");
extern const uint8_t zalaga_start[] asm("_binary_zalaga_ssd_start");
extern const uint8_t zalaga_end[] asm("_binary_zalaga_ssd_end");
extern const uint8_t daredevil_dennis_start[] asm("_binary_daredevil_dennis_ssd_start");
extern const uint8_t daredevil_dennis_end[] asm("_binary_daredevil_dennis_ssd_end");
extern const uint8_t frak_start[] asm("_binary_frak_ssd_start");
extern const uint8_t frak_end[] asm("_binary_frak_ssd_end");
extern const uint8_t repton3_start[] asm("_binary_repton3_ssd_start");
extern const uint8_t repton3_end[] asm("_binary_repton3_ssd_end");
extern const uint8_t repton2_start[] asm("_binary_repton2_ssd_start");
extern const uint8_t repton2_end[] asm("_binary_repton2_ssd_end");
extern const uint8_t snapper_start[] asm("_binary_snapper_ssd_start");
extern const uint8_t snapper_end[] asm("_binary_snapper_ssd_end");
extern const uint8_t killer_gorilla_start[] asm("_binary_killer_gorilla_ssd_start");
extern const uint8_t killer_gorilla_end[] asm("_binary_killer_gorilla_ssd_end");
extern const uint8_t mr_ee_start[] asm("_binary_mr_ee_ssd_start");
extern const uint8_t mr_ee_end[] asm("_binary_mr_ee_ssd_end");
extern const uint8_t flappy_bird_start[] asm("_binary_flappy_bird_ssd_start");
extern const uint8_t flappy_bird_end[] asm("_binary_flappy_bird_ssd_end");
extern const uint8_t painter_start[] asm("_binary_painter_ssd_start");
extern const uint8_t painter_end[] asm("_binary_painter_ssd_end");
extern const uint8_t super_breakout_start[] asm("_binary_super_breakout_ssd_start");
extern const uint8_t super_breakout_end[] asm("_binary_super_breakout_ssd_end");
extern const uint8_t bbc_tetris_start[] asm("_binary_bbc_tetris_ssd_start");
extern const uint8_t bbc_tetris_end[] asm("_binary_bbc_tetris_ssd_end");
extern const uint8_t citadel_start[] asm("_binary_citadel_ssd_start");
extern const uint8_t citadel_end[] asm("_binary_citadel_ssd_end");
extern const uint8_t elite_start[] asm("_binary_elite_ssd_start");
extern const uint8_t elite_end[] asm("_binary_elite_ssd_end");
extern const uint8_t chuckie_screen_start[] asm("_binary_chuckie_rle_start");
extern const uint8_t chuckie_screen_end[] asm("_binary_chuckie_rle_end");
extern const uint8_t planetoid_screen_start[] asm("_binary_planetoid_rle_start");
extern const uint8_t planetoid_screen_end[] asm("_binary_planetoid_rle_end");
extern const uint8_t hopper_screen_start[] asm("_binary_hopper_rle_start");
extern const uint8_t hopper_screen_end[] asm("_binary_hopper_rle_end");
extern const uint8_t arcadians_screen_start[] asm("_binary_arcadians_rle_start");
extern const uint8_t arcadians_screen_end[] asm("_binary_arcadians_rle_end");
extern const uint8_t repton_screen_start[] asm("_binary_repton_rle_start");
extern const uint8_t repton_screen_end[] asm("_binary_repton_rle_end");
extern const uint8_t thrust_screen_start[] asm("_binary_thrust_rle_start");
extern const uint8_t thrust_screen_end[] asm("_binary_thrust_rle_end");
extern const uint8_t zalaga_screen_start[] asm("_binary_zalaga_rle_start");
extern const uint8_t zalaga_screen_end[] asm("_binary_zalaga_rle_end");
extern const uint8_t daredevil_dennis_screen_start[] asm("_binary_daredevil_dennis_rle_start");
extern const uint8_t daredevil_dennis_screen_end[] asm("_binary_daredevil_dennis_rle_end");
extern const uint8_t frak_screen_start[] asm("_binary_frak_rle_start");
extern const uint8_t frak_screen_end[] asm("_binary_frak_rle_end");
extern const uint8_t repton3_screen_start[] asm("_binary_repton3_rle_start");
extern const uint8_t repton3_screen_end[] asm("_binary_repton3_rle_end");
extern const uint8_t repton2_screen_start[] asm("_binary_repton2_rle_start");
extern const uint8_t repton2_screen_end[] asm("_binary_repton2_rle_end");
extern const uint8_t snapper_screen_start[] asm("_binary_snapper_rle_start");
extern const uint8_t snapper_screen_end[] asm("_binary_snapper_rle_end");
extern const uint8_t killer_gorilla_screen_start[] asm("_binary_killer_gorilla_rle_start");
extern const uint8_t killer_gorilla_screen_end[] asm("_binary_killer_gorilla_rle_end");
extern const uint8_t mr_ee_screen_start[] asm("_binary_mr_ee_rle_start");
extern const uint8_t mr_ee_screen_end[] asm("_binary_mr_ee_rle_end");
extern const uint8_t flappy_bird_screen_start[] asm("_binary_flappy_bird_rle_start");
extern const uint8_t flappy_bird_screen_end[] asm("_binary_flappy_bird_rle_end");
extern const uint8_t painter_screen_start[] asm("_binary_painter_rle_start");
extern const uint8_t painter_screen_end[] asm("_binary_painter_rle_end");
extern const uint8_t super_breakout_screen_start[] asm("_binary_super_breakout_rle_start");
extern const uint8_t super_breakout_screen_end[] asm("_binary_super_breakout_rle_end");
extern const uint8_t bbc_tetris_screen_start[] asm("_binary_bbc_tetris_rle_start");
extern const uint8_t bbc_tetris_screen_end[] asm("_binary_bbc_tetris_rle_end");
extern const uint8_t citadel_screen_start[] asm("_binary_citadel_rle_start");
extern const uint8_t citadel_screen_end[] asm("_binary_citadel_rle_end");
extern const uint8_t elite_screen_start[] asm("_binary_elite_rle_start");
extern const uint8_t elite_screen_end[] asm("_binary_elite_rle_end");

const bbc_roms_t *bc32_assets_roms(void)
{
    static const bbc_roms_t roms = {
        .os_rom = os12_start,
        .os_rom_size = static_cast<size_t>(os12_end - os12_start),
        .basic_rom = basic2_start,
        .basic_rom_size = static_cast<size_t>(basic2_end - basic2_start),
        .dfs_rom = dnfs_start,
        .dfs_rom_size = static_cast<size_t>(dnfs_end - dnfs_start),
        .teletext_font = teletext_start,
        .teletext_font_size = static_cast<size_t>(teletext_end - teletext_start),
    };
    return &roms;
}

const uint8_t *bc32_assets_disc(bc32_disc_id_t id, size_t *size)
{
    struct disc_asset_t {
        const uint8_t *start;
        const uint8_t *end;
    };
    static const disc_asset_t discs[] = {
        {chuckie_start, chuckie_end}, {planetoid_start, planetoid_end},
        {hopper_start, hopper_end},   {arcadians_start, arcadians_end},
        {repton_start, repton_end},   {thrust_start, thrust_end},
        {zalaga_start, zalaga_end},
        {nullptr, nullptr},
        {daredevil_dennis_start, daredevil_dennis_end},
        {nullptr, nullptr},
        {frak_start, frak_end},
        {repton3_start, repton3_end},
        {repton2_start, repton2_end},
        {snapper_start, snapper_end},
        {killer_gorilla_start, killer_gorilla_end},
        {mr_ee_start, mr_ee_end},
        {flappy_bird_start, flappy_bird_end},
        {painter_start, painter_end},
        {nullptr, nullptr},
        {super_breakout_start, super_breakout_end},
        {bbc_tetris_start, bbc_tetris_end},
        {citadel_start, citadel_end},
        {elite_start, elite_end},
    };
    static_assert(sizeof(discs) / sizeof(discs[0]) == BC32_DISC_COUNT);
    if (id < 0 || id >= BC32_DISC_COUNT) {
        if (size != nullptr) *size = 0;
        return nullptr;
    }
    const disc_asset_t &disc = discs[id];
    if (disc.start == nullptr || disc.end == nullptr) {
        if (size != nullptr) *size = 0;
        return nullptr;
    }
    if (size != nullptr) *size = static_cast<size_t>(disc.end - disc.start);
    return disc.start;
}

const uint8_t *bc32_assets_font(size_t *size)
{
    if (size != nullptr) *size = static_cast<size_t>(teletext_end - teletext_start);
    return teletext_start;
}

bool bc32_assets_decode_screenshot(bc32_disc_id_t id, uint8_t *destination,
                                   size_t destination_size)
{
    struct screenshot_asset_t {
        const uint8_t *start;
        const uint8_t *end;
    };
    static const screenshot_asset_t screenshots[] = {
        {chuckie_screen_start, chuckie_screen_end},
        {planetoid_screen_start, planetoid_screen_end},
        {hopper_screen_start, hopper_screen_end},
        {arcadians_screen_start, arcadians_screen_end},
        {repton_screen_start, repton_screen_end},
        {thrust_screen_start, thrust_screen_end},
        {zalaga_screen_start, zalaga_screen_end},
        {nullptr, nullptr},
        {daredevil_dennis_screen_start, daredevil_dennis_screen_end},
        {nullptr, nullptr},
        {frak_screen_start, frak_screen_end},
        {repton3_screen_start, repton3_screen_end},
        {repton2_screen_start, repton2_screen_end},
        {snapper_screen_start, snapper_screen_end},
        {killer_gorilla_screen_start, killer_gorilla_screen_end},
        {mr_ee_screen_start, mr_ee_screen_end},
        {flappy_bird_screen_start, flappy_bird_screen_end},
        {painter_screen_start, painter_screen_end},
        {nullptr, nullptr},
        {super_breakout_screen_start, super_breakout_screen_end},
        {bbc_tetris_screen_start, bbc_tetris_screen_end},
        {citadel_screen_start, citadel_screen_end},
        {elite_screen_start, elite_screen_end},
    };
    static_assert(sizeof(screenshots) / sizeof(screenshots[0]) ==
                  BC32_DISC_COUNT);
    constexpr size_t screenshot_pixels = 640U * 256U;
    if (id < 0 || id >= BC32_DISC_COUNT || destination == nullptr ||
        destination_size < screenshot_pixels) {
        return false;
    }

    const screenshot_asset_t &screenshot = screenshots[id];
    if (screenshot.start == nullptr || screenshot.end == nullptr) return false;
    size_t output = 0;
    for (const uint8_t *input = screenshot.start; input < screenshot.end; ++input) {
        const uint8_t colour = *input >> 5;
        const size_t run = (*input & 0x1fU) + 1U;
        if (output + run > screenshot_pixels) return false;
        for (size_t index = 0; index < run; ++index) {
            destination[output++] = colour;
        }
    }
    return output == screenshot_pixels;
}
