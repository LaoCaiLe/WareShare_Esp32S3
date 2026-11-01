#include "page.h"
#include "common.h"
#include "theme.h"

#include <stdio.h>

static mon_page_ui_t s_ui;

const mon_page_ui_t *page_cpu_ui(void) { return &s_ui; }
static lv_obj_t *s_chart;
static lv_chart_series_t *s_series;

lv_obj_t *page_cpu_create(lv_obj_t *parent, lv_color_t accent)
{
    const mon_ui_metrics_t *m = mon_ui();
    const sys_sample_t *s = sampler_get();

    mon_page_ui_create(&s_ui, parent, "CPU", accent);

    s_chart = mon_chart_create(s_ui.stage, m->stage_w, m->stage_h, true);
    s_series = lv_chart_add_series(s_chart, accent, LV_CHART_AXIS_PRIMARY_Y);
    mon_chart_load(s_chart, s_series, s->cpu_hist, MON_HIST_LEN,
                   s->hist_head, 100, 100);

    mon_page_set_note(&s_ui, m->compact ? "" : "60s");
    return s_ui.root;
}

void page_cpu_update(const sys_sample_t *s)
{
    const mon_ui_metrics_t *m = mon_ui();

    char temp[24];
    char freq[24];
    const char *temp_cap = "TEMP";

    mon_page_set_hero_value(&s_ui, (double)s->cpu_load, 0, "%");

    if (s->cpu_temp_c != SYS_TEMP_UNKNOWN) {
        snprintf(temp, sizeof(temp), "%u°C", (unsigned)s->cpu_temp_c);
    }
    else if (s->cpu_procs > 0U) {
        /* Windows has no CPU sensor without a driver-backed helper, so
         * show the process count in the temperature slot instead. */
        snprintf(temp, sizeof(temp), "%u", (unsigned)s->cpu_procs);
        temp_cap = "PROC";
    }
    else {
        snprintf(temp, sizeof(temp), "--");
    }

    mon_fmt_freq(s->cpu_freq_khz, m->compact, freq, sizeof(freq));

    mon_page_set_stat(&s_ui, 0, temp, temp_cap);
    mon_page_set_stat(&s_ui, 1, freq, "FREQ");

    mon_chart_load(s_chart, s_series, s->cpu_hist, MON_HIST_LEN,
                   s->hist_head, 100, 100);
}
