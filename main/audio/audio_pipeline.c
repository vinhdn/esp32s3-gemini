#include "audio_pipeline.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board_config.h"
#include "codec_board.h"

static const char *TAG = "audio_pipeline";

#define MIC_FRAME_SAMPLES     320   // 20ms @ 16kHz
#define WAV_HEADER_SIZE       44

// ---- Tham so ghi am (tu dung khi im lang) ---------------------------------
// Thoi luong toi da 1 cau hoi. 8s @ 16kHz mono 16-bit = 256KB, nam trong PSRAM.
#define RECORD_MAX_MS         8000
// Im lang bao lau thi coi la da noi xong.
#define RECORD_SILENCE_MS     1500
// Cho bao lau ma khong noi gi thi bo cuoc.
#define RECORD_NO_SPEECH_MS   4000
// ---- Nguong VAD TUONG DOI theo nen nhieu ----------------------------------
// KHONG dung nguong tuyet doi: thiet bi nam tren XE nen nen nhieu thay doi rat
// nhieu (may no, cua kinh, dieu hoa). Do thuc te tren board: trong phong yen
// tinh nen da la avg=2330 @30dB - cao gap 4.7 lan nguong tuyet doi 500 dung
// truoc day, tuc VAD se lap tuc tuong la "co tieng noi" va khong bao gio thay
// du 1.5s im lang -> moi luot ghi du 8s toan tieng on.
//
// Cach lam: do nen nhieu trong VAD_CALIB_MS dau moi ban ghi, roi coi la co
// tieng noi khi muc am vuot nen * VAD_SPEECH_RATIO. Tieng noi thuong cao hon
// nen 10-20dB (3-10 lan) nen he so 3.0 la du nhay ma khong bat nham tieng on.
#define VAD_CALIB_MS          300
#define VAD_SPEECH_RATIO      3.0f
// San duoi: chi de chan truong hop nen do duoc gan nhu 0 (se sinh nguong qua
// nhay). Do thuc te nen tinh @30dB la avg=101 -> nguong tuong doi ~300, nen
// san 120 chi kich hoat khi nen < 40, tuc KHONG lan at logic hieu chuan.
// (Truoc day dat 200 - roi dung giua vung 180-330 nen co luc lan at, co luc
// khong, gay hanh vi that thuong.)
#define VAD_MIN_THRESHOLD     120
// Tran tren: CHONG hieu chuan bi nhiem tieng noi. Neu nguoi dung bam nut roi
// noi ngay, ca 300ms hieu chuan deu la tieng noi -> nen do duoc rat cao ->
// nguong vot len va CA LUOT do khong con nhan ra tieng noi nao nua (dung lai
// chinh loi "Khong nghe thay gi" dang phai sua). Chan nguong o 2000 de tieng noi
// binh thuong (avg ~700-2200) luon co the vuot qua, nhung van cho nguong tu do
// tang khi xe on that.
#define VAD_MAX_THRESHOLD     2000

#define RECORD_MAX_BYTES      (RECORD_MAX_MS * (BOARD_I2S_SAMPLE_RATE_HZ / 1000) * 2)

// Xa audio cu trong DMA ring truoc khi bat dau ban ghi moi. BAT BUOC phai co:
// kenh I2S RX duoc bat tu luc codec_board_init() va chay lien tuc, nen DMA ring
// luon chua audio ghi duoc TRUOC khi nguoi dung bam nut - ke ca tieng AI vua
// doc o luot truoc (mic va loa dung chung 1 bus I2S full-duplex). Neu khong xa,
// dau moi ban ghi se lan am cu va VAD lap tuc tuong la "co tieng noi".
//
// Xa theo dung luong DMA ring THAT, khong doan theo ms: du lieu da nam trong
// ring thi doc ra ngay lap tuc (khong block), nhung khi ring da can thi
// codec_board_read() se block cho du lieu moi -> xa qua nhieu se an vao tieng
// noi THAT cua nguoi dung va cat mat am dau cau.
//
// LUU Y: 1440 khong chia het cho MIC_FRAME_SAMPLES (320) nen vong lap lam tron
// LEN thanh 5 frame = 1600 mau (100ms), tuc xa qua ring 160 mau (~10ms).
// CO Y de nhu vay: 10ms bi cat o dau cau la khong dang ke (mot am tiet dai
// 80-200ms), trong khi neu lam tron XUONG thi con lai ~10ms am CU trong ring -
// co the chinh la tieng AI vua doc o luot truoc, du de lam VAD hieu sai la
// "co tieng noi" ngay lap tuc. Xa thua an toan hon xa thieu.
#define RECORD_DISCARD_SAMPLES (BOARD_I2S_DMA_DESC_NUM * BOARD_I2S_DMA_FRAME_NUM)

