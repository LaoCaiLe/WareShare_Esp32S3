/**
 * @file display.cpp
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "display.h"
#include <stdbool.h>

/*********************
 *      DEFINES
 *********************/

#define MY_DISP_HOR_RES 240

#define MY_DISP_VER_RES 240

/**********************
 *  STATIC VARIABLES
 **********************/

TFT_eSPI tft = TFT_eSPI();
/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    // uint32_t w = lv_area_get_width(area);
    // uint32_t h = lv_area_get_height(area);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.setSwapBytes(true);
    tft.pushColors((uint16_t*)color_p, w * h);
    tft.endWrite();

    lv_display_flush_ready(disp);
}

uint32_t my_get_millis(void)
{
    return millis();
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void Display::init(void)
{
    /* Init Paint */
    tft.begin();
    tft.fillScreen(TFT_ORANGE);
    tft.setRotation(0);

    /* Init lvgl */
    lv_init();

    /* Init ePaper Screen */
    lv_tick_set_cb(my_get_millis);

    /*-----------------------------
     * Create a buffer for drawing
     *----------------------------*/
    // static lv_disp_draw_buf_t draw_buf_dsc_1;
    // static lv_color_t buf_1[MY_DISP_HOR_RES * MY_DISP_VER_RES / 2];                             /*A buffer for 10 rows*/
    // static lv_color_t buf_2[MY_DISP_HOR_RES * MY_DISP_VER_RES / 10];                            /*A buffer for 10 rows*/
    // lv_disp_draw_buf_init(&draw_buf_dsc_1, buf_1, NULL, MY_DISP_HOR_RES * MY_DISP_VER_RES / 2); /*Initialize the display buffer*/

    // /*-----------------------------------
    //  * Register the display in LVGL
    //  *----------------------------------*/
    // static lv_disp_drv_t disp_drv; /*Descriptor of a display driver*/
    // lv_disp_drv_init(&disp_drv);   /*Basic initialization*/

    // /*Set up the functions to access to your display*/

    // /*Set the resolution of the display*/
    // disp_drv.hor_res = MY_DISP_HOR_RES;
    // disp_drv.ver_res = MY_DISP_VER_RES;

    // /*Used to copy the buffer's content to the display*/
    // disp_drv.flush_cb = disp_flush;

    // /*Set a display buffer*/
    // disp_drv.draw_buf = &draw_buf_dsc_1;

    // /*Finally register the driver*/
    // lv_disp_drv_register(&disp_drv);

    lv_display_t *display = lv_display_create(240, 240);
    /* 双缓冲: LVGL 在 buf1 渲染时，SPI 可以同时发送 buf2，避免撕裂 */
    static lv_color_t buf1[240 * 240 / 2];
    static lv_color_t buf2[240 * 10 / 2];
    lv_display_set_buffers(display, buf1, NULL, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    /* This callback will display the rendered image */
    lv_display_set_flush_cb(display, disp_flush);
    /* Create widgets */
}

void Display::update(void)
{
    lv_timer_handler();
    // delay(5); /* let this time pass */
}

/**
 * @brief flip the black and white color of epaper
 *
 */
