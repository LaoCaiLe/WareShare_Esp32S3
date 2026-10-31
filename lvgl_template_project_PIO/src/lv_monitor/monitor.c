#include "monitor.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "page.h"
#include "sampler.h"
#include "theme.h"
#include "touch.h"

#define MON_PI          3.14159265f
#define MON_TICKS       60
#define MON_STEP_TENTH  (3600 / MON_PAGE_COUNT)   /* one page in 0.1° units */
#define MON_TICKS_SEG   (MON_TICKS / MON_PAGE_COUNT)

static monitor_page_t s_pages[MON_PAGE_COUNT] = {
    { MON_ACCENT_CPU,  page_cpu_create,  page_cpu_update,  NULL },
    { MON_ACCENT_MEM,  page_mem_create,  page_mem_update,  NULL },
    { MON_ACCENT_DISK, page_disk_create, page_disk_update, NULL },
    { MON_ACCENT_NET,  page_net_create,  page_net_update,  NULL },
    { MON_ACCENT_SYS,  page_sys_create,  page_sys_update,  NULL },
    { MON_ACCENT_GPU,  page_gpu_create,  page_gpu_update,  NULL },
};

static lv_obj_t *s_dial;
static lv_obj_t *s_content;
static lv_obj_t *s_ring;

static uint8_t s_page_idx;
static uint8_t s_page_prev;
static uint8_t s_page_next;
static bool s_transitioning;
static bool s_started;

static int32_t s_ring_angle10;

/* ── center-seam fold: each page is clipped to a centered vertical strip ── */

typedef struct {
    lv_draw_mask_radius_param_t param;
    int16_t mask_id;
    bool active;
} page_mask_t;

static lv_coord_t s_page_clip_w[MON_PAGE_COUNT];
static page_mask_t s_page_masks[MON_PAGE_COUNT];

static void page_mask_draw_cb(lv_event_t *e)
{
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    if (idx >= MON_PAGE_COUNT) return;

    page_mask_t *mask = &s_page_masks[idx];
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_DRAW_MAIN_BEGIN) {
        lv_coord_t clip_w = s_page_clip_w[idx];
        lv_coord_t d = mon_ui()->dial_d;
        if (clip_w >= d) return;          /* fully open: no mask needed */

        lv_obj_t *obj = lv_event_get_target(e);
        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);

        lv_coord_t cx = coords.x1 + d / 2;
        if (clip_w < 0) clip_w = 0;

        lv_area_t area = {
            .x1 = cx - clip_w / 2,
            .x2 = cx + clip_w / 2 - 1,
            .y1 = coords.y1,
            .y2 = coords.y2,
        };
        if (area.x2 < area.x1) area.x2 = area.x1 - 1;

        lv_draw_mask_radius_init(&mask->param, &area, 0, false);
        mask->mask_id = lv_draw_mask_add(&mask->param, NULL);
        mask->active = true;
    }
    else if (code == LV_EVENT_DRAW_POST_END) {
        if (mask->active) {
            lv_draw_mask_remove_id(mask->mask_id);
            lv_draw_mask_free_param(&mask->param);
            mask->active = false;
        }
    }
}

/* ══════════════════════════ chrome drawing ══════════════════════════ */

static void ring_draw_cb(lv_event_t *e)
{
    const mon_ui_metrics_t *m = mon_ui();
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    if (ctx == NULL) return;

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    float cx = (float)coords.x1 + (float)m->dial_d / 2.0f;
    float cy = (float)coords.y1 + (float)m->dial_d / 2.0f;
    float r_out = (float)m->radius - (float)m->dial_d * 0.020f;
    float ring_deg = (float)(s_ring_angle10 % 3600) / 10.0f;

    for (int i = 0; i < MON_TICKS; i++) {
        float angle = (float)(i * 6) + ring_deg;

        bool is_seg = (i % MON_TICKS_SEG) == 0;
        bool is_major = (i % 6) == 0;

        float len = (float)m->dial_d *
                    (is_seg ? 0.030f : (is_major ? 0.019f : 0.011f));
        float r_in = r_out - len;

        float rad = angle * MON_PI / 180.0f;
        float sn = sinf(rad);
        float cs = cosf(rad);

        lv_point_t p1;
        lv_point_t p2;
        p1.x = (lv_coord_t)(cx + sn * r_out);
        p1.y = (lv_coord_t)(cy - cs * r_out);
        p2.x = (lv_coord_t)(cx + sn * r_in);
        p2.y = (lv_coord_t)(cy - cs * r_in);

        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);

        if (is_seg) {
            dsc.color = lv_color_hex(MON_RING_BRIGHT);
            dsc.width = 2;
            dsc.opa = 150;
        }
        else {
            dsc.color = lv_color_hex(MON_RING);
            dsc.width = 1;
            dsc.opa = 110;
        }

        lv_draw_line(ctx, &dsc, &p1, &p2);
    }
}

