/**
 * @file serial_reader.cpp
 * @brief Serial JSON frame receiver using ArduinoJson v7
 *
 * Frame format: [STX:0x02][JSON payload][ETX:0x03]
 * Each frame updates pc_data_t and sets .updated = true
 */
#include "serial_reader.h"
#include <ArduinoJson.h>

/* Frame markers */
#define STX  0x02
#define ETX  0x03

/* RX ring buffer — large enough for max JSON (~2KB) */
#define RX_BUF_SIZE  2560

static uint8_t  rx_buf[RX_BUF_SIZE];
static uint16_t rx_head = 0;
static uint16_t rx_tail = 0;
static bool     in_frame = false;

/* ArduinoJson doc — static to avoid heap fragmentation */
static JsonDocument json_doc;

/* Load average as a fixed-point ×100 value (also used as CPU fallback). */
static uint16_t load_to_x100(float value)
{
    if (value < 0.0f) value = 0.0f;
    if (value > 655.0f) value = 655.0f;
    return (uint16_t)(value * 100.0f + 0.5f);
}

void serial_reader_init(unsigned long baud)
{
    /*
     * Serial0 = Hardware UART0 (GPIO 43 TX, GPIO 44 RX)
     * Set larger RX buffer (2048) for ~500-byte JSON frames.
     */
    Serial0.begin(baud);
    Serial0.setRxBufferSize(2048);
    delay(200);
}

/**
 * Parse a complete JSON frame from rx_buf[rx_tail..rx_head)
 * Returns: true if parsing succeeded, false otherwise.
 */
static bool parse_frame(pc_data_t *data, uint16_t len)
{
    /* Null-terminate for ArduinoJson */
    if (len >= RX_BUF_SIZE) len = RX_BUF_SIZE - 1;
    rx_buf[rx_tail + len] = '\0';

    /* Deserialize — JSON is small (~400 bytes), no filter needed */
    DeserializationError err = deserializeJson(
        json_doc,
        (const char *)(rx_buf + rx_tail)
    );

    if (err) {
        data->parse_errs++;
        strncpy(data->last_err, err.c_str(), sizeof(data->last_err) - 1);
        data->last_err[sizeof(data->last_err) - 1] = '\0';
        data->last_frame_len = (int16_t)len;
        /* Debug to USB Serial (CDC), not Serial0 data channel */
        Serial.print("[JSON] parse error: ");
        Serial.println(err.c_str());
        /* Dump first 64 chars of raw data */
        char preview[65];
        uint16_t plen = len < 64 ? len : 64;
        memcpy(preview, rx_buf + rx_tail, plen);
        preview[plen] = '\0';
        Serial.print("[JSON] raw: ");
        Serial.println(preview);
        return false;
    }

    /* --- Parse CPU --- */
    JsonObject cpu = json_doc["cpu"];
    if (cpu) {
        const char *name = cpu["name"] | "";
        strncpy(data->cpu.name, name, GPU_NAME_MAX - 1);
        data->cpu.name[GPU_NAME_MAX - 1] = '\0';
        data->cpu.usage = cpu["usage"] | 0;
        data->cpu.temp  = cpu["temp"] | -1;
        data->cpu.freq  = cpu["freq"] | 0;
        data->cpu.cores = cpu["cores"] | 0;
    }

    /* --- Parse GPU --- */
    JsonObject gpu = json_doc["gpu"];
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
    JsonObject ram = json_doc["ram"];
    if (ram) {
        data->ram.used       = ram["used"] | 0;
        data->ram.total      = ram["total"] | 0;
        data->ram.swap_used  = ram["swap_used"] | 0;
        data->ram.swap_total = ram["swap_total"] | 0;
    }

    /* --- Parse Disks --- */
    JsonArray disks = json_doc["disk"];
    if (disks) {
        data->disk_count = 0;
        for (JsonObject d : disks) {
            if (data->disk_count >= MAX_DISKS) break;
            const char *name = d["name"] | "";
            strncpy(data->disks[data->disk_count].name, name, DISK_NAME_MAX - 1);
            data->disks[data->disk_count].name[DISK_NAME_MAX - 1] = '\0';
            data->disks[data->disk_count].used  = d["used"] | 0;
            data->disks[data->disk_count].total = d["total"] | 0;
            data->disks[data->disk_count].temp  = d["temp"] | -1;
            data->disk_count++;
        }
    }

    /* --- Parse network (optional) --- */
    JsonObject net = json_doc["net"];
    if (net) {
        data->net_up   = net["up"] | 0;
        data->net_down = net["down"] | 0;
        data->net_rx_total_kb = net["total_down"] | 0;
        data->net_tx_total_kb = net["total_up"] | 0;
    }

    /* --- Parse load average (optional; falls back to CPU usage) --- */
    JsonArray loadavg = json_doc["loadavg"];
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
    data->uptime      = json_doc["uptime"] | 0;
    data->ts          = json_doc["ts"] | 0;
    data->updated     = true;
    data->frame_count++;
    data->last_recv_ms = millis();

    /* Save raw preview for on-screen debug */
    {
        uint16_t plen = len < (sizeof(data->raw_preview) - 1)
                        ? len : (sizeof(data->raw_preview) - 1);
        memcpy(data->raw_preview, rx_buf + rx_tail, plen);
        data->raw_preview[plen] = '\0';
    }

    /* Debug: confirm parsed data to USB Serial (CDC) */
    if (data->frame_count <= 3 || data->frame_count % 30 == 0) {
        Serial.printf("[Ser] #%lu CPU:%u%% GPU:%u%% RAM:%luM GPUclk:%u/%u Fan:%u%% Net:%lu/%lu\n",
                      data->frame_count,
                      data->cpu.usage, data->gpu.usage,
                      data->ram.used,
                      data->gpu.core_clock, data->gpu.mem_clock, data->gpu.fan_speed,
                      data->net_down, data->net_up);
    }

    return true;
}

void serial_reader_poll(pc_data_t *data)
{
    while (Serial0.available()) {
        uint8_t c = Serial0.read();
        data->raw_bytes++;  /* count every byte (diagnostic) */

        if (c == STX) {
            data->stx_count++;
            /* Start of frame — discarding any previous partial data */
            in_frame = true;
            rx_head = 0;
            rx_tail = 0;
            continue;
        }

        if (c == ETX && in_frame) {
            in_frame = false;
            /* Got a complete frame */
            if (rx_head > 0) {
                data->last_frame_len = (int16_t)rx_head;
                parse_frame(data, rx_head);
            }
            continue;
        }

        if (in_frame) {
            if (rx_head < RX_BUF_SIZE) {
                rx_buf[rx_head++] = c;
            } else {
                /* Buffer overflow — discard frame */
                data->overflows++;
                in_frame = false;
                rx_head = 0;
            }
        }
        /* else: bytes outside of frame markers are silently ignored */
    }
}
