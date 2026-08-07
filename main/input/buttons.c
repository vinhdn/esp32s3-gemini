#include "buttons.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_config.h"

static const char *TAG = "buttons";

#define POLL_INTERVAL_MS 10

typedef struct {
    gpio_num_t pin;
    bool has_long_press;
    button_event_t click_event;
    button_event_t long_press_event;

    bool debounced_pressed;
    bool raw_last;
    int64_t last_change_us;
    int64_t press_start_us;
    bool long_press_fired;
} button_ctx_t;

static button_ctx_t s_buttons[] = {
    { .pin = BOARD_PIN_BTN_TALK, .has_long_press = false, .click_event = BUTTON_EVENT_TALK_CLICK },
    { .pin = BOARD_PIN_BTN_VOL_UP, .has_long_press = true,
      .click_event = BUTTON_EVENT_VOL_UP_CLICK, .long_press_event = BUTTON_EVENT_VOL_UP_LONG },
    { .pin = BOARD_PIN_BTN_VOL_DOWN, .has_long_press = true,
      .click_event = BUTTON_EVENT_VOL_DOWN_CLICK, .long_press_event = BUTTON_EVENT_VOL_DOWN_LONG },
};

static button_event_cb_t s_cb = NULL;
static void *s_cb_ctx = NULL;

static inline bool gpio_is_pressed(gpio_num_t pin)
{
    return gpio_get_level(pin) == BOARD_BTN_ACTIVE_LEVEL;
}

static void buttons_task(void *arg)
{
    while (1) {
        int64_t now = esp_timer_get_time();
        for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
            button_ctx_t *b = &s_buttons[i];
            bool raw = gpio_is_pressed(b->pin);

            if (raw != b->raw_last) {
                b->raw_last = raw;
                b->last_change_us = now;
            }

            bool stable = (now - b->last_change_us) >= (BOARD_BTN_DEBOUNCE_MS * 1000);
            if (stable && raw != b->debounced_pressed) {
                b->debounced_pressed = raw;
                if (raw) {
                    // Vua nhan xuong
                    b->press_start_us = now;
                    b->long_press_fired = false;
                } else {
                    // Vua tha ra: neu chua kich hoat long-press thi tinh la 1 click
                    if (!b->long_press_fired && s_cb) {
                        s_cb(b->click_event, s_cb_ctx);
                    }
                }
            }

            if (b->debounced_pressed && b->has_long_press && !b->long_press_fired) {
                if ((now - b->press_start_us) >= (BOARD_BTN_LONG_PRESS_MS * 1000)) {
                    b->long_press_fired = true;
                    if (s_cb) {
                        s_cb(b->long_press_event, s_cb_ctx);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

esp_err_t buttons_init(button_event_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;

    uint64_t pin_mask = 0;
    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
        pin_mask |= (1ULL << s_buttons[i].pin);
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = (BOARD_BTN_ACTIVE_LEVEL == 0) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (BOARD_BTN_ACTIVE_LEVEL == 1) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    int64_t now = esp_timer_get_time();
    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
        s_buttons[i].raw_last = gpio_is_pressed(s_buttons[i].pin);
        s_buttons[i].debounced_pressed = s_buttons[i].raw_last;
        s_buttons[i].last_change_us = now;
    }

    if (xTaskCreate(buttons_task, "buttons_task", 3072, NULL, 8, NULL) != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Buttons da san sang (talk=%d, vol_up=%d, vol_down=%d)",
             BOARD_PIN_BTN_TALK, BOARD_PIN_BTN_VOL_UP, BOARD_PIN_BTN_VOL_DOWN);
    return ESP_OK;
}
