/**
 * @file main.cpp
 * @brief PC Hardware Monitor — ESP32-S3 + LVGL v9
 *
 * Receives JSON frames over Serial0 (hardware UART0, GPIO 43/44)
 * from PC companion app, displays real-time hardware stats on LCD.
 */
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "display.h"
#include "serial_reader.h"
#include "ui_monitor.h"
#include "data_model.h"

static const char BUILD_TAG[] = __DATE__ " " __TIME__;

Display screen;
pc_data_t pc_data;

static uint32_t last_lvgl_tick = 0;
static uint32_t last_status_check = 0;

void setup()
{
    strncpy(pc_data.build_tag, BUILD_TAG, sizeof(pc_data.build_tag) - 1);

    /* Serial0 = Hardware UART0 (GPIO 43 TX, GPIO 44 RX → USB-UART chip) */
    serial_reader_init(921600);

    screen.init();
    ui_monitor_create(pc_data.build_tag);

    Serial0.print("[Monitor] Build: ");
    Serial0.println(BUILD_TAG);
    Serial0.println("[Monitor] Ready (Serial0 UART).");
}

void loop()
{
    serial_reader_poll(&pc_data);

    uint32_t now = millis();
    if (now - last_lvgl_tick >= 5) {
        last_lvgl_tick = now;
        ui_monitor_refresh(&pc_data);
        lv_timer_handler();
    }

    /* Status every 2s */
    if (now - last_status_check >= 2000) {
        last_status_check = now;
        pc_data.usb_mounted = 1;   /* UART always up */

        if (pc_data.frame_count > 0) {
            Serial0.printf("[Monitor] Live: %u frames\n", pc_data.frame_count);
        } else {
            Serial0.printf("[Monitor] Waiting data... raw=%lu\n",
                          pc_data.raw_bytes);
        }
    }

    delay(1);
}
