#include "bbc_core.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static std::vector<uint8_t> read_file(const char *path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static bool write_ppm(const char *path, const uint8_t *frame)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file << "P6\n" << BBC_FRAME_WIDTH << " " << BBC_FRAME_HEIGHT << "\n255\n";
    static constexpr uint8_t palette[8][3] = {
        {0, 0, 0}, {255, 0, 0}, {0, 255, 0}, {255, 255, 0},
        {0, 0, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255}};
    for (int i = 0; i < BBC_FRAME_WIDTH * BBC_FRAME_HEIGHT; ++i) {
        file.write(reinterpret_cast<const char *>(palette[frame[i] & 7]), 3);
    }
    return static_cast<bool>(file);
}

struct typed_key_t {
    int row;
    int column;
    bool shift;
};

static typed_key_t typed_key(char character)
{
    switch (character) {
    case 'A': return {4, 1, false}; case 'B': return {6, 4, false};
    case 'C': return {5, 2, false}; case 'D': return {3, 2, false};
    case 'E': return {2, 2, false}; case 'F': return {4, 3, false};
    case 'G': return {5, 3, false}; case 'H': return {5, 4, false};
    case 'I': return {2, 5, false}; case 'J': return {4, 5, false};
    case 'K': return {4, 6, false}; case 'L': return {5, 6, false};
    case 'M': return {6, 5, false}; case 'N': return {5, 5, false};
    case 'O': return {3, 6, false}; case 'P': return {3, 7, false};
    case 'Q': return {1, 0, false}; case 'R': return {3, 3, false};
    case 'S': return {5, 1, false}; case 'T': return {2, 3, false};
    case 'U': return {3, 5, false}; case 'V': return {6, 3, false};
    case 'W': return {2, 1, false}; case 'X': return {4, 2, false};
    case 'Y': return {4, 4, false}; case 'Z': return {6, 1, false};
    case '1': return {3, 0, false}; case '2': return {3, 1, false};
    case '3': return {1, 1, false}; case '4': return {1, 2, false};
    case '5': return {1, 3, false}; case '6': return {3, 4, false};
    case '7': return {2, 4, false}; case '8': return {1, 5, false};
    case '9': return {2, 6, false}; case '0': return {2, 7, false};
    case '*': return {4, 8, true};
    case ' ': return {6, 2, false};
    case '\r': return {4, 9, false};
    default: return {-1, -1, false};
    }
}

