/**
 * @file touch.cpp
 * @brief CST816S capacitive touch → LVGL pointer input + swipe detection
 *
 * Waveshare ESP32-S3-Touch-LCD-1.28: SDA 6, SCL 7, RST 13, IRQ 5.
 *
 * The controller is polled instead of using its IRQ: the interrupt only
 * signals "the panel has news", while a swipe needs the live finger
 * position across the whole gesture to measure the travel distance.
 * Swipes and taps are reported through touch_set_gesture_cb(); the LVGL
 * pointer device stays registered so widgets can use touch later on.
 */
#include "touch.h"

#include <Arduino.h>
#include <lvgl.h>

#include "CST816S.h"

#define TOUCH_POLL_MS      20
#define TOUCH_SWIPE_MIN_PX 30     /* horizontal travel that counts as a swipe */
#define TOUCH_TAP_MAX_PX   10
#define TOUCH_TAP_MAX_MS   600
#define TOUCH_PANEL_MAX    240    /* GC9A01 is 240x240: valid coords are 0..239 */
#define TOUCH_MIN_SAMPLES  2      /* a lone sample is a glitch, not a finger */

static CST816S s_touch(6, 7, 13, 5);

static touch_gesture_cb_t s_gesture_cb;

static bool s_pressed;
static lv_coord_t s_press_x;
static lv_coord_t s_press_y;
static lv_coord_t s_last_x = 120;
static lv_coord_t s_last_y = 120;
static uint32_t s_press_ms;
static uint8_t s_press_samples;

static void touch_poll_cb(lv_timer_t *timer)
{
    (void)timer;

    /* A failed or short I2C transfer leaves the register buffer undefined.
     * Reading that as a touch is what made the carousel advance on its own:
     * the garbage usually looks like a press at an impossible coordinate,
     * followed by a "release" that turns into a huge fake swipe. */
    if (!s_touch.read_touch()) return;

    uint8_t points = s_touch.data.points;
    lv_coord_t x = (lv_coord_t)s_touch.data.x;
    lv_coord_t y = (lv_coord_t)s_touch.data.y;

    /* The CST816S packs 12-bit coordinates; anything past the panel edge
     * (or more than one contact) is a bad sample, not a finger. */
    if (points > 1 || x < 0 || x >= TOUCH_PANEL_MAX ||
        y < 0 || y >= TOUCH_PANEL_MAX) {
        return;
    }

    bool pressed = (points > 0);

    if (pressed) {
        if (!s_pressed) {
            s_pressed = true;
            s_press_x = x;
            s_press_y = y;
            s_press_ms = lv_tick_get();
            s_press_samples = 0;
        }
        s_press_samples++;
        s_last_x = x;
        s_last_y = y;
        return;
    }

    if (!s_pressed) return;
    s_pressed = false;

    if (s_press_samples < TOUCH_MIN_SAMPLES) return;
    if (s_gesture_cb == NULL) return;

    lv_coord_t dx = s_last_x - s_press_x;
    lv_coord_t dy = s_last_y - s_press_y;

    if (LV_ABS(dx) >= TOUCH_SWIPE_MIN_PX && LV_ABS(dx) > LV_ABS(dy)) {
        s_gesture_cb(dx < 0 ? TOUCH_SWIPE_LEFT : TOUCH_SWIPE_RIGHT);
    }
    else if (LV_ABS(dx) <= TOUCH_TAP_MAX_PX &&
             LV_ABS(dy) <= TOUCH_TAP_MAX_PX &&
             lv_tick_elaps(s_press_ms) <= TOUCH_TAP_MAX_MS) {
        s_gesture_cb(TOUCH_TAP);
    }
}

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;

    data->point.x = s_last_x;
    data->point.y = s_last_y;
    data->state = s_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

void touch_set_gesture_cb(touch_gesture_cb_t cb)
{
    s_gesture_cb = cb;
}

void touch_init(void)
{
    s_touch.begin();

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    lv_timer_create(touch_poll_cb, TOUCH_POLL_MS, NULL);
}
