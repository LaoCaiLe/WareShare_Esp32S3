/*
 * Sampler for the ESP32 build.
 *
 * The desktop version reads /proc and /sys directly. On the ESP32 the PC
 * metrics arrive over WiFi/UDP as pc_data_t frames (see udp_reader.cpp), so
 * this sampler only translates/smooths that data into sys_sample_t and
 * keeps the 60-second history buffers the pages draw.
 */
#include "sampler.h"

#include <stdbool.h>
#include <string.h>

#include "lvgl.h"
#include "time_keeper.h"

/* The PC sends about one frame per second, so four missed frames mean the
 * companion or the WiFi link is really gone. */
#define LINK_STALE_MS 4000U

static sys_sample_t s_sample;
static const pc_data_t *s_pc;
static bool s_inited;
static bool s_gpu_valid;
static bool s_net_valid;
static bool s_seeded;
static bool s_disk_seeded;

/* Fallback cumulative counters, used when the PC sends no net totals. */
static uint64_t s_rx_total_acc;
static uint64_t s_tx_total_acc;

static uint16_t ema_u16(uint16_t old_value, uint16_t new_value)
{
    return (uint16_t)((old_value * 3U + new_value * 2U) / 5U);
}

static uint64_t ema_u64(uint64_t old_value, uint64_t new_value)
{
    return (old_value * 3U + new_value * 2U) / 5U;
}

static uint16_t clamp_pct(uint32_t value)
{
    return (uint16_t)(value > 100U ? 100U : value);
}

static void history_seed(void)
{
    for (int i = 0; i < MON_HIST_LEN; i++) {
        s_sample.cpu_hist[i] = s_sample.cpu_load;
        s_sample.mem_hist[i] = s_sample.mem_load;
        s_sample.net_rx_hist[i] = (uint32_t)s_sample.net_rx_bps;
        s_sample.net_tx_hist[i] = (uint32_t)s_sample.net_tx_bps;
        s_sample.load_hist[i] = s_sample.load1_x100;
        s_sample.gpu_hist[i] = s_sample.gpu_load;
    }
    s_sample.hist_head = 0;
    s_seeded = true;
}

static void history_push(void)
{
    if (!s_seeded) {
        history_seed();
        return;
    }

    uint16_t idx = s_sample.hist_head;
    s_sample.cpu_hist[idx] = s_sample.cpu_load;
    s_sample.mem_hist[idx] = s_sample.mem_load;
    s_sample.net_rx_hist[idx] = (uint32_t)s_sample.net_rx_bps;
    s_sample.net_tx_hist[idx] = (uint32_t)s_sample.net_tx_bps;
    s_sample.load_hist[idx] = s_sample.load1_x100;
    s_sample.gpu_hist[idx] = s_sample.gpu_load;

    s_sample.hist_head = (uint16_t)((idx + 1U) % MON_HIST_LEN);
}

