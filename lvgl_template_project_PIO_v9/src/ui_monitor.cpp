/**
 * @file ui_monitor.cpp
 * @brief LVGL v9 hardware monitor dashboard — scrollable single page
 *
 * Layout: vertical scroll with 3 snap sections
 *   [CPU Gauge + Usage]  →  [GPU Gauge + Usage]  →  [RAM + Disks + Uptime]
 *
 * Smooth scroll: snap-y + momentum + animated value transitions
 */
#include "ui_monitor.h"
#include <Arduino.h>
#include <stdio.h>

/* ================================================================
 *  Theme / Colors (dark dashboard)
 * ================================================================ */
#define BG_COLOR        lv_color_hex(0x1a1a2e)
#define CARD_COLOR      lv_color_hex(0x16213e)
#define CPU_ACCENT      lv_color_hex(0x00d2ff)
#define GPU_ACCENT      lv_color_hex(0x00ff88)
#define RAM_ACCENT      lv_color_hex(0xffaa00)
#define DISK_ACCENT     lv_color_hex(0xff6b6b)
#define TEXT_PRIMARY    lv_color_hex(0xe0e0e0)
#define TEXT_SECONDARY  lv_color_hex(0x888888)
#define TEMP_COLD       lv_color_hex(0x00ff88)
#define TEMP_WARM       lv_color_hex(0xffaa00)
#define TEMP_HOT        lv_color_hex(0xff4444)

/* ================================================================
 *  Layout constants
 * ================================================================ */
#define SCREEN_W        240
#define SECTION_H       220      /* CPU / GPU section height */
#define SYS_SECTION_H   240      /* System section height */
#define ARC_SIZE        130
#define ARC_X           (SCREEN_W / 2)
#define ARC_Y           100
#define BAR_W           200
#define BAR_H           10
#define BAR_X           ((SCREEN_W - BAR_W) / 2)

/* ================================================================
 *  Global UI object pointers (for refresh)
 * ================================================================ */
static lv_obj_t *scr;                 /* Top-level screen */
static lv_obj_t *scroll_page;         /* Scrollable page (for auto-scroll) */
static lv_obj_t *cpu_section;         /* Section containers (for scroll-to-view) */
static lv_obj_t *gpu_section;
static lv_obj_t *sys_section;

/* CPU widgets */
static lv_obj_t *cpu_arc;
static lv_obj_t *cpu_temp_label;      /* "65°C" inside arc */
static lv_obj_t *cpu_name_label;
static lv_obj_t *cpu_bar;
static lv_obj_t *cpu_usage_label;     /* "Usage: 45%" */
static lv_obj_t *cpu_freq_label;      /* "3600 MHz" */

/* GPU widgets */
static lv_obj_t *gpu_arc;
static lv_obj_t *gpu_temp_label;
static lv_obj_t *gpu_name_label;
static lv_obj_t *gpu_bar;
static lv_obj_t *gpu_usage_label;
static lv_obj_t *gpu_vram_bar;
static lv_obj_t *gpu_vram_label;

/* System widgets */
static lv_obj_t *status_label;    /* Connection status + frame count */
static lv_obj_t *ram_bar;
static lv_obj_t *ram_label;
static lv_obj_t *disk_bars[MAX_DISKS];
static lv_obj_t *disk_labels[MAX_DISKS];
static lv_obj_t *uptime_label;

/* Auto-scroll state */
#define AUTO_SCROLL_MS  4000
static int  auto_section_idx = 0;
static uint32_t auto_last_scroll = 0;

/* ================================================================
 *  Styles
 * ================================================================ */
static lv_style_t style_bg;
static lv_style_t style_section;
static lv_style_t style_title;
static lv_style_t style_arc_bg;
static lv_style_t style_bar_bg;
static lv_style_t style_bar_indicator;
static lv_style_t style_value_label;
static lv_style_t style_sub_label;