int main(int argc, char **argv)
{
    if (argc != 6 && argc != 7) {
        std::fprintf(stderr, "usage: %s os12.rom basic2.rom dnfs.rom teletext.fnt output.ppm [disc.ssd]\n", argv[0]);
        return 2;
    }
    const auto os = read_file(argv[1]);
    const auto basic = read_file(argv[2]);
    const auto dfs = read_file(argv[3]);
    const auto font = read_file(argv[4]);
    std::array<uint8_t, BBC_FRAME_WIDTH * BBC_FRAME_HEIGHT> frame_a{};
    std::array<uint8_t, BBC_FRAME_WIDTH * BBC_FRAME_HEIGHT> frame_b{};
    const bbc_roms_t roms{os.data(), os.size(), basic.data(), basic.size(), dfs.data(), dfs.size(),
                          font.data(), font.size()};
    if (!bbc_core_init(&roms, frame_a.data(), frame_b.data(), BBC_FRAME_WIDTH)) {
        std::fprintf(stderr, "invalid ROM set or framebuffer\n");
        return 3;
    }

    const bool boot_disc = argc == 7;
    if (boot_disc) {
        const auto disc = read_file(argv[6]);
        const char *writable_path = std::getenv("BC32_TEST_WRITABLE_PATH");
        const bool mounted = writable_path != nullptr
                                 ? bbc_core_mount_ssd_writable(
                                       disc.data(), disc.size(), writable_path)
                                 : bbc_core_mount_ssd(disc.data(), disc.size());
        if (!mounted) {
            std::fprintf(stderr, "invalid SSD image\n");
            return 5;
        }
        if (std::getenv("BC32_TEST_SKIP_AUTOBOOT") == nullptr) {
            bbc_core_shift_break();
        }
    }

    const auto started = std::chrono::steady_clock::now();
    unsigned target_fields = boot_disc ? 1000 : 250;
    if (const char *value = std::getenv("BC32_TEST_FIELDS")) {
        target_fields = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    }
    unsigned space_at = 0;
    if (const char *value = std::getenv("BC32_TEST_SPACE_AT")) {
        space_at = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    }
    unsigned space_count = 1;
    unsigned space_interval = 100;
    if (const char *value = std::getenv("BC32_TEST_SPACE_COUNT")) {
        space_count = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    }
    if (const char *value = std::getenv("BC32_TEST_SPACE_INTERVAL")) {
        space_interval = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    }
    unsigned start_at = 0;
    if (const char *value = std::getenv("BC32_TEST_START_AT")) {
        start_at = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    }
    unsigned start_hold_fields = 5;
    if (const char *value = std::getenv("BC32_TEST_START_HOLD_FIELDS")) {
        start_hold_fields = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    }
    unsigned player_delay_fields = 100;
    unsigned player_hold_fields = 5;
    if (const char *value = std::getenv("BC32_TEST_PLAYER_DELAY_FIELDS")) {
        player_delay_fields = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    }
    if (const char *value = std::getenv("BC32_TEST_PLAYER_HOLD_FIELDS")) {
        player_hold_fields = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    }
    unsigned move_at = 0;
    unsigned move_fields = 100;
    unsigned move_row = 0;
    unsigned move_column = 0;
    const char *type_text = std::getenv("BC32_TEST_TYPE");
    size_t type_index = 0;
    unsigned type_at = 250;
    unsigned type_phase = 0;
    typed_key_t current_typed_key{-1, -1, false};
    if (const char *value = std::getenv("BC32_TEST_TYPE_AT")) {
        type_at = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    }
    if (const char *value = std::getenv("BC32_TEST_MOVE_AT")) {
        move_at = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    }
    if (const char *value = std::getenv("BC32_TEST_MOVE_FIELDS")) {
        move_fields = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    }
    if (const char *value = std::getenv("BC32_TEST_MOVE_ROW")) {
        move_row = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    }
    if (const char *value = std::getenv("BC32_TEST_MOVE_COLUMN")) {
        move_column = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    }
    bool space_down = false;
    unsigned space_presses = 0;
    unsigned start_phase = 0;
    unsigned move_phase = 0;
    while (bbc_core_frame_count() < target_fields) {
        const unsigned field = bbc_core_frame_count();
        const unsigned next_space_at = space_at + space_presses * space_interval;
        if (space_at != 0 && !space_down && space_presses < space_count &&
            field >= next_space_at) {
            bbc_core_key_down(6, 2);
            space_down = true;
        } else if (space_down && field >= next_space_at + 5) {
            bbc_core_key_up(6, 2);
            space_down = false;
            ++space_presses;
        }
        if (start_at != 0 && start_phase == 0 && field >= start_at) {
            bbc_core_key_down(5, 1); // S: leave Chuckie Egg's attract mode.
            start_phase = 1;
        } else if (start_phase == 1 && field >= start_at + start_hold_fields) {
            bbc_core_key_up(5, 1);
            start_phase = 2;
        } else if (start_phase == 2 &&
                   field >= start_at + start_hold_fields + player_delay_fields) {
            bbc_core_key_down(3, 0); // 1: select a one-player game.
            start_phase = 3;
        } else if (start_phase == 3 &&
                   field >= start_at + start_hold_fields + player_delay_fields +
                                player_hold_fields) {
            bbc_core_key_up(3, 0);
            start_phase = 4;
        }
        if (move_at != 0 && move_phase == 0 && field >= move_at) {
            bbc_core_key_down(move_row, move_column);
            move_phase = 1;
        } else if (move_phase == 1 && field >= move_at + move_fields) {
            bbc_core_key_up(move_row, move_column);
            move_phase = 2;
        }
        if (type_text != nullptr && type_text[type_index] != '\0') {
            if (type_phase == 0 && field >= type_at) {
                current_typed_key = typed_key(type_text[type_index]);
                if (current_typed_key.row < 0) {
                    ++type_index;
                } else {
                    if (current_typed_key.shift) bbc_core_key_down(0, 0);
                    bbc_core_key_down(current_typed_key.row,
                                      current_typed_key.column);
                    type_phase = 1;
                }
            } else if (type_phase == 1 && field >= type_at + 3) {
                bbc_core_key_up(current_typed_key.row,
                                current_typed_key.column);
                if (current_typed_key.shift) bbc_core_key_up(0, 0);
                ++type_index;
                type_at = field + 4;
                type_phase = 0;
            }
        }
        bbc_core_run_batch();
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (!write_ppm(argv[5], bbc_core_framebuffer())) return 4;
    std::printf("fields=%u cycles=%llu elapsed=%.3f emulated_fps=%.1f\n",
                bbc_core_frame_count(), static_cast<unsigned long long>(bbc_core_cycle_count()), elapsed,
                bbc_core_frame_count() / elapsed);
    return 0;
}
