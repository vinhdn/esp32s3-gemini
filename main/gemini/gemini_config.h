#pragma once

// ============================================================================
// Gemini Live API (Developer API, WebSocket BidiGenerateContent) thay doi
// model id / cau truc JSON kha thuong xuyen. TRUOC KHI FLASH FIRMWARE THAT,
// doi chieu lai voi https://ai.google.dev/api/live va cap nhat cac hang so
// duoi day neu can:
//  - GEMINI_LIVE_MODEL: model id hien hanh ho tro Live API audio-to-audio
//  - Cau truc JSON "setup" (vi tri long nhau cua generationConfig/speechConfig
//    da tung thay doi giua cac phien ban API)
//  - Literal mimeType chinh xac cua audio dau ra tra ve tu server
// Xem gemini_live_client.c de biet cho nao dung cac hang so nay.
// ============================================================================

#define GEMINI_LIVE_WS_HOST "generativelanguage.googleapis.com"
#define GEMINI_LIVE_WS_PATH "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent"

// TODO: xac minh model id hien hanh tren AI Studio / ai.google.dev truoc khi dung that.
#define GEMINI_LIVE_MODEL "models/gemini-2.0-flash-live-001"

#define GEMINI_LIVE_INPUT_MIME         "audio/pcm;rate=16000"
#define GEMINI_LIVE_OUTPUT_SAMPLE_RATE 24000

// Ngon ngu AI tra loi (BCP-47).
#define GEMINI_LIVE_LANGUAGE_CODE "vi-VN"

// Ten giong doc san co cua Gemini Live (vd Puck, Charon, Kore, Fenrir, Aoede...).
// TODO: xac minh danh sach giong hien hanh truoc khi doi.
#define GEMINI_LIVE_VOICE_NAME "Kore"

// Huong dan he thong cho bot hoi dap.
#define GEMINI_LIVE_SYSTEM_INSTRUCTION \
    "Bạn là một trợ lý giọng nói thân thiện, trả lời ngắn gọn, rõ ràng bằng Tiếng Việt."
