#include "bc32_input.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define DEBOUNCE_SAMPLES 2

static const char *TAG = "boot_button";
static bc32_input_callback_t s_callback;
static void *s_callback_context;

static void emit_secondary(bool down)
{
    const bc32_input_event_t event = {
        .kind = BC32_INPUT_ACTION,
        .action = {.secondary = true, .down = down},
    };
    s_callback(&event, s_callback_context);
}

static void boot_button_task(void *argument)
{
    (void)argument;
    bool stable_pressed = gpio_get_level(BOOT_BUTTON_GPIO) == 0;
    unsigned changed_samples = 0;

    for (;;) {
        const bool pressed = gpio_get_level(BOOT_BUTTON_GPIO) == 0;
        if (pressed == stable_pressed) {
            changed_samples = 0;
        } else if (++changed_samples >= DEBOUNCE_SAMPLES) {
            changed_samples = 0;
            stable_pressed = pressed;
            emit_secondary(pressed);
            ESP_LOGI(TAG, "BOOT %s -> secondary action %s",
                     pressed ? "down" : "up", pressed ? "down" : "up");
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t bc32_boot_button_init(bc32_input_callback_t callback, void *context)
{
    if (callback == NULL) return ESP_ERR_INVALID_ARG;
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t result = gpio_config(&config);
    if (result != ESP_OK) return result;

    s_callback = callback;
    s_callback_context = context;
    if (xTaskCreatePinnedToCore(boot_button_task, "boot_button", 2048, NULL, 4,
                                NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "BOOT ready on GPIO0; hold=secondary action");
    return ESP_OK;
}
