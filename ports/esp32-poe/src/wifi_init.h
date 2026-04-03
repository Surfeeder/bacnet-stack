/*
 * WiFi station initialisation for the ESP32-POE BACnet/IP port.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later WITH GCC-exception-2.0
 */

#ifndef WIFI_INIT_H
#define WIFI_INIT_H

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/* Event-group bit set when the station has a valid IP address. */
#define WIFI_CONNECTED_BIT BIT0

/* Shared event group — wait on WIFI_CONNECTED_BIT before calling bip_init(). */
extern EventGroupHandle_t wifi_event_group;

/**
 * @brief Initialise NVS, esp_netif, the default event loop, and WiFi STA.
 *
 * Edit WIFI_SSID and WIFI_PASS below (or pass them via build_flags in
 * platformio.ini) before building.
 *
 * The function returns immediately; connection happens asynchronously.
 * When the IP is obtained, bip_set_addr() / bip_set_broadcast_addr() /
 * bip_set_port() are called automatically, and WIFI_CONNECTED_BIT is set.
 */
void wifi_init_sta(void);

#endif /* WIFI_INIT_H */
