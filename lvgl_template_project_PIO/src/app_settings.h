/**
 * @file app_settings.h
 * @brief Persistent user settings: backlight, carousel, WiFi, auto page,
 *        panel orientation and idle screen-off.
 *
 * The values live in NVS, survive reboots, and are applied immediately when
 * changed from the on-device SETTINGS screen.
 */
#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <stdint.h>

#define APP_BRIGHT_DEFAULT 160
#define APP_BRIGHT_MIN     16
#define APP_BRIGHT_MAX     255

#define APP_PAGE_DEFAULT   5000
#define APP_PAGE_MIN       2000
#define APP_PAGE_MAX       30000

#define APP_TRANS_DEFAULT  600
#define APP_TRANS_MIN      200
#define APP_TRANS_MAX      1200

#define APP_ROTATE_DEFAULT 1   /* 1 = 180° (panel mounted upside down) */
#define APP_SLEEP_DEFAULT  0   /* 0 = never turn the backlight off     */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  brightness;   /* backlight PWM duty, 0..255            */
    uint16_t page_ms;      /* auto-carousel dwell per page, ms      */
    uint16_t trans_ms;     /* page transition duration, ms          */
    uint8_t  wifi_on;      /* 0 = radio off, 1 = station enabled    */
    uint8_t  auto_on;      /* 0 = manual page swiping only          */
    uint8_t  rotation;     /* 0 = 0°, 1 = 180°                      */
    uint8_t  sleep_min;    /* idle minutes before the screen sleeps,
                            * 0 = never. Drawn from {1,5,10,30}     */
} app_settings_t;

/* Load saved values (defaults on first boot). Does not drive hardware:
 * the callers apply brightness / WiFi after their subsystem is ready. */
void app_settings_init(void);

/* Current snapshot. Never NULL. */
const app_settings_t *app_settings_get(void);

/* Appliers clamp and drive the hardware immediately but leave NVS alone.
 * Used while a slider is dragged, so a single gesture costs one write. */
void app_settings_apply_brightness(uint8_t v);

/* Raw PWM duty, neither clamped nor persisted. The idle screen-off fade
 * needs to reach 0, which app_settings_apply_brightness() deliberately
 * refuses to do. */
void app_settings_apply_backlight_raw(uint8_t level);
void app_settings_apply_page_ms(uint16_t v);
void app_settings_apply_trans_ms(uint16_t v);
void app_settings_apply_rotation(uint8_t flipped);
void app_settings_apply_sleep(uint8_t minutes);

/* Setters clamp, apply immediately and persist to NVS (call on release). */
void app_settings_set_brightness(uint8_t v);
void app_settings_set_page_ms(uint16_t v);
void app_settings_set_trans_ms(uint16_t v);
void app_settings_set_wifi(uint8_t on);
void app_settings_set_auto(uint8_t on);
void app_settings_set_rotation(uint8_t flipped);
void app_settings_set_sleep(uint8_t minutes);

#ifdef __cplusplus
}
#endif

#endif /* APP_SETTINGS_H */
