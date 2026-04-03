/*
 * BACnet/IP server for the Olimex ESP32-POE — WiFi edition.
 *
 * Network layer: lwIP raw API (same as ports/lwip).
 * Reception:     callback-driven via bip_server_callback() in bip.c.
 * Sending:       bip_send_pdu() / bip_send_mpdu() called from BACnetTask.
 *
 * Flow:
 *   app_main() → wifi_init_sta() → [DHCP] → WIFI_CONNECTED_BIT set
 *              → BACnetTask wakes → bacnet_init() → bip_init() → IAm
 *              → main loop: periodic timers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later WITH GCC-exception-2.0
 */

#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"

#include "bacnet/config.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/basic/services.h"
#include "bacnet/datalink/datalink.h"
#include "bacnet/datalink/bip.h"
#include "bacnet/dcc.h"
#include "bacnet/basic/object/device.h"
#include "bacnet/iam.h"

#include "wifi_init.h"

static const char *TAG = "bacnet_main";

/* ------------------------------------------------------------------ */
/* Configurable device parameters                                      */
/* ------------------------------------------------------------------ */
#ifndef BACNET_DEVICE_INSTANCE
#define BACNET_DEVICE_INSTANCE 1234
#endif

/* Transmit buffer used by Send_I_Am and other senders */
static uint8_t Handler_Transmit_Buffer[MAX_PDU] = { 0 };

/* ------------------------------------------------------------------ */
/* bacnet_init — register service handlers and open BACnet/IP socket  */
/* ------------------------------------------------------------------ */
static void bacnet_init(void)
{
    /* We need to handle Who-Is to support dynamic device binding */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);

    /* Send the proper reject for services we do not implement */
    apdu_set_unrecognized_service_handler_handler(
        handler_unrecognized_service);

    /* Read Property — required by the BACnet standard */
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_READ_PROP_MULTIPLE, handler_read_property_multiple);

    /* Write Property */
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_WRITE_PROPERTY, handler_write_property);

    /* COV subscription */
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_SUBSCRIBE_COV, handler_cov_subscribe);

    /* Initialise the address binding table */
    address_init();

    /* Set up the device object */
    Device_Init(NULL);
    Device_Set_Object_Instance_Number(BACNET_DEVICE_INSTANCE);

    /* Open the BACnet/IP UDP socket — IP/bcast/port must be set first
     * (done in wifi_init.c on IP_EVENT_STA_GOT_IP) */
    bip_init(NULL);

    /* Announce ourselves */
    Send_I_Am(&Handler_Transmit_Buffer[0]);

    ESP_LOGI(TAG, "BACnet device %lu ready", (unsigned long)BACNET_DEVICE_INSTANCE);
}

/* ------------------------------------------------------------------ */
/* BACnet FreeRTOS task                                                */
/* ------------------------------------------------------------------ */
static void BACnetTask(void *pvParameters)
{
    (void)pvParameters;
    bool bacnet_running = false;

    /* Start WiFi and wait until DHCP assigns an address.
     * wifi_init_sta() also calls bip_set_addr/bcast/port in the IP event. */
    wifi_init_sta();

    for (;;) {
        /* Block until we have an IP address (initial connect or reconnect). */
        ESP_LOGI(TAG, "Waiting for WiFi IP…");
        xEventGroupWaitBits(
            wifi_event_group, WIFI_CONNECTED_BIT,
            pdFALSE, /* do not clear the bit */
            pdTRUE,  /* wait for all bits */
            portMAX_DELAY);

        /* (Re-)initialise BACnet/IP each time we get a new IP address.
         * On reconnect, wifi_init.c has already called bip_cleanup() which
         * removes the old UDP PCB, so bip_init() opens a fresh one. */
        if (!bacnet_running) {
            /* First connection: register handlers and device object once. */
            bacnet_init();
        } else {
            /* Reconnection: re-open the UDP socket with the new IP. */
            bip_init(NULL);
            Send_I_Am(&Handler_Transmit_Buffer[0]);
            ESP_LOGI(TAG, "BACnet/IP re-initialised after reconnect");
        }
        bacnet_running = true;

        /* One-second tick tracking */
        TickType_t last_tick = xTaskGetTickCount();

        /* Inner loop: run while connected */
        while (xEventGroupGetBits(wifi_event_group) & WIFI_CONNECTED_BIT) {
            vTaskDelay(pdMS_TO_TICKS(10));

            TickType_t now = xTaskGetTickCount();
            if ((now - last_tick) >= pdMS_TO_TICKS(1000)) {
                last_tick = now;
                /* Per-second BACnet timers */
                dcc_timer_seconds(1);
                bvlc_maintenance_timer(1);
                handler_cov_timer_seconds(1);
                tsm_timer_milliseconds(1000);
            }

            /* COV notification dispatch */
            handler_cov_task();

            /* Note: packet reception is handled automatically by
             * bip_server_callback() which is called by the lwIP task. */
        }
        /* WiFi dropped — loop back and wait for reconnection */
    }
}

/* ------------------------------------------------------------------ */
/* app_main — ESP-IDF entry point                                      */
/* ------------------------------------------------------------------ */
void app_main(void)
{
    /* The default app_main stack (4 KB) is too small for BACnet.
     * Create a dedicated task with a larger stack. */
    xTaskCreate(
        BACnetTask, /* task function */
        "BACnetTask", /* name */
        8192, /* stack size in bytes */
        NULL, /* parameter */
        5, /* priority */
        NULL); /* handle */
}
