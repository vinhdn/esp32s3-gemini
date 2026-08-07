#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// DNS server "hijack": tra loi MOI truy van A-record bang mot dia chi IP co dinh
// (thuong la IP cua chinh SoftAP, vd 192.168.4.1) de kich hoat popup captive
// portal tren dien thoai. Chi dung khi ESP32 dang o che do AP provisioning.
//
// resolved_ip_network_order: dia chi IPv4 o dang network byte order, vi du lay
// truc tiep tu esp_netif_ip_info_t.ip.addr cua interface AP.
esp_err_t dns_server_start(uint32_t resolved_ip_network_order);
void dns_server_stop(void);

#ifdef __cplusplus
}
#endif