static void init_styles(void)
{
    /* Screen background */
    lv_style_init(&style_bg);
    lv_style_set_bg_color(&style_bg, BG_COLOR);
    lv_style_set_bg_opa(&style_bg, LV_OPA_COVER);

    /* Section card */
    lv_style_init(&style_section);
    lv_style_set_bg_color(&style_section, CARD_COLOR);
    lv_style_set_bg_opa(&style_section, LV_OPA_50);
    lv_style_set_radius(&style_section, 12);
    lv_style_set_border_width(&style_section, 1);
    lv_style_set_border_color(&style_section, lv_color_hex(0x0f3460));
    lv_style_set_border_opa(&style_section, LV_OPA_80);

    /* Section title */
    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, TEXT_PRIMARY);
    lv_style_set_text_font(&style_title, &lv_font_montserrat_16);

    /* Arc background (gray ring) */
    lv_style_init(&style_arc_bg);
    lv_style_set_arc_color(&style_arc_bg, lv_color_hex(0x1f3050));
    lv_style_set_arc_width(&style_arc_bg, 12);

    /* Bar background */
    lv_style_init(&style_bar_bg);
    lv_style_set_bg_color(&style_bar_bg, lv_color_hex(0x1f3050));
    lv_style_set_radius(&style_bar_bg, 4);

    /* Bar indicator (color set per widget) */
    lv_style_init(&style_bar_indicator);
    lv_style_set_radius(&style_bar_indicator, 4);

    /* Large value text (temperature inside arc) */
    lv_style_init(&style_value_label);
    lv_style_set_text_color(&style_value_label, TEXT_PRIMARY);
    lv_style_set_text_font(&style_value_label, &lv_font_montserrat_16);

    /* Sub label (name, usage text) */
    lv_style_init(&style_sub_label);
    lv_style_set_text_color(&style_sub_label, TEXT_SECONDARY);
    lv_style_set_text_font(&style_sub_label, &lv_font_montserrat_12);
}

/* ================================================================
 *  Helper: create a temperature arc gauge
 * ================================================================ */
static lv_obj_t *create_temp_arc(lv_obj_t *parent, lv_color_t accent,
                                  const char *title_text,
                                  lv_obj_t **temp_label_out,
                                  lv_obj_t **name_label_out)
{
    /* Arc */
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, ARC_SIZE, ARC_SIZE);
    lv_obj_center(arc);
    lv_obj_add_style(arc, &style_arc_bg, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, accent, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 12, LV_PART_INDICATOR);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);

    /* Temperature text in center */
    lv_obj_t *temp_label = lv_label_create(parent);
    lv_label_set_text(temp_label, "--°");
    lv_obj_add_style(temp_label, &style_value_label, 0);
    lv_obj_align(temp_label, LV_ALIGN_TOP_MID, 0, ARC_Y + 10);

    /* Title (CPU / GPU) top-left */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, title_text);
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);

    /* Name label below arc */
    lv_obj_t *name_label = lv_label_create(parent);
    lv_label_set_text(name_label, "Waiting...");
    lv_obj_add_style(name_label, &style_sub_label, 0);
    lv_obj_align(name_label, LV_ALIGN_TOP_MID, 0, ARC_Y + ARC_SIZE / 2 + 4);

    if (temp_label_out) *temp_label_out = temp_label;
    if (name_label_out) *name_label_out = name_label;

    return arc;
}

/* ================================================================
 *  Helper: create a usage bar with label
 * ================================================================ */
static void create_usage_bar(lv_obj_t *parent, lv_obj_t **bar_out,
                              lv_obj_t **label_out, lv_color_t color,
                              int y_offset, const char *label_text)
{
    /* Label */
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, label_text);
    lv_obj_add_style(label, &style_sub_label, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y_offset);

    /* Bar */
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, BAR_W, BAR_H);
    lv_obj_add_style(bar, &style_bar_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    lv_obj_add_style(bar, &style_bar_indicator, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_ON);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, y_offset + 18);

    if (bar_out) *bar_out = bar;
    if (label_out) *label_out = label;
}

/* ================================================================
 *  Section builders
 * ================================================================ */

static lv_obj_t *build_cpu_section(lv_obj_t *container)
{
    lv_obj_t *sec = lv_obj_create(container);
    lv_obj_set_size(sec, SCREEN_W, SECTION_H);
    lv_obj_add_style(sec, &style_section, 0);
    lv_obj_set_scrollbar_mode(sec, LV_SCROLLBAR_MODE_OFF);

    cpu_arc = create_temp_arc(sec, CPU_ACCENT, "CPU",
                               &cpu_temp_label, &cpu_name_label);

    create_usage_bar(sec, &cpu_bar, &cpu_usage_label, CPU_ACCENT,
                     ARC_Y + ARC_SIZE / 2 + 24, "Usage: --%");

    cpu_freq_label = lv_label_create(sec);
    lv_label_set_text(cpu_freq_label, "--- MHz");
    lv_obj_add_style(cpu_freq_label, &style_sub_label, 0);
    lv_obj_align(cpu_freq_label, LV_ALIGN_TOP_MID, 0,
                 ARC_Y + ARC_SIZE / 2 + 50);

    cpu_section = sec;  /* store for auto-scroll */
    return sec;
}

