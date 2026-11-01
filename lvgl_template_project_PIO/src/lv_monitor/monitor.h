#ifndef __MONITOR_H
#define __MONITOR_H

#include "lvgl.h"
#include "data_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bind the PC data snapshot that the sampler reads once per second.
 * Call before lv_monitor_show().
 */
void lv_monitor_set_pc_data(const pc_data_t *data);

/**
 * Build the monitor UI and start its sampling / carousel timers.
 * Returns immediately — the caller owns the lv_timer_handler() loop.
 *
 * Swipe left/right to change pages, swipe up for settings, swipe down to
 * leave them again. Inside settings, left/right turn its two pages and the
 * sliders ignore swipe gestures for the duration of a drag.
 */
void lv_monitor_show(void);

/**
 * Change the auto-carousel dwell and transition duration at runtime.
 * Called by app_settings on boot and whenever the SETTINGS screen changes.
 */
void monitor_set_timings(uint16_t page_ms, uint16_t trans_ms);

/**
 * Enable or disable the automatic page carousel. Manual left/right swipes
 * keep working either way.
 */
void monitor_set_auto(bool on);

#ifdef __cplusplus
}
#endif

#endif /* __MONITOR_H */
