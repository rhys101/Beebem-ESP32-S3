#include "bc32_audio.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AUDIO_SAMPLE_RATE 22050U
#define AUDIO_FRAME_SAMPLES 256U
#define SN76489_CLOCK_HZ 4000000U
#define SN76489_TONE_CLOCK_HZ (SN76489_CLOCK_HZ / 32U)

#define I2S_MCLK GPIO_NUM_16
#define I2S_BCLK GPIO_NUM_9
#define I2S_LRCLK GPIO_NUM_45
#define I2S_DOUT GPIO_NUM_8
#define SPEAKER_AMP_ENABLE GPIO_NUM_46

typedef struct {
    uint16_t tone_period[3];
    uint8_t attenuation[4];
    uint8_t noise_control;
    uint32_t noise_epoch;
    uint8_t latched_channel;
    bool latched_volume;
} psg_registers_t;

typedef struct {
    uint32_t tone_phase[3];
    uint32_t noise_phase;
    uint16_t noise_lfsr;
    uint32_t noise_epoch;
} psg_oscillators_t;

static const char *TAG = "audio";
static portMUX_TYPE s_psg_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_register_writes;
static psg_registers_t s_psg = {
    .tone_period = {1, 1, 1},
    .attenuation = {15, 15, 15, 15},
    .noise_control = 0,
    .latched_channel = 0,
    .latched_volume = false,
};

// SN76489 attenuation is approximately 2 dB per step.  Keeping the table at
// one sixth of full scale leaves headroom when all four voices are active.
static const int16_t s_level[16] = {
    5461, 4338, 3446, 2737, 2174, 1727, 1372, 1090,
    866,  688,  546,  434,  345,  274,  217,  0,
};

static uint32_t phase_step(uint32_t frequency_hz)
{
    return (uint32_t)(((uint64_t)frequency_hz << 32) / AUDIO_SAMPLE_RATE);
}

static void psg_snapshot(psg_registers_t *snapshot)
{
    taskENTER_CRITICAL(&s_psg_lock);
    *snapshot = s_psg;
    taskEXIT_CRITICAL(&s_psg_lock);
}

void bc32_audio_reg_write(uint8_t value)
{
    taskENTER_CRITICAL(&s_psg_lock);
    ++s_register_writes;

    if ((value & 0x80U) != 0) {
        const uint8_t channel = (value >> 5) & 3U;
        const bool is_volume = (value & 0x10U) != 0;
        s_psg.latched_channel = channel;
        s_psg.latched_volume = is_volume;

        if (is_volume) {
            s_psg.attenuation[channel] = value & 15U;
        } else if (channel == 3U) {
            s_psg.noise_control = value & 7U;
            ++s_psg.noise_epoch;
        } else {
            s_psg.tone_period[channel] =
                (s_psg.tone_period[channel] & 0x3f0U) | (value & 15U);
            if (s_psg.tone_period[channel] == 0) s_psg.tone_period[channel] = 1;
        }
    } else if (!s_psg.latched_volume && s_psg.latched_channel < 3U) {
        const uint8_t channel = s_psg.latched_channel;
        s_psg.tone_period[channel] =
            (s_psg.tone_period[channel] & 15U) | ((uint16_t)(value & 0x3fU) << 4);
        if (s_psg.tone_period[channel] == 0) s_psg.tone_period[channel] = 1;
    }

    taskEXIT_CRITICAL(&s_psg_lock);
}

void bc32_audio_silence(void)
{
    taskENTER_CRITICAL(&s_psg_lock);
    for (unsigned channel = 0; channel < 4; ++channel) {
        s_psg.attenuation[channel] = 15;
    }
    s_psg.latched_channel = 0;
    s_psg.latched_volume = false;
    ++s_psg.noise_epoch;
    taskEXIT_CRITICAL(&s_psg_lock);
}

static int16_t psg_sample(const psg_registers_t *registers,
                          const uint32_t tone_step[3], uint32_t noise_step,
                          psg_oscillators_t *oscillators)
{
    int32_t mixed = 0;

    for (unsigned channel = 0; channel < 3; ++channel) {
        oscillators->tone_phase[channel] += tone_step[channel];
        mixed += (oscillators->tone_phase[channel] & 0x80000000U) != 0
                     ? s_level[registers->attenuation[channel]]
                     : -s_level[registers->attenuation[channel]];
    }

    if (oscillators->noise_epoch != registers->noise_epoch) {
        oscillators->noise_epoch = registers->noise_epoch;
        oscillators->noise_lfsr = 0x4000;
        oscillators->noise_phase = 0;
    }

    const uint32_t old_noise_phase = oscillators->noise_phase;
    oscillators->noise_phase += noise_step;
    if (oscillators->noise_phase < old_noise_phase) {
        const uint16_t output = oscillators->noise_lfsr & 1U;
        const uint16_t feedback = (registers->noise_control & 4U) != 0
                                      ? (oscillators->noise_lfsr ^
                                         (oscillators->noise_lfsr >> 1)) & 1U
                                      : output;
        oscillators->noise_lfsr =
            (oscillators->noise_lfsr >> 1) | (feedback << 14);
        if (oscillators->noise_lfsr == 0) oscillators->noise_lfsr = 0x4000;
    }
    mixed += (oscillators->noise_lfsr & 1U) != 0
                 ? s_level[registers->attenuation[3]]
                 : -s_level[registers->attenuation[3]];

    return (int16_t)mixed;
}

