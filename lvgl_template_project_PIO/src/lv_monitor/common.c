#include "common.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ─────────────────────────── metrics ─────────────────────────── */

static mon_ui_metrics_t s_ui;
static bool s_ui_ready;

void mon_ui_init(void)
{
    if (s_ui_ready) return;
    s_ui_ready = true;

    lv_disp_t *disp = lv_disp_get_default();
    lv_coord_t sw = lv_disp_get_hor_res(disp);
    lv_coord_t sh = lv_disp_get_ver_res(disp);
    lv_coord_t short_side = LV_MIN(sw, sh);

    lv_coord_t d = short_side - (short_side >= 400 ? 24 : (short_side >= 240 ? 16 : 8));
    if (d < 140) d = 140;

    s_ui.disp_w = sw;
    s_ui.disp_h = sh;
    s_ui.dial_d = d;
    s_ui.radius = d / 2;
    s_ui.compact = d < 400;

    s_ui.tag_y = (lv_coord_t)(d * 130 / 1000);
    s_ui.hero_y = (lv_coord_t)(d * 400 / 1000);
    s_ui.stage_y = (lv_coord_t)(d * 630 / 1000);
    s_ui.sub_y = (lv_coord_t)(d * 795 / 1000);
    s_ui.stage_w = (lv_coord_t)(d * 720 / 1000);
    s_ui.stage_h = (lv_coord_t)(d * (d >= 400 ? 200 : (d >= 300 ? 180 : 165)) / 1000);
    s_ui.sub_w = (lv_coord_t)(d * 720 / 1000);

    if (d >= 400) {
        s_ui.font_hero = &lv_font_montserrat_48;
        s_ui.font_unit = &lv_font_montserrat_24;
        s_ui.font_tag = &lv_font_montserrat_14;
        s_ui.font_sub_value = &lv_font_montserrat_20;
        s_ui.font_sub_cap = &lv_font_montserrat_12;
        s_ui.font_note = &lv_font_montserrat_12;
    }
    else if (d >= 300) {
        s_ui.font_hero = &lv_font_montserrat_40;
        s_ui.font_unit = &lv_font_montserrat_20;
        s_ui.font_tag = &lv_font_montserrat_14;
        s_ui.font_sub_value = &lv_font_montserrat_18;
        s_ui.font_sub_cap = &lv_font_montserrat_12;
        s_ui.font_note = &lv_font_montserrat_12;
    }
    else if (d >= 240) {
        s_ui.font_hero = &lv_font_montserrat_32;
        s_ui.font_unit = &lv_font_montserrat_18;
        s_ui.font_tag = &lv_font_montserrat_12;
        s_ui.font_sub_value = &lv_font_montserrat_16;
        s_ui.font_sub_cap = &lv_font_montserrat_10;
        s_ui.font_note = &lv_font_montserrat_10;
    }
    else {
        s_ui.font_hero = &lv_font_montserrat_26;
        s_ui.font_unit = &lv_font_montserrat_16;
        /* The page name is the main orientation cue on a 240 px dial —
         * 10-12 px reads as a faint smudge at arm's length. */
        s_ui.font_tag = &lv_font_montserrat_16;
        s_ui.font_sub_value = &lv_font_montserrat_14;
        s_ui.font_sub_cap = &lv_font_montserrat_10;
        s_ui.font_note = &lv_font_montserrat_10;
    }

    s_ui.sub_h = (lv_coord_t)(s_ui.font_sub_cap->line_height +
                              s_ui.font_sub_value->line_height + 4);
}

const mon_ui_metrics_t *mon_ui(void)
{
    return &s_ui;
}

/* ─────────────────────────── helpers ─────────────────────────── */

