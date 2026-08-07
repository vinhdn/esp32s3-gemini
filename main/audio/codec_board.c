// Bring-up codec ES8311 qua esp_codec_dev (I2C dieu khien tren BOARD_I2C_*,
// I2S full-duplex tren BOARD_I2S_* — xem main/board_config.h).
//
// LUU Y: ten header/struct cua esp_codec_dev co the thay doi giua cac phien
// ban. Code nay viet theo API pho bien cua esp_codec_dev ^1.6 (dung trong
// nhieu vi du chinh thuc cua Espressif nhu esp-box). Sau khi `idf.py build`
// tai component ve managed_components/espressif__esp_codec_dev, doi chieu
// header that su neu bao loi bien dich (vd ten file es8311_codec.h/es8311.h
// co the khac giua cac ban).

#include "codec_board.h"

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "esp_log.h"

#include "board_config.h"
#include "nvs_settings.h"

static const char *TAG = "codec_board";

static i2s_chan_handle_t s_i2s_tx = NULL;
static i2s_chan_handle_t s_i2s_rx = NULL;
static esp_codec_dev_handle_t s_play_dev = NULL;
static esp_codec_dev_handle_t s_rec_dev = NULL;

static esp_err_t init_i2c_bus(i2c_master_bus_handle_t *out_bus)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_PIN_SDA,
        .scl_io_num = BOARD_I2C_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&bus_cfg, out_bus);
}

static esp_err_t init_i2s(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BOARD_I2S_PORT, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx);
    if (err != ESP_OK) {
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BOARD_I2S_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_PIN_MCLK,
            .bclk = BOARD_I2S_PIN_BCLK,
            .ws = BOARD_I2S_PIN_WS,
            .dout = BOARD_I2S_PIN_DOUT,
            .din = BOARD_I2S_PIN_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(s_i2s_tx, &std_cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = i2s_channel_init_std_mode(s_i2s_rx, &std_cfg);
    if (err != ESP_OK) {
        return err;
    }

    ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_tx));
    ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_rx));
    return ESP_OK;
}

esp_err_t codec_board_init(void)
{
    i2c_master_bus_handle_t i2c_bus;
    ESP_ERROR_CHECK(init_i2c_bus(&i2c_bus));
    ESP_ERROR_CHECK(init_i2s());

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BOARD_I2C_PORT,
        .addr = BOARD_ES8311_I2C_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl_if) {
        ESP_LOGE(TAG, "audio_codec_new_i2c_ctrl that bai");
        return ESP_FAIL;
    }

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = BOARD_PIN_PA_ENABLE,
        .use_mclk = true,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);
    if (!codec_if) {
        ESP_LOGE(TAG, "es8311_codec_new that bai");
        return ESP_FAIL;
    }

    audio_codec_i2s_cfg_t i2s_data_cfg = {
        .port = BOARD_I2S_PORT,
        .rx_handle = s_i2s_rx,
        .tx_handle = s_i2s_tx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_data_cfg);
    if (!data_if) {
        ESP_LOGE(TAG, "audio_codec_new_i2s_data that bai");
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t play_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_play_dev = esp_codec_dev_new(&play_cfg);

    esp_codec_dev_cfg_t rec_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_rec_dev = esp_codec_dev_new(&rec_cfg);

    if (!s_play_dev || !s_rec_dev) {
        ESP_LOGE(TAG, "esp_codec_dev_new that bai");
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = BOARD_I2S_SAMPLE_RATE_HZ,
        .channel = 1,
        .bits_per_sample = 16,
    };
    ESP_ERROR_CHECK(esp_codec_dev_open(s_play_dev, &fs));
    ESP_ERROR_CHECK(esp_codec_dev_open(s_rec_dev, &fs));

    codec_board_set_volume(nvs_settings_get_volume());

    ESP_LOGI(TAG, "Codec ES8311 da san sang (%d Hz mono 16-bit)", BOARD_I2S_SAMPLE_RATE_HZ);
    return ESP_OK;
}

int codec_board_read(int16_t *pcm_buf, size_t sample_count)
{
    esp_err_t err = esp_codec_dev_read(s_rec_dev, pcm_buf, sample_count * sizeof(int16_t));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "codec_board_read loi: %s", esp_err_to_name(err));
        return -1;
    }
    return (int)sample_count;
}

esp_err_t codec_board_write(const int16_t *pcm_buf, size_t sample_count)
{
    return esp_codec_dev_write(s_play_dev, (void *)pcm_buf, sample_count * sizeof(int16_t));
}

esp_err_t codec_board_set_volume(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    return esp_codec_dev_set_out_vol(s_play_dev, percent);
}
