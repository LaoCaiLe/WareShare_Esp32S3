/**
 * @file weather.h
 * @brief Current weather fetched from Open-Meteo (INFO page)
 */
#ifndef WEATHER_H
#define WEATHER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     valid;        /* at least one successful fetch since boot */
    int8_t   temp_c;
    int8_t   hi_c;
    int8_t   lo_c;
    uint8_t  humidity;     /* % */
    uint16_t code;         /* WMO weather code */
    uint32_t updated_ms;   /* millis() of the last successful fetch */
    char     desc[24];     /* short English description */
    char     place[20];
} weather_info_t;

/* Start the background fetch task. Call once after WiFi is initialized. */
void weather_init(void);

/* Snapshot copy of the latest reading; safe to call from the UI task. */
const weather_info_t *weather_get(void);

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_H */
