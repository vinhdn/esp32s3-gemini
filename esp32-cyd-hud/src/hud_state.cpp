#include "hud_state.h"

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

hud_state_t g_hud_state;
static SemaphoreHandle_t s_lock;

void hud_state_init()
{
    s_lock = xSemaphoreCreateMutex();
    memset(&g_hud_state, 0, sizeof(g_hud_state));
}

void hud_state_lock()
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

void hud_state_unlock()
{
    xSemaphoreGive(s_lock);
}
