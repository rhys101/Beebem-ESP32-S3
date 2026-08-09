#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "bbc_core.h"
#include "bc32_audio.h"
#include "bc32_assets.h"
#include "bc32_input.h"
#include "display.h"
#include "driver/i2c_master.h"
#include "driver/usb_serial_jtag.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "wear_levelling.h"

namespace {
constexpr char kTag[] = "bc32";
constexpr gpio_num_t kI2cSda = GPIO_NUM_15;
constexpr gpio_num_t kI2cScl = GPIO_NUM_14;
// A BBC monitor is 4:3.  The 640x256 framebuffer contains non-square pixels,
// so fitting its raw 5:2 storage dimensions would make circles and text too
// wide.  Compute a centred 4:3 aspect-fit viewport in logical (landscape)
// screen coordinates; this also keeps the mapping correct if the panel
// geometry changes later.
constexpr int kDisplayAspectWidth = 4;
constexpr int kDisplayAspectHeight = 3;
constexpr bool kFitToLogicalWidth =
    DISPLAY_LOGICAL_WIDTH * kDisplayAspectHeight <=
    DISPLAY_LOGICAL_HEIGHT * kDisplayAspectWidth;
constexpr int kViewportWidth =
    kFitToLogicalWidth
        ? DISPLAY_LOGICAL_WIDTH
        : DISPLAY_LOGICAL_HEIGHT * kDisplayAspectWidth / kDisplayAspectHeight;
constexpr int kViewportHeight =
    kFitToLogicalWidth
        ? DISPLAY_LOGICAL_WIDTH * kDisplayAspectHeight / kDisplayAspectWidth
        : DISPLAY_LOGICAL_HEIGHT;
constexpr int kViewportLeft = (DISPLAY_LOGICAL_WIDTH - kViewportWidth) / 2;
constexpr int kViewportTop = (DISPLAY_LOGICAL_HEIGHT - kViewportHeight) / 2;
constexpr int kLauncherModeTouchTop = DISPLAY_LOGICAL_HEIGHT * 2 / 3;
constexpr int64_t kFieldPeriodUs = 20000;
constexpr int64_t kDisplayPeriodUs = 40000;
constexpr int64_t kStatsPeriodUs = 2000000;
constexpr int64_t kLauncherTouchReleaseGuardUs = 150000;
constexpr int64_t kLauncherInputGuardUs = 300000;
constexpr int64_t kTiltBootGuardUs = 3000000;
constexpr char kStoragePath[] = "/storage";
constexpr size_t kFrameBytes = BBC_FRAME_WIDTH * BBC_FRAME_HEIGHT;

i2c_master_bus_handle_t i2c_bus;
volatile uint32_t emulated_fields;
volatile uint32_t displayed_frames;
bool emulator_paused;
bool emulator_pause_acknowledged;
bool launcher_requested;
int64_t tilt_input_enable_at_us;
QueueHandle_t input_queue;
uint16_t source_x[kViewportWidth];
uint16_t palette[8];
const uint8_t *launcher_font;
wl_handle_t storage_wl_handle = WL_INVALID_HANDLE;
const uint8_t *volatile screenshot_source;
bool screenshot_capture_active;

struct matrix_key_t {
    uint8_t row;
    uint8_t column;
    bool valid;
};

constexpr matrix_key_t key(uint8_t row, uint8_t column)
{
    return {row, column, true};
}

constexpr matrix_key_t no_key()
{
    return {0, 0, false};
}

struct game_profile_t {
    const char *name;
    const char *tilt_hint;
    bc32_disc_id_t disc;
    matrix_key_t direction[4];
    matrix_key_t direction_modifier[4];
    matrix_key_t primary;
    matrix_key_t secondary;
    matrix_key_t startup_keys[3];
    uint8_t startup_key_count;
    bool joystick_controls;
    uint16_t tilt_sensitivity_percent = 100;
    bool allow_diagonals = true;
    uint8_t direction_repeat_fields = 0;
    uint16_t tilt_vertical_sensitivity_percent = 0;
    uint8_t direction_pulse_fields = 0;
    uint8_t tilt_pulse_repeat_fields = 0;
};

#define STARTUP_SPACES(count) \
    {key(6, 2), key(6, 2), key(6, 2)}, count, false
#define STARTUP_NONE {no_key(), no_key(), no_key()}, 0, false

constexpr game_profile_t kGames[] = {
    {"CHUCKIE EGG", "MOVE: , . A Z   PWR: JUMP", BC32_DISC_CHUCKIE_EGG,
     {key(6, 6), key(6, 7), key(4, 1), key(6, 1)},
     {no_key(), no_key(), no_key(), no_key()}, key(6, 2), key(6, 2),
     STARTUP_NONE},
    {"PLANETOID", "PWR FIRE  CENTRE/RIGHT THRUST", BC32_DISC_PLANETOID,
     {key(6, 2), key(0, 0), key(4, 1), key(6, 1)},
     {no_key(), no_key(), no_key(), no_key()}, key(4, 9), key(0, 0),
     STARTUP_SPACES(5)},
    {"HOPPER", "TILT Z/X   PWR: FORWARD  BTN: BACK", BC32_DISC_HOPPER,
     {key(6, 1), key(4, 2), no_key(), no_key()},
     {no_key(), no_key(), no_key(), no_key()}, key(6, 8), key(4, 8),
     STARTUP_SPACES(2), 125, false, 10},
    {"ARCADIANS", "LEFT/RIGHT   PWR: FIRE", BC32_DISC_ARCADIANS,
     {key(4, 0), key(0, 1), no_key(), no_key()},
     {no_key(), no_key(), no_key(), no_key()}, key(4, 9), key(4, 9),
     STARTUP_SPACES(2)},
    {"REPTON", "MOVE: Z X * ?   PWR: START", BC32_DISC_REPTON,
     {key(6, 1), key(4, 2), key(4, 8), key(6, 8)},
     {no_key(), no_key(), key(0, 0), key(0, 0)}, key(6, 2), key(6, 2),
     STARTUP_NONE},
    {"THRUST", "PWR FIRE  SECOND BUTTON THRUST", BC32_DISC_THRUST,
     {key(4, 0), key(0, 1), no_key(), key(6, 2)},
     {no_key(), no_key(), no_key(), no_key()}, key(4, 9), key(0, 0),
     STARTUP_SPACES(4), 33},
    {"ZALAGA", "L/R: CAPS CTRL   PWR: FIRE", BC32_DISC_ZALAGA,
     {key(4, 0), key(0, 1), no_key(), no_key()},
     {no_key(), no_key(), no_key(), no_key()}, key(4, 9), key(4, 9),
     STARTUP_SPACES(1)},
    {"DAREDEVIL DENNIS", "PWR JUMP  CENTRE/RIGHT SPEED",
     BC32_DISC_DAREDEVIL_DENNIS,
     {key(4, 9), key(0, 0), key(6, 2), no_key()},
     {no_key(), no_key(), no_key(), no_key()}, key(6, 2), key(0, 0),
     STARTUP_NONE},
    {"FRAK!", "MOVE: Z X * ?   PWR: YO-YO", BC32_DISC_FRAK,
     {key(6, 1), key(4, 2), key(4, 8), key(6, 8)},
     {no_key(), no_key(), key(0, 0), key(0, 0)}, key(4, 9), key(4, 9),
     STARTUP_SPACES(6)},
    {"REPTON 3", "MOVE: Z X * ?   PWR: START", BC32_DISC_REPTON3,
     {key(6, 1), key(4, 2), key(4, 8), key(6, 8)},
     {no_key(), no_key(), key(0, 0), key(0, 0)}, key(6, 2), key(6, 2),
     STARTUP_NONE},
    {"REPTON 2", "MOVE: Z X * ?   PWR: START", BC32_DISC_REPTON2,
     {key(6, 1), key(4, 2), key(4, 8), key(6, 8)},
     {no_key(), no_key(), key(0, 0), key(0, 0)}, key(6, 2), key(6, 2),
     STARTUP_NONE},
    {"SNAPPER", "MOVE: Z X * ?   PWR: START", BC32_DISC_SNAPPER,
     {key(6, 1), key(4, 2), key(4, 8), key(6, 8)},
     {no_key(), no_key(), key(0, 0), key(0, 0)}, key(6, 2), key(6, 2),
     STARTUP_NONE},
    {"KILLER GORILLA", "TOUCH / BLE KEYBOARD RECOMMENDED",
     BC32_DISC_KILLER_GORILLA,
     {key(6, 1), key(4, 2), key(4, 8), key(6, 8)},
     {no_key(), no_key(), no_key(), no_key()}, key(4, 9), key(6, 2),
     STARTUP_SPACES(3), 150, true, 0, 300},
    {"MR. EE!", "MOVE: Z X : /   PWR: FIRE", BC32_DISC_MR_EE,
     {key(6, 1), key(4, 2), key(4, 8), key(6, 8)},
     {no_key(), no_key(), no_key(), no_key()}, key(4, 9), key(4, 9),
     STARTUP_SPACES(4), 125, true, 0, 300},
    {"FLAPPY BIRD", "EITHER BUTTON: FLAP", BC32_DISC_FLAPPY_BIRD,
     {no_key(), no_key(), no_key(), no_key()},
     {no_key(), no_key(), no_key(), no_key()}, key(6, 2), key(6, 2),
     STARTUP_NONE},
    {"PAINTER", "MOVE: < > A Z   PWR: GAP", BC32_DISC_PAINTER,
     {key(6, 6), key(6, 7), key(4, 1), key(6, 1)},
     {key(0, 0), key(0, 0), no_key(), no_key()}, key(6, 2), key(6, 2),
     {key(6, 2), key(3, 0), key(3, 0)}, 3, false},
    {"SUPER BREAKOUT", "TILT PADDLE   PWR: FIRE",
     BC32_DISC_SUPER_BREAKOUT,
     {no_key(), no_key(), no_key(), no_key()},
     {no_key(), no_key(), no_key(), no_key()}, key(4, 9), key(4, 9),
     {no_key(), no_key(), no_key()}, 0, true},
    {"BBC TETRIS", "Z/X MOVE   BUTTONS: ROTATE DROP", BC32_DISC_BBC_TETRIS,
     {key(6, 1), key(4, 2), no_key(), no_key()},
     {no_key(), no_key(), no_key(), no_key()}, key(4, 9), key(6, 2),
     {key(3, 0), no_key(), no_key()}, 1, false, 67, true, 0, 0, 2, 4},
    {"CITADEL", "MOVE: Z X * ?   BUTTONS: RETURN SPACE", BC32_DISC_CITADEL,
     {key(6, 1), key(4, 2), key(4, 8), key(6, 8)},
     {no_key(), no_key(), key(0, 0), key(0, 0)}, key(4, 9), key(6, 2),
     STARTUP_NONE},
    {"ELITE", "TILT/ARROWS FLY   EITHER BUTTON: FIRE", BC32_DISC_ELITE,
     {no_key(), no_key(), no_key(), no_key()},
     {no_key(), no_key(), no_key(), no_key()}, key(4, 1), key(4, 1),
     {no_key(), no_key(), no_key()}, 0, true, 100},
};
#undef STARTUP_NONE
#undef STARTUP_SPACES
constexpr unsigned kGameCount = sizeof(kGames) / sizeof(kGames[0]);

enum class picker_key_kind_t : uint8_t {
    key,
    game_chooser,
};

struct picker_key_t {
    const char *label;
    matrix_key_t matrix;
    bool shift;
    picker_key_kind_t kind = picker_key_kind_t::key;
};

// A is the initial selection. RESTART is immediately to its left, with the
// digits continuing further left. The remaining entries expose useful BBC
// editing and punctuation keys without reproducing a cramped full keyboard.
constexpr picker_key_t kPickerKeys[] = {
    {"0", key(2, 7), false}, {"1", key(3, 0), false},
    {"2", key(3, 1), false}, {"3", key(1, 1), false},
    {"4", key(1, 2), false}, {"5", key(1, 3), false},
    {"6", key(3, 4), false}, {"7", key(2, 4), false},
    {"8", key(1, 5), false}, {"9", key(2, 6), false},
    {"RESTART", no_key(), false, picker_key_kind_t::game_chooser},
    {"A", key(4, 1), false}, {"B", key(6, 4), false},
    {"C", key(5, 2), false}, {"D", key(3, 2), false},
    {"E", key(2, 2), false}, {"F", key(4, 3), false},
    {"G", key(5, 3), false}, {"H", key(5, 4), false},
    {"I", key(2, 5), false}, {"J", key(4, 5), false},
    {"K", key(4, 6), false}, {"L", key(5, 6), false},
    {"M", key(6, 5), false}, {"N", key(5, 5), false},
    {"O", key(3, 6), false}, {"P", key(3, 7), false},
    {"Q", key(1, 0), false}, {"R", key(3, 3), false},
    {"S", key(5, 1), false}, {"T", key(2, 3), false},
    {"U", key(3, 5), false}, {"V", key(6, 3), false},
    {"W", key(2, 1), false}, {"X", key(4, 2), false},
    {"Y", key(4, 4), false}, {"Z", key(6, 1), false},
    {"SPACE", key(6, 2), false}, {"RETURN", key(4, 9), false},
    {"ESC", key(7, 0), false}, {"DELETE", key(5, 9), false},
    {"TAB", key(6, 0), false}, {",", key(6, 6), false},
    {"<", key(6, 6), true}, {".", key(6, 7), false},
    {">", key(6, 7), true}, {"/", key(6, 8), false},
    {"?", key(6, 8), true}, {":", key(4, 8), false},
    {"*", key(4, 8), true}, {"@", key(4, 7), false},
    {";", key(5, 7), false}, {"+", key(5, 7), true},
    {"-", key(1, 7), false}, {"=", key(1, 7), true},
    {"[", key(3, 8), false}, {"]", key(5, 8), false},
};
constexpr unsigned kPickerKeyCount =
    sizeof(kPickerKeys) / sizeof(kPickerKeys[0]);

const game_profile_t *active_game;

enum class input_mode_t : uint8_t {
    tilt,
    touch,
    keyboard,
};

using touch_control_t = uint8_t;
constexpr touch_control_t kTouchNone = 0;
constexpr touch_control_t kTouchLeft = 1U << 0;
constexpr touch_control_t kTouchRight = 1U << 1;
constexpr touch_control_t kTouchUp = 1U << 2;
constexpr touch_control_t kTouchDown = 1U << 3;
constexpr touch_control_t kTouchAction = 1U << 4;
constexpr touch_control_t kTouchDirections =
    kTouchLeft | kTouchRight | kTouchUp | kTouchDown;

input_mode_t input_mode = input_mode_t::tilt;
bool motion_input_available;
matrix_key_t action_key_down[2] = {no_key(), no_key()};
bool action_joystick_down[2];
uint8_t action_hold_count[2];
uint8_t matrix_key_holds[8][10];
uint8_t direction_hold_count[4];
bool direction_repeat_released[4];
uint32_t direction_repeat_deadline[4];
uint8_t killer_jump_pulse_phase;
uint32_t killer_jump_pulse_deadline;
unsigned chuckie_action_count;
unsigned startup_action_count;
touch_control_t gameplay_touch_control = kTouchNone;
uint32_t chuckie_first_action_field;
bool picker_active;
unsigned picker_index = 11; // A centred; RESTART and 0-9 are to its left.
int picker_drag_x;
bool picker_touch_down;
int picker_touch_start_x;
int picker_touch_start_y;
int picker_touch_last_x;
int picker_touch_last_y;
matrix_key_t picker_injected_key = no_key();
bool picker_injected_shift;
uint32_t picker_key_release_field;

enum class chuckie_start_state_t {
    idle,
    wait_s,
    hold_s,
    wait_one,
    hold_one,
    done,
};
chuckie_start_state_t chuckie_start_state = chuckie_start_state_t::idle;
uint32_t chuckie_start_deadline;

constexpr uint16_t swap16(uint16_t value)
{
    return static_cast<uint16_t>((value << 8) | (value >> 8));
}

constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return static_cast<uint16_t>(((red & 0xf8U) << 8) |
                                 ((green & 0xfcU) << 3) | (blue >> 3));
}

