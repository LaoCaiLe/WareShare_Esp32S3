/**
 * @file udp_reader.cpp
 * @brief WiFi + UDP broadcast receiver for PC monitor data
 *
 * The companion broadcasts one JSON datagram per second; UDP keeps the
 * message boundaries, so no STX/ETX framing is needed. Datagrams go to
 * the directed broadcast of the PC's LAN, so the ESP32 needs no fixed
 * address and the PC needs to know nothing about the dial.
 */
#include "udp_reader.h"

#include <WiFi.h>
#include <WiFiUdp.h>

#include "net_config.h"
#include "serial_reader.h"

static WiFiUDP s_udp;
static bool s_udp_started;
static wl_status_t s_last_status = WL_IDLE_STATUS;
static uint32_t s_last_retry_ms;
static char s_packet[2048];

static void wifi_log_state(wl_status_t status)
{
    if (status == s_last_status) return;
    s_last_status = status;

    const char *text = "unknown";
    switch (status) {
    case WL_IDLE_STATUS:     text = "idle"; break;
    case WL_NO_SSID_AVAIL:   text = "no such SSID"; break;
    case WL_SCAN_COMPLETED:  text = "scan done"; break;
    case WL_CONNECTED:       text = "connected"; break;
    case WL_CONNECT_FAILED:  text = "connect failed"; break;
    case WL_CONNECTION_LOST: text = "connection lost"; break;
    case WL_DISCONNECTED:    text = "disconnected"; break;
    default: break;
    }

    Serial0.printf("[WiFi] %s", text);
    if (status == WL_CONNECTED) {
        Serial0.printf(" %s", WiFi.localIP().toString().c_str());
    }
    Serial0.println();
}

void udp_reader_init(void)
{
    WiFi.mode(WIFI_STA);
    /* Modem sleep: the radio dozes between DTIM beacons instead of staying
     * awake. One frame every ~1.5 s does not care about the extra 100-300 ms,
     * and this is the single biggest cut in idle power (and heat). */
    WiFi.setSleep(true);
    WiFi.setAutoReconnect(true);
    WiFi.begin(NET_WIFI_SSID, NET_WIFI_PASS);
}

void udp_reader_poll(pc_data_t *data)
{
    wl_status_t status = WiFi.status();
    wifi_log_state(status);

    if (status != WL_CONNECTED) {
        if (s_udp_started) {
            s_udp.stop();
            s_udp_started = false;
        }

        /* The core retries by itself for a while; kick it again every 10 s
         * so an access point that comes up late still gets joined. */
        uint32_t now = millis();
        if (now - s_last_retry_ms >= 10000U) {
            s_last_retry_ms = now;
            WiFi.begin(NET_WIFI_SSID, NET_WIFI_PASS);
        }
        return;
    }

    if (!s_udp_started) {
        s_udp_started = (s_udp.begin(NET_UDP_PORT) != 0);
        if (s_udp_started) {
            Serial0.printf("[WiFi] UDP listening on port %u\n",
                           (unsigned)NET_UDP_PORT);
        }
    }

    for (;;) {
        int len = s_udp.parsePacket();
        if (len <= 0) break;

        if (len > (int)sizeof(s_packet)) {
            /* Drain an oversized datagram rather than parse a truncated body. */
            while (s_udp.available()) s_udp.read();
            data->overflows++;
            continue;
        }

        int got = s_udp.read(s_packet, (size_t)len);
        if (got <= 0) continue;

        data->last_frame_len = (int16_t)got;
        serial_reader_parse_json(data, s_packet, (size_t)got);
    }
}
