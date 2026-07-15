# ESPTimeCast PlatformIO Guide

ESPTimeCast uses [pioarduino](https://github.com/pioarduino/platform-espressif32), a PlatformIO-compatible ESP32 platform with current Arduino-ESP32 and ESP-IDF support.

## Setup

Install:

- Visual Studio Code
- [pioarduino IDE extension](https://marketplace.visualstudio.com/items?itemName=pioarduino.pioarduino-ide)
- Git

Open repository root containing `platformio.ini`. Do not open only `ESPTimeCast_ESP32/`.

## Environments

| Environment | Board |
|---|---|
| `esp32` | ESP32 Dev Module |
| `esp32s2_lolin` | WEMOS LOLIN S2 Mini |
| `esp32s2_feather` | Adafruit ESP32-S2 Feather |
| `esp32c3_supermini` | ESP32-C3 SuperMini |
| `esp32s3_wroom` | ESP32-S3 WROOM / DevKit |
| `esp32s3_supermini` | ESP32-S3 SuperMini |
| `esp32s3_zero` | Waveshare ESP32-S3-Zero |

ESP8266 is not supported.

## Build

Build all targets:

```bash
pio run
```

Build one target:

```bash
pio run -e esp32c3_supermini
```

Generated application binary:

```text
.pio/build/<environment>/firmware.bin
```

## USB upload

```bash
pio run -e esp32c3_supermini -t upload
```

Serial monitor:

```bash
pio device monitor -b 115200
```

## OTA upload

Firmware provides HTTP OTA endpoints. PlatformIO `espota` protocol is not used.

Requirements:

- Device and computer connected to same trusted network
- Firmware built for correct physical board
- Stable device power during update

Check device connection:

```bash
curl http://esptimecast.local/get_version
```

Use device IP if `esptimecast.local` does not resolve.

Start update mode and upload firmware:

```bash
curl --fail http://esptimecast.local/perform_update && \
curl --fail --show-error \
  -F "update=@.pio/build/esp32c3_supermini/firmware.bin;type=application/octet-stream" \
  http://esptimecast.local/upload_ota
```

Exact command for current ESP32-C3 SuperMini binary:

```bash
curl --fail http://esptimecast.local/perform_update && \
curl --fail --show-error \
  -F "update=@/Users/obalado/proyectos/ESPTimeCast/.pio/build/esp32c3_supermini/firmware.bin;type=application/octet-stream" \
  http://esptimecast.local/upload_ota
```

Expected response:

```text
OK
```

Device reboots automatically after successful update.

## OTA safety

- Upload only `firmware.bin`.
- Never upload `bootloader.bin`, `partitions.bin`, or merged installer images through OTA.
- Do not interrupt device power during update.
- OTA endpoint has no authentication. Use only on trusted LAN.
- OTA preserves NVS and LittleFS with unchanged partition layout.

## Source layout

```text
ESPTimeCast_ESP32/main.cpp    Firmware entry point
ESPTimeCast_ESP32/*.h         Embedded UI, fonts, lookups, and version
platformio.ini                Build environments and dependencies
```

`main.cpp` is standard C++ and uses explicit forward declarations; Arduino `.ino` preprocessing is not required.
