# BACnet/IP Server for Olimex ESP32-POE — WiFi Edition

PlatformIO project that runs a BACnet/IP server on an **Olimex ESP32-POE**
board using **WiFi** (STA mode) as the network transport.

The BACnet/IP datalink layer is based on **ports/lwip** and uses the lwIP
raw UDP API (`udp_pcb`, `pbuf`, callbacks) rather than POSIX sockets.
Reception is callback-driven; the lwIP task calls `bip_server_callback()`
directly. Sending is protected by `LOCK_TCPIP_CORE()` so it is safe from
any FreeRTOS task.

## Hardware

| Board | Olimex ESP32-POE |
|-------|-----------------|
| Flash | 4 MB |
| Network | 802.11 b/g/n WiFi (STA) |

## Getting started

### 1 — Install toolchain

Install [VSCode](https://code.visualstudio.com) and the
[PlatformIO extension](https://platformio.org/install/ide?install=vscode).

### 2 — Copy BACnet stack files

See `lib/readme.txt` for the list of `.c` and `.h` files to copy from the
repository root into `lib/bacnet/`.

### 3 — Configure WiFi credentials

Edit `src/wifi_init.c` and set `WIFI_SSID` / `WIFI_PASS`, **or** pass them
as build flags in `platformio.ini`:

```ini
build_flags =
    -DWIFI_SSID='"MyNetwork"'
    -DWIFI_PASS='"MyPassword"'
    -DMAX_TSM_TRANSACTIONS=10
```

### 4 — Set the BACnet device instance number

In `platformio.ini` add:

```ini
    -DBACNET_DEVICE_INSTANCE=1234
```

Or edit the `#define BACNET_DEVICE_INSTANCE` in `src/main.c`.

### 5 — Build and upload

```
pio run -t upload
pio device monitor
```

## Architecture

```
app_main()
  └─ BACnetTask (FreeRTOS, 8 KB stack)
       ├─ wifi_init_sta()          — NVS, esp_netif, WiFi STA start
       │     └─ [DHCP] IP_EVENT_STA_GOT_IP
       │           └─ bip_set_addr / bip_set_broadcast_addr / bip_set_port
       ├─ bacnet_init()            — service handlers, Device_Init, bip_init
       │     └─ bip_init()         — udp_new / udp_bind / udp_recv (lwIP raw API)
       └─ main loop
             ├─ periodic timers (dcc, bvlc_maintenance, COV, TSM) every 1 s
             └─ handler_cov_task()

lwIP task (runs independently)
  └─ bip_server_callback()        — bvlc_handler → npdu_handler
```

## Notes

- The `bip_server_callback` is called in the lwIP task context. `npdu_handler`
  may in turn call `bip_send_pdu` which uses `LOCK_TCPIP_CORE()` (a recursive
  mutex on ESP-IDF), so re-entrancy is safe.
- The BACnet object state is not protected by a mutex between the lwIP task
  (callback path) and the BACnet application task. For a production device,
  add appropriate locking around shared BACnet object data.
- On WiFi disconnect, `bip_cleanup()` removes the UDP PCB. It is re-created
  automatically when the link comes back and `bip_init()` is called again
  via the reconnect logic in `wifi_init.c`.
