# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

LVGL v9.5.0 template project for **Adafruit Feather ESP32-S3 (2MB PSRAM)** using PlatformIO with Arduino framework. Drives a 240×240 SPI TFT LCD display (WaveShare ESP32-S3 Touch LCD 1.28", ST7789-based).

## Build Commands

```bash
# Compile (from project root)
pio run

# Compile + upload to board
pio run --target upload

# Upload with specific port
pio run --target upload --upload-port COM3

# Clean build
pio run --target clean

# Monitor serial output
pio device monitor --baud 9600

# Full cycle: compile, upload, monitor
pio run --target upload && pio device monitor --baud 9600
```

These commands must run from the project root. There is no test suite — testing is done by flashing the board and observing behavior.

## Architecture

```
src/main.cpp          → Entry point: Serial.begin(9600), screen.init(), lv_demo_music()
include/display.h     → Display class declaration
src/display.cpp       → Display class: wraps TFT_eSPI + LVGL display driver
lib/lvgl/             → LVGL v9.5.0 (local copy, not a PlatformIO lib dependency)
lib/TFT_eSPI/         → TFT_eSPI display driver (local copy)
lib/demos/            → LVGL demo applications (music, benchmark, stress, etc.)
platformio.ini        → PlatformIO config (target: adafruit_feather_esp32s3)
```

### Key files for configuration

| File | Purpose |
|------|---------|
| [platformio.ini](platformio.ini) | Board, framework, upload settings |
| [lib/lvgl/lv_conf.h](lib/lvgl/lv_conf.h) | LVGL feature flags, color depth (16-bit), memory |
| [lib/TFT_eSPI/User_Setup.h](lib/TFT_eSPI/User_Setup.h) | TFT pin mapping, driver chip, SPI config — **this is where you change display pins** |
| [src/display.cpp](src/display.cpp) | LVGL display buffer size, flush callback, render mode |
| [.vscode/c_cpp_properties.json](.vscode/c_cpp_properties.json) | Auto-generated — do NOT edit manually; config comes from `platformio.ini` |

### Display driver integration

The `Display` class in [src/display.cpp](src/display.cpp) uses LVGL v9's API:
- `lv_display_create(240, 240)` — matches the 240×240 round/square LCD
- Buffer: `240 * 240 / 2` bytes (1/10th screen, single buffer) with `LV_DISPLAY_RENDER_MODE_PARTIAL`
- Flush callback (`disp_flush`) copies rendered pixels to TFT via `tft.pushColors()`
- Tick source is `millis()` from Arduino

The old LVGL v8-compatible code (display draw buffer, `lv_disp_drv_register`) is commented out but preserved for reference.

### LVGL version notes

This is **LVGL v9** (major API change from v8). Key differences:
- `lv_display_create()` replaces `lv_disp_drv_register()`
- `lv_display_set_buffers()` replaces `lv_disp_draw_buf_init()`
- `lv_display_set_flush_cb()` replaces `disp_drv.flush_cb = ...`
- `lv_timer_handler()` replaces `lv_timer_handler()` (same name, called from `loop()`)

## Hardware notes

- **Board**: Adafruit Feather ESP32-S3 with 2MB PSRAM
- **Display**: WaveShare ESP32-S3-Touch-LCD-1.28 (240×240, ST7789, SPI)
- **Upload speed**: 3 Mbps
- **Serial**: 9600 baud for debug output
- **CPU**: 240 MHz, Xtensa ESP32-S3

## VSCode setup

Install the **PlatformIO IDE** extension (recommended in [.vscode/extensions.json](.vscode/extensions.json)). Opening this project in VSCode with PlatformIO installed handles toolchains, includes, and build targets automatically. Do NOT install `ms-vscode.cpptools-extension-pack` — it conflicts with PlatformIO's IntelliSense.
