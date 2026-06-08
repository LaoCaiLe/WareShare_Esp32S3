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

/* CPU info */
typedef struct {
    char    name[GPU_NAME_MAX];
    uint8_t usage;          /* 0-100% */
    int8_t  temp;           /* Celsius, -1 = unavailable */
    uint16_t freq;          /* MHz */
    uint8_t cores;
} cpu_info_t;

/* GPU info */
typedef struct {
    char    name[GPU_NAME_MAX];
    uint8_t usage;          /* 0-100% */
    int8_t  temp;           /* Celsius, -1 = unavailable */
    uint32_t mem_used;      /* MB */
    uint32_t mem_total;     /* MB */
    uint8_t power_w;        /* Watts */
} gpu_info_t;

/* RAM info */
typedef struct {
    uint32_t used;          /* MB */
    uint32_t total;         /* MB */
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
    uint32_t    uptime;     /* seconds */
    uint32_t    ts;         /* unix timestamp */
    bool        updated;    /* new data flag, cleared by UI */
    uint32_t    frame_count;/* total frames parsed successfully */
    uint32_t    last_recv_ms;/* millis() when last valid frame received */

    /* Diagnostics — visible on screen to debug serial issues */
    uint32_t    raw_bytes;  /* total bytes read from Serial */
    uint32_t    stx_count;  /* number of STX markers detected */
    uint32_t    parse_errs; /* number of JSON parse failures */
    uint32_t    overflows;  /* number of RX buffer overflows */
    int16_t     last_frame_len;/* length of last received frame */
    char        last_err[32];  /* last parse error message */
    uint8_t     usb_mounted;   /* 1 = USB device mounted (enumeration done) */
    uint8_t     usb_started;   /* 1 = USB.begin() called successfully */
    char        build_tag[24]; /* firmware build identifier */
} pc_data_t;

#ifdef __cplusplus
}
#endif

#endif /* DATA_MODEL_H */
