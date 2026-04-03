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
       ├─ wifi_init_sta()                — NVS, esp_netif, WiFi STA start
       └─ outer loop (reconnect-aware)
             ├─ wait for WIFI_CONNECTED_BIT
             ├─ first connect  → bacnet_init() (handlers + Device + bip_init + IAm)
             ├─ reconnect      → bip_init() + IAm only (handlers registered once)
             └─ inner loop (while connected)
                   ├─ periodic timers (dcc, bvlc_maintenance, COV, TSM) every 1 s
                   └─ handler_cov_task()

wifi_init.c event handler
  ├─ WIFI_EVENT_STA_DISCONNECTED → bip_cleanup(), clear WIFI_CONNECTED_BIT
  └─ IP_EVENT_STA_GOT_IP         → bip_set_addr/bcast/port, set WIFI_CONNECTED_BIT

lwIP task (runs independently)
  └─ bip_server_callback()       — bvlc_handler → npdu_handler
```

## Notes

- The `bip_server_callback` is called in the lwIP task context. `npdu_handler`
  may in turn call `bip_send_pdu` which uses `LOCK_TCPIP_CORE()` (a recursive
  mutex on ESP-IDF), so re-entrancy is safe.
- The BACnet object state is not protected by a mutex between the lwIP task
  (callback path) and the BACnet application task. For a production device,
  add appropriate locking around shared BACnet object data.
- On WiFi disconnect, `bip_cleanup()` removes the UDP PCB and clears
  `WIFI_CONNECTED_BIT`. The BACnet task's outer loop detects this and blocks
  until the link comes back. On reconnection `bip_init()` opens a fresh UDP
  PCB with the new IP address and `Send_I_Am()` re-announces the device.
  Service handlers and the Device object are registered only once (on first
  connect).
