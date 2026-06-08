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
        /* Print error to Serial for debugging */
        Serial0.print("[JSON] parse error: ");
        Serial0.println(err.c_str());
        /* Dump first 64 chars of raw data */
        char preview[65];
        uint16_t plen = len < 64 ? len : 64;
        memcpy(preview, rx_buf + rx_tail, plen);
        preview[plen] = '\0';
        Serial0.print("[JSON] raw: ");
        Serial0.println(preview);
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
    if (gpu) {
        const char *name = gpu["name"] | "";
        strncpy(data->gpu.name, name, GPU_NAME_MAX - 1);
        data->gpu.name[GPU_NAME_MAX - 1] = '\0';
        data->gpu.usage     = gpu["usage"] | 0;
        data->gpu.temp      = gpu["temp"] | -1;
        data->gpu.mem_used  = gpu["mem_used"] | 0;
        data->gpu.mem_total = gpu["mem_total"] | 0;
        data->gpu.power_w   = gpu["power_w"] | 0;
    }

    /* --- Parse RAM --- */
    JsonObject ram = json_doc["ram"];
    if (ram) {
        data->ram.used  = ram["used"] | 0;
        data->ram.total = ram["total"] | 0;
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

    /* --- Parse uptime & timestamp --- */
    data->uptime      = json_doc["uptime"] | 0;
    data->ts          = json_doc["ts"] | 0;
    data->updated     = true;
    data->frame_count++;
    data->last_recv_ms = millis();

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
