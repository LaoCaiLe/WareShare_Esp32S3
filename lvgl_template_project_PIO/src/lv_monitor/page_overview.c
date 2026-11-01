#include "page.h"
#include "common.h"
#include "theme.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Four small rings in a 2x2 grid: the ring sweep is the load in percent and
 * the value sits inside the circle. 60 px keeps "100%" inside a 5 px track
 * while leaving room for the little caption underneath. */
#define RING_COUNT 4
#define RING_D     60
#define RING_GAP   12
#define RING_PAD   4
#define RING_GROUP (RING_D * 2 + RING_GAP + RING_PAD * 2)
#define RING_ARC_W 5

#define RING_SWEEP_MS   500   /* normal one-second refresh                */
#define RING_ENTER_MS   700   /* entry refill, a little slower on purpose */
#define RING_ENTER_STEP 80    /* stagger between the four rings           */
#define RING_ENTER_DELAY 220  /* let the stage fade in before refilling   */

LV_FONT_DECLARE(font_cap_bold);

static mon_page_ui_t s_ui;
static lv_obj_t *s_ring[RING_COUNT];
static lv_obj_t *s_pct[RING_COUNT];

typedef struct {
    lv_obj_t *arc;
    lv_obj_t *pct;
    bool valid;
} ring_anim_t;

static ring_anim_t s_ring_anim[RING_COUNT];

static const uint32_t s_ring_color[RING_COUNT] = {
    MON_ACCENT_CPU, MON_ACCENT_GPU, MON_ACCENT_MEM, MON_ACCENT_DISK,
};
static const char *const s_ring_cap[RING_COUNT] = {
    "CPU", "GPU", "MEM", "DISK",
};

const mon_page_ui_t *page_overview_ui(void) { return &s_ui; }

static void ring_set_label(lv_obj_t *label, const char *text)
{
    const char *old = lv_label_get_text(label);
    if (old == NULL || strcmp(old, text) != 0) {
        lv_label_set_text(label, text);
    }
}

/* Animate the sweep and the number together, so the ring fills exactly as
 * fast as the percentage counts up. */
static void ring_sweep_cb(void *var, int32_t value)
{
    ring_anim_t *r = (ring_anim_t *)var;

    lv_arc_set_value(r->arc, (int16_t)value);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)value);
    lv_label_set_text(r->pct, buf);
}

