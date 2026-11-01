/**
 * @file display.cpp
 * @brief GC9A01 240x240 LCD display driver with LVGL double-buffering
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

#define MY_DISP_HOR_RES   240
#define MY_DISP_VER_RES   240

/* Backlight PWM. TFT_eSPI only switches TFT_BL fully on; driving it from
 * LEDC lets the LED run at part duty, which is the second biggest heat
 * source on this board after the radio. 0..255: lower = dimmer/cooler. */
#define BL_LEDC_CHANNEL   7
#define BL_LEDC_FREQ      5000
#define BL_LEDC_RES       8
#define BL_LEVEL          160

/**********************
 *  STATIC VARIABLES
 **********************/

TFT_eSPI tft = TFT_eSPI();

static uint32_t s_frame_count = 0;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.setSwapBytes(true);
    tft.pushColors((uint16_t *)color_p, w * h);
    tft.endWrite();

    bool last = lv_disp_flush_is_last(disp_drv);
    lv_disp_flush_ready(disp_drv);

    if (last) s_frame_count++;
}

uint32_t display_frame_count(void)
{
    return s_frame_count;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void Display::init(void)
{
    /* Init lvgl */
    lv_init();

    /* Init GC9A01 LCD via TFT_eSPI */
    tft.begin();
    tft.fillScreen(TFT_BLACK);
    /* Rotation 2 = 180°: the panel is mounted upside down in the case.
     * touch.cpp mirrors the CST816S coordinates to match. */
    tft.setRotation(2);

    /* Hand the backlight pin from plain GPIO to LEDC and dim it. */
    ledcSetup(BL_LEDC_CHANNEL, BL_LEDC_FREQ, BL_LEDC_RES);
    ledcAttachPin(TFT_BL, BL_LEDC_CHANNEL);
    ledcWrite(BL_LEDC_CHANNEL, BL_LEVEL);

    /*-----------------------------
     * Double-buffered drawing buffers
     *----------------------------*/
    static lv_disp_draw_buf_t draw_buf_dsc;
    static lv_color_t buf_1[MY_DISP_HOR_RES * MY_DISP_VER_RES / 2];  /* Half screen: 28,800 px */
    static lv_color_t buf_2[MY_DISP_HOR_RES * MY_DISP_VER_RES / 2];  /* Second buffer for tear-free rendering */
    lv_disp_draw_buf_init(&draw_buf_dsc, buf_1, buf_2,
                          MY_DISP_HOR_RES * MY_DISP_VER_RES / 2);

    /*-----------------------------------
     * Register the display in LVGL
     *----------------------------------*/
    static lv_disp_drv_t disp_drv;                  /*Descriptor of a display driver*/
    lv_disp_drv_init(&disp_drv);                    /*Basic initialization*/

    /*Set the resolution of the display*/
    disp_drv.hor_res = MY_DISP_HOR_RES;
    disp_drv.ver_res = MY_DISP_VER_RES;

    /*Used to copy the buffer's content to the display*/
    disp_drv.flush_cb = disp_flush;

    /*Set a display buffer (dual buffer for tear-free animation)*/
    disp_drv.draw_buf = &draw_buf_dsc;

    /*Finally register the driver*/
    lv_disp_drv_register(&disp_drv);
}

void Display::update(void)
{
    lv_task_handler();
}
