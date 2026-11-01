#include "page.h"
#include "common.h"
#include "theme.h"

#include <stdio.h>
#include <string.h>

static mon_page_ui_t s_ui;

const mon_page_ui_t *page_net_ui(void) { return &s_ui; }
static lv_obj_t *s_chart;
static lv_chart_series_t *s_rx;
static lv_chart_series_t *s_tx;

static uint32_t net_nice_scale(uint32_t peak)
{
    uint64_t scale = 64ULL * 1024ULL;
    while (scale < peak && scale < (1ULL << 31)) {
        scale <<= 1;
    }
    if (scale > 0xFFFFFFFFULL) scale = 0xFFFFFFFFULL;
    return (uint32_t)scale;
}

lv_obj_t *page_net_create(lv_obj_t *parent, lv_color_t accent)
{
    const mon_ui_metrics_t *m = mon_ui();
    const sys_sample_t *s = sampler_get();

    mon_page_ui_create(&s_ui, parent, "NETWORK", accent);

    s_chart = mon_chart_create(s_ui.stage, m->stage_w, m->stage_h, true);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);

    s_rx = lv_chart_add_series(s_chart, accent, LV_CHART_AXIS_PRIMARY_Y);
    s_tx = lv_chart_add_series(s_chart, lv_color_hex(0x7C8DFF),
                               LV_CHART_AXIS_PRIMARY_Y);

    uint32_t peak = 64U * 1024U;
    for (int i = 0; i < MON_HIST_LEN; i++) {
        if (s->net_rx_hist[i] > peak) peak = s->net_rx_hist[i];
        if (s->net_tx_hist[i] > peak) peak = s->net_tx_hist[i];
    }
    uint32_t scale = net_nice_scale(peak);
    mon_chart_load(s_chart, s_rx, s->net_rx_hist, MON_HIST_LEN,
                   s->hist_head, scale, 1000);
    mon_chart_load(s_chart, s_tx, s->net_tx_hist, MON_HIST_LEN,
                   s->hist_head, scale, 1000);

    if (!m->compact) mon_page_set_note(&s_ui, "MAX");
    return s_ui.root;
}

void page_net_update(const sys_sample_t *s)
{
    const mon_ui_metrics_t *m = mon_ui();

    char num[24];
    char unit[12];
    char up[32];
    char total[32];
    char note[48];

    mon_fmt_rate(s->net_rx_bps, m->compact, num, sizeof(num),
                 unit, sizeof(unit));
    mon_page_set_hero(&s_ui, num, unit);

    mon_fmt_rate_line(s->net_tx_bps, m->compact, up, sizeof(up));
    mon_page_set_stat(&s_ui, 0, up, "UPLOAD");

    mon_fmt_size((s->net_rx_total + s->net_tx_total) / 1024ULL,
                 m->compact, total, sizeof(total));
    mon_page_set_stat(&s_ui, 1, total, "TOTAL");

    uint32_t peak = 64U * 1024U;
    for (int i = 0; i < MON_HIST_LEN; i++) {
        if (s->net_rx_hist[i] > peak) peak = s->net_rx_hist[i];
        if (s->net_tx_hist[i] > peak) peak = s->net_tx_hist[i];
    }
    uint32_t scale = net_nice_scale(peak);

    mon_chart_load(s_chart, s_rx, s->net_rx_hist, MON_HIST_LEN,
                   s->hist_head, scale, 1000);
    mon_chart_load(s_chart, s_tx, s->net_tx_hist, MON_HIST_LEN,
                   s->hist_head, scale, 1000);

    if (!m->compact) {
        char scale_num[24];
        char scale_unit[12];
        mon_fmt_rate(scale, false, scale_num, sizeof(scale_num),
                     scale_unit, sizeof(scale_unit));
        snprintf(note, sizeof(note), "MAX %s %s", scale_num, scale_unit);
        mon_page_set_note(&s_ui, note);
    }
}
