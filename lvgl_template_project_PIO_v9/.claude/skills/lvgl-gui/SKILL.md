---
name: lvgl-gui
description: Build production-quality LVGL GUI programs for embedded/desktop devices. Use when asked to create an LVGL screen, widget, UI component, or complete GUI application. Integrates design-first workflow (brainstorming) with aesthetic excellence (frontend-design) adapted for LVGL v9.5.0.
---

# LVGL GUI Design & Development

Build distinctive, polished LVGL v9.5.0 GUIs by combining design thinking with deep platform knowledge. This skill covers the full workflow: requirement exploration → aesthetic direction → structured design → working C/C++ code.

## Platform Context

This project targets **ESP32-S3 (Adafruit Feather) with Arduino framework** and a 240×240 SPI TFT LCD (ST7789). The codebase uses C++ (`src/*.cpp`, `include/*.h`). Key integration points:

- **LVGL library**: `lib/lvgl/` — LVGL v9.5.0
- **Display config**: `lib/lvgl/lv_conf.h` — color depth, feature flags
- **Display driver**: `lib/TFT_eSPI/` + `src/display.cpp` — SPI TFT + LVGL flush callback
- **Pin mapping**: `lib/TFT_eSPI/User_Setup.h` — TFT pins, driver chip, SPI config
- **Entry point**: `src/main.cpp` — `screen.init()` then LVGL widgets/demos

The `Display` class wraps TFT_eSPI + LVGL initialization, setting up a 240×240 display with partial render mode and `millis()` tick source.

## Workflow

Every LVGL GUI task follows this sequence. **Do NOT skip steps.**

### 1. Explore & Clarify (brainstorming mode)

Before any code, understand the context:

- **Purpose**: What device is this for? (embedded screen / PC simulator / both). What display resolution?
- **Interaction**: Touch? Keyboard? Rotary encoder? Mouse only?
- **Constraints**: RAM budget, flash size, color depth (16/32-bit), real-time requirements. This ESP32-S3 has 2MB PSRAM.
- **Visual tone**: Pick one intentional direction — brutally minimal, luxury/refined, playful/toy-like, industrial/utilitarian, retro-futuristic, dark-tech, organic/nature. **No generic "modern clean UI."**
- **One question at a time**. Never dump a list of questions.

### 2. Design the UI (frontend-design mode)

Present the design for user approval BEFORE coding:

- **Screen layout sketch**: Describe widget hierarchy and spatial composition. Use asymmetry and intentional negative space.
- **Color system**: 3-5 cohesive colors using `lv_color_hex()` or `lv_palette_*()`. A dominant background + 1-2 accent colors + text. Avoid evenly-distributed palettes.
- **Typography**: Pick 2-3 font sizes from LVGL's built-in Montserrat (`lv_font_montserrat_14/16/18/24/28/…`). Pair a large display font with a smaller body font.
- **Motion**: Where will animations add delight? Entrance fades, spring-bounce transitions (`lv_anim_path_overshoot`), hover/press effects.
- **State management**: What data drives the UI? Timers? Sensor input? Use `lv_subject_t` for MVVM-style bindings where appropriate.

### 3. Implement (LVGL v9.5.0 C/C++ code)

Write production C/C++ code following LVGL v9.5.0 idioms. Reference the examples in `lib/lvgl/examples/` and `lib/demos/` as patterns. Place new source files in `src/`, headers in `include/`.

## LVGL v9.5.0 Quick Reference

### Architecture (this project)

```
Display::init()           → lv_init(), create display (240×240), set buffer + flush_cb
Display::update()         → lv_timer_handler() — called in loop() every cycle
TFT_eSPI                  → handles SPI writes, pushColors() in flush callback
```

### Object Lifecycle

```c
lv_obj_t * obj = lv_<widget>_create(parent);  // parent = screen or container
lv_obj_set_size(obj, w, h);
lv_obj_set_pos(obj, x, y);                     // or use layouts
lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);      // show/hide
lv_obj_add_state(obj, LV_STATE_DISABLED);      // state control
lv_obj_clean(obj);                             // delete all children
lv_obj_delete(obj);                            // delete self + children
```

### Widget Catalog (40+ types)

| Category | Widgets | Key create function |
|---|---|---|
| **Basic** | label, button, image, line, led | `lv_label_create()` |
| **Input** | textarea, keyboard, slider, switch, checkbox, dropdown, roller, spinbox, ime | `lv_slider_create()` |
| **Visual** | arc, bar, chart, scale, span, spinner, calendar | `lv_arc_create()` |
| **Containers** | obj (base), tabview, tileview, win, menu, list, msgbox | `lv_tabview_create()` |
| **Media** | canvas, lottie, gif, animimage, imagebutton | `lv_canvas_create()` |
| **Layout** | buttonmatrix, table | `lv_buttonmatrix_create()` |

