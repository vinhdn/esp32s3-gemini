#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Ghep noi codec_board (mic/loa vat ly, 16kHz mono) voi kien truc tro ly
// TURN-BASED: ghi ca cau hoi vao PSRAM roi moi gui di, thay vi streaming lien
// tuc nhu ban Gemini Live truoc day.
//
// Tao 2 task: 1 task doc mic (ghi vao buffer + tu phat hien im lang de dung),
// 1 task lay PCM tu hang doi va phat ra loa.
esp_err_t audio_pipeline_init(void);

// ---- Ghi am ---------------------------------------------------------------
// Bat dau ghi mot cau hoi moi. Viec ghi TU DONG DUNG khi:
//   - nguoi dung im lang mot khoang sau khi da noi, hoac
//   - het thoi gian cho ma khong noi gi, hoac
//   - dat gioi han thoi luong toi da.
esp_err_t audio_pipeline_record_start(void);

// Dung ghi ngay lap tuc (nguoi dung huy giua chung).
void audio_pipeline_record_stop(void);

// true khi con dang ghi. Ben goi poll ham nay de biet khi nao xong.
bool audio_pipeline_record_is_active(void);

// true neu trong luc ghi co phat hien tieng noi (nang luong vuot nguong).
// false nghia la chi ghi duoc khoang lang -> khong nen gui len server.
bool audio_pipeline_record_had_speech(void);

// Tra ve buffer WAV hoan chinh (44 byte header + PCM16 mono 16kHz) da ghi.
// Buffer thuoc so huu cua module, chi hop le cho tot khi
// audio_pipeline_record_start() duoc goi lan sau.
void audio_pipeline_record_get_wav(const uint8_t **out_wav, size_t *out_len);

// ---- Phat ra loa ----------------------------------------------------------
// Day PCM16 mono vao hang doi phat, tu resample tu src_rate_hz ve 16kHz (phan
// cung dung chung clock cho thu/phat nen chi chay duoc o 16kHz).
//
// Ham nay BLOCK khi hang doi day - do la co y: nho vay ben giai ma MP3 tu bi
// tiet che dung bang toc do phat thuc te, khong can hang doi lon.
esp_err_t audio_pipeline_play_pcm(const int16_t *pcm, size_t sample_count, int src_rate_hz);

// Chuan bi phat mot cau tra loi moi: xoa hang doi va bo co huy.
void audio_pipeline_playback_reset(void);

// Xoa het audio dang cho phat va bao cho audio_pipeline_play_pcm() dang block
// thoat ra ngay (dung khi nguoi dung huy giua luc AI dang noi).
void audio_pipeline_flush_playback(void);

// true neu hang doi phat da rong va khong con dang ghi ra loa.
bool audio_pipeline_is_playback_idle(void);

#ifdef __cplusplus
}
#endif
