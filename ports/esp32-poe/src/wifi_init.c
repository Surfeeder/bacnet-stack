/*
 * WiFi station initialisation for the ESP32-POE BACnet/IP port.
 *
 * Uses the modern esp_netif / esp_wifi / esp_event API (ESP-IDF >= 4.1).
 * On IP_EVENT_STA_GOT_IP the BACnet/IP address, broadcast address, and
 * UDP port are written into bip.c via the public bip_set_* API — exactly
 * the same pattern described in ports/lwip/README.md.
 *
 * Edit WIFI_SSID and WIFI_PASS below, or define them as build_flags in
 * platformio.ini:
 *   build_flags = -DWIFI_SSID='"MyNetwork"' -DWIFI_PASS='"MyPassword"'
 *
 * SPDX-License-Identifier: GPL-2.0-or-later WITH GCC-exception-2.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "bacnet/datalink/bip.h"

#include "wifi_init.h"

/* ------------------------------------------------------------------ */
/* Edit these or pass via build_flags                                  */
/* ------------------------------------------------------------------ */
#ifndef WIFI_SSID
#define WIFI_SSID "MyNetwork"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "MyPassword"
#endif

static const char *TAG = "wifi_init";

EventGroupHandle_t wifi_event_group;

/* ------------------------------------------------------------------ */
/* WiFi / IP event handler                                             */
/* ------------------------------------------------------------------ */
static void event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected — reconnecting…");
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        bip_cleanup();
        esp_wifi_connect();

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_netif_ip_info_t *ip_info = &event->ip_info;

        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ip_info->ip));

        /* Convert esp_ip4_addr_t fields to BACNET_IP_ADDRESS. The lwIP
         * bip.c stores addresses as uint8_t[4] in network byte order
         * (big-endian, MSB first). esp_ip4_addr_t.addr is little-endian
         * on ESP32, so we extract each octet via ntohl. */
        uint32_t ip_he = ntohl(ip_info->ip.addr);
        uint32_t nm_he = ntohl(ip_info->netmask.addr);
        uint32_t bc_he = ip_he | (~nm_he);

        BACNET_IP_ADDRESS addr = { 0 };
        BACNET_IP_ADDRESS bcast = { 0 };

        addr.address[0] = (uint8_t)(ip_he >> 24);
        addr.address[1] = (uint8_t)(ip_he >> 16);
        addr.address[2] = (uint8_t)(ip_he >> 8);
        addr.address[3] = (uint8_t)(ip_he);

        bcast.address[0] = (uint8_t)(bc_he >> 24);
        bcast.address[1] = (uint8_t)(bc_he >> 16);
        bcast.address[2] = (uint8_t)(bc_he >> 8);
        bcast.address[3] = (uint8_t)(bc_he);

        bip_set_addr(&addr);
        bip_set_broadcast_addr(&bcast);
        bip_set_port(0xBAC0U);

        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ------------------------------------------------------------------ */
/* wifi_init_sta                                                        */
/* ------------------------------------------------------------------ */
void wifi_init_sta(void)
{
    /* NVS is required by the WiFi driver */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy(
        (char *)wifi_config.sta.ssid, WIFI_SSID,
        sizeof(wifi_config.sta.ssid) - 1);
    strncpy(
        (char *)wifi_config.sta.password, WIFI_PASS,
        sizeof(wifi_config.sta.password) - 1);
    /* Use protected management frames when the AP supports it */
    wifi_config.sta.pmf_cfg.capable = true;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA started — connecting to SSID: %s", WIFI_SSID);
}
