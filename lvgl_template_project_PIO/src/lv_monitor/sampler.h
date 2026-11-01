#ifndef __MONITOR_SAMPLER_H
#define __MONITOR_SAMPLER_H

#include <stdbool.h>
#include <stdint.h>

#include "theme.h"
#include "data_model.h"

#define SYS_TEMP_UNKNOWN   0xFFFFu
#define SYS_FREQ_UNKNOWN   0u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* CPU */
    uint16_t cpu_load;             /* 0..100, smoothed */
    uint32_t cpu_temp_c;           /* SYS_TEMP_UNKNOWN if unavailable */
    uint32_t cpu_freq_khz;         /* 0 if unavailable */
    uint16_t cpu_procs;            /* running process count, 0 if unavailable */

    /* Memory */
    uint16_t mem_load;
    uint32_t mem_used_kb;
    uint32_t mem_total_kb;
    uint32_t swap_used_kb;
    uint32_t swap_total_kb;

    /* Disk */
    uint16_t disk_load;
    uint64_t disk_used_kb;
    uint64_t disk_total_kb;
    uint64_t disk_avail_kb;

    /* Network — bytes per second, smoothed */
    uint64_t net_rx_bps;
    uint64_t net_tx_bps;
    uint64_t net_rx_total;
    uint64_t net_tx_total;

    /* System */
    uint64_t uptime_sec;
    uint16_t load1_x100;
    uint16_t load5_x100;
    uint16_t load15_x100;

    /* GPU */
    bool     gpu_valid;
    uint16_t gpu_load;
    int16_t  gpu_temp_c;           /* -1 if unavailable */
    uint32_t gpu_mem_used_kb;
    uint32_t gpu_mem_total_kb;
    uint16_t gpu_power_w;
    uint16_t gpu_core_clock;       /* MHz */
    uint16_t gpu_fan;              /* % */

    /* 60-second ring buffers (oldest → newest via hist_head) */
    uint32_t cpu_hist[MON_HIST_LEN];
    uint32_t mem_hist[MON_HIST_LEN];
    uint32_t net_rx_hist[MON_HIST_LEN];
    uint32_t net_tx_hist[MON_HIST_LEN];
    uint32_t load_hist[MON_HIST_LEN];
    uint32_t gpu_hist[MON_HIST_LEN];

    uint16_t hist_head;    /* next write position */
    uint32_t sample_count; /* number of completed samples */
} sys_sample_t;

void sampler_init(void);
void sampler_set_pc_data(const pc_data_t *data);
const sys_sample_t *sampler_get(void);

#ifdef __cplusplus
}
#endif

#endif /* __MONITOR_SAMPLER_H */
