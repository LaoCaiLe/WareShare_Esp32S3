#ifndef TOUCH_H
#define TOUCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Gestures recognized on the CST816S panel. */
typedef enum {
    TOUCH_GESTURE_NONE = 0,
    TOUCH_SWIPE_LEFT,   /* finger moved right -> left */
    TOUCH_SWIPE_RIGHT,  /* finger moved left -> right */
    TOUCH_TAP,
} touch_gesture_t;

typedef void (*touch_gesture_cb_t)(touch_gesture_t gesture);

/* Start the CST816S touch panel, register it as LVGL pointer input and
 * begin polling it for swipes / taps. */
void touch_init(void);

/* Register the callback invoked on a swipe or tap (NULL disables it). */
void touch_set_gesture_cb(touch_gesture_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* TOUCH_H */
