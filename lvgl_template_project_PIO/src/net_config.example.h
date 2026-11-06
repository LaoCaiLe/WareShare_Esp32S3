/*
 * WiFi / UDP configuration — copy this file to net_config.h and fill it in.
 *
 * net_config.h is git-ignored on purpose: it holds the WiFi password.
 */
#ifndef NET_CONFIG_H
#define NET_CONFIG_H

/* The dial joins this network as a station. The ESP32-S3 has a 2.4 GHz
 * radio only, so the SSID must be a 2.4 GHz network. */
#define NET_WIFI_SSID "YOUR_SSID"
#define NET_WIFI_PASS "YOUR_PASSWORD"

/* UDP port the PC companion broadcasts to. Keep in sync with monitor.py. */
#define NET_UDP_PORT 4210

/* Local time offset from UTC in minutes, used to render the PC clock on the
 * INFO page (UTC+8 = 480). */
#define NET_TZ_OFFSET_MIN 480

/* Weather location for Open-Meteo. Change these to your city; the INFO page
 * shows the current temperature and today's high/low. */
#define NET_WEATHER_LAT   22.5431
#define NET_WEATHER_LON   114.0579
#define NET_WEATHER_PLACE "SHENZHEN"

#endif /* NET_CONFIG_H */