static void audio_task(void *argument)
{
    esp_codec_dev_handle_t speaker = (esp_codec_dev_handle_t)argument;
    psg_oscillators_t oscillators = {.noise_lfsr = 0x4000};
    int16_t samples[AUDIO_FRAME_SAMPLES];
    unsigned blocks = 0;
    int peak = 0;

    for (;;) {
        psg_registers_t registers;
        psg_snapshot(&registers);
        uint32_t tone_step[3];
        for (unsigned channel = 0; channel < 3; ++channel) {
            tone_step[channel] = phase_step(
                SN76489_TONE_CLOCK_HZ / registers.tone_period[channel]);
        }
        uint32_t noise_frequency;
        switch (registers.noise_control & 3U) {
        case 0: noise_frequency = SN76489_CLOCK_HZ / 512U; break;
        case 1: noise_frequency = SN76489_CLOCK_HZ / 1024U; break;
        case 2: noise_frequency = SN76489_CLOCK_HZ / 2048U; break;
        default:
            noise_frequency =
                SN76489_TONE_CLOCK_HZ / registers.tone_period[2];
            break;
        }
        const uint32_t noise_step = phase_step(noise_frequency);
        for (unsigned index = 0; index < AUDIO_FRAME_SAMPLES; ++index) {
            samples[index] =
                psg_sample(&registers, tone_step, noise_step, &oscillators);
            const int magnitude = samples[index] < 0 ? -samples[index] : samples[index];
            if (magnitude > peak) peak = magnitude;
        }

        const int result = esp_codec_dev_write(speaker, samples, sizeof(samples));
        if (result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "speaker write failed: %d", result);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (++blocks == 512) {
            ESP_LOGI(TAG, "PSG writes=%u, recent peak=%d",
                     (unsigned)__atomic_load_n(&s_register_writes, __ATOMIC_RELAXED),
                     peak);
            blocks = 0;
            peak = 0;
        }
    }
}

esp_err_t bc32_audio_init(i2c_master_bus_handle_t i2c_bus)
{
    ESP_RETURN_ON_FALSE(i2c_bus != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "missing shared I2C bus");

    i2s_chan_handle_t tx_handle = NULL;
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_config.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, &tx_handle, NULL), TAG,
                        "create I2S channel failed");

    const i2s_std_config_t i2s_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws = I2S_LRCLK,
            .dout = I2S_DOUT,
            .din = GPIO_NUM_NC,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_handle, &i2s_config), TAG,
                        "configure I2S failed");
    // esp_codec_dev disables the channel before applying its final sample
    // format.  Enabling once here makes that transition valid and avoids a
    // misleading driver error during an otherwise successful codec open.
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_handle), TAG,
                        "start initial I2S clock failed");

    audio_codec_i2s_cfg_t codec_i2s_config = {
        .port = I2S_NUM_0,
        .tx_handle = tx_handle,
        .rx_handle = NULL,
    };
    const audio_codec_data_if_t *data_if =
        audio_codec_new_i2s_data(&codec_i2s_config);
    ESP_RETURN_ON_FALSE(data_if != NULL, ESP_ERR_NO_MEM, TAG,
                        "create codec data interface failed");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio_if != NULL, ESP_ERR_NO_MEM, TAG,
                        "create codec GPIO interface failed");

    audio_codec_i2c_cfg_t codec_i2c_config = {
        .port = I2C_NUM_0,
        // esp_codec_dev uses the traditional 8-bit I2C address and converts
        // it to the ESP-IDF driver's 7-bit 0x18 address internally.
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *control_if =
        audio_codec_new_i2c_ctrl(&codec_i2c_config);
    ESP_RETURN_ON_FALSE(control_if != NULL, ESP_ERR_NO_MEM, TAG,
                        "create codec control interface failed");

    const esp_codec_dev_hw_gain_t hardware_gain = {
        .pa_voltage = 5.0f,
        .codec_dac_voltage = 3.3f,
    };
    es8311_codec_cfg_t es8311_config = {
        .ctrl_if = control_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = SPEAKER_AMP_ENABLE,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = hardware_gain,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_config);
    ESP_RETURN_ON_FALSE(codec_if != NULL, ESP_FAIL, TAG,
                        "create ES8311 codec failed");

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    esp_codec_dev_handle_t speaker = esp_codec_dev_new(&device_config);
    ESP_RETURN_ON_FALSE(speaker != NULL, ESP_FAIL, TAG,
                        "create speaker device failed");

    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = AUDIO_SAMPLE_RATE,
        .mclk_multiple = 256,
    };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(speaker, &sample_info) == ESP_CODEC_DEV_OK,
                        ESP_FAIL, TAG, "open ES8311 failed");
    ESP_RETURN_ON_FALSE(esp_codec_dev_set_out_vol(speaker, 60) == ESP_CODEC_DEV_OK,
                        ESP_FAIL, TAG, "set ES8311 volume failed");

    const BaseType_t created =
        xTaskCreatePinnedToCore(audio_task, "bbc_audio", 4096, speaker, 6, NULL, 0);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, TAG,
                        "create audio task failed");
    ESP_LOGI(TAG, "ES8311 ready: 4-channel SN76489 at %u Hz", AUDIO_SAMPLE_RATE);
    return ESP_OK;
}
