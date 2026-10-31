#include "page.h"
#include "common.h"
#include "theme.h"

#include <stdio.h>

static mon_page_ui_t s_ui;
static lv_obj_t *s_chart;
static lv_chart_series_t *s_series;

lv_obj_t *page_gpu_create(lv_obj_t *parent, lv_color_t accent)
{
    const mon_ui_metrics_t *m = mon_ui();
    const sys_sample_t *s = sampler_get();

    mon_page_ui_create(&s_ui, parent, "GPU", accent);

    s_chart = mon_chart_create(s_ui.stage, m->stage_w, m->stage_h, true);
    s_series = lv_chart_add_series(s_chart, accent, LV_CHART_AXIS_PRIMARY_Y);
    mon_chart_load(s_chart, s_series, s->gpu_hist, MON_HIST_LEN,
                   s->hist_head, 100, 100);

    mon_page_set_note(&s_ui, m->compact ? "" : "60s");
    return s_ui.root;
}

void page_gpu_update(const sys_sample_t *s)
{
    const mon_ui_metrics_t *m = mon_ui();

    if (!s->gpu_valid) {
        mon_page_set_hero(&s_ui, "--", "%");
        mon_page_set_stat(&s_ui, 0, "--", "TEMP");
        mon_page_set_stat(&s_ui, 1, "--", "VRAM");

        mon_chart_load(s_chart, s_series, s->gpu_hist, MON_HIST_LEN,
                       s->hist_head, 100, 100);
        return;
    }

    char temp[24];
    char vram[40];

    mon_page_set_hero_value(&s_ui, (double)s->gpu_load, 0, "%");

    if (s->gpu_temp_c > 0) {
        snprintf(temp, sizeof(temp), "%d°C", (int)s->gpu_temp_c);
    }
    else {
        snprintf(temp, sizeof(temp), "--");
    }

    mon_fmt_size_pair(s->gpu_mem_used_kb, s->gpu_mem_total_kb, m->compact,
                      vram, sizeof(vram));

    mon_page_set_stat(&s_ui, 0, temp, "TEMP");
    mon_page_set_stat(&s_ui, 1, vram, "VRAM");

    mon_chart_load(s_chart, s_series, s->gpu_hist, MON_HIST_LEN,
                   s->hist_head, 100, 100);
}
