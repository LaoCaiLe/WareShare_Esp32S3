#include "page.h"
#include "common.h"
#include "theme.h"

#include <stdio.h>

static mon_page_ui_t s_ui;

const mon_page_ui_t *page_disk_ui(void) { return &s_ui; }
static lv_obj_t *s_track;
static lv_obj_t *s_fill;
static lv_coord_t s_track_w;
static lv_coord_t s_bar_h;

static void fill_width_anim(void *var, int32_t value)
{
    lv_obj_set_width((lv_obj_t *)var, value);
}

lv_obj_t *page_disk_create(lv_obj_t *parent, lv_color_t accent)
{
    const mon_ui_metrics_t *m = mon_ui();
    const sys_sample_t *s = sampler_get();

    mon_page_ui_create(&s_ui, parent, "STORAGE", accent);

    s_bar_h = LV_MAX(12, (lv_coord_t)(m->dial_d * 60 / 1000));
    s_track_w = m->stage_w;

    s_track = mon_make_rect(s_ui.stage, s_track_w, s_bar_h,
                            0, (m->stage_h - s_bar_h) / 2,
                            s_bar_h / 2, lv_color_white());
    lv_obj_set_style_bg_opa(s_track, LV_OPA_10, 0);

    s_fill = mon_make_rect(s_track, 2, s_bar_h, 0, 0, s_bar_h / 2, accent);
    lv_obj_set_style_bg_grad_dir(s_fill, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_grad_color(s_fill,
                                   lv_color_mix(accent, lv_color_white(), 120), 0);
    lv_obj_set_style_bg_dither_mode(s_fill, LV_DITHER_ORDERED, 0);

    lv_obj_t *shine = mon_make_rect(s_fill, LV_MAX(6, s_track_w - 10),
                                    LV_MAX(1, s_bar_h / 5),
                                    0, LV_MAX(2, s_bar_h / 5),
                                    LV_MAX(1, s_bar_h / 10), lv_color_white());
    lv_obj_set_width(shine, lv_pct(80));
    lv_obj_set_style_bg_opa(shine, LV_OPA_20, 0);
    lv_obj_align(shine, LV_ALIGN_TOP_MID, 0, LV_MAX(2, s_bar_h / 5));

    /* 25 % / 50 % / 75 % notches above the fill */
    for (int i = 1; i < 4; i++) {
        lv_obj_t *tick = mon_make_rect(s_track, 1, s_bar_h,
                                       (lv_coord_t)(s_track_w * i / 4),
                                       0, 0, lv_color_white());
        lv_obj_set_style_bg_opa(tick, LV_OPA_30, 0);
    }

    /* Initial fill without animation */
    lv_coord_t initial = (lv_coord_t)(s_track_w * s->disk_load / 100);
    if (initial < 2) initial = 2;
    lv_obj_set_width(s_fill, initial);

    mon_page_set_note(&s_ui, m->compact ? "" : "ROOT FS");
    return s_ui.root;
}

void page_disk_update(const sys_sample_t *s)
{
    const mon_ui_metrics_t *m = mon_ui();

    char used[40];
    char free_kb[32];

    mon_page_set_hero_value(&s_ui, (double)s->disk_load, 0, "%");

    mon_fmt_disk_pair(s->disk_used_kb, s->disk_total_kb, m->compact,
                      used, sizeof(used));
    mon_fmt_disk_size(s->disk_avail_kb, m->compact, free_kb, sizeof(free_kb));

    mon_page_set_stat(&s_ui, 0, used, "USED");
    mon_page_set_stat(&s_ui, 1, free_kb, "FREE");

    lv_coord_t target = (lv_coord_t)(s_track_w * s->disk_load / 100);
    if (target < 2) target = 2;

    lv_coord_t current = lv_obj_get_width(s_fill);
    if (current != target) {
        lv_anim_del(s_fill, fill_width_anim);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_fill);
        lv_anim_set_exec_cb(&a, fill_width_anim);
        lv_anim_set_values(&a, current, target);
        lv_anim_set_time(&a, 650);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
}