esp_err_t init_i2c()
{
    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = kI2cSda,
        .scl_io_num = kI2cScl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {.enable_internal_pullup = true, .allow_pd = false},
    };
    return i2c_new_master_bus(&config, &i2c_bus);
}

esp_err_t init_storage()
{
    const esp_vfs_fat_mount_config_t config = {
        .format_if_mount_failed = true,
        .max_files = 2,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };
    esp_err_t result = esp_vfs_fat_spiflash_mount_rw_wl(
        kStoragePath, "storage", &config, &storage_wl_handle);
    if (result == ESP_OK) {
        ESP_LOGI(kTag, "writable game storage mounted at %s", kStoragePath);
    }
    return result;
}

uint8_t *load_writable_disc(bc32_disc_id_t id, const uint8_t *embedded,
                            size_t embedded_size, char *path,
                            size_t path_size, size_t *disc_size)
{
    if (embedded == nullptr || embedded_size < 0x108 || path == nullptr ||
        disc_size == nullptr) {
        return nullptr;
    }

    const size_t catalogue_sectors =
        (static_cast<size_t>(embedded[0x106] & 3U) << 8) | embedded[0x107];
    size_t capacity = catalogue_sectors * 256U;
    if (capacity < embedded_size) {
        capacity = (embedded_size + 255U) & ~static_cast<size_t>(255U);
    }
    if (capacity == 0 || capacity > 80U * 10U * 256U) return nullptr;

    const int path_length = snprintf(path, path_size, "%s/d%02u.ssd",
                                     kStoragePath, static_cast<unsigned>(id));
    if (path_length < 0 || static_cast<size_t>(path_length) >= path_size) {
        return nullptr;
    }

    bool create = true;
    FILE *file = fopen(path, "rb");
    if (file != nullptr) {
        if (fseek(file, 0, SEEK_END) == 0 && ftell(file) == (long)capacity) {
            create = false;
        }
        fclose(file);
    }

    if (create) {
        char temporary_path[48];
        const int temporary_length = snprintf(
            temporary_path, sizeof(temporary_path), "%s/d%02u.tmp",
            kStoragePath, static_cast<unsigned>(id));
        if (temporary_length < 0 ||
            static_cast<size_t>(temporary_length) >= sizeof(temporary_path)) {
            return nullptr;
        }
        file = fopen(temporary_path, "wb");
        if (file == nullptr) return nullptr;

        bool written = fwrite(embedded, 1, embedded_size, file) == embedded_size;
        static const uint8_t zeros[4096] = {};
        size_t remaining = capacity - embedded_size;
        while (written && remaining != 0) {
            const size_t chunk = remaining < sizeof(zeros) ? remaining : sizeof(zeros);
            written = fwrite(zeros, 1, chunk, file) == chunk;
            remaining -= chunk;
        }
        if (fclose(file) != 0) written = false;
        if (!written) {
            remove(temporary_path);
            return nullptr;
        }
        remove(path);
        if (rename(temporary_path, path) != 0) {
            remove(temporary_path);
            return nullptr;
        }
        ESP_LOGI(kTag, "created writable SSD %s (%u bytes)", path,
                 static_cast<unsigned>(capacity));
    }

    file = fopen(path, "rb");
    if (file == nullptr) return nullptr;
    auto *image = static_cast<uint8_t *>(
        heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    const bool loaded = image != nullptr && fread(image, 1, capacity, file) == capacity;
    fclose(file);
    if (!loaded) {
        heap_caps_free(image);
        return nullptr;
    }
    *disc_size = capacity;
    return image;
}

void init_scaler()
{
    for (int x = 0; x < kViewportWidth; ++x) {
        source_x[x] =
            static_cast<uint16_t>((x * BBC_FRAME_WIDTH) / kViewportWidth);
    }

    for (unsigned index = 0; index < 8; ++index) {
        const uint8_t red = (index & 1U) != 0 ? 255 : 0;
        const uint8_t green = (index & 2U) != 0 ? 255 : 0;
        const uint8_t blue = (index & 4U) != 0 ? 255 : 0;
        palette[index] = swap16(rgb565(red, green, blue));
    }
}

void render_frame(const uint8_t *framebuffer)
{
    // Publishing the pointer is effectively free during ordinary play. The
    // opt-in serial capture task makes its own short-lived copy before it
    // compresses a frame, so panel DMA and emulation never wait for USB.
    __atomic_store_n(&screenshot_source, framebuffer, __ATOMIC_RELEASE);

    for (int band = 0; band < DISPLAY_BAND_COUNT; ++band) {
        uint16_t *destination = display_acquire_band();
        const int band_y = band * DISPLAY_BAND_ROWS;
        memset(destination, 0,
               DISPLAY_WIDTH * DISPLAY_BAND_ROWS * sizeof(*destination));

        // Traverse each BBC source row across the narrow tile so reads from
        // PSRAM stay contiguous. The transposed/strided writes land in the
        // internal-SRAM DMA band and are therefore cheap.
        for (int physical_x = kViewportTop;
             physical_x < kViewportTop + kViewportHeight; ++physical_x) {
            const int source_y =
                ((physical_x - kViewportTop) * BBC_FRAME_HEIGHT) / kViewportHeight;
            const uint8_t *source = framebuffer + source_y * BBC_FRAME_WIDTH;
            for (int row = 0; row < DISPLAY_BAND_ROWS; ++row) {
                const int logical_x = DISPLAY_HEIGHT - 1 - (band_y + row);
                if (logical_x < kViewportLeft ||
                    logical_x >= kViewportLeft + kViewportWidth) {
                    continue;
                }
                destination[row * DISPLAY_WIDTH + physical_x] =
                    palette[source[source_x[logical_x - kViewportLeft]] & 7U];
            }
        }

        ESP_ERROR_CHECK(display_flush_band(band, destination));
    }
}

uint32_t screenshot_hash(const uint8_t *data, size_t size)
{
    uint32_t hash = 2166136261U;
    for (size_t offset = 0; offset < size; ++offset) {
        hash ^= data[offset];
        hash *= 16777619U;
    }
    return hash;
}

size_t encode_screenshot_rle(const uint8_t *source, uint8_t *destination)
{
    size_t source_offset = 0;
    size_t destination_offset = 0;
    while (source_offset < kFrameBytes) {
        const uint8_t colour = source[source_offset] & 7U;
        size_t run = 1;
        while (run < 32 && source_offset + run < kFrameBytes &&
               (source[source_offset + run] & 7U) == colour) {
            ++run;
        }
        destination[destination_offset++] =
            static_cast<uint8_t>((colour << 5U) | (run - 1U));
        source_offset += run;
    }
    return destination_offset;
}

bool usb_write_all(const void *data, size_t size)
{
    const auto *bytes = static_cast<const uint8_t *>(data);
    size_t written = 0;
    while (written < size) {
        // The driver's FreeRTOS ring buffer stores each write as one item, so
        // an item must be comfortably smaller than its 4096-byte capacity.
        const size_t chunk = std::min<size_t>(size - written, 1024);
        const int count = usb_serial_jtag_write_bytes(
            bytes + written, chunk, pdMS_TO_TICKS(2000));
        if (count <= 0) return false;
        written += static_cast<size_t>(count);
    }
    return true;
}

void emit_screenshot(uint8_t *snapshot, uint8_t *encoded, unsigned sequence)
{
    const uint8_t *source =
        __atomic_load_n(&screenshot_source, __ATOMIC_ACQUIRE);
    if (source == nullptr) return;

    // The copy takes well under a display period. A frame caught exactly on a
    // launcher transition may contain that transition, which is useful when
    // reviewing an automatic sequence and harmless because later frames stay.
    memcpy(snapshot, source, kFrameBytes);
    const size_t encoded_size = encode_screenshot_rle(snapshot, encoded);
    const uint32_t hash = screenshot_hash(encoded, encoded_size);

    char header[96];
    const int header_size = snprintf(
        header, sizeof(header), "\nBC32_FRAME %u %lld %u %08lx\n", sequence,
        static_cast<long long>(esp_timer_get_time() / 1000),
        static_cast<unsigned>(encoded_size), static_cast<unsigned long>(hash));

    // Keep the binary envelope free from periodic emulator/BLE log messages.
    esp_log_level_set("*", ESP_LOG_NONE);
    constexpr char kFrameEnd[] = "\nBC32_END\n";
    const bool ok = header_size > 0 &&
                    usb_write_all(header, static_cast<size_t>(header_size)) &&
                    usb_write_all(encoded, encoded_size) &&
                    usb_write_all(kFrameEnd, sizeof(kFrameEnd) - 1);
    if (ok) usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(5000));
    esp_log_level_set("*", ESP_LOG_INFO);
    if (!ok) ESP_LOGW(kTag, "USB screenshot transfer failed");
}

