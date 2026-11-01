#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Gestures recognized on the CST816S panel. */
typedef enum {
    TOUCH_GESTURE_NONE = 0,
    TOUCH_SWIPE_LEFT,   /* finger moved right -> left */
    TOUCH_SWIPE_RIGHT,  /* finger moved left -> right */
    TOUCH_SWIPE_UP,     /* finger moved bottom -> top */
    TOUCH_SWIPE_DOWN,   /* finger moved top -> bottom */
    TOUCH_TAP,
} touch_gesture_t;

typedef void (*touch_gesture_cb_t)(touch_gesture_t gesture);

/* Start the CST816S touch panel, register it as LVGL pointer input and
 * begin polling it for swipes / taps. */
void touch_init(void);

/* Register the callback invoked on a swipe or tap (NULL disables it). */
void touch_set_gesture_cb(touch_gesture_cb_t cb);

/* Whether raw panel coordinates are mirrored. Mirrors the display's 180°
 * rotation (1 = 180°, 0 = 0°). Set through app_settings_apply_rotation(). */
void touch_set_mirror(uint8_t mirror);

/* lv_tick timestamp of the last valid pressed sample. Used by the idle
 * screen-off timer; every touch, drag and tap refreshes it. */
uint32_t touch_last_activity_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* TOUCH_H */
