/**
 * @file wifi_info.h
 * @brief C-friendly accessors for the station link state (INFO page)
 */
#ifndef WIFI_INFO_H
#define WIFI_INFO_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool wifi_info_enabled(void);
bool wifi_info_connected(void);
int  wifi_info_rssi(void);              /* dBm, 0 when not connected */
void wifi_info_ip(char *buf, size_t len);
const char *wifi_info_ssid(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_INFO_H */
