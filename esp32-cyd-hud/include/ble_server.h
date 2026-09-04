#pragma once

// Khoi tao GATT server NimBLE + bat dau quang ba, gia lam board ESP32-S3
// hien tai (ten + service/characteristic giong het waze_hud_ble.c) de app
// Android (com.esp32nav, qua ImageRelayBle.kt) ket noi duoc ma khong can sua
// gi ben app. Goi 1 lan trong setup().
void ble_server_init();
