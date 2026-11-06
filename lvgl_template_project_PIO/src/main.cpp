/**
 * @file main.cpp
 * @brief PC Hardware Monitor — ESP32-S3 + LVGL 8.3
 *
 * Receives JSON frames over WiFi (UDP broadcast) from the PC companion app
 * and renders the lv_monitor round-dial carousel:
 * CPU, GPU, MEMORY, STORAGE, NETWORK, SYSTEM.
 *
 * UART0 is used for debug logs only, never for data.
 */
#include <Arduino.h>
#include "app_settings.h"
#include "display.h"
#include "udp_reader.h"
#include "data_model.h"
#include "time_keeper.h"
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

    /* Load brightness / carousel timing / WiFi preference before the
     * subsystems that consume them are initialized. */
    app_settings_init();

    /* UART0 (GPIO 43 TX / 44 RX) is debug output only. */
    Serial0.begin(921600);

    /* Restore the wall clock before anything renders: a reflash must not
     * leave the watch face blank while the PC companion is not running. */
    time_keeper_init();

    udp_reader_init();

    screen.init();
    /* Panel and touch must agree on the orientation before the first frame. */
    app_settings_apply_rotation(app_settings_get()->rotation);
    display_set_backlight(app_settings_get()->brightness);
    touch_init();

    lv_monitor_set_pc_data(&pc_data);
    lv_monitor_show();

    Serial0.printf("[Monitor] Build: %s %s\n", __DATE__, __TIME__);
    Serial0.println("[Monitor] Ready.");
}

void loop()
{
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
            Serial0.printf("[Monitor] Waiting data... | %lu.%lu FPS\n",
                           fps_x10 / 10, fps_x10 % 10);
        }
    }

    delay(1);
}