// Hang doi phat ~1s audio 16kHz. Khong can lon hon vi
// audio_pipeline_play_pcm() tu block de tiet che ben giai ma.
#define PLAYBACK_MSGBUF_BYTES (32 * 1024)

static MessageBufferHandle_t s_playback_msgbuf = NULL;
static volatile bool s_speaker_busy = false;
static volatile bool s_playback_abort = false;

// Buffer ghi am trong PSRAM: 44 byte dau danh cho WAV header, PCM ghi tu
// offset 44 tro di.
static uint8_t *s_rec_buf = NULL;
static volatile size_t s_rec_len = 0;      // so byte PCM da ghi (khong tinh header)
static volatile bool s_recording = false;
static volatile bool s_had_speech = false;

// Buffer trung gian cho resample, cap phat 1 lan luc init (xem
// audio_pipeline_play_pcm). Du cho 1 frame MP3 lon nhat (2304 mau) da resample.
#define RESAMPLE_BUF_SAMPLES 2560
static int16_t *s_resample_buf = NULL;

// ---- Resample -> 16kHz, noi suy tuyen tinh, giu trang thai giua cac lan goi
// de khong bi "click" o ranh gioi cac doan audio. ----
typedef struct {
    double phase;
    int16_t prev_sample;
    bool has_prev;
} resampler_state_t;

static resampler_state_t s_resampler = { .phase = 0.0, .prev_sample = 0, .has_prev = false };

static void resampler_reset(void)
{
    s_resampler.phase = 0.0;
    s_resampler.has_prev = false;
}

static size_t resample_to_16k(const int16_t *in, size_t in_count, int src_rate_hz,
                              int16_t *out, size_t out_capacity)
{
    if (in_count == 0) {
        return 0;
    }
    const double step = (double)src_rate_hz / (double)BOARD_I2S_SAMPLE_RATE_HZ;
    int16_t prev = s_resampler.has_prev ? s_resampler.prev_sample : in[0];
    double p = s_resampler.phase;
    size_t out_n = 0;

    while (p < (double)in_count && out_n < out_capacity) {
        size_t idx = (size_t)p;
        double frac = p - (double)idx;
        int16_t s0 = (idx == 0) ? prev : in[idx - 1];
        int16_t s1 = in[idx];
        out[out_n++] = (int16_t)(s0 + frac * (double)(s1 - s0));
        p += step;
    }

    s_resampler.phase = p - (double)in_count;
    s_resampler.prev_sample = in[in_count - 1];
    s_resampler.has_prev = true;
    return out_n;
}

// ---- WAV header ------------------------------------------------------------
static void write_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void write_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

// Groq nhan raw WAV nen chi can header RIFF/PCM 44 byte tieu chuan.
static void fill_wav_header(uint8_t *h, size_t data_len)
{
    const uint32_t sample_rate = BOARD_I2S_SAMPLE_RATE_HZ;
    const uint16_t channels = 1;
    const uint16_t bits = 16;

    memcpy(h + 0, "RIFF", 4);
    write_u32_le(h + 4, (uint32_t)(36 + data_len));
    memcpy(h + 8, "WAVE", 4);

    memcpy(h + 12, "fmt ", 4);
    write_u32_le(h + 16, 16);                                    // kich thuoc chunk fmt
    write_u16_le(h + 20, 1);                                     // 1 = PCM khong nen
    write_u16_le(h + 22, channels);
    write_u32_le(h + 24, sample_rate);
    write_u32_le(h + 28, sample_rate * channels * (bits / 8));   // byte rate
    write_u16_le(h + 32, (uint16_t)(channels * (bits / 8)));     // block align
    write_u16_le(h + 34, bits);

    memcpy(h + 36, "data", 4);
    write_u32_le(h + 40, (uint32_t)data_len);
}