static lv_obj_t *build_gpu_section(lv_obj_t *container)
{
    lv_obj_t *sec = lv_obj_create(container);
    lv_obj_set_size(sec, SCREEN_W, SECTION_H);
    lv_obj_add_style(sec, &style_section, 0);
    lv_obj_set_scrollbar_mode(sec, LV_SCROLLBAR_MODE_OFF);

    gpu_arc = create_temp_arc(sec, GPU_ACCENT, "GPU",
                               &gpu_temp_label, &gpu_name_label);

    create_usage_bar(sec, &gpu_bar, &gpu_usage_label, GPU_ACCENT,
                     ARC_Y + ARC_SIZE / 2 + 24, "Usage: --%");

    /* VRAM bar */
    gpu_vram_label = lv_label_create(sec);
    lv_label_set_text(gpu_vram_label, "VRAM: -- / -- MB");
    lv_obj_add_style(gpu_vram_label, &style_sub_label, 0);
    lv_obj_align(gpu_vram_label, LV_ALIGN_TOP_MID, 0,
                 ARC_Y + ARC_SIZE / 2 + 50);

    gpu_vram_bar = lv_bar_create(sec);
    lv_obj_set_size(gpu_vram_bar, BAR_W, BAR_H);
    lv_obj_add_style(gpu_vram_bar, &style_bar_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_color(gpu_vram_bar, GPU_ACCENT, LV_PART_INDICATOR);
    lv_obj_add_style(gpu_vram_bar, &style_bar_indicator, LV_PART_INDICATOR);
    lv_bar_set_range(gpu_vram_bar, 0, 100);
    lv_bar_set_value(gpu_vram_bar, 0, LV_ANIM_ON);
    lv_obj_align(gpu_vram_bar, LV_ALIGN_TOP_MID, 0,
                 ARC_Y + ARC_SIZE / 2 + 70);

    gpu_section = sec;  /* store for auto-scroll */
    return sec;
}

static lv_obj_t *build_system_section(lv_obj_t *container)
{
    lv_obj_t *sec = lv_obj_create(container);
    lv_obj_set_size(sec, SCREEN_W, SYS_SECTION_H);
    lv_obj_add_style(sec, &style_section, 0);
    lv_obj_set_scrollbar_mode(sec, LV_SCROLLBAR_MODE_OFF);

    /* Title */
    lv_obj_t *title = lv_label_create(sec);
    lv_label_set_text(title, "System");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);

    /* RAM bar */
    ram_label = lv_label_create(sec);
    lv_label_set_text(ram_label, "RAM: -- / -- GB");
    lv_obj_add_style(ram_label, &style_sub_label, 0);
    lv_obj_align(ram_label, LV_ALIGN_TOP_MID, 0, 40);

    ram_bar = lv_bar_create(sec);
    lv_obj_set_size(ram_bar, BAR_W, BAR_H);
    lv_obj_add_style(ram_bar, &style_bar_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ram_bar, RAM_ACCENT, LV_PART_INDICATOR);
    lv_obj_add_style(ram_bar, &style_bar_indicator, LV_PART_INDICATOR);
    lv_bar_set_range(ram_bar, 0, 100);
    lv_bar_set_value(ram_bar, 0, LV_ANIM_ON);
    lv_obj_align(ram_bar, LV_ALIGN_TOP_MID, 0, 60);

    /* Disk bars (up to MAX_DISKS) */
    for (int i = 0; i < MAX_DISKS; i++) {
        disk_labels[i] = lv_label_create(sec);
        lv_label_set_text(disk_labels[i], "");
        lv_obj_add_style(disk_labels[i], &style_sub_label, 0);
        lv_obj_align(disk_labels[i], LV_ALIGN_TOP_MID, 0, 80 + i * 38);

        disk_bars[i] = lv_bar_create(sec);
        lv_obj_set_size(disk_bars[i], BAR_W, BAR_H);
        lv_obj_add_style(disk_bars[i], &style_bar_bg, LV_PART_MAIN);
        lv_obj_set_style_bg_color(disk_bars[i], DISK_ACCENT, LV_PART_INDICATOR);
        lv_obj_add_style(disk_bars[i], &style_bar_indicator, LV_PART_INDICATOR);
        lv_bar_set_range(disk_bars[i], 0, 100);
        lv_bar_set_value(disk_bars[i], 0, LV_ANIM_OFF);
        lv_obj_align(disk_bars[i], LV_ALIGN_TOP_MID, 0, 98 + i * 38);
    }

    /* Uptime */
    uptime_label = lv_label_create(sec);
    lv_label_set_text(uptime_label, "Uptime: --");
    lv_obj_add_style(uptime_label, &style_sub_label, 0);
    lv_obj_align(uptime_label, LV_ALIGN_BOTTOM_MID, 0, -16);

    sys_section = sec;  /* store for auto-scroll */
    return sec;
}

