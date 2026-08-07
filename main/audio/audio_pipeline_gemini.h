#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Ghep noi codec_board (mic/loa vat ly, 16kHz mono) voi gemini_live_client
// (audio vao/ra qua WebSocket). Tao 2 task rieng: 1 task doc mic va gui len
// Gemini khi dang "talking", 1 task lay audio Gemini tra ve tu hang doi va
// phat ra loa (sau khi resample 24kHz -> 16kHz vi phan cung dung chung clock
// cho ca thu/phat).
esp_err_t audio_pipeline_init(void);

// Bat/tat viec doc mic va gui len Gemini (dieu khien boi nut push-to-talk).
void audio_pipeline_start_talking(void);
void audio_pipeline_stop_talking(void);

// Goi tu gemini_live_client moi khi nhan duoc 1 doan audio PCM16 24kHz mono
// tu serverContent. Ham nay resample xuong 16kHz va day vao hang doi phat.
void audio_pipeline_on_gemini_audio(const uint8_t *pcm24k_bytes, size_t byte_len);

// Xoa het audio dang cho phat (dung khi Gemini bao "interrupted" vi nguoi
// dung noi de vao trong luc AI dang tra loi).
void audio_pipeline_flush_playback(void);

// true neu hang doi phat da rong va khong con dang ghi ra loa — dung de app
// biet khi nao AI da noi xong va co the quay ve trang thai IDLE.
bool audio_pipeline_is_playback_idle(void);

// true neu mic dang phat hien nang luong am thanh vuot nguong trong
// VAD_HANGOVER_MS gan nhat (VAD noi bo, chi de hien thi UI "dang nghe" vs
// "dang xu ly", khong anh huong toi viec gui audio len Gemini).
bool audio_pipeline_is_user_speaking(void);

#ifdef __cplusplus
}
#endif
