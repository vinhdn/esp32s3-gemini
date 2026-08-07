// Client WebSocket cho Gemini Live API (BidiGenerateContent). Xem cac TODO
// trong gemini_config.h — cau truc JSON/model id cua API nay thay doi thuong
// xuyen, code duoi day theo dung schema tai thoi diem viết (xem ai.google.dev/api/live),
// nen xac minh lai truoc khi dua vao san xuat.

#include "gemini_live_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "mbedtls/base64.h"

#include "gemini_config.h"

static const char *TAG = "gemini_live";

static esp_websocket_client_handle_t s_client = NULL;
static gemini_live_audio_cb_t s_audio_cb = NULL;
static gemini_live_event_cb_t s_event_cb = NULL;
static gemini_live_text_cb_t s_text_cb = NULL;
static void *s_cb_ctx = NULL;
static volatile bool s_session_ready = false;

// Bo dem gop cac fragment cua 1 message WebSocket (esp_websocket_client co
// the tra ve 1 message JSON lon thanh nhieu event DATA lien tiep).
static uint8_t *s_rx_buf = NULL;
static size_t s_rx_len = 0;
static size_t s_rx_cap = 0;

static void ensure_rx_capacity(size_t needed)
{
    if (needed <= s_rx_cap) {
        return;
    }
    size_t new_cap = needed + 1024;
    uint8_t *p = realloc(s_rx_buf, new_cap);
    if (p) {
        s_rx_buf = p;
        s_rx_cap = new_cap;
    }
}

static void send_json(cJSON *root)
{
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        esp_websocket_client_send_text(s_client, json_str, strlen(json_str), pdMS_TO_TICKS(2000));
        cJSON_free(json_str);
    }
}

static void send_setup_message(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *setup = cJSON_AddObjectToObject(root, "setup");
    cJSON_AddStringToObject(setup, "model", GEMINI_LIVE_MODEL);

    cJSON *gen_cfg = cJSON_AddObjectToObject(setup, "generationConfig");
    cJSON *modalities = cJSON_AddArrayToObject(gen_cfg, "responseModalities");
    cJSON_AddItemToArray(modalities, cJSON_CreateString("AUDIO"));

    cJSON *speech_cfg = cJSON_AddObjectToObject(gen_cfg, "speechConfig");
    cJSON_AddStringToObject(speech_cfg, "languageCode", GEMINI_LIVE_LANGUAGE_CODE);
    cJSON *voice_cfg = cJSON_AddObjectToObject(speech_cfg, "voiceConfig");
    cJSON *prebuilt = cJSON_AddObjectToObject(voice_cfg, "prebuiltVoiceConfig");
    cJSON_AddStringToObject(prebuilt, "voiceName", GEMINI_LIVE_VOICE_NAME);

    cJSON *system_instruction = cJSON_AddObjectToObject(setup, "systemInstruction");
    cJSON *si_parts = cJSON_AddArrayToObject(system_instruction, "parts");
    cJSON *si_part = cJSON_CreateObject();
    cJSON_AddStringToObject(si_part, "text", GEMINI_LIVE_SYSTEM_INSTRUCTION);
    cJSON_AddItemToArray(si_parts, si_part);

    // Bat transcription 2 chieu de hien thi text len LCD: cau nguoi dung noi
    // (inputAudioTranscription) va cau AI dang tra loi (outputAudioTranscription).
    // Doi tuong rong {} la du de bat theo docs Gemini Live API - TODO: xac
    // minh lai truong nay chua doi ten/vi tri truoc khi flash that.
    cJSON_AddObjectToObject(setup, "inputAudioTranscription");
    cJSON_AddObjectToObject(setup, "outputAudioTranscription");

    ESP_LOGI(TAG, "Gui setup message toi Gemini Live...");
    send_json(root);
    cJSON_Delete(root);
}

static void decode_and_deliver_audio(const char *b64, size_t b64_len)
{
    size_t out_cap = (b64_len / 4 + 1) * 3 + 4;
    uint8_t *out = malloc(out_cap);
    if (!out) {
        ESP_LOGW(TAG, "Het RAM khi giai ma audio tra ve");
        return;
    }
    size_t out_len = 0;
    int ret = mbedtls_base64_decode(out, out_cap, &out_len, (const unsigned char *)b64, b64_len);
    if (ret == 0 && out_len > 0 && s_audio_cb) {
        s_audio_cb(out, out_len, s_cb_ctx);
    }
    free(out);
}

