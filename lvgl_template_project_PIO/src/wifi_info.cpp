/**
 * @file wifi_info.cpp
 * @brief Small wrappers so the C monitor code can read the WiFi link state.
 */
#include "wifi_info.h"

#include <WiFi.h>
#include <stdio.h>

#include "net_config.h"
#include "udp_reader.h"

bool wifi_info_enabled(void)
{
    return udp_reader_enabled();
}

bool wifi_info_connected(void)
{
    return WiFi.status() == WL_CONNECTED;
}

int wifi_info_rssi(void)
{
    return wifi_info_connected() ? (int)WiFi.RSSI() : 0;
}

void wifi_info_ip(char *buf, size_t len)
{
    if (buf == NULL || len == 0) return;

    if (!wifi_info_connected()) {
        snprintf(buf, len, "--");
        return;
    }

    snprintf(buf, len, "%s", WiFi.localIP().toString().c_str());
}

const char *wifi_info_ssid(void)
{
    return NET_WIFI_SSID;
}
