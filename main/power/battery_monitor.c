#include "battery_monitor.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board_config.h"

static const char *TAG = "battery_monitor";

#define ADC_HISTORY_COUNT   3
#define SAMPLE_INTERVAL_MS  10000

// Bang hieu chuan ADC(12-bit, 12dB atten) -> % pin, lay tu mach phan ap that
// tren board LC-S3-WiFi-1.54TFT. Doi lai neu do dac tren board cua ban lech.
typedef struct {
    uint16_t adc;
    uint8_t level;
} battery_level_point_t;

static const battery_level_point_t LEVELS[] = {
    { 1970, 0 }, { 2062, 20 }, { 2154, 40 }, { 2246, 60 }, { 2338, 80 }, { 2430, 100 },
};
#define LEVELS_COUNT (sizeof(LEVELS) / sizeof(LEVELS[0]))

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static esp_timer_handle_t s_timer = NULL;
static int s_history[ADC_HISTORY_COUNT];
static int s_history_len = 0;
static int s_history_pos = 0;
static uint8_t s_level = 100;
static bool s_charging = false;

static uint8_t adc_to_percent(uint32_t adc)
{
    if (adc < LEVELS[0].adc) {
        return 0;
    }
    if (adc >= LEVELS[LEVELS_COUNT - 1].adc) {
        return 100;
    }
    for (size_t i = 0; i < LEVELS_COUNT - 1; i++) {
        if (adc >= LEVELS[i].adc && adc < LEVELS[i + 1].adc) {
            float ratio = (float)(adc - LEVELS[i].adc) / (float)(LEVELS[i + 1].adc - LEVELS[i].adc);
            return (uint8_t)(LEVELS[i].level + ratio * (LEVELS[i + 1].level - LEVELS[i].level));
        }
    }
    return 100;
}

static void sample_cb(void *arg)
{
    s_charging = gpio_get_level(BOARD_PIN_BATTERY_CHARGE) == 1;

    int raw = 0;
    if (adc_oneshot_read(s_adc_handle, BOARD_BATTERY_ADC_CHANNEL, &raw) != ESP_OK) {
        return;
    }

    s_history[s_history_pos] = raw;
    s_history_pos = (s_history_pos + 1) % ADC_HISTORY_COUNT;
    if (s_history_len < ADC_HISTORY_COUNT) {
        s_history_len++;
    }

    uint32_t sum = 0;
    for (int i = 0; i < s_history_len; i++) {
        sum += s_history[i];
    }
    uint32_t avg = sum / s_history_len;
    s_level = adc_to_percent(avg);

    ESP_LOGD(TAG, "ADC raw=%d avg=%lu level=%d%% charging=%d", raw, avg, s_level, s_charging);
}

esp_err_t battery_monitor_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BOARD_PIN_BATTERY_CHARGE,
        .mode = GPIO_MODE_INPUT,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    adc_oneshot_unit_init_cfg_t init_cfg = { .unit_id = BOARD_BATTERY_ADC_UNIT };
    err = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BOARD_BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_oneshot_config_channel(s_adc_handle, BOARD_BATTERY_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        return err;
    }

    sample_cb(NULL); // doc ngay 1 lan de co gia tri ban dau khi vua boot

    esp_timer_create_args_t timer_args = {
        .callback = sample_cb,
        .name = "battery_sample",
    };
    err = esp_timer_create(&timer_args, &s_timer);
    if (err != ESP_OK) {
        return err;
    }
    return esp_timer_start_periodic(s_timer, SAMPLE_INTERVAL_MS * 1000);
}

uint8_t battery_monitor_get_level(void)
{
    return s_level;
}

bool battery_monitor_is_charging(void)
{
    return s_charging;
}