// ---- Task doc mic ----------------------------------------------------------
static void mic_task(void *arg)
{
    int16_t frame[MIC_FRAME_SAMPLES];
    int64_t start_us = 0;
    int64_t last_voice_us = 0;
    // Chi de chan doan: muc tin hieu cao nhat quan sat duoc trong ban ghi.
    int max_avg = 0;
    int16_t max_peak = 0;
    int64_t last_log_us = 0;
    // Hieu chuan nen nhieu dau moi ban ghi (xem VAD_CALIB_MS).
    int noise_floor = 0;
    int threshold = VAD_MIN_THRESHOLD;
    bool calibrated = false;

    while (1) {
        if (!s_recording) {
            vTaskDelay(pdMS_TO_TICKS(20));
            start_us = 0;
            continue;
        }

        if (start_us == 0) {
            // Xa audio cu con dong trong DMA ring truoc khi tinh la ban ghi moi.
            int discard_frames = (RECORD_DISCARD_SAMPLES + MIC_FRAME_SAMPLES - 1) / MIC_FRAME_SAMPLES;
            for (int i = 0; i < discard_frames; i++) {
                codec_board_read(frame, MIC_FRAME_SAMPLES);
            }
            start_us = esp_timer_get_time();
            last_voice_us = 0;
            max_avg = 0;
            max_peak = 0;
            last_log_us = start_us;
            noise_floor = 0;
            threshold = VAD_MIN_THRESHOLD;
            calibrated = false;
        }

        int n = codec_board_read(frame, MIC_FRAME_SAMPLES);
        if (n <= 0) {
            continue;
        }

        // Ghi vao buffer PSRAM (bo qua neu da day - se dung ngay ben duoi).
        size_t bytes = (size_t)n * sizeof(int16_t);
        if (s_rec_len + bytes <= RECORD_MAX_BYTES) {
            memcpy(s_rec_buf + WAV_HEADER_SIZE + s_rec_len, frame, bytes);
            s_rec_len += bytes;
        }

        int64_t sum_abs = 0;
        int16_t peak = 0;
        for (int i = 0; i < n; i++) {
            int16_t a = (frame[i] < 0) ? (int16_t)-frame[i] : frame[i];
            sum_abs += a;
            if (a > peak) {
                peak = a;
            }
        }
        int avg = (int)(sum_abs / n);
        if (avg > max_avg) {
            max_avg = avg;
        }
        if (peak > max_peak) {
            max_peak = peak;
        }

        int64_t now = esp_timer_get_time();

        // ---- Hieu chuan nen nhieu trong VAD_CALIB_MS dau ----
        // Lay gia tri NHO NHAT trong giai doan hieu chuan chu khong lay trung
        // binh: neu nguoi dung bam nut roi noi ngay, cac frame dau da co tieng
        // noi, lay trung binh se day nen len rat cao khien VAD "mu" ca luot.
        if (!calibrated) {
            if (noise_floor == 0 || avg < noise_floor) {
                noise_floor = avg;
            }
            if ((now - start_us) >= (VAD_CALIB_MS * 1000LL)) {
                calibrated = true;
                threshold = (int)((float)noise_floor * VAD_SPEECH_RATIO);
                if (threshold < VAD_MIN_THRESHOLD) {
                    threshold = VAD_MIN_THRESHOLD;
                }
                if (threshold > VAD_MAX_THRESHOLD) {
                    threshold = VAD_MAX_THRESHOLD;
                }
                ESP_LOGI(TAG, "[mic] nen nhieu=%d -> nguong VAD=%d", noise_floor, threshold);
            }
            // Trong luc dang hieu chuan thi chua xet co tieng noi hay khong.
            continue;
        }

        // Loi thoat khi hieu chuan bi nhiem tieng noi: neu nguoi dung bam nut
        // roi noi ngay, ca 300ms hieu chuan deu la tieng noi -> nen do duoc qua
        // cao va ca luot se "mu". O day tiep tuc ha nen theo gia tri nho nhat
        // quan sat duoc, nen chi can nguoi dung ngung mot nhip la nguong tu dong
        // hoi phuc (VAD_MAX_THRESHOLD mot minh khong du: tieng noi ~700 van thap
        // hon tran 2000 nen se khong bao gio vuot qua).
        //
        // CHI lam khi chua nhan ra tieng noi. Neu ha nguong SAU khi da co tieng
        // noi thi tieng on nhe cung bi tinh la "dang noi", khien dieu kien im
        // lang 1.5s khong bao gio dat -> moi luot ghi du 8s.
        if (!s_had_speech && avg < noise_floor) {
            noise_floor = avg;
            int t = (int)((float)noise_floor * VAD_SPEECH_RATIO);
            if (t < VAD_MIN_THRESHOLD) {
                t = VAD_MIN_THRESHOLD;
            }
            if (t > VAD_MAX_THRESHOLD) {
                t = VAD_MAX_THRESHOLD;
            }
            if (t < threshold) {
                threshold = t;
                ESP_LOGI(TAG, "[mic] ha nen nhieu=%d -> nguong VAD=%d", noise_floor, threshold);
            }
        }

        // Log dinh ky trong luc ghi de doi chieu muc tin hieu that voi nguong VAD.
        if ((now - last_log_us) > 500000LL) {
            last_log_us = now;
            ESP_LOGI(TAG, "[mic] avg=%d peak=%d (nen=%d nguong=%d, da co tieng noi=%d)",
                     avg, peak, noise_floor, threshold, s_had_speech ? 1 : 0);
        }

        if (avg > threshold) {
            last_voice_us = now;
            s_had_speech = true;
        }

        bool done = false;
        if (s_had_speech) {
            // Da noi roi: dung khi im lang du lau.
            if ((now - last_voice_us) > (RECORD_SILENCE_MS * 1000LL)) {
                ESP_LOGI(TAG, "Ghi am xong (im lang %d ms), %u byte, max_avg=%d max_peak=%d",
                         RECORD_SILENCE_MS, (unsigned)s_rec_len, max_avg, max_peak);
                done = true;
            }
        } else if ((now - start_us) > (RECORD_NO_SPEECH_MS * 1000LL)) {
            // Log ca muc do nghe duoc: neu max_avg ~0 thi mic khong ra du lieu
            // (phan cung/slot I2S), con neu max_avg chi hoi thap hon nguong thi
            // giam VAD_SPEECH_RATIO.
            ESP_LOGW(TAG, "Khong phat hien tieng noi sau %d ms (max_avg=%d, max_peak=%d, nen=%d, nguong=%d)",
                     RECORD_NO_SPEECH_MS, max_avg, max_peak, noise_floor, threshold);
            done = true;
        }
        if (s_rec_len >= RECORD_MAX_BYTES || (now - start_us) > (RECORD_MAX_MS * 1000LL)) {
            // Log ca muc am: day la nhanh se kich hoat neu nguong VAD qua thap
            // (tieng on lien tuc vuot nguong nen khong bao gio thay im lang).
            ESP_LOGI(TAG, "Ghi am xong (dat gioi han %d ms), max_avg=%d max_peak=%d nen=%d nguong=%d",
                     RECORD_MAX_MS, max_avg, max_peak, noise_floor, threshold);
            done = true;
        }

        if (done) {
            s_recording = false;
            start_us = 0;
        }
    }
}

