#include "bc32_input.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CST820_ADDRESS 0x15
#define CST820_STATUS_REGISTER 0x00
#define CST820_CHIP_ID_REGISTER 0xa7
#define CST820_DISABLE_SLEEP_REGISTER 0xfe
#define CST820_EXPECTED_CHIP_ID 0xb7

// The panel is physically portrait (368x448), while bc32 presents a clockwise
// rotated 448x368 logical surface.
#define LOGICAL_WIDTH 448
#define LOGICAL_HEIGHT 368

static const char *TAG = "touch";
static i2c_master_dev_handle_t s_touch;
static bc32_input_callback_t s_callback;
static void *s_callback_context;

static esp_err_t read_register(uint8_t reg, uint8_t *data, size_t size)
{
    return i2c_master_transmit_receive(s_touch, &reg, 1, data, size, 100);
}

static void emit_touch(uint16_t x, uint16_t y, bool down)
{
    const bc32_input_event_t event = {
        .kind = BC32_INPUT_TOUCH,
        .touch = {.x = x, .y = y, .down = down},
    };
    s_callback(&event, s_callback_context);
}

static void touch_task(void *argument)
{
    (void)argument;
    bool was_down = false;
    uint16_t last_x = 0;
    uint16_t last_y = 0;

    for (;;) {
        uint8_t data[7] = {0};
        if (read_register(CST820_STATUS_REGISTER, data, sizeof(data)) == ESP_OK) {
            const unsigned points = data[2] & 0x0fU;
            if (points == 1) {
                const uint16_t physical_x =
                    (uint16_t)(((data[3] & 0x0fU) << 8) | data[4]);
                const uint16_t physical_y =
                    (uint16_t)(((data[5] & 0x0fU) << 8) | data[6]);
                if (physical_x < LOGICAL_HEIGHT && physical_y < LOGICAL_WIDTH) {
                    last_x = (uint16_t)(LOGICAL_WIDTH - 1 - physical_y);
                    last_y = physical_x;
                    emit_touch(last_x, last_y, true);
                    was_down = true;
                }
            } else if (was_down) {
                emit_touch(last_x, last_y, false);
                was_down = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t bc32_touch_input_init(i2c_master_bus_handle_t bus,
                                bc32_input_callback_t callback, void *context)
{
    if (bus == NULL || callback == NULL) return ESP_ERR_INVALID_ARG;
    if (i2c_master_probe(bus, CST820_ADDRESS, 100) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CST820_ADDRESS,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &config, &s_touch), TAG,
                        "CST820 attach failed");

    uint8_t chip_id = 0;
    ESP_RETURN_ON_ERROR(read_register(CST820_CHIP_ID_REGISTER, &chip_id, 1), TAG,
                        "CST820 chip ID read failed");
    if (chip_id != CST820_EXPECTED_CHIP_ID) {
        ESP_LOGW(TAG, "touch controller at 0x15 reports chip ID 0x%02x", chip_id);
    }

    const uint8_t disable_sleep[] = {CST820_DISABLE_SLEEP_REGISTER, 0x01};
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_touch, disable_sleep,
                                            sizeof(disable_sleep), 100),
                        TAG, "CST820 sleep configuration failed");

    s_callback = callback;
    s_callback_context = context;
    if (xTaskCreatePinnedToCore(touch_task, "touch", 3072, NULL, 3, NULL, 0) !=
        pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "CST820 ready at 0x15 (chip ID 0x%02x)", chip_id);
    return ESP_OK;
}
