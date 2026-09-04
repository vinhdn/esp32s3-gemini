#include <Arduino.h>
#include <lvgl.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ble_server.h"
#include "hud_state.h"
#include "ui.h"

// Task rieng cho LVGL (core 1) - tach khoi task loop() mac dinh (core 1 cung,
// nhung Arduino loop() khong dam bao chu ky deu; task rieng voi vTaskDelay
// on dinh hon cho animation/ve man hinh). Callback ghi du lieu BLE chay tren
// task cua NimBLE (khac task nay), dong bo qua hud_state_lock()/unlock().
static void lvgl_task(void *arg)
{
    (void)arg;
    ui_init();
    Serial.println("[lvgl_task] UI da khoi tao");

    uint32_t last_refresh = 0;
    for (;;) {
        lv_timer_handler();

        uint32_t now = millis();
        if (now - last_refresh >= 200) {
            last_refresh = now;
            ui_refresh();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("=== esp32-cyd-hud boot ===");

    hud_state_init();
    Serial.println("[setup] hud_state_init xong");

    ble_server_init();
    Serial.println("[setup] ble_server_init xong");

    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, nullptr, 2, nullptr, 1);
    Serial.println("[setup] da tao lvgl_task, cho UI khoi tao...");
}

void loop()
{
    // Toan bo cong viec chay trong lvgl_task (UI) + callback NimBLE (BLE) -
    // khong can lam gi o day.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
