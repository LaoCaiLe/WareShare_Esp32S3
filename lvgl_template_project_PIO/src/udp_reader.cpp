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

#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <string.h>

#include "app_settings.h"
#include "net_config.h"
#include "time_keeper.h"

static WiFiUDP s_udp;
static bool s_udp_started;
static bool s_enabled = true;
static wl_status_t s_last_status = WL_IDLE_STATUS;
static uint32_t s_last_retry_ms;
static char s_packet[2048];

/* ArduinoJson document — static to avoid heap fragmentation. */
static JsonDocument s_json;

/* Load average as a fixed-point ×100 value (also used as CPU fallback). */
static uint16_t load_to_x100(float value)
{
    if (value < 0.0f) value = 0.0f;
    if (value > 655.0f) value = 655.0f;
    return (uint16_t)(value * 100.0f + 0.5f);
}

/* Parse one JSON datagram into the shared snapshot. */
static bool parse_json(pc_data_t *data, const char *json, size_t len)
{
    DeserializationError err = deserializeJson(s_json, json, len);
    if (err) {
        data->parse_errs++;
        data->last_frame_len = (int16_t)len;
        return false;
    }

    /* --- Parse CPU --- */
    JsonObject cpu = s_json["cpu"];
    if (cpu) {
        const char *name = cpu["name"] | "";
        strncpy(data->cpu.name, name, GPU_NAME_MAX - 1);
        data->cpu.name[GPU_NAME_MAX - 1] = '\0';
        /* The companion sends usage as a float (e.g. 25.4); ArduinoJson
         * refuses float -> int with `| 0` and silently yields the default. */
        float usage = cpu["usage"] | 0.0f;
        if (usage < 0.0f) usage = 0.0f;
        if (usage > 100.0f) usage = 100.0f;
        data->cpu.usage = (uint8_t)(usage + 0.5f);
        data->cpu.temp  = cpu["temp"] | -1;
        data->cpu.freq  = cpu["freq"] | 0;
        data->cpu.cores = cpu["cores"] | 0;
        data->cpu.procs = cpu["procs"] | 0;
    }

    /* --- Parse GPU --- */
    JsonObject gpu = s_json["gpu"];
    data->gpu_present = !gpu.isNull();
    if (gpu) {
        const char *name = gpu["name"] | "";
        strncpy(data->gpu.name, name, GPU_NAME_MAX - 1);
        data->gpu.name[GPU_NAME_MAX - 1] = '\0';
        data->gpu.usage       = gpu["usage"] | 0;
        data->gpu.temp        = gpu["temp"] | -1;
        data->gpu.mem_used    = gpu["mem_used"] | 0;
        data->gpu.mem_total   = gpu["mem_total"] | 0;
        data->gpu.power_w     = gpu["power_w"] | 0;
        data->gpu.core_clock  = gpu["core_clock"] | 0;
        data->gpu.mem_clock   = gpu["mem_clock"] | 0;
        data->gpu.fan_speed   = gpu["fan_speed"] | 0;
    }

    /* --- Parse RAM --- */
    JsonObject ram = s_json["ram"];
    if (ram) {
        data->ram.used       = ram["used"] | 0;
        data->ram.total      = ram["total"] | 0;
        data->ram.swap_used  = ram["swap_used"] | 0;
        data->ram.swap_total = ram["swap_total"] | 0;
    }

    /* --- Parse disks --- */
    JsonArray disks = s_json["disk"];
    if (disks) {
        data->disk_count = 0;
        for (JsonObject d : disks) {
            if (data->disk_count >= MAX_DISKS) break;
            const char *name = d["name"] | "";
            strncpy(data->disks[data->disk_count].name, name,
                    DISK_NAME_MAX - 1);
            data->disks[data->disk_count].name[DISK_NAME_MAX - 1] = '\0';
            data->disks[data->disk_count].used  = d["used"] | 0;
            data->disks[data->disk_count].total = d["total"] | 0;
            data->disks[data->disk_count].temp  = d["temp"] | -1;
            data->disk_count++;
        }
    }
    data->disk_phys_total_mb = s_json["disk_phys_total"] | 0;

    /* --- Parse network (optional) --- */
    JsonObject net = s_json["net"];
    if (net) {
        data->net_up   = net["up"] | 0;
        data->net_down = net["down"] | 0;
        data->net_rx_total_kb = net["total_down"] | 0;
        data->net_tx_total_kb = net["total_up"] | 0;
    }

    /* --- Parse load average (optional; falls back to CPU usage) --- */
    JsonArray loadavg = s_json["loadavg"];
    if (!loadavg.isNull() && loadavg.size() >= 3) {
        data->load1_x100  = load_to_x100(loadavg[0] | 0.0f);
        data->load5_x100  = load_to_x100(loadavg[1] | 0.0f);
        data->load15_x100 = load_to_x100(loadavg[2] | 0.0f);
    }
    else {
        uint16_t approx = load_to_x100((float)data->cpu.usage);
        data->load1_x100  = approx;
        data->load5_x100  = approx;
        data->load15_x100 = approx;
    }

    /* --- Parse uptime & timestamp --- */
    data->uptime  = s_json["uptime"] | 0;
    data->ts      = s_json["ts"] | 0;
    data->updated = true;
    data->frame_count++;
    data->last_recv_ms = millis();

    return true;
}

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
        /* The dial needs the time even when the PC companion is not
         * running, so bring SNTP up together with the link. */
        time_keeper_start_sntp();
    }
    Serial0.println();
}

static void wifi_start(void)
{
    WiFi.mode(WIFI_STA);
    /* Modem sleep: the radio dozes between DTIM beacons instead of staying
     * awake. One frame every ~1.5 s does not care about the extra 100-300 ms,
     * and this is the single biggest cut in idle power (and heat). */
    WiFi.setSleep(true);
    WiFi.setAutoReconnect(true);
    WiFi.begin(NET_WIFI_SSID, NET_WIFI_PASS);
    s_last_retry_ms = millis();
}

void udp_reader_init(void)
{
    s_enabled = app_settings_get()->wifi_on != 0;

    if (!s_enabled) {
        WiFi.mode(WIFI_OFF);
        Serial0.println("[WiFi] disabled in settings");
        return;
    }

    wifi_start();
}

void udp_reader_set_enabled(bool on)
{
    if (on == s_enabled) return;
    s_enabled = on;

    if (on) {
        Serial0.println("[WiFi] enabled");
        s_last_status = WL_IDLE_STATUS;   /* log the new connection state */
        wifi_start();
    } else {
        Serial0.println("[WiFi] disabled");
        if (s_udp_started) {
            s_udp.stop();
            s_udp_started = false;
        }
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        s_last_status = WL_IDLE_STATUS;
    }
}

bool udp_reader_enabled(void)
{
    return s_enabled;
}

void udp_reader_poll(pc_data_t *data)
{
    if (!s_enabled) {
        data->wifi_state = PC_WIFI_OFF;
        return;
    }

    wl_status_t status = WiFi.status();
    data->wifi_state = (status == WL_CONNECTED) ? PC_WIFI_CONNECTED
                                                : PC_WIFI_CONNECTING;
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
        parse_json(data, s_packet, (size_t)got);
    }
}
