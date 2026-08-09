#include "bc32_input.h"

#include <math.h>
#ifdef M_PI
#undef M_PI
#endif

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "qmi8658.h"

#define QMI8658_RESET_REGISTER 0x60
#define QMI8658_RESET_COMMAND 0xb0
#define QMI8658_CTRL1_VALUE 0x60
#define WARMUP_SAMPLES 100
#define CALIBRATION_SAMPLES 100
#define RADIANS_TO_DEGREES 57.2957795f

static const char *TAG = "motion";
static qmi8658_dev_t s_imu;
static bc32_input_callback_t s_callback;
static void *s_callback_context;
static volatile bool s_recalibrate_requested;
static volatile bool s_calibrating;
static volatile uint32_t s_horizontal_sensitivity_percent = 100;
static volatile uint32_t s_vertical_sensitivity_percent = 100;
static volatile bool s_allow_diagonals = true;

static void emit_direction(bc32_direction_t direction, bool down)
{
    const bc32_input_event_t event = {
        .kind = BC32_INPUT_DIRECTION,
        .direction = {.direction = direction, .down = down},
    };
    s_callback(&event, s_callback_context);
}

static void emit_shake(void)
{
    const bc32_input_event_t event = {.kind = BC32_INPUT_SHAKE};
    s_callback(&event, s_callback_context);
}

static void update_direction(bool *state, bool next,
                             bc32_direction_t direction, const char *name)
{
    if (*state != next) {
        *state = next;
        emit_direction(direction, next);
        ESP_LOGI(TAG, "%s %s", name, next ? "down" : "up");
    }
}

static float gravity_axis_angle(float component, float x, float y, float z)
{
    const float magnitude = sqrtf(x * x + y * y + z * z);
    if (magnitude < 1.0f) return 0.0f;
    float normalized = component / magnitude;
    if (normalized > 1.0f) normalized = 1.0f;
    if (normalized < -1.0f) normalized = -1.0f;
    return asinf(normalized) * RADIANS_TO_DEGREES;
}

static uint16_t analogue_axis(float angle, bool invert)
{
    // Reach full BBC analogue deflection at 13 degrees from the calibrated
    // play position, while digital movement engages much earlier.
    float normalized = angle / 13.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    if (normalized < -1.0f) normalized = -1.0f;
    if (invert) normalized = -normalized;
    return (uint16_t)(32768.0f + normalized * 32767.0f);
}