static void set_text_if_changed(lv_obj_t *label, const char *text)
{
    if (label == NULL || text == NULL) return;

    const char *old = lv_label_get_text(label);
    if (old == NULL || strcmp(old, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void make_transparent(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t *mon_make_rect(lv_obj_t *parent, lv_coord_t w, lv_coord_t h,
                        lv_coord_t x, lv_coord_t y, lv_coord_t rad,
                        lv_color_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, rad, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

/* ─────────────────────────── page skeleton ─────────────────────────── */

void mon_page_ui_create(mon_page_ui_t *ui, lv_obj_t *parent,
                        const char *tag, lv_color_t accent)
{
    const mon_ui_metrics_t *m = mon_ui();
    lv_coord_t d = m->dial_d;

    memset(ui, 0, sizeof(*ui));
    ui->accent = accent;

    ui->root = lv_obj_create(parent);
    lv_obj_set_size(ui->root, d, d);
    lv_obj_set_pos(ui->root, 0, 0);
    make_transparent(ui->root);

    /* Rotate the whole page around the dial centre during page transitions. */
    lv_obj_set_style_transform_pivot_x(ui->root, d / 2, 0);
    lv_obj_set_style_transform_pivot_y(ui->root, d / 2, 0);

    /* tag row: accent dot + uppercase label */
    lv_coord_t tag_h = m->font_tag->line_height + 6;
    ui->tag_row = lv_obj_create(ui->root);
    lv_obj_set_size(ui->tag_row, (lv_coord_t)(d * 620 / 1000), tag_h);
    lv_obj_align(ui->tag_row, LV_ALIGN_TOP_MID, 0, m->tag_y - tag_h / 2);
    make_transparent(ui->tag_row);
    lv_obj_set_flex_flow(ui->tag_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->tag_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui->tag_row, LV_MAX(6, d / 48), 0);

    lv_coord_t dot_sz = LV_MAX(6, d / 56);
    ui->tag_dot = mon_make_rect(ui->tag_row, dot_sz, dot_sz, 0, 0,
                                LV_RADIUS_CIRCLE, accent);

    ui->tag_label = lv_label_create(ui->tag_row);
    lv_label_set_text(ui->tag_label, tag);
    lv_obj_set_style_text_font(ui->tag_label, m->font_tag, 0);
    lv_obj_set_style_text_color(ui->tag_label, lv_color_hex(MON_TEXT), 0);
    lv_obj_set_style_text_letter_space(ui->tag_label, 2, 0);

    /* hero number + unit */
    lv_coord_t hero_h = m->font_hero->line_height + 8;
    ui->hero_box = lv_obj_create(ui->root);
    lv_obj_set_size(ui->hero_box, (lv_coord_t)(d * 840 / 1000), hero_h);
    lv_obj_align(ui->hero_box, LV_ALIGN_TOP_MID, 0, m->hero_y - hero_h / 2);
    make_transparent(ui->hero_box);

    ui->hero_value = lv_label_create(ui->hero_box);
    lv_label_set_text(ui->hero_value, "0");
    lv_obj_set_style_text_font(ui->hero_value, m->font_hero, 0);
    lv_obj_set_style_text_color(ui->hero_value, lv_color_hex(MON_TEXT), 0);

    ui->hero_unit = lv_label_create(ui->hero_box);
    lv_label_set_text(ui->hero_unit, "");
    lv_obj_set_style_text_font(ui->hero_unit, m->font_unit, 0);
    lv_obj_set_style_text_color(ui->hero_unit, accent, 0);

    /* stage (chart / gauge lives here) */
    ui->stage = lv_obj_create(ui->root);
    lv_obj_set_size(ui->stage, m->stage_w, m->stage_h);
    lv_obj_align(ui->stage, LV_ALIGN_TOP_MID, 0, m->stage_y - m->stage_h / 2);
    make_transparent(ui->stage);

    /* two sub statistics with a hairline divider */
    ui->sub_row = lv_obj_create(ui->root);
    lv_obj_set_size(ui->sub_row, m->sub_w, m->sub_h);
    lv_obj_align(ui->sub_row, LV_ALIGN_TOP_MID, 0, m->sub_y - m->sub_h / 2);
    make_transparent(ui->sub_row);
    lv_obj_set_flex_flow(ui->sub_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->sub_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_coord_t block_w = (lv_coord_t)(m->sub_w * 45 / 100);

    for (int i = 0; i < 2; i++) {
        lv_obj_t *block = lv_obj_create(ui->sub_row);
        lv_obj_set_size(block, block_w, m->sub_h);
        make_transparent(block);
        lv_obj_set_flex_flow(block, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(block, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(block, 2, 0);

        ui->stat_value[i] = lv_label_create(block);
        lv_label_set_text(ui->stat_value[i], "--");
        lv_obj_set_style_text_font(ui->stat_value[i], m->font_sub_value, 0);
        lv_obj_set_style_text_color(ui->stat_value[i], lv_color_hex(MON_TEXT), 0);

        ui->stat_caption[i] = lv_label_create(block);
        lv_label_set_text(ui->stat_caption[i], "");
        lv_obj_set_style_text_font(ui->stat_caption[i], m->font_sub_cap, 0);
        lv_obj_set_style_text_color(ui->stat_caption[i],
                                    lv_color_hex(MON_TEXT_DIM), 0);
        lv_obj_set_style_text_letter_space(ui->stat_caption[i], 1, 0);

        if (i == 0) {
            lv_obj_t *divider = mon_make_rect(ui->sub_row, 1,
                                              (lv_coord_t)(m->sub_h * 55 / 100),
                                              0, 0, 0, lv_color_white());
            lv_obj_set_style_bg_opa(divider, LV_OPA_20, 0);
        }
    }

    /* small chart badge, drawn above the stage and its chart */
    ui->note = lv_label_create(ui->root);
    lv_label_set_text(ui->note, "");
    lv_obj_add_flag(ui->note, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(ui->note, m->font_note, 0);
    lv_obj_set_style_text_color(ui->note, lv_color_hex(MON_TEXT_DIM), 0);
    lv_obj_set_style_text_letter_space(ui->note, 1, 0);
    lv_obj_set_style_bg_color(ui->note, lv_color_hex(MON_BG), 0);
    lv_obj_set_style_bg_opa(ui->note, LV_OPA_70, 0);
    lv_obj_set_style_radius(ui->note, 4, 0);
    lv_obj_set_style_pad_all(ui->note, 3, 0);
    lv_obj_align(ui->note, LV_ALIGN_TOP_LEFT,
                 (lv_coord_t)(d * 140 / 1000),
                 m->stage_y - m->stage_h / 2);
}

void mon_page_set_hero(mon_page_ui_t *ui, const char *value, const char *unit)
{
    const mon_ui_metrics_t *m = mon_ui();
    const char *v = value != NULL ? value : "";
    const char *u = unit != NULL ? unit : "";

    set_text_if_changed(ui->hero_value, v);
    set_text_if_changed(ui->hero_unit, u);

    lv_point_t num_size = {0, 0};
    lv_point_t unit_size = {0, 0};

    lv_txt_get_size(&num_size, v, m->font_hero, 0, 0, LV_COORD_MAX,
                    LV_TEXT_FLAG_NONE);
    if (u[0] != '\0') {
        lv_txt_get_size(&unit_size, u, m->font_unit, 0, 0, LV_COORD_MAX,
                        LV_TEXT_FLAG_NONE);
    }

    lv_coord_t gap = (u[0] != '\0') ? LV_MAX(3, m->dial_d / 84) : 0;
    lv_coord_t box_w = lv_obj_get_width(ui->hero_box);
    lv_coord_t box_h = lv_obj_get_height(ui->hero_box);
    lv_coord_t total = num_size.x + gap + unit_size.x;
    lv_coord_t x = (box_w - total) / 2;
    if (x < 0) x = 0;

    lv_coord_t top = 4;
    lv_obj_set_pos(ui->hero_value, x, top);

    lv_coord_t unit_y = top +
        (m->font_hero->line_height - m->font_unit->line_height) * 4 / 5;
    if (unit_y < 0) unit_y = 0;
    if (unit_y + m->font_unit->line_height > box_h) {
        unit_y = box_h - m->font_unit->line_height;
        if (unit_y < 0) unit_y = 0;
    }

    lv_obj_set_pos(ui->hero_unit, x + num_size.x + gap, unit_y);
}

static void hero_value_render(mon_page_ui_t *ui, double value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", (int)ui->hero_decimals, value);
    mon_page_set_hero(ui, buf, ui->hero_unit_str);
}

static void hero_value_anim_cb(void *var, int32_t value)
{
    mon_page_ui_t *ui = (mon_page_ui_t *)var;

    bool last = (value >= 1000) || (value <= 0);
    if (!last && lv_tick_elaps(ui->hero_last_tick) < 33) return;
    ui->hero_last_tick = lv_tick_get();

    double v = ui->hero_from +
               (ui->hero_to - ui->hero_from) * ((double)value / 1000.0);
    ui->hero_current = v;
    hero_value_render(ui, v);
}

static void hero_value_ready(lv_anim_t *anim)
{
    mon_page_ui_t *ui = (mon_page_ui_t *)anim->var;
    ui->hero_current = ui->hero_to;
    hero_value_render(ui, ui->hero_to);
}

void mon_page_set_hero_value(mon_page_ui_t *ui, double value,
                             uint8_t decimals, const char *unit)
{
    if (ui == NULL) return;
    if (value < 0.0) value = 0.0;

    snprintf(ui->hero_unit_str, sizeof(ui->hero_unit_str), "%s",
             unit != NULL ? unit : "");
    ui->hero_decimals = decimals;

    lv_anim_del(ui, hero_value_anim_cb);

    if (fabs(value - ui->hero_current) < 0.001) {
        ui->hero_current = value;
        hero_value_render(ui, value);
        return;
    }

    ui->hero_from = ui->hero_current;
    ui->hero_to = value;

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, ui);
    lv_anim_set_exec_cb(&anim, hero_value_anim_cb);
    lv_anim_set_values(&anim, 0, 1000);
    lv_anim_set_time(&anim, 420);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&anim, hero_value_ready);
    lv_anim_start(&anim);
}

void mon_page_set_stat(mon_page_ui_t *ui, uint8_t index,
                       const char *value, const char *caption)
{
    if (index >= 2) return;
    set_text_if_changed(ui->stat_value[index], value);
    set_text_if_changed(ui->stat_caption[index], caption);
}

void mon_page_set_note(mon_page_ui_t *ui, const char *note)
{
    if (note == NULL || note[0] == '\0') {
        if (!lv_obj_has_flag(ui->note, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(ui->note, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    set_text_if_changed(ui->note, note);
    if (lv_obj_has_flag(ui->note, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(ui->note, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ─────────────────────────── charts ─────────────────────────── */

lv_obj_t *mon_chart_create(lv_obj_t *parent, lv_coord_t w, lv_coord_t h,
                           bool fill)
{
    lv_obj_t *chart = lv_chart_create(parent);
    lv_obj_set_size(chart, w, h);
    lv_obj_set_pos(chart, 0, 0);

    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_radius(chart, 0, 0);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, MON_HIST_LEN);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(chart, 0, 0);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);

    /* Hide point markers — we only want the line. */
    lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);

    static lv_style_t line_style;
    static bool line_style_ready;
    if (!line_style_ready) {
        line_style_ready = true;
        lv_style_init(&line_style);
        lv_style_set_line_width(&line_style, 2);
    }
    lv_obj_add_style(chart, &line_style, LV_PART_ITEMS);

    if (fill) {
        lv_obj_add_event_cb(chart, mon_chart_draw_cb,
                            LV_EVENT_DRAW_PART_BEGIN, NULL);
    }

    return chart;
}

void mon_chart_load(lv_obj_t *chart, lv_chart_series_t *ser,
                    const uint32_t *history, uint16_t len, uint16_t head,
                    uint32_t max_value, lv_coord_t chart_max)
{
    if (chart == NULL || ser == NULL || ser->y_points == NULL ||
        history == NULL || len == 0) {
        return;
    }
    if (max_value == 0) max_value = 1;

    ser->start_point = 0;

    for (uint16_t i = 0; i < len; i++) {
        uint32_t v = history[(head + i) % len];
        if (v > max_value) v = max_value;
        ser->y_points[i] = (lv_coord_t)((uint64_t)v * (uint32_t)chart_max /
                                        max_value);
    }

    lv_chart_refresh(chart);
}

void mon_chart_draw_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc == NULL) return;
    if (dsc->part != LV_PART_ITEMS || dsc->p1 == NULL || dsc->p2 == NULL ||
        dsc->line_dsc == NULL) {
        return;
    }

    lv_obj_t *obj = lv_event_get_target(e);

    /* Soft gradient area under the line. */
    lv_draw_mask_line_param_t line_mask;
    lv_draw_mask_line_points_init(&line_mask,
                                  dsc->p1->x, dsc->p1->y,
                                  dsc->p2->x, dsc->p2->y,
                                  LV_DRAW_MASK_LINE_SIDE_BOTTOM);
    int16_t line_id = lv_draw_mask_add(&line_mask, NULL);

    lv_coord_t h = lv_obj_get_height(obj);
    lv_draw_mask_fade_param_t fade_mask;
    lv_draw_mask_fade_init(&fade_mask, &obj->coords,
                           LV_OPA_COVER, obj->coords.y1 + h / 8,
                           LV_OPA_TRANSP, obj->coords.y2);
    int16_t fade_id = lv_draw_mask_add(&fade_mask, NULL);

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_opa = LV_OPA_20;
    rect_dsc.bg_color = dsc->line_dsc->color;

    lv_area_t area = {
        .x1 = dsc->p1->x,
        .x2 = dsc->p2->x - 1,
        .y1 = LV_MIN(dsc->p1->y, dsc->p2->y),
        .y2 = obj->coords.y2,
    };
    lv_draw_rect(dsc->draw_ctx, &rect_dsc, &area);

    lv_draw_mask_remove_id(line_id);
    lv_draw_mask_remove_id(fade_id);
    lv_draw_mask_free_param(&line_mask);
    lv_draw_mask_free_param(&fade_mask);
}

/* ─────────────────────────── formatting ─────────────────────────── */

static void fmt_short_num(double value, char *buf, size_t buf_sz)
{
    if (value >= 100.0 || value <= -100.0) {
        snprintf(buf, buf_sz, "%.0f", value);
    }
    else {
        snprintf(buf, buf_sz, "%.1f", value);
    }
}

void mon_fmt_rate(uint64_t bytes_per_s, bool compact,
                  char *num, size_t num_sz, char *unit, size_t unit_sz)
{
    double value;
    const char *u;

    if (bytes_per_s >= 1024ULL * 1024ULL * 1024ULL) {
        value = (double)bytes_per_s / (1024.0 * 1024.0 * 1024.0);
        u = compact ? "G/s" : "GB/s";
    }
    else if (bytes_per_s >= 1024ULL * 1024ULL) {
        value = (double)bytes_per_s / (1024.0 * 1024.0);
        u = compact ? "M/s" : "MB/s";
    }
    else if (bytes_per_s >= 1024ULL) {
        value = (double)bytes_per_s / 1024.0;
        u = compact ? "K/s" : "KB/s";
    }
    else {
        value = (double)bytes_per_s;
        u = "B/s";
        snprintf(num, num_sz, "%.0f", value);
        snprintf(unit, unit_sz, "%s", u);
        return;
    }

    fmt_short_num(value, num, num_sz);
    snprintf(unit, unit_sz, "%s", u);
}

void mon_fmt_rate_line(uint64_t bytes_per_s, bool compact,
                       char *buf, size_t buf_sz)
{
    char num[24];
    char unit[12];
    mon_fmt_rate(bytes_per_s, compact, num, sizeof(num), unit, sizeof(unit));
    snprintf(buf, buf_sz, compact ? "%s%s" : "%s %s", num, unit);
}

void mon_fmt_size_pair(uint64_t used_kb, uint64_t total_kb, bool compact,
                       char *buf, size_t buf_sz)
{
    double div;
    const char *unit;

    if (total_kb >= 1024ULL * 1024ULL) {
        div = 1024.0 * 1024.0;
        unit = compact ? "G" : "GB";
    }
    else if (total_kb >= 1024ULL) {
        div = 1024.0;
        unit = compact ? "M" : "MB";
    }
    else {
        div = 1.0;
        unit = compact ? "K" : "KB";
    }

    char used[24];
    char total[24];
    fmt_short_num((double)used_kb / div, used, sizeof(used));
    fmt_short_num((double)total_kb / div, total, sizeof(total));

    snprintf(buf, buf_sz, compact ? "%s/%s%s" : "%s / %s %s",
             used, total, unit);
}

void mon_fmt_size(uint64_t kb, bool compact, char *buf, size_t buf_sz)
{
    double div;
    const char *unit;

    if (kb >= 1024ULL * 1024ULL) {
        div = 1024.0 * 1024.0;
        unit = compact ? "G" : "GB";
    }
    else if (kb >= 1024ULL) {
        div = 1024.0;
        unit = compact ? "M" : "MB";
    }
    else {
        div = 1.0;
        unit = compact ? "K" : "KB";
    }

    char num[24];
    fmt_short_num((double)kb / div, num, sizeof(num));
    snprintf(buf, buf_sz, compact ? "%s%s" : "%s %s", num, unit);
}

void mon_fmt_uptime(uint64_t seconds, bool compact, char *buf, size_t buf_sz)
{
    uint32_t days = (uint32_t)(seconds / 86400ULL);
    uint32_t hours = (uint32_t)((seconds % 86400ULL) / 3600ULL);
    uint32_t minutes = (uint32_t)((seconds % 3600ULL) / 60ULL);

    if (days > 0) {
        snprintf(buf, buf_sz, compact ? "%ud%uh" : "%ud %uh", days, hours);
    }
    else if (hours > 0) {
        snprintf(buf, buf_sz, compact ? "%uh%um" : "%uh %um", hours, minutes);
    }
    else {
        snprintf(buf, buf_sz, "%um", minutes);
    }
}

void mon_fmt_freq(uint32_t freq_khz, bool compact, char *buf, size_t buf_sz)
{
    if (freq_khz == 0) {
        snprintf(buf, buf_sz, "--");
    }
    else if (freq_khz >= 1000000U) {
        double ghz = (double)freq_khz / 1000000.0;
        snprintf(buf, buf_sz, compact ? "%.1fG" : "%.2f GHz", ghz);
    }
    else {
        double mhz = (double)freq_khz / 1000.0;
        snprintf(buf, buf_sz, compact ? "%.0fM" : "%.0f MHz", mhz);
    }
}

void mon_fmt_load(uint16_t load_x100, char *buf, size_t buf_sz)
{
    snprintf(buf, buf_sz, "%u.%02u",
             (unsigned)(load_x100 / 100U), (unsigned)(load_x100 % 100U));
}
