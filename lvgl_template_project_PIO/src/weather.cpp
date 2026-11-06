/**
 * @file weather.cpp
 * @brief Background Open-Meteo fetch for the INFO page.
 *
 * The endpoint answers over plain HTTP, so no TLS handshake is needed — the
 * board has ~100 KB of heap left after the two display buffers, and a TLS
 * session would eat most of it. The fetch runs in its own task so a slow
 * response can never stall the LVGL refresh.
 */
#include "weather.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "net_config.h"
#include "wifi_info.h"

#define WEATHER_OK_MS    (15UL * 60UL * 1000UL)  /* refresh cadence */
#define WEATHER_RETRY_MS (60UL * 1000UL)         /* online but failed */
#define WEATHER_IDLE_MS  (15UL * 1000UL)         /* WiFi down / disabled */

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static weather_info_t s_info;

/* WMO 4677 weather codes → short English labels, matching the dial's
 * all-caps typography. */
static const char *wmo_desc(int code)
{
    switch (code) {
    case 0:  return "CLEAR";
    case 1:  return "MAINLY CLEAR";
    case 2:  return "PARTLY CLOUDY";
    case 3:  return "OVERCAST";
    case 45:
    case 48: return "FOG";
    case 51:
    case 53:
    case 55: return "DRIZZLE";
    case 56:
    case 57: return "FREEZING DRIZZLE";
    case 61:
    case 63:
    case 65: return "RAIN";
    case 66:
    case 67: return "FREEZING RAIN";
    case 71:
    case 73:
    case 75: return "SNOW";
    case 77: return "SNOW GRAINS";
    case 80:
    case 81:
    case 82: return "SHOWERS";
    case 85:
    case 86: return "SNOW SHOWERS";
    case 95: return "THUNDERSTORM";
    case 96:
    case 99: return "THUNDER + HAIL";
    default: return "UNKNOWN";
    }
}

static bool fetch_once(void)
{
    char url[224];
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast"
             "?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code,relative_humidity_2m"
             "&daily=temperature_2m_max,temperature_2m_min"
             "&timezone=auto&forecast_days=1",
             (double)NET_WEATHER_LAT, (double)NET_WEATHER_LON);

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(8000);

    if (!http.begin(url)) {
        Serial0.println("[Weather] begin failed");
        return false;
    }

    int status = http.GET();
    if (status != HTTP_CODE_OK) {
        Serial0.printf("[Weather] HTTP %d\n", status);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial0.printf("[Weather] JSON %s\n", err.c_str());
        return false;
    }

    float temp = doc["current"]["temperature_2m"] | NAN;
    float hum  = doc["current"]["relative_humidity_2m"] | NAN;
    int   code = doc["current"]["weather_code"] | -1;
    float hi   = doc["daily"]["temperature_2m_max"][0] | NAN;
    float lo   = doc["daily"]["temperature_2m_min"][0] | NAN;

    if (isnan(temp) || code < 0 || temp < -60.0f || temp > 60.0f) {
        Serial0.println("[Weather] incomplete payload");
        return false;
    }
    if (isnan(hi)) hi = temp;
    if (isnan(lo)) lo = temp;
    if (isnan(hum)) hum = 0.0f;
    if (hum < 0.0f) hum = 0.0f;
    if (hum > 100.0f) hum = 100.0f;

    weather_info_t next;
    memset(&next, 0, sizeof(next));
    next.valid = true;
    next.temp_c = (int8_t)lroundf(temp);
    next.hi_c = (int8_t)lroundf(hi);
    next.lo_c = (int8_t)lroundf(lo);
    next.humidity = (uint8_t)lroundf(hum);
    next.code = (uint16_t)code;
    next.updated_ms = millis();
    snprintf(next.desc, sizeof(next.desc), "%s", wmo_desc(code));
    snprintf(next.place, sizeof(next.place), "%s", NET_WEATHER_PLACE);

    taskENTER_CRITICAL(&s_mux);
    s_info = next;
    taskEXIT_CRITICAL(&s_mux);

    Serial0.printf("[Weather] %s %dC H%d L%d HUM%u heap=%u\n",
                   next.desc, (int)next.temp_c, (int)next.hi_c,
                   (int)next.lo_c, (unsigned)next.humidity,
                   (unsigned)ESP.getFreeHeap());
    return true;
}

static void weather_task(void *arg)
{
    (void)arg;

    for (;;) {
        bool ok = false;

        if (wifi_info_enabled() && wifi_info_connected()) {
            ok = fetch_once();
        }

        uint32_t delay_ms;
        if (ok) {
            delay_ms = WEATHER_OK_MS;
        }
        else if (wifi_info_connected()) {
            delay_ms = WEATHER_RETRY_MS;
        }
        else {
            delay_ms = WEATHER_IDLE_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void weather_init(void)
{
    xTaskCreate(weather_task, "weather", 8192, NULL, 1, NULL);
}

const weather_info_t *weather_get(void)
{
    static weather_info_t snapshot;

    taskENTER_CRITICAL(&s_mux);
    snapshot = s_info;
    taskEXIT_CRITICAL(&s_mux);

    return &snapshot;
}
