/**
 * @file ui_monitor.h
 * @brief LVGL hardware monitor dashboard — scrollable single page
 */
#ifndef UI_MONITOR_H
#define UI_MONITOR_H

#include "data_model.h"
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build the full dashboard UI.
 * @param build_tag  Firmware build timestamp for identification
 * Call once after lvgl + display init.
 */
void ui_monitor_create(const char *build_tag);

/**
 * Refresh all widgets with data from pc_data.
 * Call from lv_timer_handler() context.
 * Only redraws when pc_data.updated == true.
 */
void ui_monitor_refresh(pc_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* UI_MONITOR_H */
