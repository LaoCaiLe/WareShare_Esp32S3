#ifndef __MONITOR_COMMON_H
#define __MONITOR_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"
#include "sampler.h"
#include "theme.h"

/* ───────────── runtime layout metrics ───────────── */

typedef struct {
    lv_coord_t disp_w;
    lv_coord_t disp_h;
    lv_coord_t dial_d;
    lv_coord_t radius;

    lv_coord_t tag_y;
    lv_coord_t hero_y;
    lv_coord_t stage_y;
    lv_coord_t sub_y;

    lv_coord_t stage_w;
    lv_coord_t stage_h;
    lv_coord_t sub_w;
    lv_coord_t sub_h;

    const lv_font_t *font_hero;
    const lv_font_t *font_unit;
    const lv_font_t *font_tag;
    const lv_font_t *font_sub_value;
    const lv_font_t *font_sub_cap;
    const lv_font_t *font_note;

    bool compact;
} mon_ui_metrics_t;

void mon_ui_init(void);
const mon_ui_metrics_t *mon_ui(void);

/* ───────────── page building blocks ───────────── */

typedef struct {
    lv_obj_t *root;
    lv_obj_t *tag_row;
    lv_obj_t *tag_dot;
    lv_obj_t *tag_label;
    lv_obj_t *hero_box;
    lv_obj_t *hero_value;
    lv_obj_t *hero_unit;
    lv_obj_t *stage;
    lv_obj_t *sub_row;
    lv_obj_t *stat_value[2];
    lv_obj_t *stat_caption[2];
    lv_obj_t *note;
    lv_color_t accent;

    /* animated hero number */
    double hero_current;
    double hero_from;
    double hero_to;
    uint8_t hero_decimals;
    char hero_unit_str[12];
    uint32_t hero_last_tick;
} mon_page_ui_t;

void mon_page_ui_create(mon_page_ui_t *ui, lv_obj_t *parent,
                        const char *tag, lv_color_t accent);
void mon_page_set_hero(mon_page_ui_t *ui, const char *value, const char *unit);
void mon_page_set_hero_value(mon_page_ui_t *ui, double value,
                             uint8_t decimals, const char *unit);
void mon_page_set_stat(mon_page_ui_t *ui, uint8_t index,
                       const char *value, const char *caption);
void mon_page_set_note(mon_page_ui_t *ui, const char *note);

/* ───────────── primitives ───────────── */

lv_obj_t *mon_make_rect(lv_obj_t *parent, lv_coord_t w, lv_coord_t h,
                        lv_coord_t x, lv_coord_t y, lv_coord_t rad,
                        lv_color_t color);

lv_obj_t *mon_chart_create(lv_obj_t *parent, lv_coord_t w, lv_coord_t h,
                           bool fill);

void mon_chart_load(lv_obj_t *chart, lv_chart_series_t *ser,
                    const uint32_t *history, uint16_t len, uint16_t head,
                    uint32_t max_value, lv_coord_t chart_max);

void mon_chart_draw_cb(lv_event_t *e);

/* ───────────── formatting ───────────── */

void mon_fmt_rate(uint64_t bytes_per_s, bool compact,
                  char *num, size_t num_sz, char *unit, size_t unit_sz);
void mon_fmt_rate_line(uint64_t bytes_per_s, bool compact,
                       char *buf, size_t buf_sz);
void mon_fmt_size_pair(uint64_t used_kb, uint64_t total_kb, bool compact,
                       char *buf, size_t buf_sz);
void mon_fmt_size(uint64_t kb, bool compact, char *buf, size_t buf_sz);
void mon_fmt_uptime(uint64_t seconds, bool compact, char *buf, size_t buf_sz);
void mon_fmt_freq(uint32_t freq_khz, bool compact, char *buf, size_t buf_sz);
void mon_fmt_load(uint16_t load_x100, char *buf, size_t buf_sz);

#endif /* __MONITOR_COMMON_H */
