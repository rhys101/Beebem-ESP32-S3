#include "bc32_input.h"

#include <string.h>

#include "esp_check.h"
#include "esp_hid_gap.h"
#include "esp_hidh.h"
#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "ble_keyboard";
static bc32_input_callback_t s_callback;
static void *s_callback_context;
static volatile bool s_connected;
static volatile bool s_hid_open;
static volatile bool s_link_secure;
static volatile uint32_t s_pairing_passkey;
static struct ble_gap_event_listener s_gap_listener;

void esp_hidh_pairing_passkey_notify(uint32_t passkey)
{
    s_pairing_passkey = passkey;
}
static uint8_t s_previous_modifiers;
static uint8_t s_previous_keys[6];

typedef struct {
    int8_t row;
    int8_t column;
} matrix_key_t;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
} saved_keyboard_t;

static bool load_saved_keyboard(saved_keyboard_t *keyboard)
{
    nvs_handle_t handle;
    if (nvs_open("bc32_ble", NVS_READONLY, &handle) != ESP_OK) return false;
    size_t length = sizeof(*keyboard);
    const esp_err_t result = nvs_get_blob(handle, "keyboard", keyboard, &length);
    nvs_close(handle);
    return result == ESP_OK && length == sizeof(*keyboard);
}

static void save_keyboard(const esp_hid_scan_result_t *result)
{
    saved_keyboard_t keyboard = {.address_type = result->ble.addr_type};
    memcpy(keyboard.address, result->bda, sizeof(keyboard.address));
    nvs_handle_t handle;
    if (nvs_open("bc32_ble", NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_set_blob(handle, "keyboard", &keyboard, sizeof(keyboard)) == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
}

static const matrix_key_t s_hid_to_bbc[256] = {
    [0x04] = {4, 1}, [0x05] = {6, 4}, [0x06] = {5, 2}, [0x07] = {3, 2},
    [0x08] = {2, 2}, [0x09] = {4, 3}, [0x0a] = {5, 3}, [0x0b] = {5, 4},
    [0x0c] = {2, 5}, [0x0d] = {4, 5}, [0x0e] = {4, 6}, [0x0f] = {5, 6},
    [0x10] = {6, 5}, [0x11] = {5, 5}, [0x12] = {3, 6}, [0x13] = {3, 7},
    [0x14] = {1, 0}, [0x15] = {3, 3}, [0x16] = {5, 1}, [0x17] = {2, 3},
    [0x18] = {3, 5}, [0x19] = {6, 3}, [0x1a] = {2, 1}, [0x1b] = {4, 2},
    [0x1c] = {4, 4}, [0x1d] = {6, 1},
    [0x1e] = {3, 0}, [0x1f] = {3, 1}, [0x20] = {1, 1}, [0x21] = {1, 2},
    [0x22] = {1, 3}, [0x23] = {3, 4}, [0x24] = {2, 4}, [0x25] = {1, 5},
    [0x26] = {2, 6}, [0x27] = {2, 7},
    [0x28] = {4, 9}, [0x29] = {7, 0}, [0x2a] = {5, 9}, [0x2b] = {6, 0},
    [0x2c] = {6, 2}, [0x2d] = {5, 7}, [0x2e] = {1, 7}, [0x2f] = {3, 8},
    [0x30] = {5, 8}, [0x31] = {7, 8}, [0x33] = {4, 7}, [0x34] = {4, 8},
    [0x35] = {2, 8}, [0x36] = {6, 6}, [0x37] = {6, 7}, [0x38] = {6, 8},
    [0x39] = {4, 0},
    [0x3a] = {7, 1}, [0x3b] = {7, 2}, [0x3c] = {7, 3}, [0x3d] = {1, 4},
    [0x3e] = {7, 4}, [0x3f] = {7, 5}, [0x40] = {1, 6}, [0x41] = {7, 6},
    [0x42] = {7, 7}, [0x43] = {2, 0},
    [0x49] = {6, 9}, [0x4c] = {5, 9}, [0x4f] = {7, 9}, [0x50] = {1, 9},
    [0x51] = {2, 9}, [0x52] = {3, 9},
};

static void emit_key(uint8_t row, uint8_t column, bool down)
{
    const bc32_input_event_t event = {
        .kind = BC32_INPUT_KEY,
        .key = {.row = row, .column = column, .down = down},
    };
    s_callback(&event, s_callback_context);
}

static void emit_break(void)
{
    const bc32_input_event_t event = {.kind = BC32_INPUT_BREAK};
    s_callback(&event, s_callback_context);
}

static void emit_game_chooser(void)
{
    const bc32_input_event_t event = {.kind = BC32_INPUT_GAME_CHOOSER};
    s_callback(&event, s_callback_context);
}

static bool report_has_key(const uint8_t keys[6], uint8_t usage)
{
    for (unsigned i = 0; i < 6; ++i) {
        if (keys[i] == usage) return true;
    }
    return false;
}

static matrix_key_t translated_key(uint8_t usage, uint8_t modifiers)
{
    const bool shifted = (modifiers & 0x22) != 0;
    // Translate the characters printed on a modern keyboard to their BBC
    // equivalents.  In particular, BBC games conventionally use the */:
    // key for up; on the K380s '*' is Shift+8 and ':' is Shift+semicolon.
    if (shifted && (usage == 0x25 || usage == 0x33)) {
        return (matrix_key_t){4, 8};
    }
    return s_hid_to_bbc[usage];
}

static bool shift_is_consumed(const uint8_t keys[6], uint8_t modifiers)
{
    return (modifiers & 0x22) != 0 && report_has_key(keys, 0x25);
}

static void emit_usage(uint8_t usage, uint8_t modifiers, bool down)
{
    if (usage == 0) return;
    if ((usage == 0x45 || usage == 0x48) && down) {
        emit_break();
        return;
    }
    // The K380S top-right Delete/Lock key reports HID Keyboard Delete Forward.
    // Make it a convenient return to bc32's chooser instead of BBC DELETE.
    if (usage == 0x4c) {
        if (down) emit_game_chooser();
        return;
    }
    const matrix_key_t key = translated_key(usage, modifiers);
    if (key.row == 0 && key.column == 0) return;
    emit_key((uint8_t)key.row, (uint8_t)key.column, down);
}

static void release_report(void)
{
    for (unsigned i = 0; i < 6; ++i) {
        emit_usage(s_previous_keys[i], s_previous_modifiers, false);
    }
    if ((s_previous_modifiers & 0x22) &&
        !shift_is_consumed(s_previous_keys, s_previous_modifiers)) {
        emit_key(0, 0, false);
    }
    if (s_previous_modifiers & 0x11) emit_key(0, 1, false);
    memset(s_previous_keys, 0, sizeof(s_previous_keys));
    s_previous_modifiers = 0;
}

static void handle_boot_keyboard_report(const uint8_t *data, size_t length)
{
    // Boot protocol has a reserved byte after the modifier (8 bytes total).
    // Logitech report protocol omits it (7 bytes total).
    if (length != 7 && length < 8) {
        ESP_LOGW(TAG, "unsupported keyboard report length %u", (unsigned)length);
        return;
    }

    const uint8_t modifiers = data[0];
    const uint8_t *keys = data + (length == 7 ? 1 : 2);
    const bool old_shift = (s_previous_modifiers & 0x22) != 0 &&
                           !shift_is_consumed(s_previous_keys,
                                              s_previous_modifiers);
    const bool new_shift = (modifiers & 0x22) != 0 &&
                           !shift_is_consumed(keys, modifiers);
    const bool old_control = (s_previous_modifiers & 0x11) != 0;
    const bool new_control = (modifiers & 0x11) != 0;
    for (unsigned i = 0; i < 6; ++i) {
        const uint8_t old_usage = s_previous_keys[i];
        if (old_usage != 0 && !report_has_key(keys, old_usage)) {
            emit_usage(old_usage, s_previous_modifiers, false);
        }
    }
    if (old_shift != new_shift) emit_key(0, 0, new_shift);
    if (old_control != new_control) emit_key(0, 1, new_control);
    for (unsigned i = 0; i < 6; ++i) {
        const uint8_t new_usage = keys[i];
        if (new_usage != 0 && !report_has_key(s_previous_keys, new_usage)) {
            emit_usage(new_usage, modifiers, true);
        }
    }
    memcpy(s_previous_keys, keys, sizeof(s_previous_keys));
    s_previous_modifiers = modifiers;
}

static void hidh_callback(void *handler_args, esp_event_base_t base, int32_t id,
                          void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_hidh_event_data_t *parameter = (esp_hidh_event_data_t *)event_data;
    switch ((esp_hidh_event_t)id) {
    case ESP_HIDH_OPEN_EVENT:
        if (parameter->open.status == ESP_OK) {
            s_hid_open = true;
            s_connected = s_link_secure;
            ESP_LOGI(TAG, "HID ready: %s (secure=%d)",
                     esp_hidh_dev_name_get(parameter->open.dev), s_link_secure);
            esp_hidh_dev_dump(parameter->open.dev, stdout);
        } else {
            ESP_LOGW(TAG, "keyboard connection failed");
        }
        break;
    case ESP_HIDH_INPUT_EVENT:
        ESP_LOGI(TAG, "input usage=%s map=%u report=%u length=%u",
                 esp_hid_usage_str(parameter->input.usage),
                 parameter->input.map_index, parameter->input.report_id,
                 (unsigned)parameter->input.length);
        if (parameter->input.usage == ESP_HID_USAGE_KEYBOARD) {
            handle_boot_keyboard_report(parameter->input.data, parameter->input.length);
        }
        break;
    case ESP_HIDH_CLOSE_EVENT:
        release_report();
        s_hid_open = false;
        s_link_secure = false;
        s_connected = false;
        s_pairing_passkey = 0;
        ESP_LOGI(TAG, "keyboard disconnected; scanning will resume");
        break;
    default:
        break;
    }
}

static int gap_listener(struct ble_gap_event *event, void *context)
{
    (void)context;
    switch (event->type) {
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc description;
        if (event->enc_change.status == 0 &&
            ble_gap_conn_find(event->enc_change.conn_handle, &description) == 0) {
            s_link_secure = description.sec_state.encrypted &&
                            description.sec_state.authenticated;
            s_connected = s_link_secure && s_hid_open;
            if (s_link_secure) {
                s_pairing_passkey = 0;
                ESP_LOGI(TAG, "keyboard pairing authenticated%s",
                         description.sec_state.bonded ? " and saved" : "");
            }
        } else {
            s_link_secure = false;
            s_connected = false;
            ESP_LOGW(TAG, "keyboard authentication failed (status=%d)",
                     event->enc_change.status);
        }
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        s_hid_open = false;
        s_link_secure = false;
        s_connected = false;
        s_pairing_passkey = 0;
        break;
    default:
        break;
    }
    return 0;
}

static void nimble_host_task(void *parameter)
{
    (void)parameter;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void scan_task(void *parameter)
{
    (void)parameter;
    saved_keyboard_t saved;
    if (load_saved_keyboard(&saved)) {
        ESP_LOGI(TAG, "reconnecting to saved BLE keyboard");
        esp_hidh_dev_open(saved.address, ESP_HID_TRANSPORT_BLE,
                          saved.address_type);
    }
    for (;;) {
        if (s_connected || s_hid_open) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        size_t result_count = 0;
        esp_hid_scan_result_t *results = NULL;
        ESP_LOGI(TAG, "scanning continuously for BLE keyboards");
        if (esp_hid_scan(8, &result_count, &results) == ESP_OK) {
            esp_hid_scan_result_t *choice = NULL;
            for (esp_hid_scan_result_t *result = results; result != NULL;
                 result = result->next) {
                ESP_LOGI(TAG, "found %s RSSI %d usage %s",
                         result->name ? result->name : "unnamed", result->rssi,
                         esp_hid_usage_str(result->usage));
                if (choice == NULL || result->usage == ESP_HID_USAGE_KEYBOARD) {
                    choice = result;
                }
                if (result->usage == ESP_HID_USAGE_KEYBOARD) break;
            }
            if (choice != NULL) {
                ESP_LOGI(TAG, "opening %s", choice->name ? choice->name : "BLE HID device");
                save_keyboard(choice);
                esp_hidh_dev_open(choice->bda, choice->transport, choice->ble.addr_type);
            }
            esp_hid_scan_results_free(results);
        }
        // Keep the gap shorter than a keyboard's brief reconnect advert.
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void ble_store_config_init(void);

esp_err_t bc32_ble_keyboard_init(bc32_input_callback_t callback, void *context)
{
    if (callback == NULL) return ESP_ERR_INVALID_ARG;
    s_callback = callback;
    s_callback_context = context;

    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    if (result != ESP_OK) return result;

    ESP_RETURN_ON_ERROR(esp_hid_gap_init(HIDH_BLE_MODE), TAG, "BLE GAP init");
    const esp_hidh_config_t config = {
        .callback = hidh_callback,
        .event_stack_size = 4096,
        .callback_arg = NULL,
    };
    ESP_RETURN_ON_ERROR(esp_hidh_init(&config), TAG, "HID host init");

    // A BLE keyboard has keyboard input but no display.  As its host, bc32
    // displays the passkey and the user types it on the keyboard.
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    const int listener_result =
        ble_gap_event_listener_register(&s_gap_listener, gap_listener, NULL);
    if (listener_result != 0) {
        ESP_LOGE(TAG, "could not register BLE security listener: %d",
                 listener_result);
        return ESP_FAIL;
    }

    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    nimble_port_freertos_init(nimble_host_task);
    vTaskDelay(pdMS_TO_TICKS(200));

    uint8_t own_addr_type = 0;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) return ESP_FAIL;
    if (xTaskCreate(scan_task, "ble_scan", 6144, NULL, 2, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "BLE HID host ready; put a keyboard into pairing mode");
    return ESP_OK;
}

bool bc32_ble_keyboard_connected(void)
{
    return s_connected;
}

uint32_t bc32_ble_keyboard_pairing_passkey(void)
{
    return s_pairing_passkey;
}
