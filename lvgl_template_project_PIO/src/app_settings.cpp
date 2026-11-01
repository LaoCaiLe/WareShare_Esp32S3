/**
 * @file app_settings.cpp
 * @brief NVS-backed settings store for the monitor firmware.
 */
#include "app_settings.h"

#include <Arduino.h>
#include <Preferences.h>

#include "display.h"
#include "touch.h"
#include "udp_reader.h"
#include "lv_monitor/monitor.h"
#include "lv_monitor/theme.h"

static Preferences s_prefs;
static app_settings_t s_cfg = {
    APP_BRIGHT_DEFAULT,
    APP_PAGE_DEFAULT,
    APP_TRANS_DEFAULT,
    1,
    1,
    APP_ROTATE_DEFAULT,
    APP_SLEEP_DEFAULT,
};

static uint16_t clamp_u16(uint16_t v, uint16_t lo, uint16_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void app_settings_init(void)
{
    s_prefs.begin("monitor", false);

    s_cfg.brightness = (uint8_t)clamp_u16(
        s_prefs.getUChar("bright", APP_BRIGHT_DEFAULT),
        APP_BRIGHT_MIN, APP_BRIGHT_MAX);
    s_cfg.page_ms = clamp_u16(
        s_prefs.getUShort("page_ms", APP_PAGE_DEFAULT),
        APP_PAGE_MIN, APP_PAGE_MAX);
    s_cfg.trans_ms = clamp_u16(
        s_prefs.getUShort("trans_ms", APP_TRANS_DEFAULT),
        APP_TRANS_MIN, APP_TRANS_MAX);
    s_cfg.wifi_on = s_prefs.getBool("wifi", true) ? 1 : 0;
    s_cfg.auto_on = s_prefs.getBool("auto", true) ? 1 : 0;
    s_cfg.rotation = s_prefs.getBool("rot180", APP_ROTATE_DEFAULT != 0) ? 1 : 0;
    s_cfg.sleep_min = (uint8_t)clamp_u16(
        s_prefs.getUChar("sleep", APP_SLEEP_DEFAULT), 0, 60);

    monitor_set_timings(s_cfg.page_ms, s_cfg.trans_ms);
    monitor_set_auto(s_cfg.auto_on != 0);
}

const app_settings_t *app_settings_get(void)
{
    return &s_cfg;
}

void app_settings_apply_backlight_raw(uint8_t level)
{
    display_set_backlight(level);
}

void app_settings_apply_brightness(uint8_t v)
{
    if (v < APP_BRIGHT_MIN) v = APP_BRIGHT_MIN;
    if (v > APP_BRIGHT_MAX) v = APP_BRIGHT_MAX;

    s_cfg.brightness = v;
    display_set_backlight(v);
}

void app_settings_apply_page_ms(uint16_t v)
{
    s_cfg.page_ms = clamp_u16(v, APP_PAGE_MIN, APP_PAGE_MAX);
    monitor_set_timings(s_cfg.page_ms, s_cfg.trans_ms);
}

void app_settings_apply_trans_ms(uint16_t v)
{
    s_cfg.trans_ms = clamp_u16(v, APP_TRANS_MIN, APP_TRANS_MAX);
    monitor_set_timings(s_cfg.page_ms, s_cfg.trans_ms);
}

void app_settings_set_brightness(uint8_t v)
{
    app_settings_apply_brightness(v);
    s_prefs.putUChar("bright", s_cfg.brightness);
}

void app_settings_set_page_ms(uint16_t v)
{
    app_settings_apply_page_ms(v);
    s_prefs.putUShort("page_ms", s_cfg.page_ms);
}

void app_settings_set_trans_ms(uint16_t v)
{
    app_settings_apply_trans_ms(v);
    s_prefs.putUShort("trans_ms", s_cfg.trans_ms);
}

void app_settings_set_wifi(uint8_t on)
{
    s_cfg.wifi_on = on ? 1 : 0;
    s_prefs.putBool("wifi", s_cfg.wifi_on != 0);
    udp_reader_set_enabled(s_cfg.wifi_on != 0);
}

void app_settings_set_auto(uint8_t on)
{
    s_cfg.auto_on = on ? 1 : 0;
    s_prefs.putBool("auto", s_cfg.auto_on != 0);
    monitor_set_auto(s_cfg.auto_on != 0);
}

void app_settings_apply_rotation(uint8_t flipped)
{
    s_cfg.rotation = flipped ? 1 : 0;

    /* Both the panel and the touch layer have to flip together, otherwise
     * the finger and the pixels disagree about left and right. */
    display_set_rotation(s_cfg.rotation);
    touch_set_mirror(s_cfg.rotation != 0);
}

void app_settings_apply_sleep(uint8_t minutes)
{
    s_cfg.sleep_min = (uint8_t)clamp_u16(minutes, 0, 60);
}

void app_settings_set_rotation(uint8_t flipped)
{
    app_settings_apply_rotation(flipped);
    s_prefs.putBool("rot180", s_cfg.rotation != 0);
}

void app_settings_set_sleep(uint8_t minutes)
{
    app_settings_apply_sleep(minutes);
    s_prefs.putUChar("sleep", s_cfg.sleep_min);
}
