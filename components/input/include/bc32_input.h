#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BC32_INPUT_KEY,
    BC32_INPUT_JOYSTICK,
    BC32_INPUT_JOYSTICK_BUTTON,
    BC32_INPUT_BREAK,
    BC32_INPUT_DIRECTION,
    BC32_INPUT_ACTION,
    BC32_INPUT_TOUCH,
    BC32_INPUT_SHAKE,
    BC32_INPUT_GAME_CHOOSER,
} bc32_input_kind_t;

typedef enum {
    BC32_DIRECTION_LEFT,
    BC32_DIRECTION_RIGHT,
    BC32_DIRECTION_UP,
    BC32_DIRECTION_DOWN,
} bc32_direction_t;

typedef struct {
    bc32_input_kind_t kind;
    union {
        struct {
            uint8_t row;
            uint8_t column;
            bool down;
        } key;
        struct {
            uint16_t x;
            uint16_t y;
        } joystick;
        struct {
            bool down;
        } joystick_button;
        struct {
            bc32_direction_t direction;
            bool down;
        } direction;
        struct {
            bool secondary;
            bool down;
        } action;
        struct {
            uint16_t x;
            uint16_t y;
            bool down;
        } touch;
    };
} bc32_input_event_t;

typedef void (*bc32_input_callback_t)(const bc32_input_event_t *event, void *context);

esp_err_t bc32_motion_input_init(i2c_master_bus_handle_t bus,
                                 bc32_input_callback_t callback, void *context);
esp_err_t bc32_power_button_init(i2c_master_bus_handle_t bus,
                                 bc32_input_callback_t callback, void *context);
esp_err_t bc32_boot_button_init(bc32_input_callback_t callback, void *context);
esp_err_t bc32_touch_input_init(i2c_master_bus_handle_t bus,
                                bc32_input_callback_t callback, void *context);
esp_err_t bc32_ble_keyboard_init(bc32_input_callback_t callback, void *context);
bool bc32_ble_keyboard_connected(void);
// Non-zero while a BLE keyboard is waiting for this passkey to be typed,
// followed by Enter, on the keyboard itself.
uint32_t bc32_ble_keyboard_pairing_passkey(void);
// Percentage gain applied to calibrated tilt for both analogue and digital
// movement. 100 is the normal response; lower values require a larger tilt.
void bc32_motion_input_set_sensitivity(uint16_t percent);
void bc32_motion_input_set_allow_diagonals(bool allow);
void bc32_motion_input_recalibrate(void);
bool bc32_motion_input_is_calibrating(void);

#ifdef __cplusplus
}
#endif