### Styles — The Core of LVGL Look

Styles cascade: **local → inherited → theme defaults**. Every visual property is a style.

```c
static lv_style_t style_btn;
lv_style_init(&style_btn);
lv_style_set_radius(&style_btn, 12);
lv_style_set_bg_color(&style_btn, lv_color_hex(0x6366f1));
lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
lv_style_set_text_color(&style_btn, lv_color_hex(0xffffff));
lv_style_set_pad_all(&style_btn, 16);

lv_obj_t * btn = lv_button_create(parent);
lv_obj_add_style(btn, &style_btn, 0);                    // default state
lv_obj_add_style(btn, &style_pressed, LV_STATE_PRESSED);  // pressed state
lv_obj_add_style(btn, &style_focused, LV_STATE_FOCUSED);  // focused state
```

**Key style properties by category:**
- **Background**: `bg_color`, `bg_opa`, `bg_grad_color`, `bg_grad_dir`, `bg_main_stop`, `bg_grad_stop`
- **Border**: `border_color`, `border_width`, `border_opa`, `border_side`, `radius`
- **Outline**: `outline_color`, `outline_width`, `outline_opa`, `outline_pad`
- **Shadow**: `shadow_color`, `shadow_width`, `shadow_ofs_x/y`, `shadow_opa`
- **Text**: `text_color`, `text_font`, `text_align`, `text_letter_space`, `text_line_space`
- **Transform**: `transform_scale_x/y`, `transform_rotation`, `transform_pivot_x/y`
- **Layout**: `layout`, `flex_flow`, `flex_align`, `grid_column_dsc_array`, `grid_row_dsc_array`, `pad_*`
- **Arc-specific**: `arc_color`, `arc_width`, `arc_rounded`, `arc_image_src`
- **Slider-specific**: (use `lv_slider_set_*` functions for values)

**Working with states and parts:**
```c
lv_obj_add_style(btn, &style, LV_STATE_PRESSED | LV_PART_MAIN);   // state + part combo
lv_obj_add_style(arc, &style_indicator, LV_PART_INDICATOR);       // widget parts
lv_obj_add_style(slider, &style_knob, LV_PART_KNOB);
```

### Layouts — Flex and Grid

**Flex** (1D): Use for toolbars, button rows, lists, centered groups.
```c
lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);        // wrap when overflow
lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY,    // main axis
                            LV_FLEX_ALIGN_CENTER,           // cross axis
                            LV_FLEX_ALIGN_CENTER);          // track (wrap) axis
lv_obj_set_style_flex_main_place(cont, LV_FLEX_ALIGN_CENTER, 0);  // for flex on screen
lv_obj_set_style_flex_grow(child, 1);                      // child stretches to fill
```

**Grid** (2D): Use for dashboards, keypads, form layouts, icon grids.
```c
static const int32_t col_dsc[] = {120, 120, 120, LV_GRID_TEMPLATE_LAST};
static const int32_t row_dsc[] = {60, 60, 60, LV_GRID_TEMPLATE_LAST};
lv_obj_set_style_grid_column_dsc_array(cont, col_dsc, 0);
lv_obj_set_style_grid_row_dsc_array(cont, row_dsc, 0);
lv_obj_set_style_layout(cont, LV_LAYOUT_GRID, 0);

// Position each child:
lv_obj_set_style_grid_cell_column_pos(child, col, 0);
lv_obj_set_style_grid_cell_row_pos(child, row, 0);
lv_obj_set_style_grid_cell_x_align(child, LV_GRID_ALIGN_STRETCH, 0);  // fill cell
```

**FR units** (proportional columns):
```c
static const int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(2), LV_GRID_TEMPLATE_LAST};
```

### Events

```c
// One callback, filter by event code
lv_obj_add_event_cb(obj, cb, LV_EVENT_ALL, user_data);

static void cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target_obj(e);
    void * data = lv_event_get_user_data(e);

    if(code == LV_EVENT_CLICKED) { /* ... */ }
    else if(code == LV_EVENT_VALUE_CHANGED) {
        int32_t val = lv_slider_get_value(target);
    }
}
```

**Common event codes**: `LV_EVENT_CLICKED`, `LV_EVENT_PRESSED`, `LV_EVENT_VALUE_CHANGED`, `LV_EVENT_DRAW_POST_END`, `LV_EVENT_SCROLL`, `LV_EVENT_DELETE`.

