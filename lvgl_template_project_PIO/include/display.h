#ifndef DISPLAY_H
#define DISPLAY_H

#include <lvgl.h>
#include <TFT_eSPI.h>

class Display
{
private:

public:
	void init(void);
	void update(void);
};

/* Number of completed LVGL refreshes since boot (FPS diagnostics). */
uint32_t display_frame_count(void);

/* Backlight PWM duty, 0..255 (16 is the practical minimum here). */
void display_set_backlight(uint8_t level);

/* Panel orientation: 0 = 0°, 1 = 180°. Clears the panel and forces a full
 * LVGL redraw when the orientation actually changes. */
void display_set_rotation(uint8_t flipped);

#endif
