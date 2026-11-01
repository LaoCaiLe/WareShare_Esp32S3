#include "page.h"
#include "common.h"
#include "theme.h"

#include <stdio.h>

static mon_page_ui_t s_ui;

const mon_page_ui_t *page_mem_ui(void) { return &s_ui; }
static lv_obj_t *s_chart;
static lv_chart_series_t *s_series;

lv_obj_t *page_mem_create(lv_obj_t *parent, lv_color_t accent)
{
    const mon_ui_metrics_t *m = mon_ui();
    const sys_sample_t *s = sampler_get();

    mon_page_ui_create(&s_ui, parent, "MEMORY", accent);

    s_chart = mon_chart_create(s_ui.stage, m->stage_w, m->stage_h, true);
    s_series = lv_chart_add_series(s_chart, accent, LV_CHART_AXIS_PRIMARY_Y);
    mon_chart_load(s_chart, s_series, s->mem_hist, MON_HIST_LEN,
                   s->hist_head, 100, 100);

    mon_page_set_note(&s_ui, m->compact ? "" : "60s");
    return s_ui.root;
}

void page_mem_update(const sys_sample_t *s)
{
    const mon_ui_metrics_t *m = mon_ui();

    char used[40];
    char swap[32];

    mon_page_set_hero_value(&s_ui, (double)s->mem_load, 0, "%");

    mon_fmt_size_pair(s->mem_used_kb, s->mem_total_kb, m->compact,
                      used, sizeof(used));

    if (s->swap_total_kb > 0) {
        mon_fmt_size(s->swap_used_kb, m->compact, swap, sizeof(swap));
    }
    else {
        snprintf(swap, sizeof(swap), "OFF");
    }

    mon_page_set_stat(&s_ui, 0, used, "USED");
    mon_page_set_stat(&s_ui, 1, swap, "SWAP");

    mon_chart_load(s_chart, s_series, s->mem_hist, MON_HIST_LEN,
                   s->hist_head, 100, 100);
}