/* ══════════════════════════ animations ══════════════════════════ */

typedef struct {
    int32_t from;
    int32_t to;
} ring_anim_t;

static ring_anim_t s_ring_anim;

static void ring_anim_cb(void *var, int32_t value)
{
    ring_anim_t *anim = (ring_anim_t *)var;
    s_ring_angle10 = anim->from + (anim->to - anim->from) * value / 1000;
    lv_obj_invalidate(s_ring);
}

static void ring_ready_cb(lv_anim_t *anim)
{
    (void)anim;
    /* 3600 -> 0, -600 -> 3000: the tick pattern repeats every turn. */
    s_ring_angle10 = ((s_ring_angle10 % 3600) + 3600) % 3600;
    lv_obj_invalidate(s_ring);
}

typedef struct {
    lv_obj_t *out;
    lv_obj_t *in;
} fold_anim_t;

static fold_anim_t s_fold;

static void fold_anim_cb(void *var, int32_t value)
{
    fold_anim_t *fold = (fold_anim_t *)var;
    lv_coord_t d = mon_ui()->dial_d;

    lv_coord_t w_out = 0;
    lv_coord_t w_in = 0;

    if (value < 500) {
        w_out = (lv_coord_t)((int32_t)d * (500 - value) / 500);

        /* Only the outgoing page needs drawing in the first half. */
        if (lv_obj_has_flag(fold->out, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(fold->out, LV_OBJ_FLAG_HIDDEN);
        }
        if (!lv_obj_has_flag(fold->in, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(fold->in, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else {
        w_in = (lv_coord_t)((int32_t)d * (value - 500) / 500);

        /* From the midpoint on, only the incoming page needs drawing. */
        if (!lv_obj_has_flag(fold->out, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(fold->out, LV_OBJ_FLAG_HIDDEN);
        }
        if (lv_obj_has_flag(fold->in, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(fold->in, LV_OBJ_FLAG_HIDDEN);
        }
    }

    s_page_clip_w[s_page_prev] = w_out;
    s_page_clip_w[s_page_next] = w_in;

    lv_obj_invalidate(value < 500 ? fold->out : fold->in);
}

/* ══════════════════════════ page transition ══════════════════════════ */

static void fold_ready(lv_anim_t *anim)
{
    (void)anim;

    lv_obj_t *out = s_pages[s_page_prev].root;
    lv_obj_t *in = s_pages[s_page_next].root;

    s_page_clip_w[s_page_prev] = 0;
    s_page_clip_w[s_page_next] = mon_ui()->dial_d;

    lv_obj_add_flag(out, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(in, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(out, 0, 0);
    lv_obj_set_pos(in, 0, 0);
    lv_obj_set_style_opa(out, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(in, LV_OPA_COVER, 0);

    s_page_idx = s_page_next;
    s_transitioning = false;

    lv_obj_invalidate(s_ring);

    s_pages[s_page_idx].update(sampler_get());
}

static void start_transition(int8_t dir)
{
    if (s_transitioning) return;

    s_transitioning = true;

    s_page_prev = s_page_idx;
    s_page_next = (uint8_t)((s_page_idx + MON_PAGE_COUNT + dir) % MON_PAGE_COUNT);

    s_pages[s_page_next].update(sampler_get());

    lv_obj_t *out = s_pages[s_page_prev].root;
    lv_obj_t *in = s_pages[s_page_next].root;

    s_page_clip_w[s_page_prev] = mon_ui()->dial_d;
    s_page_clip_w[s_page_next] = 0;

    lv_obj_clear_flag(out, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(in, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(out, 0, 0);
    lv_obj_set_pos(in, 0, 0);
    lv_obj_set_style_opa(out, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(in, LV_OPA_COVER, 0);

    s_fold.out = out;
    s_fold.in = in;

    lv_anim_del(&s_fold, fold_anim_cb);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &s_fold);
    lv_anim_set_exec_cb(&anim, fold_anim_cb);
    lv_anim_set_values(&anim, 0, 1000);
    lv_anim_set_time(&anim, MON_TRANS_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&anim, fold_ready);
    lv_anim_start(&anim);

    lv_obj_invalidate(out);
    lv_obj_invalidate(in);

    /* The tick bezel turns one page step, in the swipe direction. */
    s_ring_anim.from = s_ring_angle10;
    s_ring_anim.to = s_ring_angle10 + (int32_t)dir * MON_STEP_TENTH;

    lv_anim_del(&s_ring_anim, ring_anim_cb);

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &s_ring_anim);
    lv_anim_set_exec_cb(&anim, ring_anim_cb);
    lv_anim_set_values(&anim, 0, 1000);
    lv_anim_set_time(&anim, MON_TRANS_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&anim, ring_ready_cb);
    lv_anim_start(&anim);
}

/* ══════════════════════════ timers / input ══════════════════════════ */

static void monitor_gesture_cb(touch_gesture_t gesture)
{
    if (gesture == TOUCH_SWIPE_LEFT) {
        start_transition(1);
    }
    else if (gesture == TOUCH_SWIPE_RIGHT) {
        start_transition(-1);
    }
    /* Taps on purpose do nothing: a phantom touch only produces a single
     * point, which must never page the carousel. */
}

static void data_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_transitioning) return;
    s_pages[s_page_idx].update(sampler_get());
}

/* ══════════════════════════ build ══════════════════════════ */

static void build_chrome(void)
{
    const mon_ui_metrics_t *m = mon_ui();
    lv_coord_t d = m->dial_d;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(MON_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bezel = mon_make_rect(scr, d + 2, d + 2, 0, 0,
                                    LV_RADIUS_CIRCLE, lv_color_hex(MON_BEZEL));
    lv_obj_center(bezel);

    s_dial = lv_obj_create(scr);
    lv_obj_set_size(s_dial, d, d);
    lv_obj_set_style_radius(s_dial, LV_RADIUS_CIRCLE, 0);
    /* Flat black on purpose: a near-black gradient only spans a handful of
     * RGB565 steps across 224 px, so it either bands or — once dithered —
     * shows an 8x8 grain pattern. Neither reads as black on this panel. */
    lv_obj_set_style_bg_color(s_dial, lv_color_hex(MON_DIAL), 0);
    lv_obj_set_style_bg_opa(s_dial, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_dial, 0, 0);
    lv_obj_set_style_pad_all(s_dial, 0, 0);
    lv_obj_set_style_clip_corner(s_dial, true, 0);
    lv_obj_clear_flag(s_dial, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(s_dial);

    s_content = lv_obj_create(s_dial);
    lv_obj_set_size(s_content, d, d);
    lv_obj_set_pos(s_content, 0, 0);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);
    lv_obj_set_style_radius(s_content, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(s_content, true, 0);
    lv_obj_clear_flag(s_content,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* pages, built once and kept alive */
    for (int i = 0; i < MON_PAGE_COUNT; i++) {
        s_pages[i].root = s_pages[i].create(s_content,
                                            lv_color_hex(s_pages[i].accent_hex));
        lv_obj_set_pos(s_pages[i].root, 0, 0);
        lv_obj_set_style_opa(s_pages[i].root, LV_OPA_COVER, 0);
        lv_obj_add_flag(s_pages[i].root, LV_OBJ_FLAG_HIDDEN);

        s_page_clip_w[i] = 0;
        lv_obj_add_event_cb(s_pages[i].root, page_mask_draw_cb,
                            LV_EVENT_DRAW_MAIN_BEGIN, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(s_pages[i].root, page_mask_draw_cb,
                            LV_EVENT_DRAW_POST_END, (void *)(uintptr_t)i);
    }

    /* tick ring + five 72° sector markers */
    s_ring = lv_obj_create(s_dial);
    lv_obj_set_size(s_ring, d, d);
    lv_obj_set_pos(s_ring, 0, 0);
    lv_obj_set_style_bg_opa(s_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ring, 0, 0);
    lv_obj_set_style_pad_all(s_ring, 0, 0);
    lv_obj_set_style_radius(s_ring, 0, 0);
    lv_obj_clear_flag(s_ring,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ring, ring_draw_cb, LV_EVENT_DRAW_MAIN_BEGIN, NULL);

    /* reveal page 0 */
    s_page_idx = 0;
    s_page_clip_w[0] = d;
    lv_obj_clear_flag(s_pages[0].root, LV_OBJ_FLAG_HIDDEN);
    s_pages[0].update(sampler_get());
}

void lv_monitor_show(void)
{
    if (s_started) return;
    s_started = true;

    sampler_init();
    mon_ui_init();
    build_chrome();

    touch_set_gesture_cb(monitor_gesture_cb);
    lv_timer_create(data_timer_cb, 1000, NULL);
}

void lv_monitor_set_pc_data(const pc_data_t *data)
{
    sampler_set_pc_data(data);
}