void screenshot_command_task(void *)
{
    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = 4096,
        .rx_buffer_size = 256,
    };
    const esp_err_t driver_result = usb_serial_jtag_driver_install(&config);
    if (driver_result != ESP_OK && driver_result != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "screenshot USB driver unavailable: %s",
                 esp_err_to_name(driver_result));
        vTaskDelete(nullptr);
    }

    auto *snapshot = static_cast<uint8_t *>(
        heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto *encoded = static_cast<uint8_t *>(
        heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (snapshot == nullptr || encoded == nullptr) {
        ESP_LOGW(kTag, "not enough PSRAM for screenshot capture");
        heap_caps_free(snapshot);
        heap_caps_free(encoded);
        vTaskDelete(nullptr);
    }

    char command[48] = {};
    size_t command_size = 0;
    unsigned sequence = 0;
    for (;;) {
        uint8_t byte = 0;
        const int received = usb_serial_jtag_read_bytes(
            &byte, 1, pdMS_TO_TICKS(250));
        if (received <= 0) continue;
        if (byte == '\r') continue;
        if (byte != '\n' && command_size + 1 < sizeof(command)) {
            command[command_size++] = static_cast<char>(byte);
            continue;
        }

        command[command_size] = '\0';
        if (strcmp(command, "BC32_CAPTURE_START") == 0) {
            screenshot_capture_active = true;
            constexpr char kReady[] = "\nBC32_CAPTURE_READY 640 256 8\n";
            usb_write_all(kReady, sizeof(kReady) - 1);
        } else if (strcmp(command, "BC32_CAPTURE_STOP") == 0) {
            screenshot_capture_active = false;
            constexpr char kStopped[] = "\nBC32_CAPTURE_STOPPED\n";
            usb_write_all(kStopped, sizeof(kStopped) - 1);
        } else if (strcmp(command, "BC32_SCREENSHOT") == 0 &&
                   screenshot_capture_active) {
            emit_screenshot(snapshot, encoded, sequence++);
        }
        command_size = 0;
    }
}

void draw_text(uint8_t *framebuffer, int x, int y, const char *text,
               uint8_t colour, int scale = 1)
{
    constexpr int kGlyphWidth = 12;
    constexpr int kGlyphHeight = 18;
    while (*text != '\0') {
        unsigned character = static_cast<unsigned char>(*text++);
        if (character < 32 || character > 127) character = '?';
        const uint8_t *glyph = launcher_font + (character - 32) * 36;
        for (int gy = 0; gy < kGlyphHeight; ++gy) {
            const uint16_t bits = static_cast<uint16_t>(glyph[gy * 2]) |
                                  (static_cast<uint16_t>(glyph[gy * 2 + 1]) << 8);
            for (int gx = 0; gx < 10; ++gx) {
                if ((bits & (1U << (9 - gx))) == 0) continue;
                for (int sy = 0; sy < scale; ++sy) {
                    const int py = y + gy * scale + sy;
                    if (py < 0 || py >= BBC_FRAME_HEIGHT) continue;
                    for (int sx = 0; sx < scale; ++sx) {
                        const int px = x + (gx + 1) * scale + sx;
                        if (px >= 0 && px < BBC_FRAME_WIDTH) {
                            framebuffer[py * BBC_FRAME_WIDTH + px] = colour;
                        }
                    }
                }
            }
        }
        x += kGlyphWidth * scale;
    }
}

void draw_centred_text(uint8_t *framebuffer, int y, const char *text,
                       uint8_t colour, int scale = 1)
{
    const int width = static_cast<int>(strlen(text)) * 12 * scale;
    draw_text(framebuffer, (BBC_FRAME_WIDTH - width) / 2, y, text, colour,
              scale);
}

void fill_rect(uint8_t *framebuffer, int x, int y, int width, int height,
               uint8_t colour)
{
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x + width > BBC_FRAME_WIDTH) width = BBC_FRAME_WIDTH - x;
    if (y + height > BBC_FRAME_HEIGHT) height = BBC_FRAME_HEIGHT - y;
    if (width <= 0 || height <= 0) return;
    for (int row = 0; row < height; ++row) {
        memset(framebuffer + (y + row) * BBC_FRAME_WIDTH + x, colour,
               static_cast<size_t>(width));
    }
}

void fill_ellipse(uint8_t *framebuffer, int centre_x, int centre_y,
                  int radius_x, int radius_y, uint8_t colour)
{
    const int64_t radius_x_squared = radius_x * radius_x;
    const int64_t radius_y_squared = radius_y * radius_y;
    const int64_t ellipse_limit = radius_x_squared * radius_y_squared;
    for (int y = centre_y - radius_y; y <= centre_y + radius_y; ++y) {
        if (y < 0 || y >= BBC_FRAME_HEIGHT) continue;
        const int dy = y - centre_y;
        for (int x = centre_x - radius_x; x <= centre_x + radius_x; ++x) {
            if (x < 0 || x >= BBC_FRAME_WIDTH) continue;
            const int dx = x - centre_x;
            if (static_cast<int64_t>(dx) * dx * radius_y_squared +
                    static_cast<int64_t>(dy) * dy * radius_x_squared <=
                ellipse_limit) {
                framebuffer[y * BBC_FRAME_WIDTH + x] = colour;
            }
        }
    }
}

unsigned wrap_picker_index(int index)
{
    const int count = static_cast<int>(kPickerKeyCount);
    index %= count;
    if (index < 0) index += count;
    return static_cast<unsigned>(index);
}

void render_character_picker(uint8_t *framebuffer)
{
    const unsigned selected =
        __atomic_load_n(&picker_index, __ATOMIC_ACQUIRE) % kPickerKeyCount;
    const int logical_drag =
        __atomic_load_n(&picker_drag_x, __ATOMIC_ACQUIRE);
    const int source_drag = logical_drag * BBC_FRAME_WIDTH /
                            DISPLAY_LOGICAL_WIDTH;

    fill_rect(framebuffer, 0, 0, BBC_FRAME_WIDTH, BBC_FRAME_HEIGHT, 0);
    draw_centred_text(framebuffer, 12, "SHAKE KEYBOARD", 6);
    draw_centred_text(framebuffer, 37, "SWIPE TO CHOOSE", 7);

    constexpr int kCardY = 69;
    constexpr int kCardWidth = 96;
    constexpr int kCardHeight = 106;
    constexpr int kCardStride = 116;
    for (int offset = -4; offset <= 4; ++offset) {
        const int centre_x = BBC_FRAME_WIDTH / 2 + offset * kCardStride +
                             source_drag;
        const int card_x = centre_x - kCardWidth / 2;
        if (card_x >= BBC_FRAME_WIDTH || card_x + kCardWidth <= 0) continue;

        const picker_key_t &entry =
            kPickerKeys[wrap_picker_index(static_cast<int>(selected) + offset)];
        const bool is_selected = offset == 0;
        fill_rect(framebuffer, card_x, kCardY, kCardWidth, kCardHeight,
                  is_selected ? 6 : 7);
        fill_rect(framebuffer, card_x + 4, kCardY + 4, kCardWidth - 8,
                  kCardHeight - 8, is_selected ? 0 : 1);

        const size_t length = strlen(entry.label);
        const int scale = length == 1 ? 3 : length <= 3 ? 2 : 1;
        const int text_width = static_cast<int>(length) * 12 * scale;
        const int text_y = kCardY + (kCardHeight - 18 * scale) / 2;
        draw_text(framebuffer, centre_x - text_width / 2, text_y,
                  entry.label, is_selected ? 6 : 7, scale);
    }

    char position[32];
    snprintf(position, sizeof(position), "%s  %u/%u",
             kPickerKeys[selected].label, selected + 1, kPickerKeyCount);
    draw_centred_text(framebuffer, 188, position, 6);
    draw_centred_text(framebuffer, 214, "TAP TO TYPE", 7);
    draw_centred_text(framebuffer, 235, "SHAKE AGAIN TO CLOSE", 1);
}