// ---- Task phat ra loa ------------------------------------------------------
static void speaker_task(void *arg)
{
    int16_t frame[MIC_FRAME_SAMPLES];
    while (1) {
        size_t got = xMessageBufferReceive(s_playback_msgbuf, frame, sizeof(frame), pdMS_TO_TICKS(100));
        if (got > 0) {
            s_speaker_busy = true;
            codec_board_write(frame, got / sizeof(int16_t));
            s_speaker_busy = false;
        }
    }
}

esp_err_t audio_pipeline_init(void)
{
    s_playback_msgbuf = xMessageBufferCreate(PLAYBACK_MSGBUF_BYTES);
    if (!s_playback_msgbuf) {
        ESP_LOGE(TAG, "Khong tao duoc playback message buffer");
        return ESP_FAIL;
    }

    // Buffer ghi am nam trong PSRAM: 256KB nay khong the lay tu RAM noi bo.
    s_rec_buf = heap_caps_malloc(WAV_HEADER_SIZE + RECORD_MAX_BYTES, MALLOC_CAP_SPIRAM);
    if (!s_rec_buf) {
        ESP_LOGE(TAG, "Khong cap phat duoc %u byte PSRAM cho buffer ghi am",
                 (unsigned)(WAV_HEADER_SIZE + RECORD_MAX_BYTES));
        return ESP_ERR_NO_MEM;
    }

    s_resample_buf = malloc(RESAMPLE_BUF_SAMPLES * sizeof(int16_t));
    if (!s_resample_buf) {
        ESP_LOGE(TAG, "Khong cap phat duoc buffer resample");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(mic_task, "mic_task", 4096, NULL, 10, NULL) != pdPASS) {
        return ESP_FAIL;
    }
    if (xTaskCreate(speaker_task, "speaker_task", 4096, NULL, 10, NULL) != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Audio pipeline da san sang (buffer ghi am %u KB trong PSRAM)",
             (unsigned)(RECORD_MAX_BYTES / 1024));
    return ESP_OK;
}

