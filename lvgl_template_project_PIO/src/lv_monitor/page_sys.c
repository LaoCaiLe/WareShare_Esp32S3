#include "page.h"
#include "common.h"
#include "theme.h"

#include <stdio.h>

static mon_page_ui_t s_ui;
static lv_obj_t *s_chart;
static lv_chart_series_t *s_series;

lv_obj_t *page_sys_create(lv_obj_t *parent, lv_color_t accent)
{
    const mon_ui_metrics_t *m = mon_ui();
    const sys_sample_t *s = sampler_get();

    mon_page_ui_create(&s_ui, parent, "SYSTEM", accent);

    s_chart = mon_chart_create(s_ui.stage, m->stage_w, m->stage_h, true);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    s_series = lv_chart_add_series(s_chart, accent, LV_CHART_AXIS_PRIMARY_Y);

    uint32_t peak = 100;
    for (int i = 0; i < MON_HIST_LEN; i++) {
        if (s->load_hist[i] > peak) peak = s->load_hist[i];
    }
    uint32_t scale = peak + peak / 4;
    if (scale < 100) scale = 100;

    mon_chart_load(s_chart, s_series, s->load_hist, MON_HIST_LEN,
                   s->hist_head, scale, 1000);

    if (!m->compact) mon_page_set_note(&s_ui, "1M");
    return s_ui.root;
}

void page_sys_update(const sys_sample_t *s)
{
    const mon_ui_metrics_t *m = mon_ui();

    char uptime[32];
    char load5[16];
    char load15[16];
    char note[24];

    mon_fmt_uptime(s->uptime_sec, m->compact, uptime, sizeof(uptime));
    mon_page_set_hero(&s_ui, uptime, "");

    mon_fmt_load(s->load5_x100, load5, sizeof(load5));
    mon_fmt_load(s->load15_x100, load15, sizeof(load15));

    mon_page_set_stat(&s_ui, 0, load5, "LOAD 5M");
    mon_page_set_stat(&s_ui, 1, load15, "LOAD 15M");

    uint32_t peak = 100;
    for (int i = 0; i < MON_HIST_LEN; i++) {
        if (s->load_hist[i] > peak) peak = s->load_hist[i];
    }
    uint32_t scale = peak + peak / 4;
    if (scale < 100) scale = 100;

    mon_chart_load(s_chart, s_series, s->load_hist, MON_HIST_LEN,
                   s->hist_head, scale, 1000);

    if (m->compact) {
        mon_page_set_note(&s_ui, "");
    }
    else {
        char load1[16];
        mon_fmt_load(s->load1_x100, load1, sizeof(load1));
        snprintf(note, sizeof(note), "1M %s", load1);
        mon_page_set_note(&s_ui, note);
    }
}
