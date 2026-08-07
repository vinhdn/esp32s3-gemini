#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GEMINI_LIVE_EVENT_SESSION_READY,  // setupComplete nhan duoc, co the bat dau gui audio
    GEMINI_LIVE_EVENT_DISCONNECTED,
    GEMINI_LIVE_EVENT_TURN_COMPLETE,  // AI da noi xong 1 luot
    GEMINI_LIVE_EVENT_INTERRUPTED,    // nguoi dung noi de vao giua luc AI dang tra loi
} gemini_live_event_t;

typedef enum {
    GEMINI_LIVE_TEXT_USER_INPUT,  // transcript cua audio nguoi dung noi (STT)
    GEMINI_LIVE_TEXT_AI_OUTPUT,   // transcript cua audio AI dang tra loi
} gemini_live_text_kind_t;

// pcm_bytes: PCM16 mono, sample rate = GEMINI_LIVE_OUTPUT_SAMPLE_RATE (24kHz).
typedef void (*gemini_live_audio_cb_t)(const uint8_t *pcm_bytes, size_t byte_len, void *ctx);
typedef void (*gemini_live_event_cb_t)(gemini_live_event_t event, void *ctx);

// text_chunk la 1 doan transcript moi nhan duoc tu server (co the la delta,
// khong phai toan bo cau) - ben goi chiu trach nhiem gop lai de hien thi.
typedef void (*gemini_live_text_cb_t)(gemini_live_text_kind_t kind, const char *text_chunk, void *ctx);

esp_err_t gemini_live_client_init(gemini_live_audio_cb_t audio_cb, gemini_live_event_cb_t event_cb,
                                   gemini_live_text_cb_t text_cb, void *ctx);

// Mo WebSocket toi Gemini Live va gui message "setup". api_key lay tu NVS
// (nguoi dung nhap qua captive portal luc provisioning).
esp_err_t gemini_live_client_connect(const char *api_key);

void gemini_live_client_disconnect(void);
bool gemini_live_client_is_session_ready(void);

// Gui 1 khung PCM16 mono 16kHz (se duoc base64-encode va boc trong JSON
// realtimeInput ben trong ham nay). Khong lam gi neu session chua san sang.
esp_err_t gemini_live_client_send_audio(const int16_t *pcm16k, size_t sample_count);

#ifdef __cplusplus
}
#endif