### Animations

```c
lv_anim_t a;
lv_anim_init(&a);
lv_anim_set_var(&a, obj);                              // target object
lv_anim_set_exec_cb(&a, anim_x_cb);                    // callback that sets property
lv_anim_set_values(&a, start_val, end_val);
lv_anim_set_duration(&a, 500);                         // ms
lv_anim_set_path_cb(&a, lv_anim_path_overshoot);       // easing
lv_anim_set_playback_duration(&a, 300);                // auto-reverse
lv_anim_start(&a);

// Custom exec callback — set any property
static void anim_x_cb(void * var, int32_t v) {
    lv_obj_set_x((lv_obj_t *)var, v);
}
```

**Built-in easing paths**:
- `lv_anim_path_linear` — constant speed
- `lv_anim_path_ease_in/out/in_out` — smooth acceleration
- `lv_anim_path_overshoot` — spring bounce (great for attention-grabbing)
- `lv_anim_path_bounce` — bouncing ball

### Style Transitions (auto-animate on state change)

```c
static const lv_style_prop_t props[] = {
    LV_STYLE_BG_COLOR, LV_STYLE_BORDER_COLOR, 0  // properties to animate
};
static lv_style_transition_dsc_t trans;
lv_style_transition_dsc_init(&trans, props, lv_anim_path_ease_out, 300, 0, NULL);
lv_style_set_transition(&style_pressed, &trans);   // animate when this style activates
```

### Subject/Observer (MVVM binding)

```c
static lv_subject_t subject;
lv_subject_init_int(&subject, 50);                     // initial value
lv_slider_bind_value(slider, &subject);                // two-way bind
lv_label_bind_text(label, &subject, "Value: %d");      // one-way bind with format
lv_arc_bind_value(arc, &subject);                      // same subject, multiple views
```

### Custom Drawing (for unique widgets)

```c
lv_obj_add_event_cb(obj, draw_cb, LV_EVENT_DRAW_POST_END, user_data);

static void draw_cb(lv_event_t * e) {
    lv_layer_t * layer = lv_event_get_layer(e);

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0xff372e);
    line_dsc.width = 2;
    line_dsc.round_start = 1;
    line_dsc.round_end = 1;
    line_dsc.p1.x = 10; line_dsc.p1.y = 10;
    line_dsc.p2.x = 100; line_dsc.p2.y = 100;
    lv_draw_line(layer, &line_dsc);

    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.center.x = 50; arc_dsc.center.y = 50;
    arc_dsc.radius = 30;
    arc_dsc.width = 4;
    arc_dsc.color = lv_color_hex(0xffffff);
    arc_dsc.start_angle = 0;    // 0.1° units!
    arc_dsc.end_angle = 2700;   // 270°
    lv_draw_arc(layer, &arc_dsc);
}
```

**Available draw descriptors**: `lv_draw_line_dsc_t`, `lv_draw_arc_dsc_t`, `lv_draw_rect_dsc_t`, `lv_draw_label_dsc_t`, `lv_draw_image_dsc_t`, `lv_draw_triangle_dsc_t`.

### Timers (periodic updates)

```c
lv_timer_t * timer = lv_timer_create(timer_cb, period_ms, user_data);

static void timer_cb(lv_timer_t * timer) {
    my_state_t * state = lv_timer_get_user_data(timer);
    // update labels, invalidate widgets...
    lv_obj_invalidate(state->widget);  // trigger redraw
}
```

### Colors and Palettes

```c
lv_color_hex(0x6366f1)            // from RGB hex
lv_color_make(r, g, b)            // from 0-255 components
lv_palette_main(LV_PALETTE_BLUE)  // palette main color
lv_palette_lighten(p, level)      // lighter variant
lv_palette_darken(p, level)       // darker variant
lv_color_darken(color, opa)       // darken by opacity
lv_color_white() / lv_color_black()
```

### Screen Management

```c
lv_obj_t * scr = lv_screen_active();       // current screen
lv_obj_t * new_scr = lv_obj_create(NULL);  // NO parent = new screen
lv_scr_load(new_scr);                      // switch screens (with optional animation)
lv_scr_load_anim(new_scr, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
```

### Size Helpers

```c
lv_pct(50)           // 50% of parent
LV_SIZE_CONTENT      // auto-fit content
LV_DPX(n)            // convert physical px to display px (using DPI)
lv_obj_set_content_width(obj, w);  // for auto-sizing
```

## Code Patterns from Official Demos