void draw_play_button(uint8_t *framebuffer)
{
    constexpr int centre_x = BBC_FRAME_WIDTH / 2;
    constexpr int centre_y = 108;
    // 640x256 BBC pixels are non-square when shown in the 4:3 viewport. These
    // source radii become a 38-pixel circle after the display scaler.
    fill_ellipse(framebuffer, centre_x, centre_y, 54, 29, 7);
    fill_ellipse(framebuffer, centre_x, centre_y, 47, 25, 4);
    for (int y = -16; y <= 16; ++y) {
        const int right = 31 - (y < 0 ? -y : y) * 43 / 16;
        for (int x = -12; x <= right; ++x) {
            framebuffer[(centre_y + y) * BBC_FRAME_WIDTH + centre_x + x] = 7;
        }
    }
}

void draw_input_button(uint8_t *framebuffer, int x, int width,
                       const char *label, bool selected, bool enabled)
{
    const uint8_t border = enabled ? (selected ? 6 : 7) : 1;
    fill_rect(framebuffer, x, 229, width, 25, border);
    fill_rect(framebuffer, x + 3, 232, width - 6, 19,
              enabled && selected ? 6 : 0);
    const int text_width = static_cast<int>(strlen(label)) * 12;
    draw_text(framebuffer, x + (width - text_width) / 2, 232, label,
              enabled ? (selected ? 0 : 7) : 1);
}

void render_launcher(uint8_t *framebuffer, unsigned selected_game,
                     input_mode_t mode, bool keyboard_connected)
{
    ESP_ERROR_CHECK(bc32_assets_decode_screenshot(
        kGames[selected_game].disc, framebuffer,
        BBC_FRAME_WIDTH * BBC_FRAME_HEIGHT) ? ESP_OK : ESP_FAIL);

    fill_rect(framebuffer, 0, 0, BBC_FRAME_WIDTH, 34, 0);
    draw_centred_text(framebuffer, 7, kGames[selected_game].name, 7);
    draw_text(framebuffer, 14, 94, "<", 7, 2);
    draw_text(framebuffer, 594, 94, ">", 7, 2);
    draw_play_button(framebuffer);

    fill_rect(framebuffer, 0, 179, BBC_FRAME_WIDTH, 77, 0);
    draw_centred_text(framebuffer, 182, kGames[selected_game].tilt_hint, 6);
    const int dots_width = static_cast<int>(kGameCount) * 18 - 8;
    int dot_x = (BBC_FRAME_WIDTH - dots_width) / 2;
    for (unsigned index = 0; index < kGameCount; ++index, dot_x += 18) {
        fill_rect(framebuffer, dot_x, 213, 10, 6,
                  index == selected_game ? 3 : 7);
    }
    draw_input_button(framebuffer, 24, 160, "TILT",
                      mode == input_mode_t::tilt, true);
    draw_input_button(framebuffer, 240, 160, "TOUCH",
                      mode == input_mode_t::touch, true);
    char keyboard_label[20];
    const uint32_t passkey = bc32_ble_keyboard_pairing_passkey();
    if (keyboard_connected) {
        snprintf(keyboard_label, sizeof(keyboard_label), "KEYBOARD");
    } else if (passkey != 0) {
        snprintf(keyboard_label, sizeof(keyboard_label), "%06lu ENTER",
                 static_cast<unsigned long>(passkey));
    } else {
        snprintf(keyboard_label, sizeof(keyboard_label), "PAIRING...");
    }
    draw_input_button(framebuffer, 456, 160, keyboard_label,
                      mode == input_mode_t::keyboard && keyboard_connected,
                      keyboard_connected);
    fill_rect(framebuffer, 600, 235, 7, 7,
              keyboard_connected ? 2 : 1);
}

void render_keyboard_notice(uint8_t *framebuffer, bool connected)
{
    fill_rect(framebuffer, 50, 70, 540, 106, 7);
    fill_rect(framebuffer, 56, 76, 528, 94, 0);
    draw_centred_text(framebuffer, 91,
                      connected ? "BLE KEYBOARD CONNECTED"
                                : "KEYBOARD DISCONNECTED",
                      connected ? 2 : 1);
    draw_centred_text(framebuffer, 128,
                      connected ? "KEYBOARD MODE UNLOCKED"
                                : motion_input_available
                                      ? "USING TILT CONTROLS"
                                      : "USING TOUCH CONTROLS",
                      7);
}

void render_pairing_notice(uint8_t *framebuffer, uint32_t passkey)
{
    fill_rect(framebuffer, 40, 58, 560, 132, 7);
    fill_rect(framebuffer, 46, 64, 548, 120, 0);
    draw_centred_text(framebuffer, 78, "PAIR BLUETOOTH KEYBOARD", 7);
    char code[16];
    snprintf(code, sizeof(code), "%06lu", static_cast<unsigned long>(passkey));
    draw_centred_text(framebuffer, 112, code, 2, 2);
    draw_centred_text(framebuffer, 158, "TYPE CODE, THEN PRESS ENTER", 7);
}

void render_calibration(uint8_t *framebuffer, unsigned selected_game)
{
    ESP_ERROR_CHECK(bc32_assets_decode_screenshot(
        kGames[selected_game].disc, framebuffer,
        BBC_FRAME_WIDTH * BBC_FRAME_HEIGHT) ? ESP_OK : ESP_FAIL);
    fill_rect(framebuffer, 66, 72, 508, 112, 7);
    fill_rect(framebuffer, 72, 78, 496, 100, 0);
    draw_centred_text(framebuffer, 92, "HOLD AT PLAY ANGLE", 6);
    draw_centred_text(framebuffer, 128, "CALIBRATING TILT...", 7);
}

void animate_launcher(uint8_t *current, uint8_t *incoming, uint8_t *composite,
                      unsigned selected_game, int direction, input_mode_t mode,
                      bool keyboard_connected)
{
    render_launcher(incoming, selected_game, mode, keyboard_connected);
    constexpr int kFrames = 7;
    for (int frame = 1; frame <= kFrames; ++frame) {
        // Smoothstep makes the page leave and settle gently without needing
        // floating point: p = t*t*(3-2*t).
        const int t = frame * 1024 / kFrames;
        const int eased = t * t / 1024 * (3072 - 2 * t) / 1024;
        const int shift = eased * BBC_FRAME_WIDTH / 1024;
        for (int y = 0; y < BBC_FRAME_HEIGHT; ++y) {
            const uint8_t *old_row = current + y * BBC_FRAME_WIDTH;
            const uint8_t *new_row = incoming + y * BBC_FRAME_WIDTH;
            uint8_t *output = composite + y * BBC_FRAME_WIDTH;
            if (direction > 0) {
                const int old_width = BBC_FRAME_WIDTH - shift;
                if (old_width > 0) {
                    memcpy(output, old_row + shift,
                           static_cast<size_t>(old_width));
                }
                if (shift > 0) {
                    memcpy(output + old_width, new_row,
                           static_cast<size_t>(shift));
                }
            } else {
                if (shift > 0) {
                    memcpy(output, new_row + BBC_FRAME_WIDTH - shift,
                           static_cast<size_t>(shift));
                }
                const int old_width = BBC_FRAME_WIDTH - shift;
                if (old_width > 0) {
                    memcpy(output + shift, old_row,
                           static_cast<size_t>(old_width));
                }
            }
        }
        render_frame(composite);
    }
    memcpy(current, incoming, BBC_FRAME_WIDTH * BBC_FRAME_HEIGHT);
}

void queue_input(const bc32_input_event_t *event, void *)
{
    if (input_queue != nullptr) {
        // Producers run on the motion and NimBLE tasks. The emulator drains
        // this queue on core 1, keeping BeebEm state single-threaded.
        (void)xQueueSend(input_queue, event, 0);
    }
}

bool matrix_key_matches(const bc32_input_event_t &event, matrix_key_t key_value)
{
    return event.kind == BC32_INPUT_KEY && event.key.row == key_value.row &&
           event.key.column == key_value.column;
}

const char *input_mode_name(input_mode_t mode)
{
    switch (mode) {
    case input_mode_t::tilt: return "tilt";
    case input_mode_t::touch: return "touch";
    case input_mode_t::keyboard: return "Bluetooth keyboard";
    }
    return "unknown";
}

input_mode_t cycle_input_mode(input_mode_t mode, int direction,
                              bool keyboard_connected)
{
    int candidate = static_cast<int>(mode);
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        candidate = (candidate + direction + 3) % 3;
        const auto next = static_cast<input_mode_t>(candidate);
        if (next != input_mode_t::keyboard || keyboard_connected) return next;
    }
    return input_mode_t::touch;
}

