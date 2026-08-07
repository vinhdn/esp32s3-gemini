#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Khoi tao I2C (dieu khien ES8311) + I2S (du lieu audio) + mo 2 thiet bi
// playback/record qua esp_codec_dev. Sample rate co dinh = BOARD_I2S_SAMPLE_RATE_HZ
// (16kHz) cho ca 2 chieu vi dung chung 1 I2S bus full-duplex.
esp_err_t codec_board_init(void);

// Doc "sample_count" mau PCM16 mono tu mic (blocking). Tra ve so mau doc duoc.
int codec_board_read(int16_t *pcm_buf, size_t sample_count);

// Ghi "sample_count" mau PCM16 mono ra loa (blocking).
esp_err_t codec_board_write(const int16_t *pcm_buf, size_t sample_count);

// Chinh volume loa, 0-100%.
esp_err_t codec_board_set_volume(uint8_t percent);

#ifdef __cplusplus
}
#endif