### Pattern 1: Reusable Component (demos/widgets pattern)
```c
// In my_component.h
void my_component_create(lv_obj_t * parent);

// In my_component.c
void my_component_create(lv_obj_t * parent) {
    lv_obj_t * cont = lv_obj_create(parent);
    lv_obj_set_size(cont, lv_pct(100), LV_SIZE_CONTENT);
    // ... build the component
}
```

### Pattern 2: Multi-tab Application (demos/widgets)
```c
lv_obj_t * tv = lv_tabview_create(lv_screen_active());
lv_obj_t * tab1 = lv_tabview_add_tab(tv, "Tab 1");
lv_obj_t * tab2 = lv_tabview_add_tab(tv, "Tab 2");
create_tab1_content(tab1);
create_tab2_content(tab2);
```

### Pattern 3: Style Pool (demos/widgets/components)
```c
// Shared styles initialized once, reused everywhere
lv_style_t style_title;
lv_style_t style_text_muted;
lv_style_t style_icon;

void styles_init(void) {
    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, &lv_font_montserrat_24);
    // ... etc
}
```

### Pattern 4: This Project's Display Integration
```cpp
// In src/main.cpp — typical app structure
#include <Arduino.h>
#include "display.h"

Display screen;

void setup() {
    Serial.begin(9600);
    screen.init();  // lv_init() + display setup

    // Build your GUI here
    lv_obj_t * label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Hello, LVGL!");
}

void loop() {
    screen.update();  // calls lv_timer_handler()
}
```

## Design Checklist

Before finalizing code, verify:

- [ ] **Color depth matches target**: This project uses `LV_COLOR_DEPTH 16` (RGB565) — gradients will show banding
- [ ] **Fonts are enabled in lv_conf.h**: e.g., `LV_FONT_MONTSERRAT_24`
- [ ] **Memory budget**: each widget ~200-500 bytes; images/lottie/canvas much more. ESP32-S3 has 2MB PSRAM available.
- [ ] **Layout uses flex/grid**, not absolute positioning (except for custom-drawn widgets)
- [ ] **Styles are init-once static**, not created per-widget
- [ ] **Animations have matching durations** (200-500ms for micro, 500-800ms for emphasis)
- [ ] **Touch targets are ≥ 44×44** display px for touch screens
- [ ] **Dark/light mode** uses palette or `lv_style_theme_*()` functions
- [ ] **No magic numbers** — use `#define` or `static const` for sizes, colors, timing

## Gotchas

- **`lv_style_init()` is heap-allocating**: call once with a static var, never per-object
- **Arc/angle values are in 0.1° units**: `3600` = full circle, `900` = 90°
- **`lv_obj_remove_style_all(obj)` removes size/position too**: set them AFTER this call
- **`LV_EVENT_DRAW_POST_END` draws on top**: use for overlays, custom graphics. `DRAW_POST` draws before children.
- **`lv_pct()` is relative to parent's content area**, not padding area
- **Screen with `lv_obj_create(NULL)` has no parent styles**: set everything explicitly
- **Timer callbacks run in `lv_timer_handler()` context**: don't block, don't call `lv_timer_handler()` recursively
- **`LV_FONT_DEFAULT` depends on lv_conf.h**: specify explicit fonts for consistent appearance
- **Grid cell children need explicit positions**: unlike HTML grid, LVGL doesn't auto-flow (set `column_pos`/`row_pos` per child)

## Project-Specific Notes

- **Display is 240×240 pixels, round/square shape** (WaveShare ESP32-S3-Touch-LCD-1.28, ST7789)
- **Code is C++** — use `.cpp`/`.h` files. The Arduino `setup()`/`loop()` pattern is the entry point.
- **Serial is 9600 baud** for debug output via `Serial.print()`
- **Build with `pio run`** in the project root; upload with `pio run --target upload`
- **LVGL config** is at `lib/lvgl/lv_conf.h` — enable/disable features and fonts there
- **TFT pin mapping** is in `lib/TFT_eSPI/User_Setup.h` — change display pins there, not in code
- **The `Display` class** in `src/display.cpp` handles all LVGL init — new widgets go in setup() or separate files

## Reference Materials

- **Official docs**: https://docs.lvgl.io/ (v9.5.0)
- **LVGL examples**: `lib/lvgl/examples/` — every widget, style, layout, animation
- **LVGL demos**: `lib/demos/` (music, benchmark, stress, etc.)
- **Display driver**: `src/display.cpp` — LVGL v9 display creation and flush callback
- **Display header**: `include/display.h` — Display class API