static void ring_sweep(uint8_t idx, uint16_t pct, bool valid,
                       uint32_t delay, uint32_t duration)
{
    ring_anim_t *r = &s_ring_anim[idx];

    if (!valid) {
        lv_anim_del(r, ring_sweep_cb);
        lv_arc_set_value(r->arc, 0);
        ring_set_label(r->pct, "--");
        r->valid = false;
        return;
    }

    int16_t target = (int16_t)(pct > 100U ? 100U : pct);
    int16_t current = lv_arc_get_value(r->arc);
    r->valid = true;

    if (current == target) {
        lv_anim_del(r, ring_sweep_cb);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", (int)target);
        ring_set_label(r->pct, buf);
        return;
    }

    lv_anim_del(r, ring_sweep_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, r);
    lv_anim_set_exec_cb(&a, ring_sweep_cb);
    lv_anim_set_values(&a, current, target);
    lv_anim_set_time(&a, duration);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void ring_set(uint8_t idx, uint16_t pct, bool valid)
{
    ring_sweep(idx, pct, valid, 0, RING_SWEEP_MS);
}

/* Called when the carousel lands on this page: empty every ring and fill
 * them again with a short stagger. The page is still transparent at that
 * moment, so the reset itself never shows. */
void page_overview_enter(void)
{
    const sys_sample_t *s = sampler_get();

    for (uint8_t i = 0; i < RING_COUNT; i++) {
        lv_anim_del(&s_ring_anim[i], ring_sweep_cb);
        lv_arc_set_value(s_ring[i], 0);
        ring_set_label(s_pct[i], "0%");
    }

    ring_sweep(0, s->cpu_load, true,
               RING_ENTER_DELAY, RING_ENTER_MS);
    ring_sweep(1, s->gpu_load, s->gpu_valid,
               RING_ENTER_DELAY + RING_ENTER_STEP, RING_ENTER_MS);
    ring_sweep(2, s->mem_load, true,
               RING_ENTER_DELAY + RING_ENTER_STEP * 2, RING_ENTER_MS);
    ring_sweep(3, s->disk_load, true,
               RING_ENTER_DELAY + RING_ENTER_STEP * 3, RING_ENTER_MS);
}

static void ring_create(uint8_t idx, lv_coord_t x, lv_coord_t y)
{
    lv_color_t accent = lv_color_hex(s_ring_color[idx]);

    lv_obj_t *arc = lv_arc_create(s_ui.stage);
    s_ring[idx] = arc;

    lv_obj_set_size(arc, RING_D, RING_D);
    lv_obj_set_pos(arc, x, y);
    /* The dial is read-only and the page is paged by the touch layer, so the
     * ring must never take clicks or steal a vertical drag. */
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                           LV_OBJ_FLAG_SCROLL_CHAIN);

    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, RING_ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(MON_RING), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);

    lv_obj_set_style_arc_width(arc, RING_ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, accent, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    /* No knob: the value is shown by the sweep and the label, a knob at the
     * tip would just eat the inner circle on a 60 px ring. */
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_bg_angles(arc, 0, 360);
    /* 0 deg is 3 o'clock in LVGL; rotate so the sweep starts at 12 o'clock. */
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_value(arc, 0);

    lv_obj_t *pct = lv_label_create(arc);
    lv_label_set_text(pct, "0%");
    lv_obj_set_style_text_font(pct, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pct, lv_color_hex(MON_TEXT), 0);
    lv_obj_align(pct, LV_ALIGN_CENTER, 0, -6);
    lv_obj_clear_flag(pct, LV_OBJ_FLAG_CLICKABLE);
    s_pct[idx] = pct;

    lv_obj_t *cap = lv_label_create(arc);
    lv_label_set_text(cap, s_ring_cap[idx]);
    lv_obj_set_style_text_font(cap, &font_cap_bold, 0);
    lv_obj_set_style_text_color(cap, lv_color_hex(MON_TEXT_DIM), 0);
    lv_obj_set_style_text_letter_space(cap, 1, 0);
    lv_obj_align(cap, LV_ALIGN_CENTER, 0, 9);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_CLICKABLE);

    s_ring_anim[idx].arc = arc;
    s_ring_anim[idx].pct = pct;
    s_ring_anim[idx].valid = true;
}

lv_obj_t *page_overview_create(lv_obj_t *parent, lv_color_t accent)
{
    const mon_ui_metrics_t *m = mon_ui();

    mon_page_ui_create(&s_ui, parent, "ALL", accent);

    /* This page has no hero number and no sub statistics; the rings replace
     * them. Hiding the boxes keeps the carousel's fade/zoom hooks harmless. */
    lv_obj_add_flag(s_ui.hero_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.sub_row, LV_OBJ_FLAG_HIDDEN);

    /* Grow the stage into a square that holds the rings. The stage cover is
     * resized with it so the left-to-right wipe still hides every ring. */
    lv_obj_set_size(s_ui.stage, RING_GROUP, RING_GROUP);
    lv_obj_align(s_ui.stage, LV_ALIGN_TOP_MID, 0,
                 (m->dial_d - RING_GROUP) / 2);
    lv_obj_set_size(s_ui.stage_cover, RING_GROUP, RING_GROUP);

    for (uint8_t i = 0; i < RING_COUNT; i++) {
        lv_coord_t x = RING_PAD + (i % 2) * (RING_D + RING_GAP);
        lv_coord_t y = RING_PAD + (i / 2) * (RING_D + RING_GAP);
        ring_create(i, x, y);
    }

    return s_ui.root;
}

void page_overview_update(const sys_sample_t *s)
{
    ring_set(0, s->cpu_load, true);
    ring_set(1, s->gpu_load, s->gpu_valid);
    ring_set(2, s->mem_load, true);
    ring_set(3, s->disk_load, true);
}