esp_err_t audio_pipeline_record_start(void)
{
    if (!s_rec_buf) {
        return ESP_ERR_INVALID_STATE;
    }
    s_rec_len = 0;
    s_had_speech = false;
    s_recording = true;
    return ESP_OK;
}

void audio_pipeline_record_stop(void)
{
    s_recording = false;
}

bool audio_pipeline_record_is_active(void)
{
    return s_recording;
}

bool audio_pipeline_record_had_speech(void)
{
    return s_had_speech;
}

void audio_pipeline_record_get_wav(const uint8_t **out_wav, size_t *out_len)
{
    size_t data_len = s_rec_len;
    fill_wav_header(s_rec_buf, data_len);
    *out_wav = s_rec_buf;
    *out_len = WAV_HEADER_SIZE + data_len;
}

esp_err_t audio_pipeline_play_pcm(const int16_t *pcm, size_t sample_count, int src_rate_hz)
{
    if (!pcm || sample_count == 0 || src_rate_hz <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_playback_abort) {
        return ESP_ERR_INVALID_STATE;
    }

    // Dung buffer cap phat 1 lan luc init thay vi malloc moi frame MP3 (~24ms):
    // van de gay dau ca du an nay la RAM NOI BO bi phan manh, nen tranh
    // malloc/free lien tuc trong luc dang co ket noi mang.
    if (!s_resample_buf) {
        return ESP_ERR_INVALID_STATE;
    }

    // Sau resample len toi da (16000/src_rate) lan so mau; +8 cho phan du.
    size_t out_cap = (sample_count * (size_t)BOARD_I2S_SAMPLE_RATE_HZ) / (size_t)src_rate_hz + 8;
    if (out_cap > RESAMPLE_BUF_SAMPLES) {
        ESP_LOGW(TAG, "Doan audio %u mau qua lon cho buffer resample, bo qua", (unsigned)sample_count);
        return ESP_ERR_INVALID_SIZE;
    }
    int16_t *out = s_resample_buf;

    size_t out_n = resample_to_16k(pcm, sample_count, src_rate_hz, out, out_cap);

    // Gui theo khoi <= MIC_FRAME_SAMPLES: speaker_task nhan vao buffer
    // MIC_FRAME_SAMPLES nen message lon hon the se khong bao gio nhan duoc.
    size_t off = 0;
    while (off < out_n) {
        if (s_playback_abort) {
            return ESP_ERR_INVALID_STATE;
        }
        size_t chunk = out_n - off;
        if (chunk > MIC_FRAME_SAMPLES) {
            chunk = MIC_FRAME_SAMPLES;
        }
        size_t sent = xMessageBufferSend(s_playback_msgbuf, out + off,
                                         chunk * sizeof(int16_t), pdMS_TO_TICKS(200));
        if (sent > 0) {
            off += chunk;
        }
        // sent == 0: hang doi day -> lap lai, chinh la co che tiet che toc do.
    }

    return ESP_OK;
}

void audio_pipeline_playback_reset(void)
{
    s_playback_abort = false;
    resampler_reset();
    xMessageBufferReset(s_playback_msgbuf);
}

void audio_pipeline_flush_playback(void)
{
    s_playback_abort = true;
    xMessageBufferReset(s_playback_msgbuf);
}

bool audio_pipeline_is_playback_idle(void)
{
    return xMessageBufferIsEmpty(s_playback_msgbuf) && !s_speaker_busy;
}
