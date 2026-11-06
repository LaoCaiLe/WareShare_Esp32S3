#include "monitor.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_settings.h"
#include "common.h"
#include "net_config.h"
#include "page.h"
#include "sampler.h"
#include "theme.h"
#include "time_keeper.h"
#include "touch.h"
#include "weather.h"
#include "wifi_info.h"

/* Montserrat 700 at 14 px for the settings option names. */
LV_FONT_DECLARE(font_option_bold);
/* Montserrat 700 at 10 px for the small captions, reused by the link notice. */
LV_FONT_DECLARE(font_cap_bold);
/* Montserrat 700 at 48 px — digits and colon only, for the info clock. */
LV_FONT_DECLARE(font_clock_bold);
/* Montserrat 700 at 40 px — digits, minus and degree, for the temperature. */
LV_FONT_DECLARE(font_temp_bold);

static monitor_page_t s_pages[MON_PAGE_COUNT] = {
    { MON_TEXT,        page_overview_create, page_overview_update,
      page_overview_ui, NULL, page_overview_enter },
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
static uint16_t s_page_ms = MON_PAGE_MS;   /* runtime, SETTINGS can change */
static uint16_t s_trans_ms = MON_TRANS_MS;
static bool s_auto_on = true;              /* auto carousel, SETTINGS */
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
    int32_t total = (int32_t)s_trans_ms;
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

    /* Wipe the incoming chart / progress bar in from the left: the opaque
     * cover starts over the whole stage and slides off its right edge. */
    if (a->in->stage_cover != NULL) {
        lv_obj_set_x(a->in->stage_cover,
                     lerp_i(0, lv_obj_get_width(a->in->stage), stage_in_p));
    }

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
    if (in->stage_cover != NULL) {
        lv_obj_add_flag(in->stage_cover, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(in->stage_cover, 0);
    }

    lv_obj_add_flag(out_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(in_root, LV_OBJ_FLAG_HIDDEN);

    s_page_idx = s_page_next;
    s_page_start = lv_tick_get();
    s_transitioning = false;
    s_dot_pos = (float)s_page_idx;
    lv_obj_invalidate(s_dots);

    s_pages[s_page_idx].update(sampler_get());
}

/* ══════════════════════════ boot intro ══════════════════════════ */

/* The first page is revealed without a carousel swipe, so it gets the same
 * layered entrance as the incoming half of a page transition: the tag fades
 * in, the stage rises a few pixels while its cover wipes right, and any
 * page-owned entry effect (the overview rings) replays. */

static const mon_page_ui_t *s_intro_ui;
static char s_intro_tag[24];
static uint8_t s_intro_anim;

static void intro_ready(lv_anim_t *anim)
{
    (void)anim;

    const mon_page_ui_t *in = s_intro_ui;
    if (in == NULL) return;

    tag_state(in, LV_OPA_COVER, 0);
    lv_label_set_text(in->tag_label, s_intro_tag);
    lv_obj_set_style_opa(in->hero_box, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_zoom(in->hero_box, 256, 0);
    lv_obj_set_style_opa(in->stage, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(in->note, LV_OPA_COVER, 0);
    lv_obj_set_style_translate_y(in->stage, 0, 0);
    lv_obj_set_style_translate_y(in->note, 0, 0);
    lv_obj_set_style_opa(in->sub_row, LV_OPA_COVER, 0);
    lv_obj_set_style_translate_y(in->sub_row, 0, 0);

    if (in->stage_cover != NULL) {
        lv_obj_add_flag(in->stage_cover, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(in->stage_cover, 0);
    }

    s_intro_ui = NULL;
}

static void intro_anim_cb(void *var, int32_t value)
{
    (void)var;

    const mon_page_ui_t *in = s_intro_ui;
    if (in == NULL) return;

    int32_t total = (int32_t)s_trans_ms;
    int32_t ms = value * total / 1000;

    int32_t enter_start = total * MON_ENTER_PCT / 100;
    int32_t stage_in_start = total * MON_STAGE_IN_PCT / 100;
    int32_t stage_in_end = total * MON_STAGE_IN_END_PCT / 100;

    int32_t in_p = phase_i(ms, enter_start, total);
    int32_t in_e = ease_out_i(in_p);
    lv_opa_t in_opa = (lv_opa_t)(255 * in_e / 1000);

    lv_obj_set_style_opa(in->hero_box, in_opa, 0);
    lv_obj_set_style_transform_zoom(in->hero_box, lerp_i(64, 256, in_e), 0);
    lv_obj_set_style_opa(in->sub_row, in_opa, 0);
    lv_obj_set_style_translate_y(in->sub_row, lerp_i(84, 0, in_e), 0);

    int32_t stage_p = ease_out_i(phase_i(ms, stage_in_start, stage_in_end));
    lv_opa_t stage_opa = (lv_opa_t)(255 * stage_p / 1000);
    lv_obj_set_style_opa(in->stage, stage_opa, 0);
    lv_obj_set_style_opa(in->note, stage_opa, 0);
    lv_obj_set_style_translate_y(in->stage, lerp_i(6, 0, stage_p), 0);
    lv_obj_set_style_translate_y(in->note, lerp_i(6, 0, stage_p), 0);

    if (in->stage_cover != NULL) {
        lv_obj_set_x(in->stage_cover,
                     lerp_i(0, lv_obj_get_width(in->stage), stage_p));
    }

    if (ms >= enter_start) {
        int32_t jitter = ((value / 60 + 1) % 3) - 1;
        tag_state(in, in_opa, jitter);
    }
    else {
        tag_state(in, LV_OPA_TRANSP, 0);
    }

    int32_t step = value * 20 / 1000;
    if (step != s_glitch_step) {
        s_glitch_step = step;
        if (ms >= enter_start) {
            int32_t reveal = (ms - enter_start) * 100 /
                             (total - enter_start);
            glitch_label(in->tag_label, s_intro_tag, reveal, step);
        }
    }

    if (value >= 1000) {
        lv_label_set_text(in->tag_label, s_intro_tag);
    }
}

/* A swipe during the intro takes over: restore the page to its settled look
 * before start_transition() snapshots the outgoing tag text. */
static void intro_cancel(void)
{
    if (s_intro_ui == NULL) return;
    lv_anim_del(&s_intro_anim, intro_anim_cb);
    intro_ready(NULL);
}

static void intro_page(const mon_page_ui_t *in)
{
    const char *tag = lv_label_get_text(in->tag_label);
    snprintf(s_intro_tag, sizeof(s_intro_tag), "%s", tag != NULL ? tag : "");

    tag_state(in, LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(in->hero_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_transform_zoom(in->hero_box, 64, 0);
    lv_obj_set_style_opa(in->stage, LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(in->note, LV_OPA_TRANSP, 0);
    lv_obj_set_style_translate_y(in->stage, 6, 0);
    lv_obj_set_style_translate_y(in->note, 6, 0);
    lv_obj_set_style_opa(in->sub_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_translate_y(in->sub_row, 84, 0);

    if (in->stage_cover != NULL) {
        lv_obj_clear_flag(in->stage_cover, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(in->stage_cover, 0);
        lv_obj_move_foreground(in->stage_cover);
    }

    s_intro_ui = in;
    s_glitch_step = -1;

    lv_anim_del(&s_intro_anim, intro_anim_cb);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &s_intro_anim);
    lv_anim_set_exec_cb(&anim, intro_anim_cb);
    lv_anim_set_values(&anim, 0, 1000);
    lv_anim_set_time(&anim, s_trans_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_set_ready_cb(&anim, intro_ready);
    lv_anim_start(&anim);
}

static void start_transition(int8_t dir)
{
    if (s_transitioning) return;

    intro_cancel();

    s_transitioning = true;
    s_glitch_step = -1;

    s_page_prev = s_page_idx;
    s_page_next = (uint8_t)((s_page_idx + MON_PAGE_COUNT + dir) % MON_PAGE_COUNT);

    const mon_page_ui_t *out = s_pages[s_page_prev].ui();
    const mon_page_ui_t *in = s_pages[s_page_next].ui();
    lv_obj_t *out_root = s_pages[s_page_prev].root;
    lv_obj_t *in_root = s_pages[s_page_next].root;

    s_pages[s_page_next].update(sampler_get());

    /* Pages with their own entry effect replay it while the wipe runs. */
    if (s_pages[s_page_next].enter != NULL) {
        s_pages[s_page_next].enter();
    }

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

    /* the incoming stage starts fully covered; layer_anim_cb wipes it open */
    if (out->stage_cover != NULL) {
        lv_obj_add_flag(out->stage_cover, LV_OBJ_FLAG_HIDDEN);
    }
    if (in->stage_cover != NULL) {
        lv_obj_clear_flag(in->stage_cover, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(in->stage_cover, 0);
        lv_obj_move_foreground(in->stage_cover);
    }

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
    lv_anim_set_time(&anim, s_trans_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_set_ready_cb(&anim, layer_ready);
    lv_anim_start(&anim);

    lv_obj_invalidate(out_root);
    lv_obj_invalidate(in_root);
}

/* ══════════════════════════ settings overlay ══════════════════════════ */

/*
 * A full-screen sheet that slides up from the bottom. Swipe up twice from the
 * clock — once to the data pages, once more for this sheet — then swipe left
 * or right to dismiss; there is no CLOSE button. Inside the sheet the vertical
 * swipe belongs to the list, so scrolling and dismissing can never fight over
 * the same gesture. (Swipe down belongs to the clock overlay, so the two
 * sheets never compete.)
 *
 * Every setting lives in one vertically scrolling list. It snaps to whole
 * rows, and each row scales and fades with its distance from the centre, so
 * the setting you are touching reads as the selected one, watchOS style.
 *
 * Each numeric setting is a colour-coded dot, a caption, a right-aligned
 * value and a slider. Only the knob starts a drag; the track itself is inert,
 * so a finger that grazes a slider while scrolling the list cannot change the
 * value. The knob gets a 10 px extended click area so it stays comfortable
 * under a finger. Values apply on every drag step; only the release writes
 * NVS, so one gesture costs one flash write.
 *
 * Dragging a slider moves the finger mostly horizontally, but diagonally
 * enough to look like a swipe on release. The touch layer reports that
 * release before LVGL does, so each slider tracks the pointer itself and
 * marks a real horizontal drag; the gesture handler then ignores the dismiss
 * swipe for that gesture.
 *
 * A vertical drag that starts on a slider would make LVGL jump the value to
 * the finger's x on the first move. The slider snapshots the numeric settings
 * on press; if the drag turns into a list scroll, SCROLL_BEGIN puts them
 * back before anything is drawn.
 */

#define SETTINGS_ANIM_MS      180
#define SETTINGS_LIST_Y       48
#define SETTINGS_LIST_H       146
#define SETTINGS_ROW_W        196
#define SETTINGS_ROW_H        58
#define SETTINGS_ROW_GAP      6
#define SETTINGS_ROW_COUNT    7
#define SETTINGS_SLIDER_COUNT 4
#define SETTINGS_TRACK_BG     0x1B2533
#define SETTINGS_DRAG_MIN_X   12
#define SETTINGS_ZOOM_MAX     297   /* 116 % (LVGL zoom: 256 = 100 %) */
#define SETTINGS_ZOOM_MIN     174   /* 68 % */
#define SETTINGS_OPA_MIN      130
#define SETTINGS_SLEEP_FADE   300   /* backlight fade on sleep / wake, ms */

static lv_obj_t *s_settings;
static lv_obj_t *s_set_list;
static lv_obj_t *s_set_row[SETTINGS_ROW_COUNT];
static lv_obj_t *s_set_slider[SETTINGS_SLIDER_COUNT];
static lv_obj_t *s_set_value[SETTINGS_SLIDER_COUNT];
static lv_obj_t *s_set_wifi;
static lv_obj_t *s_set_auto;
static lv_obj_t *s_set_rotate;
static bool s_set_slider_drag;
static bool s_set_slider_snap;
static uint8_t s_set_snap[SETTINGS_SLIDER_COUNT];
static lv_coord_t s_set_press_x;
static bool s_settings_open;
static bool s_info_open;

/* Idle screen-off: after sleep_min minutes without a touch the backlight
 * fades out; the next touch fades it back in and is swallowed so it cannot
 * page or open the settings by accident. */
static bool s_screen_off;
static bool s_ignore_gesture;
static uint32_t s_sleep_activity;
static uint32_t s_wake_ms;
static uint8_t s_bl_anim;
static uint8_t s_bl_level;

/* Minutes behind each SLEEP slider step; 0 means never. */
static const uint8_t s_sleep_choices[5] = { 0, 3, 5, 10, 30 };

static uint8_t sleep_minutes_from_index(int32_t idx)
{
    if (idx < 0) idx = 0;
    if (idx > 4) idx = 4;
    return s_sleep_choices[idx];
}

static int32_t sleep_index_from_minutes(uint8_t minutes)
{
    for (int i = 4; i > 0; i--) {
        if (minutes >= s_sleep_choices[i]) return i;
    }
    return 0;
}

static void settings_value_text(int idx, int32_t v)
{
    char buf[12];

    if (idx == 0) {
        snprintf(buf, sizeof(buf), "%d", (int)v);
    } else if (idx == 1) {
        snprintf(buf, sizeof(buf), "%ds", (int)v);
    } else if (idx == 2) {
        snprintf(buf, sizeof(buf), "%dms", (int)(v * 100));
    } else {
        uint8_t minutes = sleep_minutes_from_index(v);
        if (minutes == 0) {
            snprintf(buf, sizeof(buf), "NEVER");
        } else {
            snprintf(buf, sizeof(buf), "%um", (unsigned)minutes);
        }
    }
    lv_label_set_text(s_set_value[idx], buf);
}

static void settings_refresh(void)
{
    const app_settings_t *cfg = app_settings_get();
    int32_t vals[SETTINGS_SLIDER_COUNT];

    vals[0] = (int32_t)cfg->brightness;
    vals[1] = (int32_t)(cfg->page_ms / 1000U);
    vals[2] = (int32_t)(cfg->trans_ms / 100U);
    vals[3] = sleep_index_from_minutes(cfg->sleep_min);

    for (int i = 0; i < SETTINGS_SLIDER_COUNT; i++) {
        lv_slider_set_value(s_set_slider[i], vals[i], LV_ANIM_OFF);
        settings_value_text(i, vals[i]);
    }

    if (cfg->wifi_on) {
        lv_obj_add_state(s_set_wifi, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_set_wifi, LV_STATE_CHECKED);
    }

    if (cfg->auto_on) {
        lv_obj_add_state(s_set_auto, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_set_auto, LV_STATE_CHECKED);
    }

    if (cfg->rotation) {
        lv_obj_add_state(s_set_rotate, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_set_rotate, LV_STATE_CHECKED);
    }
}

/* Scale each row by how far it is from the centre of the viewport. The row
 * in the middle reads at full size, its neighbours shrink and dim, and the
 * rest stay at the minimum. Called on every scroll step, so it tracks the
 * snap animation smoothly. */
static void settings_rows_scale_update(void)
{
    if (s_set_list == NULL) return;

    lv_area_t list_area;
    lv_obj_get_coords(s_set_list, &list_area);
    lv_coord_t center = (list_area.y1 + list_area.y2) / 2;

    /* Two row steps take a row from full size down to the minimum. */
    const lv_coord_t span = (SETTINGS_ROW_H + SETTINGS_ROW_GAP) * 2;

    for (int i = 0; i < SETTINGS_ROW_COUNT; i++) {
        lv_obj_t *row = s_set_row[i];
        if (row == NULL) continue;

        lv_area_t row_area;
        lv_obj_get_coords(row, &row_area);
        lv_coord_t row_center = (row_area.y1 + row_area.y2) / 2;

        int32_t t = LV_ABS(row_center - center) * 100 / span;
        if (t > 100) t = 100;

        int32_t zoom = SETTINGS_ZOOM_MAX -
                       ((SETTINGS_ZOOM_MAX - SETTINGS_ZOOM_MIN) * t) / 100;
        lv_opa_t opa = (lv_opa_t)(255 -
                       ((255 - SETTINGS_OPA_MIN) * t) / 100);

        lv_obj_set_style_transform_zoom(row, (lv_coord_t)zoom, 0);
        lv_obj_set_style_opa(row, opa, 0);
    }
}

static void settings_scroll_cb(lv_event_t *e)
{
    (void)e;
    settings_rows_scale_update();
}

static void settings_scroll_begin_cb(lv_event_t *e)
{
    (void)e;
    if (!s_set_slider_snap) return;
    s_set_slider_snap = false;

    /* The press became a list scroll: LVGL had already moved the slider to
     * the finger's x. Restore the real values before the frame is drawn. */
    app_settings_apply_brightness(s_set_snap[0]);
    app_settings_apply_page_ms((uint16_t)s_set_snap[1] * 1000U);
    app_settings_apply_trans_ms((uint16_t)s_set_snap[2] * 100U);
    app_settings_apply_sleep(s_set_snap[3]);
    settings_refresh();
}

static void settings_scroll_end_cb(lv_event_t *e)
{
    (void)e;
    /* The slider's fill may have followed the finger during the scroll even
     * though the setting stayed put; snap it back to the real value. */
    if (!s_set_slider_drag) settings_refresh();
}

/* Called while the slider is held. A mostly-horizontal drag is a slider
 * gesture, not a page swipe; remember that so the release cannot flip the
 * settings page or close the sheet. */
static void settings_slider_press_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();

    if (code == LV_EVENT_PRESSED) {
        const app_settings_t *cfg = app_settings_get();

        s_set_slider_drag = false;
        s_set_snap[0] = cfg->brightness;
        s_set_snap[1] = (uint8_t)(cfg->page_ms / 1000U);
        s_set_snap[2] = (uint8_t)(cfg->trans_ms / 100U);
        s_set_snap[3] = cfg->sleep_min;
        s_set_slider_snap = true;

        if (indev != NULL &&
            lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            s_set_press_x = p.x;
        }
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        if (!s_set_slider_drag && indev != NULL &&
            lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            if (LV_ABS(p.x - s_set_press_x) >= SETTINGS_DRAG_MIN_X) {
                s_set_slider_drag = true;
            }
        }
        return;
    }

    /* RELEASED / PRESS_LOST. The touch timer reports the swipe before LVGL
     * delivers this event, so the flag has already been read by then. */
    s_set_slider_drag = false;
    s_set_slider_snap = false;
}

static void settings_slider_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();

    /* A vertical drag that started on the slider is scrolling the list. The
     * slider follows the finger's x internally, but the setting must not. */
    if (indev != NULL && lv_indev_get_scroll_obj(indev) != NULL) return;

    lv_obj_t *slider = lv_event_get_target(e);
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    int32_t v = lv_slider_get_value(slider);

    /* Apply live so the screen reacts under the finger ... */
    if (idx == 0) {
        app_settings_apply_brightness((uint8_t)v);
    } else if (idx == 1) {
        app_settings_apply_page_ms((uint16_t)(v * 1000));
    } else if (idx == 2) {
        app_settings_apply_trans_ms((uint16_t)(v * 100));
    } else {
        app_settings_apply_sleep(sleep_minutes_from_index(v));
    }

    settings_value_text(idx, v);

    /* ... and persist once, when the finger lifts. */
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        if (idx == 0) {
            app_settings_set_brightness((uint8_t)v);
        } else if (idx == 1) {
            app_settings_set_page_ms((uint16_t)(v * 1000));
        } else if (idx == 2) {
            app_settings_set_trans_ms((uint16_t)(v * 100));
        } else {
            app_settings_set_sleep(sleep_minutes_from_index(v));
        }
    }
}

static void settings_wifi_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    app_settings_set_wifi(lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0);
}

static void settings_auto_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    app_settings_set_auto(lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0);
}

static void settings_rotate_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    app_settings_set_rotation(lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0);
}

static void settings_sheet_anim_cb(void *var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, (lv_coord_t)v);
}

static void settings_sheet_hidden_cb(lv_anim_t *a)
{
    (void)a;
    if (!s_settings_open && s_settings != NULL) {
        lv_obj_add_flag(s_settings, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(s_settings, 0);
        /* Parked below the screen: an upward swipe pulls the sheet in. */
        lv_obj_set_y(s_settings, lv_obj_get_height(s_settings));
    }
}

static void settings_slide_to(int32_t y, lv_anim_ready_cb_t ready_cb,
                              lv_anim_path_cb_t path_cb)
{
    lv_anim_del(s_settings, settings_sheet_anim_cb);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_settings);
    lv_anim_set_exec_cb(&anim, settings_sheet_anim_cb);
    lv_anim_set_values(&anim, lv_obj_get_y(s_settings), y);
    lv_anim_set_time(&anim, SETTINGS_ANIM_MS);
    lv_anim_set_path_cb(&anim, path_cb);
    lv_anim_set_ready_cb(&anim, ready_cb);
    lv_anim_start(&anim);
}

static void settings_slide_x_cb(void *var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, (lv_coord_t)v);
}

static void settings_close(int8_t dir)
{
    if (!s_settings_open || s_settings == NULL) return;
    s_settings_open = false;
    s_page_start = lv_tick_get();   /* a full page dwell after leaving */

    /* Slide out the way the finger went, rather than dropping the sheet. */
    lv_anim_del(s_settings, NULL);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_settings);
    lv_anim_set_exec_cb(&anim, settings_slide_x_cb);
    lv_anim_set_values(&anim, lv_obj_get_x(s_settings),
                       (int32_t)dir * lv_obj_get_width(s_settings));
    lv_anim_set_time(&anim, SETTINGS_ANIM_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&anim, settings_sheet_hidden_cb);
    lv_anim_start(&anim);
}

static void settings_open(void)
{
    if (s_settings_open || s_settings == NULL) return;
    s_settings_open = true;
    settings_refresh();

    lv_obj_set_x(s_settings, 0);
    lv_obj_set_y(s_settings, lv_obj_get_height(s_settings));
    lv_obj_clear_flag(s_settings, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_settings);

    /* Start from the first row, centred, and light up its scale. */
    lv_obj_update_layout(s_settings);
    lv_obj_scroll_to_y(s_set_list, 0, LV_ANIM_OFF);
    settings_rows_scale_update();

    settings_slide_to(0, NULL, lv_anim_path_ease_out);
}

/* ─────────────── idle screen-off (SLEEP setting) ─────────────── */

static void backlight_anim_cb(void *var, int32_t v)
{
    (void)var;
    s_bl_level = (uint8_t)v;
    app_settings_apply_backlight_raw(s_bl_level);
}

static void backlight_fade(uint8_t from, uint8_t to)
{
    if (from == to) {
        backlight_anim_cb(NULL, to);
        return;
    }

    lv_anim_del(&s_bl_anim, NULL);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &s_bl_anim);
    lv_anim_set_exec_cb(&anim, backlight_anim_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_time(&anim, SETTINGS_SLEEP_FADE);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_start(&anim);
}

static void link_pulse_stop(void);
static void link_status_update(void);
static void info_refresh(void);

static void screen_sleep(void)
{
    if (s_screen_off) return;
    s_screen_off = true;

    /* The breathing notice would otherwise keep invalidating the label while
     * the LCD is supposed to be idle. It restarts on wake. */
    link_pulse_stop();

    s_sleep_activity = touch_last_activity_ms();
    backlight_fade(app_settings_get()->brightness, 0);
}

static void screen_wake(void)
{
    if (!s_screen_off) return;
    s_screen_off = false;
    link_status_update();

    /* The clock may have slept for minutes: catch it up before the backlight
     * fades in. The ring freezes while the screen is off, so the invalidation
     * only happens here and then once per second again. */
    if (s_info_open) info_refresh();

    /* The touch that woke the panel must not page or open settings too. */
    s_ignore_gesture = true;
    s_wake_ms = lv_tick_get();
    s_page_start = lv_tick_get();

    backlight_fade(s_bl_level, app_settings_get()->brightness);
}

static void idle_tick_cb(lv_timer_t *timer)
{
    (void)timer;

    /* A missed release (a tap shorter than the sample threshold) must not
     * leave the guard armed forever. */
    if (s_ignore_gesture && lv_tick_elaps(s_wake_ms) > 1500U) {
        s_ignore_gesture = false;
    }

    if (s_screen_off) {
        if (touch_last_activity_ms() != s_sleep_activity) screen_wake();
        return;
    }

    uint8_t sleep_min = app_settings_get()->sleep_min;
    /* The clock is the home screen, not an overlay: it honours the sleep
     * timeout like any other page, or the setting would never trigger. */
    if (sleep_min == 0U || s_settings_open) return;

    if (lv_tick_elaps(touch_last_activity_ms()) >=
        (uint32_t)sleep_min * 60000U) {
        screen_sleep();
    }
}

/* One row in the scrolling list. The row itself is transparent — it only
 * carries its children and the watchOS-style zoom. */
static lv_obj_t *settings_make_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, SETTINGS_ROW_W, SETTINGS_ROW_H);
    /* Vertical drags belong to the list, never to the row. */
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    /* Zoom around the row's own centre, not its top-left corner. */
    lv_obj_set_style_transform_pivot_x(row, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(row, LV_PCT(50), 0);
    return row;
}

static lv_obj_t *settings_make_label(lv_obj_t *parent, const char *text,
                                     const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static void settings_make_dot(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                              uint32_t color)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_pos(dot, x, y);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
}

static lv_obj_t *settings_make_slider(lv_obj_t *row, int idx,
                                      int32_t min, int32_t max,
                                      int32_t value, uint32_t accent)
{
    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_size(slider, SETTINGS_ROW_W - 20, 8);
    lv_obj_set_pos(slider, 10, 33);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    /* The track is only 8 px tall; widen the touch target vertically. */
    lv_obj_set_ext_click_area(slider, 10);
    /* lv_slider ships a hit test that only accepts the knob, but it is
     * dormant unless advanced hit testing is enabled. With it the track
     * stops swallowing presses, so a swipe that grazes the slider scrolls
     * the list instead of jumping the value; only a touch on the knob (plus
     * the extended click area) starts a drag. */
    lv_obj_add_flag(slider, LV_OBJ_FLAG_ADV_HITTEST);
    /* lv_slider clears SCROLL_CHAIN on creation. Without it a vertical drag
     * that lands on the track can never reach the list, so the list would
     * feel dead wherever a slider sits. */
    lv_obj_add_flag(slider, LV_OBJ_FLAG_SCROLL_CHAIN);

    lv_obj_set_style_bg_color(slider, lv_color_hex(SETTINGS_TRACK_BG),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN);

    lv_obj_set_style_bg_color(slider, lv_color_hex(accent),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    lv_obj_set_style_bg_color(slider, lv_color_hex(MON_TEXT), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 3, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_KNOB);

    lv_obj_add_event_cb(slider, settings_slider_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)idx);
    lv_obj_add_event_cb(slider, settings_slider_cb, LV_EVENT_RELEASED,
                        (void *)(uintptr_t)idx);
    lv_obj_add_event_cb(slider, settings_slider_press_cb, LV_EVENT_PRESSED,
                        NULL);
    lv_obj_add_event_cb(slider, settings_slider_press_cb, LV_EVENT_PRESSING,
                        NULL);
    lv_obj_add_event_cb(slider, settings_slider_press_cb, LV_EVENT_RELEASED,
                        NULL);
    lv_obj_add_event_cb(slider, settings_slider_press_cb, LV_EVENT_PRESS_LOST,
                        NULL);

    return slider;
}

static lv_obj_t *settings_make_switch(lv_obj_t *row, const char *caption,
                                      uint32_t accent, lv_event_cb_t cb)
{
    settings_make_dot(row, 10, 26, accent);

    lv_obj_t *label = settings_make_label(row, caption,
                                          &font_option_bold,
                                          MON_TEXT);
    lv_obj_set_pos(label, 22, 21);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 42, 24);
    lv_obj_set_pos(sw, 144, 17);
    lv_obj_set_style_bg_color(sw, lv_color_hex(SETTINGS_TRACK_BG), 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sw, 0, 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(accent),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(MON_TEXT), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    /* Theme default is -4; restore it explicitly after removing shadows. */
    lv_obj_set_style_pad_all(sw, -4, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(sw, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);

    return sw;
}

static void settings_build(lv_obj_t *scr)
{
    const mon_ui_metrics_t *m = mon_ui();
    const app_settings_t *cfg = app_settings_get();

    static const char *const caps[SETTINGS_SLIDER_COUNT] = {
        "BRIGHT", "PAGE", "TRANS", "SLEEP",
    };
    static const uint32_t accents[SETTINGS_SLIDER_COUNT] = {
        MON_ACCENT_DISK, MON_ACCENT_MEM, MON_ACCENT_GPU, MON_ACCENT_SYS,
    };
    static const int32_t mins[SETTINGS_SLIDER_COUNT] = {
        APP_BRIGHT_MIN, APP_PAGE_MIN / 1000, APP_TRANS_MIN / 100, 0,
    };
    static const int32_t maxs[SETTINGS_SLIDER_COUNT] = {
        APP_BRIGHT_MAX, APP_PAGE_MAX / 1000, APP_TRANS_MAX / 100, 4,
    };

    s_settings = lv_obj_create(scr);
    lv_obj_set_size(s_settings, m->disp_w, m->disp_h);
    /* Parked below the screen: opening slides the sheet up. */
    lv_obj_set_pos(s_settings, 0, m->disp_h);
    lv_obj_set_style_bg_color(s_settings, lv_color_hex(MON_BG), 0);
    lv_obj_set_style_bg_opa(s_settings, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_settings, 0, 0);
    lv_obj_set_style_radius(s_settings, 0, 0);
    lv_obj_set_style_pad_all(s_settings, 0, 0);
    lv_obj_clear_flag(s_settings, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_settings, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_settings, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = settings_make_label(s_settings, "SETTINGS",
                                          &lv_font_montserrat_16, MON_TEXT);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    /* Hairline divider with a short accent notch in the middle. */
    lv_obj_t *rule = lv_obj_create(s_settings);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, 120, 1);
    lv_obj_set_pos(rule, 60, 42);
    lv_obj_set_style_bg_color(rule, lv_color_hex(MON_RING), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *notch = lv_obj_create(s_settings);
    lv_obj_remove_style_all(notch);
    lv_obj_set_size(notch, 28, 2);
    lv_obj_set_pos(notch, 106, 41);
    lv_obj_set_style_bg_color(notch, lv_color_hex(MON_ACCENT_SYS), 0);
    lv_obj_set_style_bg_opa(notch, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(notch, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(notch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(notch, LV_OBJ_FLAG_SCROLLABLE);

    /* One list under the shared title, clipped to the rounded part of the
     * glass. Rows snap to its centre; pad_top/pad_bottom equal (list − row)/2
     * so the first and last row can both reach the centre. */
    s_set_list = lv_obj_create(s_settings);
    lv_obj_remove_style_all(s_set_list);
    lv_obj_set_size(s_set_list, m->disp_w, SETTINGS_LIST_H);
    lv_obj_set_pos(s_set_list, 0, SETTINGS_LIST_Y);
    lv_obj_set_style_pad_top(s_set_list,
                             (SETTINGS_LIST_H - SETTINGS_ROW_H) / 2, 0);
    lv_obj_set_style_pad_bottom(s_set_list,
                                (SETTINGS_LIST_H - SETTINGS_ROW_H) / 2, 0);
    lv_obj_set_style_pad_row(s_set_list, SETTINGS_ROW_GAP, 0);
    lv_obj_set_scroll_dir(s_set_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_set_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_y(s_set_list, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_flex_flow(s_set_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_set_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(s_set_list, settings_scroll_cb, LV_EVENT_SCROLL,
                        NULL);
    lv_obj_add_event_cb(s_set_list, settings_scroll_begin_cb,
                        LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(s_set_list, settings_scroll_end_cb,
                        LV_EVENT_SCROLL_END, NULL);

    int32_t values[SETTINGS_SLIDER_COUNT] = {
        (int32_t)cfg->brightness,
        (int32_t)(cfg->page_ms / 1000U),
        (int32_t)(cfg->trans_ms / 100U),
        sleep_index_from_minutes(cfg->sleep_min),
    };

    /* Rows 0-2 — on/off switches first: they need one tap, while the
     * sliders below reward scrolling past the everyday toggles. */
    s_set_row[0] = settings_make_row(s_set_list);
    s_set_wifi = settings_make_switch(s_set_row[0], "WIFI",
                                      MON_ACCENT_NET, settings_wifi_cb);

    s_set_row[1] = settings_make_row(s_set_list);
    s_set_auto = settings_make_switch(s_set_row[1], "AUTO TR",
                                      MON_ACCENT_SYS, settings_auto_cb);

    s_set_row[2] = settings_make_row(s_set_list);
    s_set_rotate = settings_make_switch(s_set_row[2], "ROTATE",
                                        MON_ACCENT_CPU, settings_rotate_cb);

    /* Rows 3-6 — numeric sliders */
    for (int i = 0; i < SETTINGS_SLIDER_COUNT; i++) {
        lv_obj_t *row = settings_make_row(s_set_list);
        s_set_row[3 + i] = row;

        settings_make_dot(row, 10, 14, accents[i]);

        lv_obj_t *cap = settings_make_label(row, caps[i],
                                            &font_option_bold,
                                            MON_TEXT_DIM);
        lv_obj_set_pos(cap, 22, 9);

        lv_obj_t *value = settings_make_label(row, "--",
                                              &lv_font_montserrat_14,
                                              accents[i]);
        lv_obj_set_width(value, 60);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(value, 126, 9);
        s_set_value[i] = value;

        s_set_slider[i] = settings_make_slider(row, i, mins[i], maxs[i],
                                               values[i], accents[i]);
    }

    settings_refresh();
}

/* ══════════════════════════ info overlay ══════════════════════════ */

/*
 * The watch face — and the home screen. It is on screen at boot and shows the
 * keeper clock (PC frame timestamp, SNTP, or the NVS/RTC time restored at
 * boot), today's Open-Meteo reading and a WiFi strength glyph. Swiping up lifts the face away to the
 * data pages, whose next upward swipe opens SETTINGS; swiping down from the
 * pages drops the clock back over them. Nothing inside scrolls, so the
 * vertical gesture is unambiguous.
 */

#define INFO_ANIM_MS 180

/* Weather icon palette. These are the only saturated colours on the sheet,
 * so the page keeps the quiet black canvas of a watch face. */
#define INFO_ICON_CLOUD  0xE9EFF8
#define INFO_ICON_RAIN   MON_ACCENT_MEM
#define INFO_ICON_SNOW   0xCFE4FF
#define INFO_ICON_FOG    0x9FB3CC
#define INFO_ICON_DIM    MON_RING_BRIGHT

typedef enum {
    INFO_WICON_CLOUD,
    INFO_WICON_SUN,
    INFO_WICON_PARTLY,
    INFO_WICON_FOG,
    INFO_WICON_RAIN,
    INFO_WICON_SNOW,
    INFO_WICON_STORM,
} info_wicon_t;

static lv_obj_t *s_info;
static lv_obj_t *s_info_ring;
static lv_obj_t *s_info_time;
static lv_obj_t *s_info_date;
static lv_obj_t *s_info_wicon;
static lv_obj_t *s_info_weather;
static lv_obj_t *s_info_desc;
static lv_obj_t *s_info_hl;
static lv_obj_t *s_info_wifi_icon;

static info_wicon_t s_info_wicon_kind = INFO_WICON_CLOUD;
static bool s_info_wicon_ready;
/* Seconds shown on the bezel ring; -1 while no time source is valid. */
static int16_t s_info_ring_sec = -1;
/* 3 = strong, 2 = fair, 1 = weak, -1 = no link. */
static int8_t s_info_wifi_level = -1;
static uint32_t s_info_wifi_color = MON_RING_BRIGHT;

/* ── weather icon artwork ──
 * Drawn from LVGL primitives instead of a bitmap: the icon picks up the
 * sheet's palette, needs no flash and stays crisp at any dial size. */

static void info_draw_rect(lv_draw_ctx_t *ctx, lv_coord_t x1, lv_coord_t y1,
                           lv_coord_t x2, lv_coord_t y2, lv_coord_t radius,
                           lv_color_t color)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = radius;

    lv_area_t area = { x1, y1, x2, y2 };
    lv_draw_rect(ctx, &dsc, &area);
}

static void info_draw_circle(lv_draw_ctx_t *ctx, lv_coord_t cx, lv_coord_t cy,
                             lv_coord_t r, lv_color_t color)
{
    info_draw_rect(ctx, cx - r, cy - r, cx + r, cy + r,
                   LV_RADIUS_CIRCLE, color);
}

static void info_draw_line(lv_draw_ctx_t *ctx, lv_coord_t x1, lv_coord_t y1,
                           lv_coord_t x2, lv_coord_t y2, lv_coord_t width,
                           lv_color_t color)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = width;
    dsc.round_start = 1;
    dsc.round_end = 1;

    lv_point_t p1 = { x1, y1 };
    lv_point_t p2 = { x2, y2 };
    lv_draw_line(ctx, &dsc, &p1, &p2);
}

/* Three bumps over a capsule — the classic cloud silhouette. */
static void info_draw_cloud(lv_draw_ctx_t *ctx, lv_coord_t cx, lv_coord_t cy,
                            lv_coord_t w, lv_color_t color)
{
    lv_coord_t base_h = w * 10 / 32;

    info_draw_rect(ctx, cx - w / 2, cy + w * 3 / 32 - base_h / 2,
                   cx + w / 2, cy + w * 3 / 32 + base_h / 2,
                   base_h / 2, color);
    info_draw_circle(ctx, cx - w * 7 / 32, cy - w * 2 / 32, w * 6 / 32, color);
    info_draw_circle(ctx, cx + w * 2 / 32, cy - w * 5 / 32, w * 9 / 32, color);
    info_draw_circle(ctx, cx + w * 10 / 32, cy + w * 1 / 32, w * 5 / 32, color);
}

static void info_draw_sun(lv_draw_ctx_t *ctx, lv_coord_t cx, lv_coord_t cy,
                          lv_coord_t r, lv_color_t color)
{
    /* cos/sin of 0..315° in 45° steps, fixed point /100. */
    static const int8_t vx[8] = { 100, 71, 0, -71, -100, -71, 0, 71 };
    static const int8_t vy[8] = { 0, 71, 100, 71, 0, -71, -100, -71 };

    info_draw_circle(ctx, cx, cy, r, color);
    for (int i = 0; i < 8; i++) {
        info_draw_line(ctx,
                       cx + vx[i] * (r + 4) / 100,
                       cy + vy[i] * (r + 4) / 100,
                       cx + vx[i] * (r + 9) / 100,
                       cy + vy[i] * (r + 9) / 100,
                       2, color);
    }
}

static void info_draw_flake(lv_draw_ctx_t *ctx, lv_coord_t cx, lv_coord_t cy,
                            lv_coord_t r, lv_color_t color)
{
    info_draw_line(ctx, cx - r, cy, cx + r, cy, 1, color);
    info_draw_line(ctx, cx - r / 2, cy - r * 7 / 8,
                   cx + r / 2, cy + r * 7 / 8, 1, color);
    info_draw_line(ctx, cx - r / 2, cy + r * 7 / 8,
                   cx + r / 2, cy - r * 7 / 8, 1, color);
}

/* One rainbow segment per elapsed second: the bezel turns the minute into a
 * colour ring, while a white bead marks "now" and doubles as the twelve
 * o'clock marker at :00. */
static void info_ring_draw_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN_BEGIN) return;

    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_obj_t *obj = lv_event_get_target(e);

    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    lv_point_t center = { (a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2 };
    uint16_t radius = (uint16_t)((a.x2 - a.x1) / 2) - 2;

    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.width = 3;
    dsc.rounded = 0;

    dsc.color = lv_color_hex(MON_RING);
    dsc.opa = LV_OPA_60;
    lv_draw_arc(ctx, &dsc, &center, radius, 270, 630);

    if (s_info_ring_sec < 0) return;

    dsc.opa = LV_OPA_COVER;
    const lv_area_t *clip_saved = ctx->clip_area;

    for (int16_t i = 0; i < s_info_ring_sec; i++) {
        /* 6° per second; the extra degree closes the seam between
         * neighbours so the ring reads as one continuous gradient. */
        uint16_t start = (uint16_t)(270 + i * 6);
        uint16_t end = (uint16_t)(start + 7);

        /* lv_draw_arc() masks the whole circle even for a 7° slice. Clip
         * each segment to its own arc area so a full minute stays cheap. */
        lv_area_t slice;
        lv_draw_arc_get_area(center.x, center.y, radius, start, end,
                             dsc.width, false, &slice);
        lv_area_t clip;
        if (!_lv_area_intersect(&clip, clip_saved, &slice)) continue;

        dsc.color = lv_color_hsv_to_rgb((uint16_t)(i * 6), 78, 100);
        ctx->clip_area = &clip;
        lv_draw_arc(ctx, &dsc, &center, radius, start, end);
    }
    ctx->clip_area = clip_saved;

    int16_t angle = (int16_t)(270 + s_info_ring_sec * 6);
    lv_coord_t bx = center.x +
                    ((lv_trigo_sin((int16_t)(angle + 90)) * radius) >>
                     LV_TRIGO_SHIFT);
    lv_coord_t by = center.y +
                    ((lv_trigo_sin(angle) * radius) >> LV_TRIGO_SHIFT);
    info_draw_circle(ctx, bx, by, 2, lv_color_white());
}

static void info_wicon_draw_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN_BEGIN) return;

    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_obj_t *obj = lv_event_get_target(e);

    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    lv_coord_t cx = (a.x1 + a.x2) / 2;
    lv_coord_t cy = (a.y1 + a.y2) / 2;

    bool ready = s_info_wicon_ready;
    lv_color_t cloud = lv_color_hex(ready ? INFO_ICON_CLOUD : INFO_ICON_DIM);
    lv_color_t sun = lv_color_hex(ready ? MON_ACCENT_SYS : INFO_ICON_DIM);
    lv_color_t rain = lv_color_hex(ready ? INFO_ICON_RAIN : INFO_ICON_DIM);
    lv_color_t snow = lv_color_hex(ready ? INFO_ICON_SNOW : INFO_ICON_DIM);
    lv_color_t fog = lv_color_hex(ready ? INFO_ICON_FOG : INFO_ICON_DIM);

    switch (s_info_wicon_kind) {
    case INFO_WICON_SUN:
        info_draw_sun(ctx, cx, cy - 1, 7, sun);
        break;
    case INFO_WICON_PARTLY:
        info_draw_sun(ctx, cx - 7, cy - 6, 5, sun);
        info_draw_cloud(ctx, cx + 2, cy + 1, 28, cloud);
        break;
    case INFO_WICON_FOG:
        info_draw_cloud(ctx, cx, cy - 4, 34, cloud);
        info_draw_line(ctx, cx - 11, cy + 9, cx + 9, cy + 9, 2, fog);
        info_draw_line(ctx, cx - 16, cy + 13, cx + 13, cy + 13, 2, fog);
        info_draw_line(ctx, cx - 8, cy + 17, cx + 15, cy + 17, 2, fog);
        break;
    case INFO_WICON_RAIN:
        info_draw_cloud(ctx, cx, cy - 4, 34, cloud);
        info_draw_line(ctx, cx - 6, cy + 8, cx - 8, cy + 14, 2, rain);
        info_draw_line(ctx, cx + 1, cy + 8, cx - 1, cy + 14, 2, rain);
        info_draw_line(ctx, cx + 8, cy + 8, cx + 6, cy + 14, 2, rain);
        break;
    case INFO_WICON_SNOW:
        info_draw_cloud(ctx, cx, cy - 4, 34, cloud);
        info_draw_flake(ctx, cx - 7, cy + 11, 3, snow);
        info_draw_flake(ctx, cx, cy + 13, 3, snow);
        info_draw_flake(ctx, cx + 7, cy + 11, 3, snow);
        break;
    case INFO_WICON_STORM: {
        info_draw_cloud(ctx, cx, cy - 5, 34, cloud);

        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = sun;
        dsc.bg_opa = LV_OPA_COVER;

        lv_point_t bolt[] = {
            { cx + 3, cy + 3 }, { cx - 4, cy + 10 }, { cx, cy + 10 },
            { cx - 3, cy + 17 }, { cx + 5, cy + 8 }, { cx + 1, cy + 8 },
        };
        lv_draw_polygon(ctx, &dsc, bolt, 6);
        break;
    }
    case INFO_WICON_CLOUD:
    default:
        info_draw_cloud(ctx, cx, cy, 34, cloud);
        break;
    }
}

static void info_wifi_draw_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN_BEGIN) return;

    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_obj_t *obj = lv_event_get_target(e);

    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    lv_coord_t cx = (a.x1 + a.x2) / 2;
    lv_coord_t cy = a.y2 - 1;

    lv_color_t on = lv_color_hex(s_info_wifi_color);
    lv_color_t off = lv_color_hex(MON_RING_BRIGHT);

    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.width = 2;
    dsc.rounded = 1;

    lv_point_t center = { cx, cy };
    /* The fan is drawn inside out: a weak link keeps only the inner wave
     * lit, so signal strength reads without a single character of text. */
    static const uint8_t radii[3] = { 4, 8, 12 };
    for (int i = 0; i < 3; i++) {
        dsc.color = (s_info_wifi_level > i) ? on : off;
        lv_draw_arc(ctx, &dsc, &center, radii[i], 225, 315);
    }

    info_draw_circle(ctx, cx, cy, 1, on);

    if (s_info_wifi_level < 0) {
        /* No link at all: a slash says "offline" on its own. */
        info_draw_line(ctx, cx - 7, a.y1 + 2, cx + 7, a.y2 - 1, 2,
                       lv_color_hex(MON_TEXT_DIM));
    }
}

static void info_set_text(lv_obj_t *label, const char *text)
{
    if (label == NULL || text == NULL) return;

    const char *old = lv_label_get_text(label);
    if (old == NULL || strcmp(old, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static lv_obj_t *info_make_plain(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

/* A centered flex row: the icons and their labels find their own spacing,
 * so changing a temperature from 8° to -12° cannot unbalance the line. */
static lv_obj_t *info_make_row(lv_obj_t *parent, lv_coord_t y, lv_coord_t w,
                               lv_coord_t h, lv_coord_t gap)
{
    lv_obj_t *row = info_make_plain(parent);
    lv_obj_set_size(row, w, h);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, gap, 0);
    return row;
}

static lv_obj_t *info_make_label(lv_obj_t *parent, const char *text,
                                 const lv_font_t *font, uint32_t color,
                                 lv_coord_t letter_space)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    if (letter_space != 0) {
        lv_obj_set_style_text_letter_space(label, letter_space, 0);
    }
    return label;
}

static void info_refresh(void)
{
    char buf[56];

    /* ── clock ── */
    uint32_t secs;
    if (time_keeper_now(&secs)) {
        time_t local = (time_t)secs + (time_t)NET_TZ_OFFSET_MIN * 60;
        struct tm tm;
        gmtime_r(&local, &tm);

        /* Watch-face convention: minutes only, no seconds in the large
         * type — the seconds live on the bezel ring. */
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
        info_set_text(s_info_time, buf);

        static const char *const weekday[7] = {
            "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
        };
        static const char *const month[12] = {
            "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
            "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
        };
        snprintf(buf, sizeof(buf), "%s %d %s", weekday[tm.tm_wday % 7],
                 tm.tm_mday, month[tm.tm_mon % 12]);
        info_set_text(s_info_date, buf);

        int16_t sec = (int16_t)tm.tm_sec;
        if (sec != s_info_ring_sec) {
            s_info_ring_sec = sec;
            if (s_info_ring != NULL) lv_obj_invalidate(s_info_ring);
        }
    }
    else {
        info_set_text(s_info_time, "--:--");
        info_set_text(s_info_date, "SETTING CLOCK");
        if (s_info_ring_sec != -1) {
            s_info_ring_sec = -1;
            if (s_info_ring != NULL) lv_obj_invalidate(s_info_ring);
        }
    }

    /* ── weather ── */
    const weather_info_t *w = weather_get();

    info_wicon_t kind = INFO_WICON_CLOUD;
    if (w->valid) {
        int code = (int)w->code;
        if (code == 0) kind = INFO_WICON_SUN;
        else if (code <= 2) kind = INFO_WICON_PARTLY;
        else if (code == 45 || code == 48) kind = INFO_WICON_FOG;
        else if ((code >= 51 && code <= 67) ||
                 (code >= 80 && code <= 82)) kind = INFO_WICON_RAIN;
        else if ((code >= 71 && code <= 77) ||
                 code == 85 || code == 86) kind = INFO_WICON_SNOW;
        else if (code >= 95) kind = INFO_WICON_STORM;
    }

    if (kind != s_info_wicon_kind || w->valid != s_info_wicon_ready) {
        s_info_wicon_kind = kind;
        s_info_wicon_ready = w->valid;
        if (s_info_wicon != NULL) lv_obj_invalidate(s_info_wicon);
    }

    if (w->valid) {
        snprintf(buf, sizeof(buf), "%d°", (int)w->temp_c);
        info_set_text(s_info_weather, buf);
        info_set_text(s_info_desc, w->desc);

        snprintf(buf, sizeof(buf),
                 LV_SYMBOL_UP " %d°   " LV_SYMBOL_DOWN " %d°   "
                 LV_SYMBOL_TINT " %u%%",
                 (int)w->hi_c, (int)w->lo_c, (unsigned)w->humidity);
        info_set_text(s_info_hl, buf);
    }
    else {
        info_set_text(s_info_weather, "--°");
        info_set_text(s_info_desc, "NO WEATHER DATA");
        info_set_text(s_info_hl, "");
    }

    /* ── WiFi glyph: lit waves for the signal, a slash for no link ── */
    int8_t wifi_level = -1;
    uint32_t wifi_color = MON_RING_BRIGHT;

    if (wifi_info_enabled() && wifi_info_connected()) {
        int rssi = wifi_info_rssi();
        if (rssi >= -60) wifi_level = 3;
        else if (rssi >= -72) wifi_level = 2;
        else wifi_level = 1;

        /* Amber from the first weak step: fewer waves and a warmer colour
         * both flag a link that is about to become a problem. */
        wifi_color = wifi_level >= 2 ? MON_ACCENT_NET : MON_ACCENT_SYS;
    }

    if (wifi_level != s_info_wifi_level || wifi_color != s_info_wifi_color) {
        s_info_wifi_level = wifi_level;
        s_info_wifi_color = wifi_color;
        if (s_info_wifi_icon != NULL) lv_obj_invalidate(s_info_wifi_icon);
    }
}

static void info_sheet_anim_cb(void *var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, (lv_coord_t)v);
}

static void info_sheet_hidden_cb(lv_anim_t *a)
{
    (void)a;
    if (!s_info_open && s_info != NULL) {
        lv_obj_add_flag(s_info, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(s_info, 0);
        /* Parked above the screen: the face is home, so it drops back in
         * from the top when returning from the pages. */
        lv_obj_set_y(s_info, -lv_obj_get_height(s_info));
    }
}

static void info_slide_to(int32_t y, lv_anim_ready_cb_t ready_cb,
                          lv_anim_path_cb_t path_cb)
{
    lv_anim_del(s_info, info_sheet_anim_cb);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_info);
    lv_anim_set_exec_cb(&anim, info_sheet_anim_cb);
    lv_anim_set_values(&anim, lv_obj_get_y(s_info), y);
    lv_anim_set_time(&anim, INFO_ANIM_MS);
    lv_anim_set_path_cb(&anim, path_cb);
    lv_anim_set_ready_cb(&anim, ready_cb);
    lv_anim_start(&anim);
}

static void info_open(void)
{
    if (s_info_open || s_info == NULL) return;

    s_info_open = true;
    info_refresh();

    lv_obj_set_x(s_info, 0);
    lv_obj_set_y(s_info, -lv_obj_get_height(s_info));
    lv_obj_clear_flag(s_info, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_info);

    info_slide_to(0, NULL, lv_anim_path_ease_out);
}

/* Boot lands on the watch face without an animation: it is the home layer,
 * not a sheet pulled over the data pages. */
static void info_show_home(void)
{
    if (s_info == NULL) return;

    s_info_open = true;
    info_refresh();

    lv_obj_set_x(s_info, 0);
    lv_obj_set_y(s_info, 0);
    lv_obj_clear_flag(s_info, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_info);
}

static void info_close(void)
{
    if (!s_info_open || s_info == NULL) return;
    s_info_open = false;
    s_page_start = lv_tick_get();   /* a full page dwell after returning */

    /* The face lifts away upwards, revealing the data pages underneath. */
    info_slide_to(-lv_obj_get_height(s_info), info_sheet_hidden_cb,
                  lv_anim_path_ease_in);
}

static void info_build(lv_obj_t *scr)
{
    const mon_ui_metrics_t *m = mon_ui();

    s_info = lv_obj_create(scr);
    lv_obj_set_size(s_info, m->disp_w, m->disp_h);
    /* Parked above the screen: returning from the pages slides it down. */
    lv_obj_set_pos(s_info, 0, -m->disp_h);
    lv_obj_set_style_bg_color(s_info, lv_color_hex(MON_BG), 0);
    lv_obj_set_style_bg_opa(s_info, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_info, 0, 0);
    lv_obj_set_style_radius(s_info, 0, 0);
    lv_obj_set_style_pad_all(s_info, 0, 0);
    lv_obj_clear_flag(s_info, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_info, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_info, LV_OBJ_FLAG_HIDDEN);

    /* Rainbow seconds bezel — the one moving element, so the sheet reads as
     * a watch face rather than a data card. */
    s_info_ring = info_make_plain(s_info);
    lv_obj_set_size(s_info_ring, 228, 228);
    lv_obj_align(s_info_ring, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(s_info_ring, info_ring_draw_cb,
                        LV_EVENT_DRAW_MAIN_BEGIN, NULL);

    s_info_date = info_make_label(s_info, "", &lv_font_montserrat_12,
                                  MON_TEXT_DIM, 2);
    lv_obj_align(s_info_date, LV_ALIGN_TOP_MID, 0, 22);

    s_info_time = info_make_label(s_info, "--:--", &font_clock_bold,
                                  MON_TEXT, 0);
    lv_obj_align(s_info_time, LV_ALIGN_TOP_MID, 0, 48);

    lv_obj_t *weather_row = info_make_row(s_info, 100, 220, 46, 4);
    s_info_wicon = info_make_plain(weather_row);
    lv_obj_set_size(s_info_wicon, 46, 40);
    lv_obj_add_event_cb(s_info_wicon, info_wicon_draw_cb,
                        LV_EVENT_DRAW_MAIN_BEGIN, NULL);

    s_info_weather = info_make_label(weather_row, "--°", &font_temp_bold,
                                     MON_TEXT, 0);

    s_info_desc = info_make_label(s_info, "", &lv_font_montserrat_12,
                                  MON_TEXT_DIM, 1);
    lv_obj_align(s_info_desc, LV_ALIGN_TOP_MID, 0, 150);

    s_info_hl = info_make_label(s_info, "", &lv_font_montserrat_10,
                                MON_TEXT_DIM, 0);
    lv_obj_align(s_info_hl, LV_ALIGN_TOP_MID, 0, 170);

    /* The network state is a glyph, not a line of text: three waves for a
     * strong link, one amber wave when it weakens, a slash when it is gone. */
    s_info_wifi_icon = info_make_plain(s_info);
    lv_obj_set_size(s_info_wifi_icon, 22, 16);
    lv_obj_align(s_info_wifi_icon, LV_ALIGN_TOP_MID, 0, 198);
    lv_obj_add_event_cb(s_info_wifi_icon, info_wifi_draw_cb,
                        LV_EVENT_DRAW_MAIN_BEGIN, NULL);
}

/* ══════════════════════════ timers / input ══════════════════════════ */

static void monitor_gesture_cb(touch_gesture_t gesture)
{
    /* A dark screen wakes on the first touch; that touch is not a command.
     * The gesture itself is the one being swallowed, so the guard that
     * screen_wake() arms for the timer path must not stay armed here. */
    if (s_screen_off) {
        screen_wake();
        s_ignore_gesture = false;
        return;
    }
    if (s_ignore_gesture) {
        s_ignore_gesture = false;
        return;
    }

    if (s_info_open) {
        /* The face is home and nothing inside it scrolls, so an upward
         * swipe is unambiguously "on to the pages". */
        if (gesture == TOUCH_SWIPE_UP) info_close();
        return;
    }

    if (s_settings_open) {
        /* The touch timer runs before LVGL sees the lift, so a finger that
         * is finishing a slider drag is still flagged here. Treat that
         * release as part of the drag, not as a sheet swipe. */
        if (s_set_slider_drag) return;

        /* Vertical swipes belong to the list, so the sheet can only be
         * dismissed sideways. */
        if (gesture == TOUCH_SWIPE_LEFT) {
            settings_close(-1);
        } else if (gesture == TOUCH_SWIPE_RIGHT) {
            settings_close(1);
        }
        return;
    }

    /* Data pages: swipe up opens SETTINGS, swipe down returns to the clock,
     * so the home → pages → settings stack is walked with upward swipes and
     * climbed with downward ones. */
    if (gesture == TOUCH_SWIPE_UP) {
        if (!s_transitioning) settings_open();
        return;
    }
    if (gesture == TOUCH_SWIPE_DOWN) {
        if (!s_transitioning) info_open();
        return;
    }

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
    if (!s_auto_on || s_settings_open || s_info_open ||
        s_transitioning || s_screen_off) {
        return;
    }
    if (lv_tick_elaps(s_page_start) < s_page_ms) return;
    start_transition(1);
}

/* ══════════════════════════ link notice ══════════════════════════ */

/* A WiFi join after boot can take a few seconds; don't flash a warning while
 * the stack is still doing its normal startup. */
#define LINK_SETTLE_MS 8000U

static lv_obj_t *s_link;
static bool s_link_pulsing;

static void link_pulse_cb(void *var, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)value, 0);
}

static void link_pulse_start(void)
{
    if (s_link_pulsing) return;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_link);
    lv_anim_set_exec_cb(&a, link_pulse_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, 96);
    lv_anim_set_time(&a, 1000);
    lv_anim_set_playback_time(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
    s_link_pulsing = true;
}

static void link_pulse_stop(void)
{
    if (!s_link_pulsing) return;
    lv_anim_del(s_link, link_pulse_cb);
    lv_obj_set_style_opa(s_link, LV_OPA_COVER, 0);
    s_link_pulsing = false;
}

/* The notice takes over the page-dot row while it is visible: there is no
 * room for both at this radius, and a warning matters more than the dots. */
static void link_show(const char *text)
{
    const char *old = lv_label_get_text(s_link);
    if (old == NULL || strcmp(old, text) != 0) {
        lv_label_set_text(s_link, text);
    }
    if (lv_obj_has_flag(s_link, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(s_link, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_dots, LV_OBJ_FLAG_HIDDEN);
    }
    /* Also restarts after a screen wake, when the pulse was stopped. */
    link_pulse_start();
}

static void link_hide(void)
{
    if (!lv_obj_has_flag(s_link, LV_OBJ_FLAG_HIDDEN)) {
        link_pulse_stop();
        lv_obj_add_flag(s_link, LV_OBJ_FLAG_HIDDEN);
    }
    /* Guarded: lv_obj_clear_flag(HIDDEN) always invalidates, and this runs
     * once per second even when nothing changes. */
    if (lv_obj_has_flag(s_dots, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(s_dots, LV_OBJ_FLAG_HIDDEN);
    }
}

static void link_status_update(void)
{
    /* Screen off: leave the notice as it was and keep the LCD idle. On wake,
     * screen_wake() calls this again before the backlight fades in. */
    if (s_screen_off) return;

    const sys_sample_t *s = sampler_get();
    const app_settings_t *cfg = app_settings_get();
    bool settle = lv_tick_get() < LINK_SETTLE_MS;

    /* WiFi off is a deliberate choice, not a fault: freeze quietly. */
    if (cfg->wifi_on == 0U || s->link_live) {
        link_hide();
        return;
    }

    if (s->wifi_state != PC_WIFI_CONNECTED) {
        if (!s->link_ever && settle) {
            link_hide();
        } else {
            link_show("NO WIFI");
        }
        return;
    }

    if (s->link_ever) {
        link_show("NO DATA");
    } else if (settle) {
        link_hide();
    } else {
        link_show("WAITING FOR PC");
    }
}

static void data_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    /* Keep the notice current even during a page transition or while a
     * sheet is open. A dark panel must not repaint the clock. */
    link_status_update();
    if (s_info_open && !s_screen_off) info_refresh();
    if (s_settings_open || s_transitioning || s_screen_off) return;
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
    s_dot_size = m->dot_size;
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

    /* Link notice, drawn in the page-dot row when data stops arriving. */
    s_link = lv_label_create(s_dial);
    lv_label_set_text(s_link, "NO DATA");
    lv_obj_set_style_text_font(s_link, &font_cap_bold, 0);
    lv_obj_set_style_text_color(s_link, lv_color_hex(MON_ACCENT_DISK), 0);
    lv_obj_set_style_text_letter_space(s_link, 1, 0);
    lv_obj_align(s_link, LV_ALIGN_TOP_MID, 0,
                 m->dots_y - lv_font_get_line_height(&font_cap_bold) / 2);
    lv_obj_add_flag(s_link, LV_OBJ_FLAG_HIDDEN);
    link_status_update();

    /* reveal page 0 with the same entrance the carousel uses */
    s_page_idx = 0;
    lv_obj_clear_flag(s_pages[0].root, LV_OBJ_FLAG_HIDDEN);
    s_pages[0].update(sampler_get());
    intro_page(s_pages[0].ui());
    if (s_pages[0].enter != NULL) {
        s_pages[0].enter();
    }
    s_dot_pos = 0.0f;
    s_page_start = lv_tick_get();

    /* built last so they always draw above the dial */
    settings_build(scr);
    info_build(scr);
}

void lv_monitor_show(void)
{
    if (s_started) return;
    s_started = true;

    sampler_init();
    weather_init();
    mon_ui_init();
    build_chrome();

    /* The clock is the home screen. */
    info_show_home();

    touch_set_gesture_cb(monitor_gesture_cb);
    lv_timer_create(carousel_tick_cb, 250, NULL);
    lv_timer_create(data_timer_cb, 1000, NULL);
    lv_timer_create(idle_tick_cb, 250, NULL);
}

void lv_monitor_set_pc_data(const pc_data_t *data)
{
    sampler_set_pc_data(data);
}

void monitor_set_timings(uint16_t page_ms, uint16_t trans_ms)
{
    s_page_ms = page_ms;
    s_trans_ms = trans_ms;
}

void monitor_set_auto(bool on)
{
    s_auto_on = on;
    if (on) {
        /* Give the visible page its full dwell instead of advancing the
         * moment the switch is flipped. */
        s_page_start = lv_tick_get();
    }
}
