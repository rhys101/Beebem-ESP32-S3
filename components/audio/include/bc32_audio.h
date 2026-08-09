#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts the board's ES8311 speaker and a 22.05 kHz SN76489 renderer.
esp_err_t bc32_audio_init(i2c_master_bus_handle_t i2c_bus);

// Accepts one byte written to the BBC Micro's SN76489 sound generator.
// This is non-blocking and safe to call from the emulator task.
void bc32_audio_reg_write(uint8_t value);

// Immediately attenuate every PSG channel. Used when leaving a running game
// so its final latched tone cannot continue underneath the launcher.
void bc32_audio_silence(void);

#ifdef __cplusplus
}
#endif