/* ================================================================
 *  Public: create the full dashboard
 * ================================================================ */
void ui_monitor_create(const char *build_tag)
{
    init_styles();

    scr = lv_screen_active();
    lv_obj_add_style(scr, &style_bg, 0);

    /*
     * Scrollable page container.
     * With snap scrolling + auto-scroll cycling through sections.
     */
    scroll_page = lv_obj_create(scr);
    lv_obj_set_size(scroll_page, SCREEN_W, SCREEN_W);     /* Viewport = screen size */
    lv_obj_set_flex_flow(scroll_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(scroll_page, 8, 0);
    lv_obj_set_style_pad_all(scroll_page, 4, 0);
    lv_obj_set_style_bg_opa(scroll_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll_page, 0, 0);
    lv_obj_set_scrollbar_mode(scroll_page, LV_SCROLLBAR_MODE_OFF);

    /* Build sections */
    build_cpu_section(scroll_page);
    build_gpu_section(scroll_page);
    build_system_section(scroll_page);

    /* Enable snap scrolling for smooth section transitions */
    lv_obj_set_scroll_snap_y(scroll_page, LV_SCROLL_SNAP_START);
    lv_obj_set_scroll_dir(scroll_page, LV_DIR_VER);

    /* Init auto-scroll timer */
    auto_last_scroll = millis();
    auto_section_idx = 0;

    /* Status bar at very top (outside scroll) — connection status */
    status_label = lv_label_create(scr);
    lv_label_set_text(status_label, build_tag ? build_tag : "Waiting...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xffaa00), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 4);
}

/* ================================================================
 *  Helper: get temperature color
 * ================================================================ */
static lv_color_t temp_color(int8_t temp)
{
    if (temp < 0)  return TEXT_SECONDARY;
    if (temp < 45) return TEMP_COLD;
    if (temp < 70) return TEMP_WARM;
    return TEMP_HOT;
}

/* ================================================================
 *  Helper: format uptime
 * ================================================================ */
static void format_uptime(char *buf, size_t len, uint32_t secs)
{
    uint32_t d = secs / 86400;
    uint32_t h = (secs % 86400) / 3600;
    uint32_t m = (secs % 3600) / 60;
    if (d > 0)
        snprintf(buf, len, "Uptime: %lud %luh", d, h);
    else if (h > 0)
        snprintf(buf, len, "Uptime: %luh %lum", h, m);
    else
        snprintf(buf, len, "Uptime: %lum", m);
}

/* ================================================================
 *  Public: refresh UI with new data
 * ================================================================ */
void ui_monitor_refresh(pc_data_t *data)
{
    if (!data) return;

    char buf[128];

    /* ---- Diagnostic status: ALWAYS update, even without valid frames ---- */
    if (data->frame_count > 0) {
        /* Got valid frames — show live counter */
        uint32_t elapsed = millis() - data->last_recv_ms;
        if (elapsed < 3000) {
            snprintf(buf, sizeof(buf), "Live #%lu", data->frame_count);
            lv_obj_set_style_text_color(status_label, lv_color_hex(0x00ff88), 0);
        } else {
            snprintf(buf, sizeof(buf), "Stale #%lu", data->frame_count);
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xffaa00), 0);
        }
        lv_label_set_text(status_label, buf);
    } else if (data->raw_bytes == 0) {
        /* No data at all — PC side might not be sending */
        snprintf(buf, sizeof(buf), "Waiting data...");
        lv_label_set_text(status_label, buf);
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xffaa00), 0);
    } else if (data->stx_count > 0) {
        /* Got STX markers but no complete frame — buffer or parse issue */
        snprintf(buf, sizeof(buf), "RX:%lu STX:%lu E:%lu",
                 data->raw_bytes, data->stx_count, data->parse_errs);
        lv_label_set_text(status_label, buf);
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xffaa00), 0);
    } else {
        /* Got raw bytes but no STX detected — wrong data or garbage */
        snprintf(buf, sizeof(buf), "Raw:%lub noSTX", data->raw_bytes);
        lv_label_set_text(status_label, buf);
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xff4444), 0);
    }

    /* ---- Auto-scroll: cycle through sections regardless of data ---- */
    if (scroll_page && auto_last_scroll > 0) {
        uint32_t now = millis();
        if (now - auto_last_scroll >= AUTO_SCROLL_MS) {
            auto_last_scroll = now;
            auto_section_idx = (auto_section_idx + 1) % 3;
            lv_obj_t *sections[] = { cpu_section, gpu_section, sys_section };
            if (sections[auto_section_idx]) {
                lv_obj_scroll_to_view(sections[auto_section_idx], LV_ANIM_ON);
            }
        }
    }

    if (!data->updated) return;  /* only full refresh below when new frame arrived */

    /* ---- CPU ---- */
    /* Arc shows usage% (always available), temp shown as text */
    lv_arc_set_value(cpu_arc, data->cpu.usage);
    if (data->cpu.temp >= 0) {
        snprintf(buf, sizeof(buf), "%d°C", data->cpu.temp);
        lv_obj_set_style_text_color(cpu_temp_label, temp_color(data->cpu.temp), 0);
    } else {
        snprintf(buf, sizeof(buf), "%u%%", data->cpu.usage);
        lv_obj_set_style_text_color(cpu_temp_label, CPU_ACCENT, 0);
    }
    lv_label_set_text(cpu_temp_label, buf);

    lv_label_set_text(cpu_name_label, data->cpu.name[0] ? data->cpu.name : "Unknown");
    lv_bar_set_value(cpu_bar, data->cpu.usage, LV_ANIM_ON);
    snprintf(buf, sizeof(buf), "Usage: %u%%", data->cpu.usage);
    lv_label_set_text(cpu_usage_label, buf);
    snprintf(buf, sizeof(buf), "%u MHz", data->cpu.freq);
    lv_label_set_text(cpu_freq_label, buf);

    /* ---- GPU ---- */
    /* Arc shows usage%, temp as text */
    lv_arc_set_value(gpu_arc, data->gpu.usage);
    if (data->gpu.temp >= 0) {
        snprintf(buf, sizeof(buf), "%d°C", data->gpu.temp);
        lv_obj_set_style_text_color(gpu_temp_label, temp_color(data->gpu.temp), 0);
    } else {
        snprintf(buf, sizeof(buf), "%u%%", data->gpu.usage);
        lv_obj_set_style_text_color(gpu_temp_label, GPU_ACCENT, 0);
    }
    lv_label_set_text(gpu_temp_label, buf);

    lv_label_set_text(gpu_name_label, data->gpu.name[0] ? data->gpu.name : "No GPU");
    lv_bar_set_value(gpu_bar, data->gpu.usage, LV_ANIM_ON);
    snprintf(buf, sizeof(buf), "Usage: %u%%", data->gpu.usage);
    lv_label_set_text(gpu_usage_label, buf);

    if (data->gpu.mem_total > 0) {
        int vram_pct = (int)((uint64_t)data->gpu.mem_used * 100 / data->gpu.mem_total);
        lv_bar_set_value(gpu_vram_bar, vram_pct, LV_ANIM_ON);
        snprintf(buf, sizeof(buf), "VRAM: %lu / %lu MB",
                 data->gpu.mem_used, data->gpu.mem_total);
        lv_label_set_text(gpu_vram_label, buf);
    }

    /* ---- RAM ---- */
    if (data->ram.total > 0) {
        int ram_pct = (int)((uint64_t)data->ram.used * 100 / data->ram.total);
        lv_bar_set_value(ram_bar, ram_pct, LV_ANIM_ON);
        snprintf(buf, sizeof(buf), "RAM: %.1f / %.1f GB",
                 data->ram.used / 1024.0, data->ram.total / 1024.0);
        lv_label_set_text(ram_label, buf);
    }

    /* ---- Disks ---- */
    for (int i = 0; i < MAX_DISKS; i++) {
        if (i < data->disk_count && data->disks[i].total > 0) {
            int pct = (int)((uint64_t)data->disks[i].used * 100 / data->disks[i].total);
            lv_bar_set_value(disk_bars[i], pct, LV_ANIM_ON);
            snprintf(buf, sizeof(buf), "%s: %.0f / %.0f GB",
                     data->disks[i].name,
                     data->disks[i].used / 1024.0,
                     data->disks[i].total / 1024.0);
            lv_label_set_text(disk_labels[i], buf);
            lv_obj_clear_flag(disk_bars[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(disk_labels[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(disk_bars[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(disk_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* ---- Uptime ---- */
    format_uptime(buf, sizeof(buf), data->uptime);
    lv_label_set_text(uptime_label, buf);

    /* Consume the update */
    data->updated = false;
}