static void motion_task(void *argument)
{
    (void)argument;
    float filtered_x = 0.0f;
    float filtered_y = 0.0f;
    float neutral_accel_x = 0.0f;
    float neutral_accel_y = 0.0f;
    float neutral_accel_z = 0.0f;
    float neutral_angle_x = 0.0f;
    float neutral_angle_y = 0.0f;
    unsigned warmup_count = 0;
    unsigned calibration_count = 0;
    bool primed = false;
    bool left = false, right = false, up = false, down = false;
    bool have_previous_accel = false;
    float previous_accel_x = 0.0f;
    float previous_accel_y = 0.0f;
    float previous_accel_z = 0.0f;
    unsigned shake_impulses = 0;
    int64_t shake_window_start_us = 0;
    int64_t shake_cooldown_until_us = 0;
    int log_count = 0;

    ESP_LOGI(TAG, "hold steady at the preferred play angle for four seconds");

    for (;;) {
        if (__atomic_exchange_n(&s_recalibrate_requested, false,
                                __ATOMIC_ACQ_REL)) {
            update_direction(&left, false, BC32_DIRECTION_LEFT, "left");
            update_direction(&right, false, BC32_DIRECTION_RIGHT, "right");
            update_direction(&up, false, BC32_DIRECTION_UP, "up");
            update_direction(&down, false, BC32_DIRECTION_DOWN, "down");
            filtered_x = 0.0f;
            filtered_y = 0.0f;
            neutral_accel_x = 0.0f;
            neutral_accel_y = 0.0f;
            neutral_accel_z = 0.0f;
            warmup_count = 0;
            calibration_count = 0;
            primed = false;
            have_previous_accel = false;
            shake_impulses = 0;
            shake_window_start_us = 0;
            __atomic_store_n(&s_calibrating, true, __ATOMIC_RELEASE);
            ESP_LOGI(TAG, "recalibrating: hold at the preferred game angle");
        }

        qmi8658_data_t sample;
        bool ready = false;
        if (qmi8658_is_data_ready(&s_imu, &ready) == ESP_OK && ready &&
            qmi8658_read_sensor_data(&s_imu, &sample) == ESP_OK) {
            if (warmup_count < WARMUP_SAMPLES) {
                ++warmup_count;
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            if (calibration_count < CALIBRATION_SAMPLES) {
                neutral_accel_x += sample.accelX;
                neutral_accel_y += sample.accelY;
                neutral_accel_z += sample.accelZ;
                if (++calibration_count == CALIBRATION_SAMPLES) {
                    neutral_accel_x /= CALIBRATION_SAMPLES;
                    neutral_accel_y /= CALIBRATION_SAMPLES;
                    neutral_accel_z /= CALIBRATION_SAMPLES;
                    neutral_angle_x = gravity_axis_angle(
                        neutral_accel_x, neutral_accel_x, neutral_accel_y,
                        neutral_accel_z);
                    neutral_angle_y = gravity_axis_angle(
                        neutral_accel_y, neutral_accel_x, neutral_accel_y,
                        neutral_accel_z);
                    ESP_LOGI(TAG,
                             "play position calibrated at %.1f, %.1f degrees",
                             (double)neutral_angle_x, (double)neutral_angle_y);
                    __atomic_store_n(&s_calibrating, false, __ATOMIC_RELEASE);
                }
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }

            const float motion_x =
                gravity_axis_angle(sample.accelX, sample.accelX, sample.accelY,
                                   sample.accelZ) -
                neutral_angle_x;
            const float motion_y =
                gravity_axis_angle(sample.accelY, sample.accelX, sample.accelY,
                                   sample.accelZ) -
                neutral_angle_y;

            // A deliberate shake produces several sharp changes in linear
            // acceleration. Requiring three impulses in a short window avoids
            // opening the picker during ordinary steering, while the cooldown
            // prevents one shake from generating multiple events.
            if (have_previous_accel) {
                const float delta_x = sample.accelX - previous_accel_x;
                const float delta_y = sample.accelY - previous_accel_y;
                const float delta_z = sample.accelZ - previous_accel_z;
                const float jerk =
                    sqrtf(delta_x * delta_x + delta_y * delta_y +
                          delta_z * delta_z);
                const int64_t now_us = esp_timer_get_time();
                if (jerk >= 5.0f && now_us >= shake_cooldown_until_us) {
                    if (shake_impulses == 0 ||
                        now_us - shake_window_start_us > 600000) {
                        shake_impulses = 1;
                        shake_window_start_us = now_us;
                    } else {
                        ++shake_impulses;
                    }
                    if (shake_impulses >= 3) {
                        shake_impulses = 0;
                        shake_cooldown_until_us = now_us + 1800000;
                        emit_shake();
                        ESP_LOGI(TAG, "shake detected -> character picker");
                    }
                } else if (shake_impulses != 0 &&
                           now_us - shake_window_start_us > 600000) {
                    shake_impulses = 0;
                }
            }
            previous_accel_x = sample.accelX;
            previous_accel_y = sample.accelY;
            previous_accel_z = sample.accelZ;
            have_previous_accel = true;

            // With the BBC image rotated clockwise into the native portrait
            // panel, raw accelerometer X/Y align with logical screen X/Y.
            if (!primed) {
                filtered_x = motion_x;
                filtered_y = motion_y;
                primed = true;
            } else {
                filtered_x += 0.50f * (motion_x - filtered_x);
                filtered_y += 0.50f * (motion_y - filtered_y);
            }

            const float horizontal_sensitivity =
                (float)__atomic_load_n(&s_horizontal_sensitivity_percent,
                                       __ATOMIC_ACQUIRE) /
                100.0f;
            const float vertical_sensitivity =
                (float)__atomic_load_n(&s_vertical_sensitivity_percent,
                                       __ATOMIC_ACQUIRE) /
                100.0f;
            const float control_x = filtered_x * horizontal_sensitivity;
            const float control_y = filtered_y * vertical_sensitivity;
            const bc32_input_event_t joystick = {
                .kind = BC32_INPUT_JOYSTICK,
                // BBC analogue inputs use zero at right/down.
                .joystick = {.x = analogue_axis(control_x, false),
                             .y = analogue_axis(control_y, false)},
            };
            s_callback(&joystick, s_callback_context);

            // Movement engages at five degrees from the calibrated pose. The
            // 2.5-degree release point keeps a held direction stable while the
            // quicker filter makes short steering motions register promptly.
            const float press = 5.0f;
            const float release = 2.5f;

            bool next_left = left ? control_x > release : control_x > press;
            bool next_right =
                right ? control_x < -release : control_x < -press;
            bool next_up = up ? control_y > release : control_y > press;
            bool next_down =
                down ? control_y < -release : control_y < -press;
            if (!__atomic_load_n(&s_allow_diagonals, __ATOMIC_ACQUIRE) &&
                (next_left || next_right) && (next_up || next_down)) {
                if (fabsf(control_x) >= fabsf(control_y)) {
                    next_up = false;
                    next_down = false;
                } else {
                    next_left = false;
                    next_right = false;
                }
            }

            update_direction(&left, next_left, BC32_DIRECTION_LEFT, "left");
            update_direction(&right, next_right, BC32_DIRECTION_RIGHT, "right");
            update_direction(&up, next_up, BC32_DIRECTION_UP, "up");
            update_direction(&down, next_down, BC32_DIRECTION_DOWN, "down");

            if (++log_count == 100) {
                ESP_LOGI(TAG, "relative tilt %.1f %.1f degrees at %u/%u%% -> joystick %u,%u",
                         (double)filtered_x, (double)filtered_y,
                         (unsigned)__atomic_load_n(
                             &s_horizontal_sensitivity_percent,
                             __ATOMIC_RELAXED),
                         (unsigned)__atomic_load_n(
                             &s_vertical_sensitivity_percent,
                             __ATOMIC_RELAXED),
                         joystick.joystick.x, joystick.joystick.y);
                log_count = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t bc32_motion_input_init(i2c_master_bus_handle_t bus,
                                 bc32_input_callback_t callback, void *context)
{
    if (bus == NULL || callback == NULL) return ESP_ERR_INVALID_ARG;

    uint8_t address = 0;
    if (i2c_master_probe(bus, QMI8658_ADDRESS_HIGH, 100) == ESP_OK) {
        address = QMI8658_ADDRESS_HIGH;
    } else if (i2c_master_probe(bus, QMI8658_ADDRESS_LOW, 100) == ESP_OK) {
        address = QMI8658_ADDRESS_LOW;
    } else {
        return ESP_ERR_NOT_FOUND;
    }

    ESP_RETURN_ON_ERROR(qmi8658_init(&s_imu, bus, address), TAG, "QMI8658 init");
    ESP_RETURN_ON_ERROR(qmi8658_write_register(&s_imu, QMI8658_RESET_REGISTER,
                                                QMI8658_RESET_COMMAND),
                        TAG, "QMI8658 reset");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(qmi8658_write_register(&s_imu, QMI8658_CTRL1,
                                                QMI8658_CTRL1_VALUE),
                        TAG, "QMI8658 ctrl1");
    ESP_RETURN_ON_ERROR(qmi8658_set_accel_range(&s_imu, QMI8658_ACCEL_RANGE_4G),
                        TAG, "accelerometer range");
    ESP_RETURN_ON_ERROR(qmi8658_set_accel_odr(&s_imu, QMI8658_ACCEL_ODR_125HZ),
                        TAG, "accelerometer rate");
    qmi8658_set_accel_unit_mps2(&s_imu, true);
    ESP_RETURN_ON_ERROR(qmi8658_enable_sensors(&s_imu, QMI8658_ENABLE_ACCEL),
                        TAG, "accelerometer enable");

    s_callback = callback;
    s_callback_context = context;
    __atomic_store_n(&s_calibrating, true, __ATOMIC_RELEASE);
    if (xTaskCreatePinnedToCore(motion_task, "motion", 4096, NULL, 3, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "QMI8658 ready at 0x%02x; calibrated tilt directions active", address);
    return ESP_OK;
}

void bc32_motion_input_set_sensitivity(uint16_t horizontal_percent,
                                       uint16_t vertical_percent)
{
    if (horizontal_percent < 10) horizontal_percent = 10;
    if (horizontal_percent > 500) horizontal_percent = 500;
    if (vertical_percent < 10) vertical_percent = 10;
    if (vertical_percent > 500) vertical_percent = 500;
    __atomic_store_n(&s_horizontal_sensitivity_percent, horizontal_percent,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&s_vertical_sensitivity_percent, vertical_percent,
                     __ATOMIC_RELEASE);
    ESP_LOGI(TAG, "tilt sensitivity set to horizontal %u%%, vertical %u%%",
             (unsigned)horizontal_percent, (unsigned)vertical_percent);
}

void bc32_motion_input_set_allow_diagonals(bool allow)
{
    __atomic_store_n(&s_allow_diagonals, allow, __ATOMIC_RELEASE);
    ESP_LOGI(TAG, "tilt diagonals %s", allow ? "enabled" : "disabled");
}

void bc32_motion_input_recalibrate(void)
{
    __atomic_store_n(&s_calibrating, true, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recalibrate_requested, true, __ATOMIC_RELEASE);
}

bool bc32_motion_input_is_calibrating(void)
{
    return __atomic_load_n(&s_calibrating, __ATOMIC_ACQUIRE);
}