unsigned run_launcher(uint8_t *framebuffer, uint8_t *incoming,
                      uint8_t *composite, input_mode_t *mode)
{
    unsigned selected_game = 0;
    bool keyboard_connected = bc32_ble_keyboard_connected();
    *mode = keyboard_connected
                ? input_mode_t::keyboard
                : motion_input_available ? input_mode_t::tilt
                                         : input_mode_t::touch;
    uint32_t keyboard_passkey = bc32_ble_keyboard_pairing_passkey();
    render_launcher(framebuffer, selected_game, *mode, keyboard_connected);
    render_frame(framebuffer);
    ESP_LOGI(kTag,
             "launcher ready: swipe or BLE arrows to browse, tap PLAY to launch");
    if (keyboard_connected) {
        render_keyboard_notice(framebuffer, true);
        render_frame(framebuffer);
        vTaskDelay(pdMS_TO_TICKS(1800));
        render_launcher(framebuffer, selected_game, *mode, true);
        render_frame(framebuffer);
    }

    // Input producers continue running while notices are displayed. Discard
    // anything accumulated during the transition so a stale action/key press
    // cannot immediately launch the game shown underneath the notice.
    xQueueReset(input_queue);

    bool touch_down = false;
    int touch_start_x = 0;
    int touch_start_y = 0;
    int touch_last_x = 0;
    int touch_last_y = 0;
    // A touch that selected RESTART can still be reported for a few scan
    // periods after the game/launcher transition. Require a quiet release
    // window before accepting launcher touches, otherwise that same contact
    // can become an unintended tap on the central Play button.
    bool launcher_touch_armed = false;
    int64_t touch_quiet_since_us = esp_timer_get_time();
    int64_t launcher_input_guard_until_us =
        touch_quiet_since_us + kLauncherInputGuardUs;

    for (;;) {
        bc32_input_event_t event{};
        bool have_event =
            xQueueReceive(input_queue, &event, pdMS_TO_TICKS(250)) == pdTRUE;
        const bool connected = bc32_ble_keyboard_connected();
        const uint32_t passkey = bc32_ble_keyboard_pairing_passkey();
        if (!connected && passkey != keyboard_passkey) {
            keyboard_passkey = passkey;
            render_launcher(framebuffer, selected_game, *mode, false);
            if (passkey != 0) render_pairing_notice(framebuffer, passkey);
            render_frame(framebuffer);
        }
        if (connected != keyboard_connected) {
            keyboard_connected = connected;
            keyboard_passkey = passkey;
            if (keyboard_connected) {
                *mode = input_mode_t::keyboard;
            } else if (*mode == input_mode_t::keyboard) {
                *mode = motion_input_available ? input_mode_t::tilt
                                               : input_mode_t::touch;
            }
            render_launcher(framebuffer, selected_game, *mode,
                            keyboard_connected);
            render_keyboard_notice(framebuffer, keyboard_connected);
            render_frame(framebuffer);
            ESP_LOGI(kTag, "BLE keyboard %s; keyboard mode %s",
                     keyboard_connected ? "connected" : "disconnected",
                     keyboard_connected ? "unlocked" : "locked");
            vTaskDelay(pdMS_TO_TICKS(keyboard_connected ? 1800 : 1200));
            render_launcher(framebuffer, selected_game, *mode,
                            keyboard_connected);
            render_frame(framebuffer);
            xQueueReset(input_queue);
            have_event = false;
            launcher_touch_armed = false;
            touch_down = false;
            touch_quiet_since_us = esp_timer_get_time();
            launcher_input_guard_until_us =
                touch_quiet_since_us + kLauncherInputGuardUs;
        }

        const int64_t input_now_us = esp_timer_get_time();
        if (!launcher_touch_armed &&
            input_now_us - touch_quiet_since_us >=
                kLauncherTouchReleaseGuardUs) {
            launcher_touch_armed = true;
            ESP_LOGI(kTag, "launcher touch armed after release");
        }
        if (!have_event) continue;

        if (event.kind == BC32_INPUT_TOUCH && !launcher_touch_armed) {
            touch_quiet_since_us = input_now_us;
            touch_down = false;
            continue;
        }
        if (input_now_us < launcher_input_guard_until_us &&
            ((event.kind == BC32_INPUT_KEY && event.key.down) ||
             (event.kind == BC32_INPUT_ACTION && event.action.down))) {
            ESP_LOGI(kTag, "discarding carried input during launcher guard");
            continue;
        }

        bool redraw = false;
        bool launch = false;
        int page_direction = 0;
        // Motion direction events are deliberately ignored in the launcher:
        // browse games with a horizontal swipe or BLE Left/Right arrows. Tilt
        // starts controlling movement only after a game has launched.
        if (event.kind == BC32_INPUT_KEY && event.key.down) {
            if (matrix_key_matches(event, key(1, 9))) {
                selected_game = (selected_game + kGameCount - 1) % kGameCount;
                page_direction = -1;
            } else if (matrix_key_matches(event, key(7, 9))) {
                selected_game = (selected_game + 1) % kGameCount;
                page_direction = 1;
            } else if (matrix_key_matches(event, key(3, 9))) {
                *mode = cycle_input_mode(*mode, -1, keyboard_connected);
                redraw = true;
            } else if (matrix_key_matches(event, key(2, 9))) {
                *mode = cycle_input_mode(*mode, 1, keyboard_connected);
                redraw = true;
            } else if (matrix_key_matches(event, key(4, 9)) ||
                       matrix_key_matches(event, key(6, 2))) {
                launch = true;
            }
        } else if (event.kind == BC32_INPUT_ACTION && event.action.down) {
            launch = true;
        } else if (event.kind == BC32_INPUT_TOUCH) {
            if (event.touch.down) {
                if (!touch_down) {
                    touch_down = true;
                    touch_start_x = event.touch.x;
                    touch_start_y = event.touch.y;
                }
                touch_last_x = event.touch.x;
                touch_last_y = event.touch.y;
            } else if (touch_down) {
                touch_down = false;
                const int delta_x = touch_last_x - touch_start_x;
                const int delta_y = touch_last_y - touch_start_y;
                const int abs_x = delta_x < 0 ? -delta_x : delta_x;
                const int abs_y = delta_y < 0 ? -delta_y : delta_y;
                if (abs_x >= 60 && abs_x > abs_y + 20) {
                    if (delta_x < 0) {
                        selected_game = (selected_game + 1) % kGameCount;
                        page_direction = 1;
                    } else {
                        selected_game =
                            (selected_game + kGameCount - 1) % kGameCount;
                        page_direction = -1;
                    }
                    ESP_LOGI(kTag, "launcher swipe -> %s",
                             kGames[selected_game].name);
                } else if (abs_x < 24 && abs_y < 24 &&
                           touch_last_y >= kViewportTop &&
                           touch_last_y < kViewportTop + kViewportHeight) {
                    const int source_touch_x =
                        (touch_last_x - kViewportLeft) * BBC_FRAME_WIDTH /
                        kViewportWidth;
                    const int source_touch_y =
                        (touch_last_y - kViewportTop) * BBC_FRAME_HEIGHT /
                        kViewportHeight;
                    const int play_x = source_touch_x - BBC_FRAME_WIDTH / 2;
                    const int play_y = source_touch_y - 108;
                    const bool in_mode_touch_area =
                        touch_start_y >= kLauncherModeTouchTop &&
                        touch_last_y >= kLauncherModeTouchTop;
                    if (play_x * play_x + play_y * play_y <= 55 * 55) {
                        launch = true;
                    } else if (in_mode_touch_area && source_touch_y >= 224 &&
                               source_touch_x >= 24 && source_touch_x < 184) {
                        *mode = input_mode_t::tilt;
                        redraw = true;
                    } else if (in_mode_touch_area && source_touch_y >= 224 &&
                               source_touch_x >= 240 && source_touch_x < 400) {
                        *mode = input_mode_t::touch;
                        redraw = true;
                    } else if (in_mode_touch_area && source_touch_y >= 224 &&
                               source_touch_x >= 456 && source_touch_x < 616) {
                        if (keyboard_connected) {
                            *mode = input_mode_t::keyboard;
                            redraw = true;
                        }
                    }
                }
            }
        }

        if (launch && *mode == input_mode_t::keyboard &&
            !bc32_ble_keyboard_connected()) {
            keyboard_connected = false;
            *mode = motion_input_available ? input_mode_t::tilt
                                           : input_mode_t::touch;
            launch = false;
            redraw = true;
        }
        if (launch) {
            ESP_LOGI(kTag, "launcher selected %s with %s input",
                     kGames[selected_game].name,
                     input_mode_name(*mode));
            const uint16_t vertical_sensitivity =
                kGames[selected_game].tilt_vertical_sensitivity_percent != 0
                    ? kGames[selected_game].tilt_vertical_sensitivity_percent
                    : kGames[selected_game].tilt_sensitivity_percent;
            bc32_motion_input_set_sensitivity(
                kGames[selected_game].tilt_sensitivity_percent,
                vertical_sensitivity);
            bc32_motion_input_set_allow_diagonals(
                kGames[selected_game].allow_diagonals);
            if (*mode == input_mode_t::tilt && motion_input_available) {
                bc32_motion_input_recalibrate();
                render_calibration(framebuffer, selected_game);
                render_frame(framebuffer);
                unsigned wait_ticks = 0;
                while (bc32_motion_input_is_calibrating() && wait_ticks < 150) {
                    vTaskDelay(pdMS_TO_TICKS(40));
                    ++wait_ticks;
                }
                if (bc32_motion_input_is_calibrating()) {
                    ESP_LOGW(kTag, "tilt recalibration timed out; continuing");
                }
            }
            xQueueReset(input_queue);
            return selected_game;
        }
        if (page_direction != 0) {
            animate_launcher(framebuffer, incoming, composite, selected_game,
                             page_direction, *mode, keyboard_connected);
        } else if (redraw) {
            render_launcher(framebuffer, selected_game, *mode,
                            keyboard_connected);
            render_frame(framebuffer);
        }
    }
}

void write_sound_register(uint8_t value, void *)
{
    bc32_audio_reg_write(value);
}

void set_matrix_key(matrix_key_t key_value, bool down)
{
    if (!key_value.valid) return;
    if (key_value.row >= 8 || key_value.column >= 10) return;
    uint8_t &holds = matrix_key_holds[key_value.row][key_value.column];
    if (down) {
        if (holds != UINT8_MAX && holds++ == 0) {
            bbc_core_key_down(key_value.row, key_value.column);
        }
    } else {
        if (holds != 0 && --holds == 0) {
            bbc_core_key_up(key_value.row, key_value.column);
        }
    }
}

void set_game_direction(unsigned direction, bool down)
{
    if (active_game == nullptr || direction > BC32_DIRECTION_DOWN) return;
    uint8_t &holds = direction_hold_count[direction];
    if (down) {
        if (holds == UINT8_MAX || holds++ != 0) return;
    } else {
        if (holds == 0 || --holds != 0) return;
    }
    const matrix_key_t modifier = active_game->direction_modifier[direction];
    const matrix_key_t direction_key = active_game->direction[direction];
    if (down) {
        set_matrix_key(modifier, true);
        set_matrix_key(direction_key, true);
        direction_repeat_released[direction] = false;
        const bool pulse_tilt =
            input_mode == input_mode_t::tilt &&
            active_game->direction_pulse_fields != 0;
        const uint8_t timing_fields =
            pulse_tilt
                ? active_game->direction_pulse_fields
                : active_game->direction_repeat_fields;
        direction_repeat_deadline[direction] =
            direction_key.valid && timing_fields != 0
                ? bbc_core_frame_count() + timing_fields
                : 0;
    } else {
        if (!direction_repeat_released[direction]) {
            set_matrix_key(direction_key, false);
            set_matrix_key(modifier, false);
        }
        direction_repeat_released[direction] = false;
        direction_repeat_deadline[direction] = 0;
    }

    // Joystick-only titles still need useful digital controls in Touch mode.
    // Tilt supplies proportional values continuously; these cardinal values
    // are harmless threshold updates there and are soon replaced by the next
    // motion sample.
    if (active_game->joystick_controls) {
        const uint16_t x = direction_hold_count[BC32_DIRECTION_LEFT] != 0
                               ? UINT16_MAX
                           : direction_hold_count[BC32_DIRECTION_RIGHT] != 0
                               ? 0
                               : 32768;
        const uint16_t y = direction_hold_count[BC32_DIRECTION_UP] != 0
                               ? UINT16_MAX
                           : direction_hold_count[BC32_DIRECTION_DOWN] != 0
                               ? 0
                               : 32768;
        bbc_core_set_joystick(x, y);
    }
}

