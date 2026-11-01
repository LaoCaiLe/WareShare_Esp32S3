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

#endif /* NET_CONFIG_H */
