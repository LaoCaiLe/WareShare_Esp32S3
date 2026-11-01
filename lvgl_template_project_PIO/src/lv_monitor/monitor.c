#include "monitor.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "page.h"
#include "sampler.h"
#include "theme.h"
#include "touch.h"

static monitor_page_t s_pages[MON_PAGE_COUNT] = {
    { MON_ACCENT_CPU,  page_cpu_create,  page_cpu_update,  page_cpu_ui,  NULL },
    { MON_ACCENT_GPU,  page_gpu_create,  page_gpu_update,  page_gpu_ui,  NULL },
    { MON_ACCENT_MEM,  page_mem_create,  page_mem_update,  page_mem_ui,  NULL },
    { MON_ACCENT_DISK, page_disk_create, page_disk_update, page_disk_ui, NULL },
    { MON_ACCENT_NET,  page_net_create,  page_net_update,  page_net_ui,  NULL },
    { MON_ACCENT_SYS,  page_sys_create,  page_sys_update,  page_sys_ui,  NULL },
};

static lv_obj_t *s_dial;
static lv_obj_t *s_content;
static lv_obj_t *s_dots;

static uint8_t s_page_idx;
static uint8_t s_page_prev;
static uint8_t s_page_next;
static bool s_transitioning;
static bool s_started;

static uint32_t s_page_start;
static float s_dot_pos;
static float s_dot_from;
static float s_dot_to;

static lv_coord_t s_dot_step;
static lv_coord_t s_dot_size;
static lv_coord_t s_dot_pill_w;

/* ── layered page transition phases, as % of MON_TRANS_MS ── */

#define MON_EXIT_PCT          43
#define MON_ENTER_PCT         52
#define MON_STAGE_OUT_PCT     48
#define MON_STAGE_IN_PCT      40
#define MON_STAGE_IN_END_PCT  88

/* ══════════════════════════ page dots ══════════════════════════ */

static void dots_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    if (ctx == NULL) return;

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    lv_coord_t width = lv_area_get_width(&coords);
    lv_coord_t height = lv_area_get_height(&coords);
    lv_coord_t cy = coords.y1 + height / 2;
    lv_coord_t total = (MON_PAGE_COUNT - 1) * s_dot_step;
    lv_coord_t x0 = coords.x1 + (width - total) / 2;

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.border_width = 0;
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.bg_color = lv_color_white();
    dsc.bg_opa = 64;

    for (int i = 0; i < MON_PAGE_COUNT; i++) {
        lv_area_t dot = {
            .x1 = x0 + i * s_dot_step - s_dot_size / 2,
            .x2 = x0 + i * s_dot_step + s_dot_size / 2,
            .y1 = cy - s_dot_size / 2,
            .y2 = cy + s_dot_size / 2,
        };
        lv_draw_rect(ctx, &dsc, &dot);
    }

    float pos = s_dot_pos;
    if (pos < 0.0f) pos = 0.0f;
    if (pos > (float)(MON_PAGE_COUNT - 1)) pos = (float)(MON_PAGE_COUNT - 1);

    lv_coord_t pill_x = x0 + (lv_coord_t)(pos * (float)s_dot_step);
    lv_coord_t pill_h = s_dot_size + 2;

    /* Derive the pill colour directly from its rendered position: at rest it
     * is the current page accent, in motion it blends toward the next one.
     * No separate accent state means it can never flash ahead or lag. */
    int from_pg = (int)(s_dot_from + 0.5f);
    int to_pg = (int)(s_dot_to + 0.5f);
    if (from_pg < 0) from_pg = 0;
    if (from_pg >= MON_PAGE_COUNT) from_pg = MON_PAGE_COUNT - 1;
    if (to_pg < 0) to_pg = 0;
    if (to_pg >= MON_PAGE_COUNT) to_pg = MON_PAGE_COUNT - 1;

    float span = s_dot_to - s_dot_from;
    float t = 0.0f;
    if (span > 0.0001f || span < -0.0001f) {
        t = (s_dot_pos - s_dot_from) / span;
    }
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    /* lv_color_mix: mix = 0 -> c2, mix = 255 -> c1. Pass the destination
     * colour first so t=0 is the old accent and t=1 is the new one. */
    lv_color_t accent = lv_color_mix(
        lv_color_hex(s_pages[to_pg].accent_hex),
        lv_color_hex(s_pages[from_pg].accent_hex),
        (uint8_t)(t * 255.0f));

    lv_draw_rect_dsc_t glow;
    lv_draw_rect_dsc_init(&glow);
    glow.border_width = 0;
    glow.radius = LV_RADIUS_CIRCLE;
    glow.bg_color = accent;
    glow.bg_opa = 40;

    lv_area_t glow_area = {
        .x1 = pill_x - s_dot_pill_w / 2 - 4,
        .x2 = pill_x + s_dot_pill_w / 2 + 4,
        .y1 = cy - pill_h / 2 - 3,
        .y2 = cy + pill_h / 2 + 3,
    };
    lv_draw_rect(ctx, &glow, &glow_area);

    lv_draw_rect_dsc_t pill;
    lv_draw_rect_dsc_init(&pill);
    pill.border_width = 0;
    pill.radius = LV_RADIUS_CIRCLE;
    pill.bg_color = accent;
    pill.bg_opa = LV_OPA_COVER;

    lv_area_t pill_area = {
        .x1 = pill_x - s_dot_pill_w / 2,
        .x2 = pill_x + s_dot_pill_w / 2,
        .y1 = cy - pill_h / 2,
        .y2 = cy + pill_h / 2,
    };
    lv_draw_rect(ctx, &pill, &pill_area);
}

