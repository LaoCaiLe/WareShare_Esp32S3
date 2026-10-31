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

#endif