void service_direction_repeat()
{
    if (active_game == nullptr ||
        (active_game->direction_repeat_fields == 0 &&
         (input_mode != input_mode_t::tilt ||
          active_game->direction_pulse_fields == 0))) {
        return;
    }
    const uint32_t frame = bbc_core_frame_count();
    for (unsigned direction = BC32_DIRECTION_LEFT;
         direction <= BC32_DIRECTION_DOWN; ++direction) {
        if (direction_hold_count[direction] == 0 ||
            direction_repeat_deadline[direction] == 0 ||
            static_cast<int32_t>(frame -
                                 direction_repeat_deadline[direction]) < 0) {
            continue;
        }
        const matrix_key_t modifier =
            active_game->direction_modifier[direction];
        const matrix_key_t direction_key = active_game->direction[direction];
        if (input_mode == input_mode_t::tilt &&
            active_game->direction_pulse_fields != 0) {
            if (direction_repeat_released[direction]) {
                set_matrix_key(modifier, true);
                set_matrix_key(direction_key, true);
                direction_repeat_released[direction] = false;
                direction_repeat_deadline[direction] =
                    frame + active_game->direction_pulse_fields;
            } else {
                set_matrix_key(direction_key, false);
                set_matrix_key(modifier, false);
                direction_repeat_released[direction] = true;
                direction_repeat_deadline[direction] =
                    active_game->tilt_pulse_repeat_fields != 0
                        ? frame + active_game->tilt_pulse_repeat_fields
                        : 0;
            }
        } else if (direction_repeat_released[direction]) {
            set_matrix_key(modifier, true);
            set_matrix_key(direction_key, true);
            direction_repeat_released[direction] = false;
            direction_repeat_deadline[direction] =
                frame + active_game->direction_repeat_fields;
        } else {
            set_matrix_key(direction_key, false);
            set_matrix_key(modifier, false);
            direction_repeat_released[direction] = true;
            direction_repeat_deadline[direction] = frame + 2;
        }
    }
}

constexpr touch_control_t classify_touch_control(int x, int y)
{
    constexpr int centre_x = DISPLAY_LOGICAL_WIDTH / 2;
    constexpr int centre_y = DISPLAY_LOGICAL_HEIGHT / 2;
    constexpr int action_radius = 56;
    const int dx = x - centre_x;
    const int dy = y - centre_y;
    if (dx * dx + dy * dy <= action_radius * action_radius) {
        return kTouchAction;
    }
    const int abs_x = dx < 0 ? -dx : dx;
    const int abs_y = dy < 0 ? -dy : dy;
    // Correct for the landscape screen's aspect ratio, then divide the full
    // circle into eight 45-degree sectors. 414/1000 approximates tan(22.5°),
    // the boundary between a cardinal and its neighbouring diagonal.
    const int scaled_x = abs_x * (DISPLAY_LOGICAL_HEIGHT / 2);
    const int scaled_y = abs_y * (DISPLAY_LOGICAL_WIDTH / 2);
    const touch_control_t horizontal = dx < 0 ? kTouchLeft : kTouchRight;
    const touch_control_t vertical = dy < 0 ? kTouchUp : kTouchDown;
    if (scaled_y * 1000 <= scaled_x * 414) return horizontal;
    if (scaled_x * 1000 <= scaled_y * 414) return vertical;
    return horizontal | vertical;
}

static_assert(classify_touch_control(224, 184) == kTouchAction);
static_assert(classify_touch_control(10, 184) == kTouchLeft);
static_assert(classify_touch_control(438, 184) == kTouchRight);
static_assert(classify_touch_control(224, 10) == kTouchUp);
static_assert(classify_touch_control(224, 358) == kTouchDown);
static_assert(classify_touch_control(48, 40) == (kTouchUp | kTouchLeft));
static_assert(classify_touch_control(400, 40) == (kTouchUp | kTouchRight));
static_assert(classify_touch_control(48, 328) == (kTouchDown | kTouchLeft));
static_assert(classify_touch_control(400, 328) ==
              (kTouchDown | kTouchRight));

const char *touch_control_name(touch_control_t control)
{
    switch (control) {
    case kTouchNone: return "released";
    case kTouchLeft: return "left";
    case kTouchRight: return "right";
    case kTouchUp: return "up";
    case kTouchDown: return "down";
    case kTouchUp | kTouchLeft: return "up-left";
    case kTouchUp | kTouchRight: return "up-right";
    case kTouchDown | kTouchLeft: return "down-left";
    case kTouchDown | kTouchRight: return "down-right";
    case kTouchAction: return "action";
    }
    return "unknown";
}

void arm_chuckie_start(bool wait_for_loader_guard)
{
    const uint32_t frame = bbc_core_frame_count();
    chuckie_start_deadline = frame;
    if (wait_for_loader_guard && chuckie_first_action_field + 250 > frame) {
        chuckie_start_deadline = chuckie_first_action_field + 250;
    }
    chuckie_start_state = chuckie_start_state_t::wait_s;
    ESP_LOGI(kTag, "Chuckie Egg start armed at field %u",
             static_cast<unsigned>(chuckie_start_deadline));
}

void service_game_macro()
{
    if (active_game == nullptr || active_game->disc != BC32_DISC_CHUCKIE_EGG) return;
    const uint32_t frame = bbc_core_frame_count();
    if (frame < chuckie_start_deadline) return;

    switch (chuckie_start_state) {
    case chuckie_start_state_t::wait_s:
        set_matrix_key(key(5, 1), true);
        chuckie_start_deadline = frame + 250;
        chuckie_start_state = chuckie_start_state_t::hold_s;
        ESP_LOGI(kTag, "holding S across Chuckie Egg attract input windows");
        break;
    case chuckie_start_state_t::hold_s:
        set_matrix_key(key(5, 1), false);
        chuckie_start_deadline = frame + 5;
        chuckie_start_state = chuckie_start_state_t::wait_one;
        break;
    case chuckie_start_state_t::wait_one:
        set_matrix_key(key(3, 0), true);
        // The game redraws before polling the player-count prompt. Keep 1
        // asserted across that transition instead of relying on a very short
        // five-field pulse that can fall between its input windows.
        chuckie_start_deadline = frame + 100;
        chuckie_start_state = chuckie_start_state_t::hold_one;
        ESP_LOGI(kTag, "holding Chuckie Egg one-player selection");
        break;
    case chuckie_start_state_t::hold_one:
        set_matrix_key(key(3, 0), false);
        chuckie_start_state = chuckie_start_state_t::done;
        break;
    case chuckie_start_state_t::idle:
    case chuckie_start_state_t::done:
        break;
    }
}

void apply_action(const bc32_input_event_t &event)
{
    const unsigned slot = event.action.secondary ? 1U : 0U;
    if (event.action.down) {
        if (action_hold_count[slot] == UINT8_MAX ||
            action_hold_count[slot]++ != 0) {
            return;
        }
    } else {
        if (action_hold_count[slot] == 0 || --action_hold_count[slot] != 0) {
            return;
        }
        set_matrix_key(action_key_down[slot], false);
        action_key_down[slot] = no_key();
        if (action_joystick_down[slot]) {
            action_joystick_down[slot] = false;
            if (!action_joystick_down[0] && !action_joystick_down[1]) {
                bbc_core_set_joystick_button(false);
            }
        }
        return;
    }

    matrix_key_t selected = event.action.secondary ? active_game->secondary
                                                    : active_game->primary;
    if (active_game->disc == BC32_DISC_CHUCKIE_EGG) {
        if (chuckie_action_count == 0) {
            selected = key(6, 2);
            chuckie_first_action_field = bbc_core_frame_count();
            ++chuckie_action_count;
        } else if (chuckie_action_count == 1) {
            ++chuckie_action_count;
            arm_chuckie_start(true);
            selected = no_key();
        }
    } else if (startup_action_count < active_game->startup_key_count) {
        const unsigned step = startup_action_count < 3
                                  ? startup_action_count
                                  : 2;
        selected = active_game->startup_keys[step];
        ++startup_action_count;
        ESP_LOGI(kTag, "%s startup key %u/%u", active_game->name,
                 startup_action_count, active_game->startup_key_count);
    }

    action_key_down[slot] = selected;
    set_matrix_key(action_key_down[slot], true);
    if (active_game->disc == BC32_DISC_KILLER_GORILLA &&
        !event.action.secondary &&
        selected.valid && selected.row == 4 && selected.column == 9) {
        // Killer Gorilla samples RETURN in narrow windows. Three short pulses
        // make one physical press reliable without leaving the key held.
        killer_jump_pulse_phase = 1;
        killer_jump_pulse_deadline = bbc_core_frame_count() + 4;
    }
    // In Keyboard mode keep joystick fire inactive. Elite permanently switches
    // its primary flight controls from keyboard to analogue joystick as soon
    // as it sees joystick fire; the profile's BBC key remains usable instead.
    if (active_game->joystick_controls && input_mode != input_mode_t::keyboard) {
        bbc_core_set_joystick_button(true);
        action_joystick_down[slot] = true;
    }
}

void service_killer_jump_pulse()
{
    if (killer_jump_pulse_phase == 0) return;
    const uint32_t frame = bbc_core_frame_count();
    if (static_cast<int32_t>(frame - killer_jump_pulse_deadline) < 0) return;

    const bool press = killer_jump_pulse_phase == 2 ||
                       killer_jump_pulse_phase == 4;
    set_matrix_key(key(4, 9), press);
    if (killer_jump_pulse_phase == 5) {
        killer_jump_pulse_phase = 0;
        killer_jump_pulse_deadline = 0;
    } else {
        ++killer_jump_pulse_phase;
        // Hold RETURN for four fields, but leave only a one-field release gap
        // so at least one pulse crosses the game's keyboard polling window.
        const bool next_is_press = killer_jump_pulse_phase == 2 ||
                                   killer_jump_pulse_phase == 4;
        killer_jump_pulse_deadline = frame + (next_is_press ? 1 : 4);
    }
}

void apply_keyboard_key(const bc32_input_event_t &event)
{
    matrix_key_t selected = key(event.key.row, event.key.column);
    matrix_key_t modifier = no_key();
    if (active_game != nullptr && active_game->disc == BC32_DISC_ELITE) {
        // Elite's native flight keys are < > X S rather than the cursor keys.
        // Preserve those native keys while making the K380S arrows intuitive.
        if (matrix_key_matches(event, key(1, 9))) {
            selected = key(6, 6); // < (SHIFT+,)
            modifier = key(0, 0);
        } else if (matrix_key_matches(event, key(7, 9))) {
            selected = key(6, 7); // > (SHIFT+.)
            modifier = key(0, 0);
        } else if (matrix_key_matches(event, key(3, 9))) {
            selected = key(4, 2); // X: pull up
        } else if (matrix_key_matches(event, key(2, 9))) {
            selected = key(5, 1); // S: pitch down
        }
    }
    if (event.key.down) {
        set_matrix_key(modifier, true);
        set_matrix_key(selected, true);
    } else {
        set_matrix_key(selected, false);
        set_matrix_key(modifier, false);
    }
}

void set_touch_action(bool down)
{
    const bc32_input_event_t action = {
        .kind = BC32_INPUT_ACTION,
        // Killer Gorilla reserves the secondary physical button for SPACE so
        // games can be replayed, but its centre touch action should remain the
        // primary RETURN/jump control.
        .action = {.secondary = active_game == nullptr ||
                                        active_game->disc !=
                                            BC32_DISC_KILLER_GORILLA,
                   .down = down},
    };
    apply_action(action);
}

