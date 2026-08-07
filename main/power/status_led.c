#include "status_led.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_config.h"

static volatile led_pattern_t s_pattern = LED_PATTERN_OFF;

static void led_task(void *arg)
{
    bool on = false;
    while (1) {
        switch (s_pattern) {
        case LED_PATTERN_SOLID:
            gpio_set_level(BOARD_PIN_STATUS_LED, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
        case LED_PATTERN_SLOW_BLINK:
            on = !on;
            gpio_set_level(BOARD_PIN_STATUS_LED, on);
            vTaskDelay(pdMS_TO_TICKS(800));
            break;
        case LED_PATTERN_FAST_BLINK:
            on = !on;
            gpio_set_level(BOARD_PIN_STATUS_LED, on);
            vTaskDelay(pdMS_TO_TICKS(150));
            break;
        case LED_PATTERN_OFF:
        default:
            gpio_set_level(BOARD_PIN_STATUS_LED, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
        }
    }
}

esp_err_t status_led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BOARD_PIN_STATUS_LED,
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }
    return xTaskCreate(led_task, "status_led", 2048, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_FAIL;
}

void status_led_set_pattern(led_pattern_t pattern)
{
    s_pattern = pattern;
}