static void handle_complete_message(const uint8_t *json_bytes, size_t len)
{
    cJSON *root = cJSON_ParseWithLength((const char *)json_bytes, len);
    if (!root) {
        ESP_LOGW(TAG, "Khong parse duoc JSON tu Gemini (%u bytes)", (unsigned)len);
        return;
    }

    if (cJSON_GetObjectItemCaseSensitive(root, "setupComplete")) {
        ESP_LOGI(TAG, "Gemini Live: session da san sang");
        s_session_ready = true;
        if (s_event_cb) {
            s_event_cb(GEMINI_LIVE_EVENT_SESSION_READY, s_cb_ctx);
        }
    }

    cJSON *server_content = cJSON_GetObjectItemCaseSensitive(root, "serverContent");
    if (server_content) {
        cJSON *interrupted = cJSON_GetObjectItemCaseSensitive(server_content, "interrupted");
        if (cJSON_IsTrue(interrupted) && s_event_cb) {
            s_event_cb(GEMINI_LIVE_EVENT_INTERRUPTED, s_cb_ctx);
        }

        cJSON *model_turn = cJSON_GetObjectItemCaseSensitive(server_content, "modelTurn");
        if (model_turn) {
            cJSON *parts = cJSON_GetObjectItemCaseSensitive(model_turn, "parts");
            cJSON *part = NULL;
            cJSON_ArrayForEach(part, parts) {
                cJSON *inline_data = cJSON_GetObjectItemCaseSensitive(part, "inlineData");
                if (inline_data) {
                    cJSON *data_b64 = cJSON_GetObjectItemCaseSensitive(inline_data, "data");
                    if (cJSON_IsString(data_b64) && data_b64->valuestring) {
                        decode_and_deliver_audio(data_b64->valuestring, strlen(data_b64->valuestring));
                    }
                }
            }
        }

        cJSON *turn_complete = cJSON_GetObjectItemCaseSensitive(server_content, "turnComplete");
        if (cJSON_IsTrue(turn_complete) && s_event_cb) {
            s_event_cb(GEMINI_LIVE_EVENT_TURN_COMPLETE, s_cb_ctx);
        }

        cJSON *input_transcription = cJSON_GetObjectItemCaseSensitive(server_content, "inputTranscription");
        if (input_transcription) {
            cJSON *text = cJSON_GetObjectItemCaseSensitive(input_transcription, "text");
            if (cJSON_IsString(text) && text->valuestring && s_text_cb) {
                s_text_cb(GEMINI_LIVE_TEXT_USER_INPUT, text->valuestring, s_cb_ctx);
            }
        }

        cJSON *output_transcription = cJSON_GetObjectItemCaseSensitive(server_content, "outputTranscription");
        if (output_transcription) {
            cJSON *text = cJSON_GetObjectItemCaseSensitive(output_transcription, "text");
            if (cJSON_IsString(text) && text->valuestring && s_text_cb) {
                s_text_cb(GEMINI_LIVE_TEXT_AI_OUTPUT, text->valuestring, s_cb_ctx);
            }
        }
    }

    cJSON_Delete(root);
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket toi Gemini Live da ket noi");
        send_setup_message();
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->data_len <= 0) {
            break;
        }
        if (data->payload_offset == 0) {
            s_rx_len = 0;
        }
        ensure_rx_capacity(s_rx_len + data->data_len);
        if (s_rx_cap >= s_rx_len + data->data_len) {
            memcpy(s_rx_buf + s_rx_len, data->data_ptr, data->data_len);
            s_rx_len += data->data_len;
        }
        if (s_rx_len >= (size_t)data->payload_len) {
            handle_complete_message(s_rx_buf, s_rx_len);
            s_rx_len = 0;
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGW(TAG, "WebSocket Gemini Live ngat ket noi/loi");
        s_session_ready = false;
        if (s_event_cb) {
            s_event_cb(GEMINI_LIVE_EVENT_DISCONNECTED, s_cb_ctx);
        }
        break;

    default:
        break;
    }
}

esp_err_t gemini_live_client_init(gemini_live_audio_cb_t audio_cb, gemini_live_event_cb_t event_cb,
                                   gemini_live_text_cb_t text_cb, void *ctx)
{
    s_audio_cb = audio_cb;
    s_event_cb = event_cb;
    s_text_cb = text_cb;
    s_cb_ctx = ctx;
    return ESP_OK;
}

esp_err_t gemini_live_client_connect(const char *api_key)
{
    if (!api_key || strlen(api_key) == 0) {
        ESP_LOGE(TAG, "Chua co Gemini API key (nhap qua trang cau hinh WiFi)");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_client) {
        gemini_live_client_disconnect();
    }

    char url[300];
    snprintf(url, sizeof(url), "wss://%s%s?key=%s", GEMINI_LIVE_WS_HOST, GEMINI_LIVE_WS_PATH, api_key);

    esp_websocket_client_config_t ws_cfg = {
        .uri = url,
        .buffer_size = 16384,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 10000,
    };

    s_client = esp_websocket_client_init(&ws_cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "esp_websocket_client_init that bai");
        return ESP_FAIL;
    }

    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);

    esp_err_t err = esp_websocket_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_websocket_client_start that bai: %s", esp_err_to_name(err));
    }
    return err;
}

void gemini_live_client_disconnect(void)
{
    s_session_ready = false;
    if (s_client) {
        esp_websocket_client_stop(s_client);
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }
}

bool gemini_live_client_is_session_ready(void)
{
    return s_session_ready;
}

esp_err_t gemini_live_client_send_audio(const int16_t *pcm16k, size_t sample_count)
{
    if (!s_session_ready || !s_client) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t raw_len = sample_count * sizeof(int16_t);
    size_t b64_cap = ((raw_len + 2) / 3) * 4 + 4;
    char *b64 = malloc(b64_cap);
    if (!b64) {
        return ESP_ERR_NO_MEM;
    }

    size_t b64_len = 0;
    mbedtls_base64_encode((unsigned char *)b64, b64_cap, &b64_len, (const unsigned char *)pcm16k, raw_len);
    b64[b64_len] = '\0';

    cJSON *root = cJSON_CreateObject();
    cJSON *realtime_input = cJSON_AddObjectToObject(root, "realtimeInput");
    cJSON *audio = cJSON_AddObjectToObject(realtime_input, "audio");
    cJSON_AddStringToObject(audio, "data", b64);
    cJSON_AddStringToObject(audio, "mimeType", GEMINI_LIVE_INPUT_MIME);

    send_json(root);

    cJSON_Delete(root);
    free(b64);
    return ESP_OK;
}