void update_touch_control(touch_control_t previous, touch_control_t next)
{
    const touch_control_t released = previous & ~next;
    const touch_control_t pressed = next & ~previous;

    if ((released & kTouchAction) != 0) set_touch_action(false);
    if ((released & kTouchLeft) != 0) {
        set_game_direction(BC32_DIRECTION_LEFT, false);
    }
    if ((released & kTouchRight) != 0) {
        set_game_direction(BC32_DIRECTION_RIGHT, false);
    }
    if ((released & kTouchUp) != 0) {
        set_game_direction(BC32_DIRECTION_UP, false);
    }
    if ((released & kTouchDown) != 0) {
        set_game_direction(BC32_DIRECTION_DOWN, false);
    }

    if ((pressed & kTouchLeft) != 0) {
        set_game_direction(BC32_DIRECTION_LEFT, true);
    }
    if ((pressed & kTouchRight) != 0) {
        set_game_direction(BC32_DIRECTION_RIGHT, true);
    }
    if ((pressed & kTouchUp) != 0) {
        set_game_direction(BC32_DIRECTION_UP, true);
    }
    if ((pressed & kTouchDown) != 0) {
        set_game_direction(BC32_DIRECTION_DOWN, true);
    }
    if ((pressed & kTouchAction) != 0) set_touch_action(true);
}

void apply_gameplay_touch(const bc32_input_event_t &event)
{
    touch_control_t next =
        !event.touch.down
            ? kTouchNone
            : input_mode == input_mode_t::touch
                  ? classify_touch_control(event.touch.x, event.touch.y)
                  : kTouchAction;
    if (event.touch.down && input_mode == input_mode_t::touch &&
        active_game != nullptr && !active_game->allow_diagonals &&
        (next & (kTouchLeft | kTouchRight)) != 0 &&
        (next & (kTouchUp | kTouchDown)) != 0) {
        const int dx = event.touch.x - DISPLAY_LOGICAL_WIDTH / 2;
        const int dy = event.touch.y - DISPLAY_LOGICAL_HEIGHT / 2;
        const int abs_x = dx < 0 ? -dx : dx;
        const int abs_y = dy < 0 ? -dy : dy;
        const int scaled_x = abs_x * (DISPLAY_LOGICAL_HEIGHT / 2);
        const int scaled_y = abs_y * (DISPLAY_LOGICAL_WIDTH / 2);
        next = scaled_x >= scaled_y
                   ? (dx < 0 ? kTouchLeft : kTouchRight)
                   : (dy < 0 ? kTouchUp : kTouchDown);
    }
    if (next == gameplay_touch_control) return;

    // Apply only the changed bits. Sliding from up to up-right therefore keeps
    // UP continuously held while RIGHT joins it, just like two physical keys.
    update_touch_control(gameplay_touch_control, next);
    gameplay_touch_control = next;
    ESP_LOGI(kTag, "touch control: %s",
             touch_control_name(gameplay_touch_control));
}

bool character_picker_is_active()
{
    return __atomic_load_n(&picker_active, __ATOMIC_ACQUIRE);
}

void release_gameplay_controls()
{
    update_touch_control(gameplay_touch_control, kTouchNone);
    gameplay_touch_control = kTouchNone;

    for (unsigned direction = BC32_DIRECTION_LEFT;
         direction <= BC32_DIRECTION_DOWN; ++direction) {
        while (direction_hold_count[direction] != 0) {
            set_game_direction(direction, false);
        }
    }
    for (unsigned slot = 0; slot < 2; ++slot) {
        while (action_hold_count[slot] != 0) {
            const bc32_input_event_t release = {
                .kind = BC32_INPUT_ACTION,
                .action = {.secondary = slot != 0, .down = false},
            };
            apply_action(release);
        }
    }
    killer_jump_pulse_phase = 0;
    killer_jump_pulse_deadline = 0;
    set_matrix_key(key(4, 9), false);
    bbc_core_set_joystick(32768, 32768);
}

void request_game_chooser()
{
    ESP_LOGI(kTag, "returning to game chooser (BLE remains active)");
    release_gameplay_controls();
    bc32_audio_silence();
    __atomic_store_n(&emulator_paused, true, __ATOMIC_RELEASE);
    __atomic_store_n(&launcher_requested, true, __ATOMIC_RELEASE);
}

void reset_game_session_state()
{
    // Clear BeebEm's matrix unconditionally as well as our reference counts.
    // This recovers from any dropped release event or direct core key injection
    // and guarantees that no key can repeat into the next disc's BASIC prompt.
    bbc_core_release_all_keys();
    memset(matrix_key_holds, 0, sizeof(matrix_key_holds));
    memset(direction_hold_count, 0, sizeof(direction_hold_count));
    memset(direction_repeat_released, 0, sizeof(direction_repeat_released));
    memset(direction_repeat_deadline, 0, sizeof(direction_repeat_deadline));
    memset(action_hold_count, 0, sizeof(action_hold_count));
    action_key_down[0] = no_key();
    action_key_down[1] = no_key();
    action_joystick_down[0] = false;
    action_joystick_down[1] = false;
    bbc_core_set_joystick(32768, 32768);
    bbc_core_set_joystick_button(false);
    gameplay_touch_control = kTouchNone;
    picker_injected_key = no_key();
    picker_injected_shift = false;
    chuckie_action_count = 0;
    startup_action_count = 0;
    chuckie_first_action_field = 0;
    chuckie_start_state = chuckie_start_state_t::idle;
    chuckie_start_deadline = 0;
    killer_jump_pulse_phase = 0;
    killer_jump_pulse_deadline = 0;
}

void open_character_picker()
{
    release_gameplay_controls();
    picker_touch_down = false;
    __atomic_store_n(&picker_drag_x, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&picker_active, true, __ATOMIC_RELEASE);
    const unsigned selected =
        __atomic_load_n(&picker_index, __ATOMIC_ACQUIRE) % kPickerKeyCount;
    ESP_LOGI(kTag, "character picker opened at %s",
             kPickerKeys[selected].label);
}

void close_character_picker()
{
    __atomic_store_n(&picker_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&picker_drag_x, 0, __ATOMIC_RELEASE);
    picker_touch_down = false;
    ESP_LOGI(kTag, "character picker closed");
}

void inject_picker_key(unsigned index)
{
    if (picker_injected_key.valid) {
        set_matrix_key(picker_injected_key, false);
        if (picker_injected_shift) set_matrix_key(key(0, 0), false);
    }

    const picker_key_t &entry = kPickerKeys[index % kPickerKeyCount];
    if (entry.kind == picker_key_kind_t::game_chooser) {
        request_game_chooser();
        return;
    }
    picker_injected_key = entry.matrix;
    picker_injected_shift = entry.shift;
    if (picker_injected_shift) set_matrix_key(key(0, 0), true);
    set_matrix_key(picker_injected_key, true);
    picker_key_release_field = bbc_core_frame_count() + 6;
    ESP_LOGI(kTag, "picker typed %s%s", entry.shift ? "SHIFT+" : "",
             entry.label);
}

void service_picker_key()
{
    if (!picker_injected_key.valid) return;
    const uint32_t frame = bbc_core_frame_count();
    if (static_cast<int32_t>(frame - picker_key_release_field) < 0) return;
    set_matrix_key(picker_injected_key, false);
    if (picker_injected_shift) set_matrix_key(key(0, 0), false);
    picker_injected_key = no_key();
    picker_injected_shift = false;
}

void apply_picker_touch(const bc32_input_event_t &event)
{
    if (event.touch.down) {
        if (!picker_touch_down) {
            picker_touch_down = true;
            picker_touch_start_x = event.touch.x;
            picker_touch_start_y = event.touch.y;
        }
        picker_touch_last_x = event.touch.x;
        picker_touch_last_y = event.touch.y;
        int drag = picker_touch_last_x - picker_touch_start_x;
        if (drag > 180) drag = 180;
        if (drag < -180) drag = -180;
        __atomic_store_n(&picker_drag_x, drag, __ATOMIC_RELEASE);
        return;
    }
    if (!picker_touch_down) return;

    picker_touch_down = false;
    const int delta_x = picker_touch_last_x - picker_touch_start_x;
    const int delta_y = picker_touch_last_y - picker_touch_start_y;
    const int abs_x = delta_x < 0 ? -delta_x : delta_x;
    const int abs_y = delta_y < 0 ? -delta_y : delta_y;
    __atomic_store_n(&picker_drag_x, 0, __ATOMIC_RELEASE);

    if (abs_x >= 38 && abs_x > abs_y + 12) {
        unsigned steps = static_cast<unsigned>((abs_x + 39) / 80);
        if (steps > 6) steps = 6;
        const unsigned current =
            __atomic_load_n(&picker_index, __ATOMIC_ACQUIRE);
        const int next = static_cast<int>(current) +
                         (delta_x < 0 ? static_cast<int>(steps)
                                      : -static_cast<int>(steps));
        const unsigned selected = wrap_picker_index(next);
        __atomic_store_n(&picker_index, selected, __ATOMIC_RELEASE);
        ESP_LOGI(kTag, "character picker -> %s",
                 kPickerKeys[selected].label);
    } else if (abs_x < 24 && abs_y < 24) {
        const unsigned selected =
            __atomic_load_n(&picker_index, __ATOMIC_ACQUIRE) % kPickerKeyCount;
        close_character_picker();
        inject_picker_key(selected);
    }
}

void apply_pending_input()
{
    bc32_input_event_t event{};
    while (xQueueReceive(input_queue, &event, 0) == pdTRUE) {
        if (event.kind == BC32_INPUT_SHAKE) {
            if (character_picker_is_active()) {
                close_character_picker();
            } else {
                open_character_picker();
            }
            continue;
        }
        if (character_picker_is_active()) {
            if (event.kind == BC32_INPUT_TOUCH) apply_picker_touch(event);
            continue;
        }
        switch (event.kind) {
        case BC32_INPUT_KEY:
            if (input_mode == input_mode_t::keyboard) {
                apply_keyboard_key(event);
            }
            break;
        case BC32_INPUT_JOYSTICK:
            if (input_mode == input_mode_t::tilt &&
                esp_timer_get_time() >= tilt_input_enable_at_us) {
                bbc_core_set_joystick(event.joystick.x, event.joystick.y);
            }
            break;
        case BC32_INPUT_JOYSTICK_BUTTON:
            bbc_core_set_joystick_button(event.joystick_button.down);
            break;
        case BC32_INPUT_BREAK:
            if (input_mode == input_mode_t::keyboard) bbc_core_reset();
            break;
        case BC32_INPUT_DIRECTION:
            if (input_mode == input_mode_t::tilt &&
                esp_timer_get_time() >= tilt_input_enable_at_us) {
                set_game_direction(event.direction.direction,
                                   event.direction.down);
            }
            break;
        case BC32_INPUT_ACTION:
            apply_action(event);
            break;
        case BC32_INPUT_TOUCH:
            apply_gameplay_touch(event);
            break;
        case BC32_INPUT_SHAKE:
            break;
        case BC32_INPUT_GAME_CHOOSER:
            request_game_chooser();
            // Do not apply any events queued behind Restart to the old game.
            // The main task will pause the core and reset the queue/state.
            return;
        }
    }
}