static void sample_once(void)
{
    const pc_data_t *p = s_pc;
    uint32_t tick = s_sample.sample_count;

    if (p == NULL) {
        s_sample.link_live = false;
        s_sample.link_ever = false;
        s_sample.wifi_state = PC_WIFI_OFF;
        if (tick > 0U) history_push();
        s_sample.sample_count = tick + 1U;
        return;
    }

    /* A frame older than 3 s means the PC companion stopped sending. */
    bool fresh = (p->last_recv_ms != 0U) &&
                 (lv_tick_elaps(p->last_recv_ms) < 3000U);

    /* The dial's "no data" notice tolerates one more missed frame than the
     * network rates, so a single dropped datagram never blinks it. */
    s_sample.link_live = (p->last_recv_ms != 0U) &&
                         (lv_tick_elaps(p->last_recv_ms) < LINK_STALE_MS);
    s_sample.link_ever = (p->frame_count > 0U);
    s_sample.wifi_state = p->wifi_state;

    /* CPU ------------------------------------------------------------ */
    /* No EMA here: the PC already averages % Processor Utility over the
     * whole frame interval, and extra smoothing made the dial lag Task
     * Manager. The hero tween still animates the number. */
    if (p->frame_count > 0U) {
        s_sample.cpu_load = clamp_pct(p->cpu.usage);
    }

    s_sample.cpu_temp_c = (p->cpu.temp > 0)
                          ? (uint32_t)p->cpu.temp : SYS_TEMP_UNKNOWN;
    s_sample.cpu_freq_khz = (p->cpu.freq > 0U)
                            ? (uint32_t)p->cpu.freq * 1000U
                            : SYS_FREQ_UNKNOWN;
    s_sample.cpu_procs = p->cpu.procs;
    s_sample.uptime_sec = p->uptime;

    /* PC clock: hand the sender's unix seconds to the persistent keeper.
     * It keeps running on its own when frames stop (NVS/RTC restore), and
     * SNTP can feed it independently when only the WiFi link is up. */
    if (fresh && p->ts >= TIME_KEEPER_MIN_VALID) {
        time_keeper_sync(p->ts);
    }

    /* Memory --------------------------------------------------------- */
    uint32_t mem_used_kb = p->ram.used * 1024U;
    uint32_t mem_total_kb = p->ram.total * 1024U;
    s_sample.mem_used_kb = mem_used_kb;
    s_sample.mem_total_kb = mem_total_kb;
    s_sample.mem_load = (mem_total_kb > 0U)
                        ? (uint16_t)((uint64_t)mem_used_kb * 100ULL /
                                     mem_total_kb)
                        : 0U;

    s_sample.swap_used_kb = p->ram.swap_used * 1024U;
    s_sample.swap_total_kb = p->ram.swap_total * 1024U;

    /* Disk — aggregate every fixed volume. Refresh every 5 s, but seed as
     * soon as the first frame with volumes arrives: waiting for tick 5 made
     * the DISK ring stay at 0 for the first ~5 s after boot. */
    if (!s_disk_seeded || tick % 5U == 0U) {
        uint64_t total_mb = 0;
        uint64_t used_mb = 0;

        for (uint8_t i = 0; i < p->disk_count && i < MAX_DISKS; i++) {
            used_mb += p->disks[i].used;
            total_mb += p->disks[i].total;
        }

        if (used_mb > total_mb) used_mb = total_mb;

        uint64_t avail_mb = total_mb - used_mb;

        /* Mounted volumes miss recovery and unmounted partitions, so their
         * sum reads below the drives' rated capacity. Prefer the physical
         * total the PC reports; whatever the volumes don't cover (hidden
         * partitions, unallocated space) counts as used. */
        if (p->disk_phys_total_mb > total_mb) {
            total_mb = p->disk_phys_total_mb;
            used_mb = total_mb - avail_mb;
        }

        s_sample.disk_total_kb = total_mb * 1024ULL;
        s_sample.disk_used_kb = used_mb * 1024ULL;
        s_sample.disk_avail_kb = avail_mb * 1024ULL;
        s_sample.disk_load = (total_mb > 0)
                             ? (uint16_t)(used_mb * 100ULL / total_mb)
                             : 0U;

        /* Keep retrying every second until a frame actually carries volumes
         * (the first frames may arrive before the PC's disk enumeration). */
        s_disk_seeded = (p->disk_count > 0U);
    }

    /* Network -------------------------------------------------------- */
    if (fresh) {
        uint64_t rx_bps = (uint64_t)p->net_down * 1024ULL; /* KB/s → B/s */
        uint64_t tx_bps = (uint64_t)p->net_up * 1024ULL;

        if (!s_net_valid) {
            s_sample.net_rx_bps = rx_bps;
            s_sample.net_tx_bps = tx_bps;
            s_net_valid = true;
        }
        else {
            s_sample.net_rx_bps = ema_u64(s_sample.net_rx_bps, rx_bps);
            s_sample.net_tx_bps = ema_u64(s_sample.net_tx_bps, tx_bps);
        }

        if (p->net_rx_total_kb > 0U || p->net_tx_total_kb > 0U) {
            s_sample.net_rx_total = (uint64_t)p->net_rx_total_kb * 1024ULL;
            s_sample.net_tx_total = (uint64_t)p->net_tx_total_kb * 1024ULL;
        }
        else {
            s_rx_total_acc += rx_bps;
            s_tx_total_acc += tx_bps;
            s_sample.net_rx_total = s_rx_total_acc;
            s_sample.net_tx_total = s_tx_total_acc;
        }
    }
    else {
        /* Stale link: do not keep showing the last rate forever. */
        s_sample.net_rx_bps = 0;
        s_sample.net_tx_bps = 0;
        s_net_valid = false;
    }

    /* Load average --------------------------------------------------- */
    s_sample.load1_x100 = p->load1_x100;
    s_sample.load5_x100 = p->load5_x100;
    s_sample.load15_x100 = p->load15_x100;

    /* GPU ------------------------------------------------------------ */
    bool gpu_ok = p->gpu_present && (p->gpu.name[0] != '\0');
    s_sample.gpu_valid = gpu_ok;

    if (gpu_ok) {
        uint16_t raw_gpu = clamp_pct(p->gpu.usage);
        if (!s_gpu_valid) {
            s_sample.gpu_load = raw_gpu;
            s_gpu_valid = true;
        }
        else {
            s_sample.gpu_load = ema_u16(s_sample.gpu_load, raw_gpu);
        }

        s_sample.gpu_temp_c = p->gpu.temp;
        s_sample.gpu_mem_used_kb = p->gpu.mem_used * 1024U;
        s_sample.gpu_mem_total_kb = p->gpu.mem_total * 1024U;
        s_sample.gpu_power_w = p->gpu.power_w;
        s_sample.gpu_core_clock = p->gpu.core_clock;
        s_sample.gpu_fan = p->gpu.fan_speed;
    }
    else {
        s_gpu_valid = false;
        s_sample.gpu_load = 0;
        s_sample.gpu_temp_c = -1;
        s_sample.gpu_mem_used_kb = 0;
        s_sample.gpu_mem_total_kb = 0;
        s_sample.gpu_power_w = 0;
        s_sample.gpu_core_clock = 0;
        s_sample.gpu_fan = 0;
    }

    /* History -------------------------------------------------------- */
    if (tick > 0U) {
        history_push();
    }

    s_sample.sample_count = tick + 1U;
}

static void sampler_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    time_keeper_poll();
    sample_once();
}

void sampler_set_pc_data(const pc_data_t *data)
{
    s_pc = data;
}

void sampler_init(void)
{
    if (s_inited) return;
    s_inited = true;

    memset(&s_sample, 0, sizeof(s_sample));
    s_sample.cpu_temp_c = SYS_TEMP_UNKNOWN;
    s_sample.gpu_temp_c = -1;

    sample_once();

    lv_timer_create(sampler_timer_cb, 1000, NULL);
}

const sys_sample_t *sampler_get(void)
{
    return &s_sample;
}
