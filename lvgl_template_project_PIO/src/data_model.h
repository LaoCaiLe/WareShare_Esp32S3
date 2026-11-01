/**
 * @file data_model.h
 * @brief PC hardware monitor data structures
 */
#ifndef DATA_MODEL_H
#define DATA_MODEL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum string lengths */
#define GPU_NAME_MAX    64
#define DISK_NAME_MAX   16
#define MAX_DISKS       6

/* WiFi link state written by udp_reader_poll() */
#define PC_WIFI_OFF        0
#define PC_WIFI_CONNECTING 1
#define PC_WIFI_CONNECTED  2

/* CPU info */
typedef struct {
    char    name[GPU_NAME_MAX];
    uint8_t usage;          /* 0-100% */
    int8_t  temp;           /* Celsius, -1 = unavailable */
    uint16_t freq;          /* MHz */
    uint8_t cores;
    uint16_t procs;         /* running process count, 0 = unavailable */
} cpu_info_t;

/* GPU info */
typedef struct {
    char    name[GPU_NAME_MAX];
    uint8_t usage;          /* 0-100% */
    int8_t  temp;           /* Celsius, -1 = unavailable */
    uint32_t mem_used;      /* MB */
    uint32_t mem_total;     /* MB */
    uint8_t power_w;        /* Watts */
    uint16_t core_clock;    /* MHz, 0 = unavailable */
    uint16_t mem_clock;     /* MHz, 0 = unavailable */
    uint8_t  fan_speed;     /* %, 0-100, 0 = unavailable */
} gpu_info_t;

/* RAM info */
typedef struct {
    uint32_t used;          /* MB */
    uint32_t total;         /* MB */
    uint32_t swap_used;     /* MB */
    uint32_t swap_total;    /* MB */
} ram_info_t;

/* Disk info */
typedef struct {
    char    name[DISK_NAME_MAX];
    uint32_t used;          /* MB */
    uint32_t total;         /* MB */
    int8_t  temp;           /* Celsius, -1 = unavailable */
} disk_info_t;

/* Full PC hardware snapshot */
typedef struct {
    cpu_info_t  cpu;
    gpu_info_t  gpu;
    ram_info_t  ram;
    disk_info_t disks[MAX_DISKS];
    uint8_t     disk_count;
    uint32_t    disk_phys_total_mb; /* all physical disks, MiB, 0 = unavailable */
    uint32_t    uptime;     /* seconds */
    uint32_t    ts;         /* unix timestamp */
    uint32_t    net_up;     /* KB/s upload speed */
    uint32_t    net_down;   /* KB/s download speed */
    uint32_t    net_rx_total_kb; /* cumulative bytes since PC boot, KB */
    uint32_t    net_tx_total_kb;
    uint16_t    load1_x100; /* 1-minute load average ×100 */
    uint16_t    load5_x100;
    uint16_t    load15_x100;
    uint8_t     gpu_present;/* 1 = frame carried a GPU object */
    bool        updated;    /* new data flag, cleared by UI */
    uint32_t    frame_count;/* total frames parsed successfully */
    uint32_t    last_recv_ms;/* millis() when last valid frame received */
    uint8_t     wifi_state; /* PC_WIFI_OFF / _CONNECTING / _CONNECTED */

    /* Diagnostics */
    uint32_t    parse_errs; /* number of JSON parse failures */
    uint32_t    overflows;  /* number of oversized datagrams dropped */
    int16_t     last_frame_len; /* length of the last datagram */
} pc_data_t;

#ifdef __cplusplus
}
#endif

#endif /* DATA_MODEL_H */
