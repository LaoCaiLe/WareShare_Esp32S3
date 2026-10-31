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
 * Click anywhere on the dial to advance to the next page immediately.
 */
void lv_monitor_show(void);

#ifdef __cplusplus
}
#endif

#endif /* __MONITOR_H */
