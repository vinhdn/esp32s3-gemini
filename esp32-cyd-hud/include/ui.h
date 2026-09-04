#pragma once

// Khoi tao display driver (TFT_eSPI + touch XPT2046) + LVGL + dung cay widget
// HUD. Goi 1 lan tu task LVGL truoc vong lap chinh.
void ui_init();

// Doc hud_state_t (qua hud_state_lock/unlock) va cap nhat lai widget - goi
// dinh ky (vai tram ms la du, khong can moi frame) tu task LVGL.
void ui_refresh();
