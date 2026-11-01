/**
 * @file main.cpp
 * @brief PC Hardware Monitor — ESP32-S3 + LVGL 8.3
 *
 * Receives JSON frames over WiFi (UDP broadcast) from the PC companion app,
 * with Serial0 (UART0, GPIO 43/44) kept as a wired fallback, and renders the
 * lv_monitor round-dial carousel:
 * CPU, GPU, MEMORY, STORAGE, NETWORK, SYSTEM.
 */
#include <Arduino.h>
#include "display.h"
#include "serial_reader.h"
#include "udp_reader.h"
#include "data_model.h"
#include "touch.h"
#include "lv_monitor/monitor.h"

Display screen;
pc_data_t pc_data;

static uint32_t last_lvgl_tick = 0;
static uint32_t last_status_check = 0;
static uint32_t last_frame_count = 0;

void setup()
{
    /* 160 MHz is plenty for 1 Hz data and the 600 ms page transitions, and
     * cuts the SoC's dynamic power noticeably versus the 240 MHz default. */
    setCpuFrequencyMhz(160);

    /* Serial0 = Hardware UART0 (GPIO 43 TX, GPIO 44 RX → USB-UART chip) */
    serial_reader_init(921600);
    udp_reader_init();

    screen.init();
    touch_init();

    lv_monitor_set_pc_data(&pc_data);
    lv_monitor_show();

    Serial0.printf("[Monitor] Build: %s %s\n", __DATE__, __TIME__);
    Serial0.println("[Monitor] Ready (Serial0 UART).");
}

void loop()
{
    serial_reader_poll(&pc_data);
    udp_reader_poll(&pc_data);

    uint32_t now = millis();
    /* LVGL's own refresh timer (10 ms) paces frames; poll as often as
     * possible so a new frame starts as soon as the previous flush ends. */
    if (now - last_lvgl_tick >= 5) {
        last_lvgl_tick = now;
        lv_task_handler();
    }

    /* Status every 2s */
    if (now - last_status_check >= 2000) {
        uint32_t elapsed = now - last_status_check;
        uint32_t frames = display_frame_count();
        uint32_t fps_x10 = (uint32_t)((uint64_t)(frames - last_frame_count) *
                                      10000ULL / elapsed);

        last_status_check = now;
        last_frame_count = frames;

        if (pc_data.frame_count > 0) {
            Serial0.printf("[Monitor] Live: %u frames | %lu.%lu FPS\n",
                           pc_data.frame_count, fps_x10 / 10, fps_x10 % 10);
        } else {
            Serial0.printf("[Monitor] Waiting data... raw=%lu | %lu.%lu FPS\n",
                           pc_data.raw_bytes, fps_x10 / 10, fps_x10 % 10);
        }
    }

    delay(1);
}