void emulator_task(void *)
{
    int64_t epoch_us = esp_timer_get_time();
    uint64_t paced_fields = 0;
    bool was_paused = false;

    for (;;) {
        if (__atomic_load_n(&emulator_paused, __ATOMIC_ACQUIRE)) {
            __atomic_store_n(&emulator_pause_acknowledged, true,
                             __ATOMIC_RELEASE);
            was_paused = true;
            vTaskDelay(1);
            continue;
        }
        __atomic_store_n(&emulator_pause_acknowledged, false,
                         __ATOMIC_RELEASE);
        if (was_paused) {
            // Do not try to catch up the wall-clock time spent in the chooser.
            epoch_us = esp_timer_get_time() -
                       static_cast<int64_t>(paced_fields) * kFieldPeriodUs;
            was_paused = false;
        }
        apply_pending_input();
        if (__atomic_load_n(&emulator_paused, __ATOMIC_ACQUIRE)) continue;
        service_picker_key();
        service_game_macro();
        service_killer_jump_pulse();
        service_direction_repeat();
        const unsigned completed = bbc_core_run_batch();
        if (completed == 0) {
            continue;
        }

        paced_fields += completed;
        __atomic_add_fetch(&emulated_fields, completed, __ATOMIC_RELAXED);
        const int64_t wait_us =
            epoch_us + static_cast<int64_t>(paced_fields) * kFieldPeriodUs - esp_timer_get_time();
        if (wait_us > 1000) {
            vTaskDelay(pdMS_TO_TICKS(static_cast<uint32_t>(wait_us / 1000)));
        } else if ((paced_fields & 7U) == 0) {
            // taskYIELD cannot run the lower-priority idle task. One tick every
            // eight fields keeps IDLE1 and its watchdog healthy at negligible
            // cost, and the epoch pacing catches that tick up later.
            vTaskDelay(1);
        } else {
            taskYIELD();
        }
    }
}
} // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(kTag,
             "bc32 starting: %dx%d BBC framebuffer -> centred %dx%d 4:3 viewport on %dx%d AMOLED",
             BBC_FRAME_WIDTH, BBC_FRAME_HEIGHT, kViewportWidth, kViewportHeight,
             DISPLAY_LOGICAL_WIDTH, DISPLAY_LOGICAL_HEIGHT);
    ESP_LOGI(kTag, "internal free=%u, PSRAM free=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    ESP_ERROR_CHECK(init_i2c());
    ESP_ERROR_CHECK(display_init(i2c_bus));
    ESP_ERROR_CHECK(display_set_brightness(210));
    ESP_ERROR_CHECK(init_storage());
    init_scaler();

    esp_err_t audio_result = bc32_audio_init(i2c_bus);
    if (audio_result == ESP_OK) {
        bbc_core_set_sound_callback(write_sound_register, nullptr);
    } else {
        ESP_LOGW(kTag, "audio unavailable: %s", esp_err_to_name(audio_result));
    }

    auto *framebuffer_a = static_cast<uint8_t *>(
        heap_caps_calloc(kFrameBytes, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto *framebuffer_b = static_cast<uint8_t *>(
        heap_caps_calloc(kFrameBytes, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto *display_snapshot = static_cast<uint8_t *>(
        heap_caps_calloc(kFrameBytes, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ESP_ERROR_CHECK(framebuffer_a != nullptr && framebuffer_b != nullptr &&
                            display_snapshot != nullptr
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(bbc_core_init(bc32_assets_roms(), framebuffer_a, framebuffer_b,
                                  BBC_FRAME_WIDTH)
                        ? ESP_OK
                        : ESP_FAIL);
    size_t font_size = 0;
    launcher_font = bc32_assets_font(&font_size);
    ESP_ERROR_CHECK(launcher_font != nullptr && font_size == 96 * 18 * 2
                        ? ESP_OK
                        : ESP_FAIL);
    input_queue = xQueueCreate(64, sizeof(bc32_input_event_t));
    ESP_ERROR_CHECK(input_queue != nullptr ? ESP_OK : ESP_ERR_NO_MEM);
    const BaseType_t screenshot_task_created = xTaskCreatePinnedToCore(
        screenshot_command_task, "screenshots", 4096, nullptr, 3, nullptr, 0);
    ESP_ERROR_CHECK(screenshot_task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    esp_err_t input_result = bc32_motion_input_init(i2c_bus, queue_input, nullptr);
    motion_input_available = input_result == ESP_OK;
    if (input_result != ESP_OK) {
        ESP_LOGW(kTag, "motion input unavailable: %s", esp_err_to_name(input_result));
    }
    input_result = bc32_power_button_init(i2c_bus, queue_input, nullptr);
    if (input_result != ESP_OK) {
        ESP_LOGW(kTag, "PWR button unavailable: %s", esp_err_to_name(input_result));
    }
    input_result = bc32_boot_button_init(queue_input, nullptr);
    if (input_result != ESP_OK) {
        ESP_LOGW(kTag, "BOOT button unavailable: %s", esp_err_to_name(input_result));
    }
    input_result = bc32_touch_input_init(i2c_bus, queue_input, nullptr);
    if (input_result != ESP_OK) {
        ESP_LOGW(kTag, "touch input unavailable: %s", esp_err_to_name(input_result));
    }
    input_result = bc32_ble_keyboard_init(queue_input, nullptr);
    if (input_result != ESP_OK) {
        ESP_LOGW(kTag, "BLE keyboard unavailable: %s", esp_err_to_name(input_result));
    }

    bool emulator_started = false;
    for (;;) {
        const unsigned selected_game =
            run_launcher(display_snapshot, framebuffer_a, framebuffer_b,
                         &input_mode);
        active_game = &kGames[selected_game];
        reset_game_session_state();

        size_t embedded_size = 0;
        const uint8_t *embedded =
            bc32_assets_disc(active_game->disc, &embedded_size);
        char disc_path[48] = {};
        size_t disc_size = 0;
        uint8_t *disc = load_writable_disc(active_game->disc, embedded,
                                           embedded_size, disc_path,
                                           sizeof(disc_path), &disc_size);
        ESP_LOGI(kTag, "mounting writable %s SSD from %s (%u bytes)",
                 active_game->name, disc_path,
                 static_cast<unsigned>(disc_size));
        const bool mounted =
            disc != nullptr &&
            bbc_core_mount_ssd_writable(disc, disc_size, disc_path);
        heap_caps_free(disc);
        ESP_ERROR_CHECK(mounted ? ESP_OK : ESP_FAIL);

        ESP_LOGI(kTag, "core ready: internal free=%u, PSRAM free=%u",
                 static_cast<unsigned>(
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(
                     heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

        // Bluetooth setup may write NVS calibration data and temporarily
        // suspend flash/PSRAM access. Finish it before the 6502 and 8271 begin
        // executing so disc boot is deterministic on every cold start.
        bbc_core_shift_break();
        // Motion producers resume immediately, but direction keys present
        // during SHIFT+BREAK can cancel autoboot and then type Z/X/*/? at the
        // BASIC prompt. Keep tilt neutral until the DFS boot hand-off is safe.
        tilt_input_enable_at_us = esp_timer_get_time() + kTiltBootGuardUs;
        __atomic_store_n(&launcher_requested, false, __ATOMIC_RELEASE);
        if (!emulator_started) {
            const BaseType_t created = xTaskCreatePinnedToCore(
                emulator_task, "bbc6502", 8192, nullptr, 8, nullptr, 1);
            ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
            emulator_started = true;
        } else {
            __atomic_store_n(&emulator_paused, false, __ATOMIC_RELEASE);
        }

        uint32_t last_frame = UINT32_MAX;
        uint32_t last_emu_total =
            __atomic_load_n(&emulated_fields, __ATOMIC_RELAXED);
        uint32_t last_display_total =
            __atomic_load_n(&displayed_frames, __ATOMIC_RELAXED);
        int64_t last_stats_us = esp_timer_get_time();
        int64_t last_display_us = 0;

        while (!__atomic_load_n(&launcher_requested, __ATOMIC_ACQUIRE)) {
            const uint32_t frame = bbc_core_frame_count();
            if (frame == last_frame) {
                vTaskDelay(1);
                continue;
            }

            last_frame = frame;
            const int64_t frame_now_us = esp_timer_get_time();
            if (frame_now_us - last_display_us < kDisplayPeriodUs) continue;

            // The completed double-buffer remains untouched for the following
            // BBC field, so this short copy is stable without holding up the
            // emulation task while the panel transfer runs.
            memcpy(display_snapshot, bbc_core_framebuffer(), kFrameBytes);
            if (character_picker_is_active()) {
                render_character_picker(display_snapshot);
            }
            render_frame(display_snapshot);
            last_display_us = frame_now_us;
            __atomic_add_fetch(&displayed_frames, 1U, __ATOMIC_RELAXED);
            vTaskDelay(1);

            const int64_t now_us = esp_timer_get_time();
            if (now_us - last_stats_us >= kStatsPeriodUs) {
                const uint32_t emu_total =
                    __atomic_load_n(&emulated_fields, __ATOMIC_RELAXED);
                const uint32_t display_total =
                    __atomic_load_n(&displayed_frames, __ATOMIC_RELAXED);
                const double elapsed =
                    static_cast<double>(now_us - last_stats_us) / 1000000.0;
                ESP_LOGI(kTag,
                         "%.1f BBC fields/s | %.1f display fps | field=%u | internal=%u PSRAM=%u",
                         static_cast<double>(emu_total - last_emu_total) /
                             elapsed,
                         static_cast<double>(display_total -
                                             last_display_total) /
                             elapsed,
                         frame,
                         static_cast<unsigned>(heap_caps_get_free_size(
                             MALLOC_CAP_INTERNAL)),
                         static_cast<unsigned>(heap_caps_get_free_size(
                             MALLOC_CAP_SPIRAM)));
                last_emu_total = emu_total;
                last_display_total = display_total;
                last_stats_us = now_us;
            }
        }

        // The emulator owns all BeebEm state. Wait until it has stopped before
        // clearing held keys or mounting the next disc. BLE itself keeps
        // running throughout this transition.
        while (!__atomic_load_n(&emulator_pause_acknowledged,
                                __ATOMIC_ACQUIRE)) {
            vTaskDelay(1);
        }
        reset_game_session_state();
        active_game = nullptr;
        close_character_picker();
        xQueueReset(input_queue);
        ESP_LOGI(kTag, "game chooser resumed; BLE keyboard connected=%s",
                 bc32_ble_keyboard_connected() ? "yes" : "no");
    }
}
