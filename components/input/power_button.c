#include "bc32_input.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define EXPANDER_ADDRESS 0x20
#define EXPANDER_INPUT_REGISTER 0x00
#define EXPANDER_CONFIG_REGISTER 0x03
#define POWER_BUTTON_BIT (1U << 4)
#define SHORT_PRESS_MAX_MS 1500
#define KEY_HOLD_MS 80

static const char *TAG = "power_button";
static i2c_master_dev_handle_t s_expander;
static bc32_input_callback_t s_callback;
static void *s_callback_context;

static esp_err_t read_register(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_expander, &reg, 1, value, 1, 100);
}

static esp_err_t write_register(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(s_expander, data, sizeof(data), 100);
}

static void emit_action(bool secondary, bool down)
{
    const bc32_input_event_t event = {
        .kind = BC32_INPUT_ACTION,
        .action = {.secondary = secondary, .down = down},
    };
    s_callback(&event, s_callback_context);
}

static void button_task(void *argument)
{
    (void)argument;
    bool stable_pressed = false;
    unsigned changed_samples = 0;
    unsigned read_failures = 0;
    int64_t pressed_at = 0;
    bool action_held = false;
    bool action_secondary = false;
    int64_t action_release_at = 0;

    for (;;) {
        const int64_t now = esp_timer_get_time();
        if (action_held && now >= action_release_at) {
            emit_action(action_secondary, false);
            action_held = false;
        }

        uint8_t input = 0;
        if (read_register(EXPANDER_INPUT_REGISTER, &input) == ESP_OK) {
            read_failures = 0;
            const bool pressed = (input & POWER_BUTTON_BIT) != 0;
            if (pressed == stable_pressed) {
                changed_samples = 0;
            } else if (++changed_samples >= 2) {
                changed_samples = 0;
                stable_pressed = pressed;
                if (pressed) {
                    pressed_at = esp_timer_get_time();
                    ESP_LOGI(TAG, "PWR down (input 0x%02x)", input);
                } else {
                    const int64_t held_ms =
                        (esp_timer_get_time() - pressed_at) / 1000;
                    ESP_LOGI(TAG, "PWR up after %lld ms (input 0x%02x)",
                             (long long)held_ms, input);
                    if (held_ms <= SHORT_PRESS_MAX_MS) {
                        if (action_held) emit_action(action_secondary, false);
                        // The board has a dedicated BOOT/secondary button.
                        // Keep PWR unambiguously primary even on a relaxed tap.
                        action_secondary = false;
                        emit_action(action_secondary, true);
                        action_held = true;
                        action_release_at =
                            esp_timer_get_time() + KEY_HOLD_MS * 1000LL;
                        ESP_LOGI(TAG, "%s action",
                                 action_secondary ? "secondary" : "primary");
                    }
                }
            }
        } else if (++read_failures == 50) {
            ESP_LOGW(TAG, "50 consecutive expander read failures");
            read_failures = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t bc32_power_button_init(i2c_master_bus_handle_t bus,
                                 bc32_input_callback_t callback, void *context)
{
    if (bus == NULL || callback == NULL) return ESP_ERR_INVALID_ARG;
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = EXPANDER_ADDRESS,
        .scl_speed_hz = 400000,
    };
    esp_err_t result = i2c_master_bus_add_device(bus, &config, &s_expander);
    if (result != ESP_OK) return result;

    uint8_t direction = 0xff;
    result = read_register(EXPANDER_CONFIG_REGISTER, &direction);
    if (result != ESP_OK) return result;
    if ((direction & POWER_BUTTON_BIT) == 0) {
        result = write_register(EXPANDER_CONFIG_REGISTER,
                                direction | POWER_BUTTON_BIT);
        if (result != ESP_OK) return result;
    }

    uint8_t input = 0;
    result = read_register(EXPANDER_INPUT_REGISTER, &input);
    if (result != ESP_OK) return result;

    s_callback = callback;
    s_callback_context = context;
    if (xTaskCreatePinnedToCore(button_task, "pwr_button", 3072, NULL, 4, NULL, 0) !=
        pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "PWR ready on EXIO4 (config 0x%02x, input 0x%02x); primary action",
             direction | POWER_BUTTON_BIT, input);
    return ESP_OK;
}
