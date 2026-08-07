#include "dns_server.h"

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "dns_server";

#define DNS_PORT 53
#define DNS_MAX_LEN 512

static TaskHandle_t s_task_handle = NULL;
static int s_sock = -1;
static uint32_t s_resolved_ip = 0; // network byte order

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint8_t flags1;
    uint8_t flags2;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

typedef struct __attribute__((packed)) {
    uint16_t type;
    uint16_t cls;
} dns_question_footer_t;

typedef struct __attribute__((packed)) {
    uint16_t name_ptr; // con tro nen (0xC00C) tro ve ten cau hoi dau tien
    uint16_t type;
    uint16_t cls;
    uint32_t ttl;
    uint16_t rd_len;
    uint32_t ip;
} dns_answer_t;

static void dns_server_task(void *arg)
{
    static uint8_t rx_buf[DNS_MAX_LEN];
    static uint8_t tx_buf[DNS_MAX_LEN];

    while (1) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int len = recvfrom(s_sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&from_addr, &from_len);
        if (len < (int)sizeof(dns_header_t)) {
            continue;
        }

        const dns_header_t *req_hdr = (const dns_header_t *)rx_buf;
        if (ntohs(req_hdr->qdcount) < 1) {
            continue; // khong co cau hoi nao, bo qua
        }

        // Ten mien la chuoi cac label (do-dai + noi dung), ket thuc bang byte 0x00.
        size_t pos = sizeof(dns_header_t);
        while (pos < (size_t)len && rx_buf[pos] != 0) {
            pos += rx_buf[pos] + 1;
        }
        if (pos >= (size_t)len) {
            continue; // goi tin loi/cat cut
        }
        size_t qname_end = pos + 1; // bao gom byte 0 ket thuc ten mien
        size_t question_len = (qname_end - sizeof(dns_header_t)) + sizeof(dns_question_footer_t);
        if (sizeof(dns_header_t) + question_len > (size_t)len) {
            continue;
        }

        size_t resp_len = sizeof(dns_header_t) + question_len;
        if (resp_len + sizeof(dns_answer_t) > sizeof(tx_buf)) {
            continue;
        }

        // Response = copy nguyen header + question tu request, chi doi flags/counts,
        // roi noi them 1 answer record tro ve s_resolved_ip.
        memcpy(tx_buf, rx_buf, resp_len);

        dns_header_t *resp_hdr = (dns_header_t *)tx_buf;
        resp_hdr->flags1 = 0x84; // QR=1 (response), Opcode=0 (query chuan), AA=1 (authoritative)
        resp_hdr->flags2 = 0x00; // RCODE = 0 (khong loi)
        resp_hdr->qdcount = htons(1);
        resp_hdr->ancount = htons(1);
        resp_hdr->nscount = 0;
        resp_hdr->arcount = 0;

        dns_answer_t answer = {
            .name_ptr = htons(0xC00C), // 0x0C = offset 12 = ngay sau header, noi ten cau hoi bat dau
            .type = htons(1),          // A record
            .cls = htons(1),           // IN
            .ttl = htonl(60),
            .rd_len = htons(4),
            .ip = s_resolved_ip,
        };
        memcpy(tx_buf + resp_len, &answer, sizeof(answer));
        resp_len += sizeof(answer);

        sendto(s_sock, tx_buf, resp_len, 0, (struct sockaddr *)&from_addr, from_len);
    }
}

esp_err_t dns_server_start(uint32_t resolved_ip_network_order)
{
    if (s_sock >= 0) {
        return ESP_OK; // da chay
    }
    s_resolved_ip = resolved_ip_network_order;

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "Khong tao duoc UDP socket cho DNS server");
        return ESP_FAIL;
    }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(s_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        ESP_LOGE(TAG, "Khong bind duoc UDP port %d", DNS_PORT);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    if (xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &s_task_handle) != pdPASS) {
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "DNS hijack server dang chay tren port %d", DNS_PORT);
    return ESP_OK;
}

void dns_server_stop(void)
{
    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
}