/* ══════════════════════════ animations ══════════════════════════ */

typedef struct {
    const mon_page_ui_t *out;
    const mon_page_ui_t *in;
} layer_anim_t;

static layer_anim_t s_layer;
static char s_tag_out[24];
static char s_tag_in[24];
static int32_t s_glitch_step;

static int32_t lerp_i(int32_t a, int32_t b, int32_t t)
{
    return a + (b - a) * t / 1000;
}

static int32_t phase_i(int32_t ms, int32_t start, int32_t end)
{
    if (ms <= start) return 0;
    if (ms >= end) return 1000;
    return (ms - start) * 1000 / (end - start);
}

static int32_t ease_in_i(int32_t t)
{
    return t * t / 1000;
}

static int32_t ease_out_i(int32_t t)
{
    int32_t inv = 1000 - t;
    return 1000 - inv * inv / 1000;
}

static int32_t ease_in_out_i(int32_t t)
{
    if (t < 500) return 2 * t * t / 1000;
    int32_t inv = 1000 - t;
    return 1000 - 2 * inv * inv / 1000;
}

static uint32_t glitch_rand(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 16;
}

static void glitch_label(lv_obj_t *label, const char *target,
                         int32_t reveal, int32_t step)
{
    static const char trash[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#%&/<>*+-";
    char buf[24];

    size_t len = strlen(target);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;

    uint32_t state = (uint32_t)lv_tick_get() ^
                     ((uint32_t)step * 2654435761u);

    for (size_t i = 0; i < len; i++) {
        if (target[i] == ' ' ||
            (int32_t)(glitch_rand(&state) % 100) < reveal) {
            buf[i] = target[i];
        }
        else {
            buf[i] = trash[glitch_rand(&state) % (sizeof(trash) - 1)];
        }
    }
    buf[len] = '\0';
    lv_label_set_text(label, buf);
}

static void tag_state(const mon_page_ui_t *ui, lv_opa_t opa, lv_coord_t jitter)
{
    lv_obj_set_style_opa(ui->tag_row, opa, 0);
    lv_obj_set_style_translate_x(ui->tag_row, jitter, 0);
}

static void layer_anim_cb(void *var, int32_t value)
{
    layer_anim_t *a = (layer_anim_t *)var;
    int32_t total = (int32_t)MON_TRANS_MS;
    int32_t ms = value * total / 1000;

    int32_t exit_end = total * MON_EXIT_PCT / 100;
    int32_t enter_start = total * MON_ENTER_PCT / 100;
    int32_t stage_out_end = total * MON_STAGE_OUT_PCT / 100;
    int32_t stage_in_start = total * MON_STAGE_IN_PCT / 100;
    int32_t stage_in_end = total * MON_STAGE_IN_END_PCT / 100;

    /* The pill travels with the same eased progress as the rest of the
     * transition; its colour is derived from that position when drawn, so
     * colour and movement can never drift apart. */
    int32_t morph = ease_in_out_i(value);
    s_dot_pos = s_dot_from +
                (s_dot_to - s_dot_from) * ((float)morph / 1000.0f);
    lv_obj_invalidate(s_dots);

    /* ── outgoing: exit ── */
    int32_t out_p = phase_i(ms, 0, exit_end);
    int32_t out_e = ease_in_i(out_p);
    lv_opa_t out_opa = (lv_opa_t)(255 - 255 * out_e / 1000);

    lv_obj_set_style_opa(a->out->hero_box, out_opa, 0);
    lv_obj_set_style_transform_zoom(a->out->hero_box,
                                    lerp_i(256, 64, out_e), 0);

    lv_obj_set_style_opa(a->out->sub_row, out_opa, 0);
    lv_obj_set_style_translate_y(a->out->sub_row,
                                 lerp_i(0, 84, out_e), 0);

    int32_t stage_out_p = ease_in_i(phase_i(ms, 0, stage_out_end));
    lv_opa_t stage_out_opa = (lv_opa_t)(255 - 255 * stage_out_p / 1000);
    lv_obj_set_style_opa(a->out->stage, stage_out_opa, 0);
    lv_obj_set_style_opa(a->out->note, stage_out_opa, 0);
    lv_obj_set_style_translate_y(a->out->stage,
                                 lerp_i(0, -6, stage_out_p), 0);
    lv_obj_set_style_translate_y(a->out->note,
                                 lerp_i(0, -6, stage_out_p), 0);

    if (ms < exit_end) {
        int32_t jitter = ((value / 60 + 1) % 3) - 1;
        tag_state(a->out, (lv_opa_t)(255 - 255 * out_p / 1000), jitter);
    }
    else {
        tag_state(a->out, LV_OPA_TRANSP, 0);
    }

    /* ── incoming: enter ── */
    int32_t in_p = phase_i(ms, enter_start, total);
    int32_t in_e = ease_out_i(in_p);
    lv_opa_t in_opa = (lv_opa_t)(255 * in_e / 1000);

    lv_obj_set_style_opa(a->in->hero_box, in_opa, 0);
    lv_obj_set_style_transform_zoom(a->in->hero_box,
                                    lerp_i(64, 256, in_e), 0);

    lv_obj_set_style_opa(a->in->sub_row, in_opa, 0);
    lv_obj_set_style_translate_y(a->in->sub_row,
                                 lerp_i(84, 0, in_e), 0);

    int32_t stage_in_p = ease_out_i(
        phase_i(ms, stage_in_start, stage_in_end));
    lv_opa_t stage_in_opa = (lv_opa_t)(255 * stage_in_p / 1000);
    lv_obj_set_style_opa(a->in->stage, stage_in_opa, 0);
    lv_obj_set_style_opa(a->in->note, stage_in_opa, 0);
    lv_obj_set_style_translate_y(a->in->stage,
                                 lerp_i(6, 0, stage_in_p), 0);
    lv_obj_set_style_translate_y(a->in->note,
                                 lerp_i(6, 0, stage_in_p), 0);

    if (ms >= enter_start) {
        int32_t jitter = ((value / 60 + 1) % 3) - 1;
        tag_state(a->in, in_opa, jitter);
    }
    else {
        tag_state(a->in, LV_OPA_TRANSP, 0);
    }

    /* ── tag glitch text, updated on step boundaries ── */
    int32_t step = value * 20 / 1000;
    if (step != s_glitch_step) {
        s_glitch_step = step;

        if (ms < exit_end) {
            int32_t reveal = 100 - ms * 100 / exit_end;
            glitch_label(a->out->tag_label, s_tag_out, reveal, step);
        }
        if (ms >= enter_start) {
            int32_t reveal = (ms - enter_start) * 100 /
                             (total - enter_start);
            glitch_label(a->in->tag_label, s_tag_in, reveal, step);
        }
    }

    if (value >= 1000) {
        lv_label_set_text(a->out->tag_label, s_tag_out);
        lv_label_set_text(a->in->tag_label, s_tag_in);
    }
}

/* ══════════════════════════ page transition ══════════════════════════ */

static void layer_ready(lv_anim_t *anim)
{
    (void)anim;

    const mon_page_ui_t *out = s_layer.out;
    const mon_page_ui_t *in = s_layer.in;
    lv_obj_t *out_root = s_pages[s_page_prev].root;
    lv_obj_t *in_root = s_pages[s_page_next].root;

    /* reset both pages so either can be used again next cycle */
    tag_state(out, LV_OPA_COVER, 0);
    lv_label_set_text(out->tag_label, s_tag_out);
    lv_obj_set_style_opa(out->hero_box, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_zoom(out->hero_box, 256, 0);
    lv_obj_set_style_opa(out->stage, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(out->note, LV_OPA_COVER, 0);
    lv_obj_set_style_translate_y(out->stage, 0, 0);
    lv_obj_set_style_translate_y(out->note, 0, 0);
    lv_obj_set_style_opa(out->sub_row, LV_OPA_COVER, 0);
    lv_obj_set_style_translate_y(out->sub_row, 0, 0);

    tag_state(in, LV_OPA_COVER, 0);
    lv_label_set_text(in->tag_label, s_tag_in);
    lv_obj_set_style_opa(in->hero_box, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_zoom(in->hero_box, 256, 0);
    lv_obj_set_style_opa(in->stage, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(in->note, LV_OPA_COVER, 0);
    lv_obj_set_style_translate_y(in->stage, 0, 0);
    lv_obj_set_style_translate_y(in->note, 0, 0);
    lv_obj_set_style_opa(in->sub_row, LV_OPA_COVER, 0);
    lv_obj_set_style_translate_y(in->sub_row, 0, 0);

    lv_obj_add_flag(out_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(in_root, LV_OBJ_FLAG_HIDDEN);

    s_page_idx = s_page_next;
    s_page_start = lv_tick_get();
    s_transitioning = false;
    s_dot_pos = (float)s_page_idx;
    lv_obj_invalidate(s_dots);

    s_pages[s_page_idx].update(sampler_get());
}

static void start_transition(int8_t dir)
{
    if (s_transitioning) return;

    s_transitioning = true;
    s_glitch_step = -1;

    s_page_prev = s_page_idx;
    s_page_next = (uint8_t)((s_page_idx + MON_PAGE_COUNT + dir) % MON_PAGE_COUNT);

    const mon_page_ui_t *out = s_pages[s_page_prev].ui();
    const mon_page_ui_t *in = s_pages[s_page_next].ui();
    lv_obj_t *out_root = s_pages[s_page_prev].root;
    lv_obj_t *in_root = s_pages[s_page_next].root;

    s_pages[s_page_next].update(sampler_get());

    /* keep the tag targets for the glitch */
    const char *out_tag = lv_label_get_text(out->tag_label);
    const char *in_tag = lv_label_get_text(in->tag_label);
    snprintf(s_tag_out, sizeof(s_tag_out), "%s",
             out_tag != NULL ? out_tag : "");
    snprintf(s_tag_in, sizeof(s_tag_in), "%s",
             in_tag != NULL ? in_tag : "");

    lv_obj_clear_flag(out_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(in_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(in_root);

    /* incoming: start collapsed below / transparent */
    tag_state(in, LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(in->hero_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_transform_zoom(in->hero_box, 64, 0);
    lv_obj_set_style_opa(in->stage, LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(in->note, LV_OPA_TRANSP, 0);
    lv_obj_set_style_translate_y(in->stage, 6, 0);
    lv_obj_set_style_translate_y(in->note, 6, 0);
    lv_obj_set_style_opa(in->sub_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_translate_y(in->sub_row, 84, 0);

    /* outgoing: start clean */
    tag_state(out, LV_OPA_COVER, 0);
    lv_label_set_text(out->tag_label, s_tag_out);
    lv_obj_set_style_opa(out->hero_box, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_zoom(out->hero_box, 256, 0);
    lv_obj_set_style_opa(out->stage, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(out->note, LV_OPA_COVER, 0);
    lv_obj_set_style_translate_y(out->stage, 0, 0);
    lv_obj_set_style_translate_y(out->note, 0, 0);
    lv_obj_set_style_opa(out->sub_row, LV_OPA_COVER, 0);
    lv_obj_set_style_translate_y(out->sub_row, 0, 0);
    lv_label_set_text(in->tag_label, s_tag_in);

    s_layer.out = out;
    s_layer.in = in;

    /* the pill slides to the next index along with the layer animation */
    s_dot_from = s_dot_pos;
    s_dot_to = (float)s_page_next;

    lv_anim_del(&s_layer, layer_anim_cb);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &s_layer);
    lv_anim_set_exec_cb(&anim, layer_anim_cb);
    lv_anim_set_values(&anim, 0, 1000);
    lv_anim_set_time(&anim, MON_TRANS_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_set_ready_cb(&anim, layer_ready);
    lv_anim_start(&anim);

    lv_obj_invalidate(out_root);
    lv_obj_invalidate(in_root);
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

static void carousel_tick_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_transitioning) return;
    if (lv_tick_elaps(s_page_start) < MON_PAGE_MS) return;
    start_transition(1);
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

        /* zoom the hero number around its own centre */
        const mon_page_ui_t *ui = s_pages[i].ui();
        lv_obj_set_style_transform_pivot_x(ui->hero_box, LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(ui->hero_box, LV_PCT(50), 0);
    }

    /* page dots */
    s_dot_step = LV_MAX(8, (lv_coord_t)(d * 46 / 1000));
    s_dot_size = LV_MAX(3, (lv_coord_t)(d / 90));
    s_dot_pill_w = LV_MAX(8, s_dot_step * 4 / 5);

    lv_coord_t dots_w = (MON_PAGE_COUNT - 1) * s_dot_step +
                        s_dot_pill_w + 14;
    lv_coord_t dots_h = s_dot_size + 10;

    s_dots = lv_obj_create(s_dial);
    lv_obj_set_size(s_dots, dots_w, dots_h);
    lv_obj_align(s_dots, LV_ALIGN_TOP_MID, 0, m->dots_y - dots_h / 2);
    lv_obj_set_style_bg_opa(s_dots, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_dots, 0, 0);
    lv_obj_set_style_pad_all(s_dots, 0, 0);
    lv_obj_set_style_radius(s_dots, 0, 0);
    lv_obj_clear_flag(s_dots,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_dots, dots_draw_cb, LV_EVENT_DRAW_MAIN_BEGIN, NULL);

    /* reveal page 0 */
    s_page_idx = 0;
    lv_obj_clear_flag(s_pages[0].root, LV_OBJ_FLAG_HIDDEN);
    s_pages[0].update(sampler_get());
    s_dot_pos = 0.0f;
    s_page_start = lv_tick_get();
}

void lv_monitor_show(void)
{
    if (s_started) return;
    s_started = true;

    sampler_init();
    mon_ui_init();
    build_chrome();

    touch_set_gesture_cb(monitor_gesture_cb);
    lv_timer_create(carousel_tick_cb, 250, NULL);
    lv_timer_create(data_timer_cb, 1000, NULL);
}

void lv_monitor_set_pc_data(const pc_data_t *data)
{
    sampler_set_pc_data(data);
}
