/**
 * @file time_keeper.cpp
 * @brief Persistent wall clock (NVS anchor + ESP32 RTC timer + SNTP).
 *
 * See time_keeper.h for the rationale.
 */
#include "time_keeper.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <sys/time.h>
#include <time.h>

#include "net_config.h"

/* ESP-IDF only declares this in its private esp_hw_support header, which
 * the Arduino include path does not expose. The symbol is exported by
 * libesp_hw_support and returns the RTC timer in microseconds; unlike
 * esp_timer_get_time(), that counter survives a CPU reset while powered. */
extern "C" uint64_t esp_rtc_get_time_us(void);

#define ANCHOR_MAGIC    0x544D4B31UL   /* "TMK1" */
#define SAVE_PERIOD_MS  (60UL * 1000UL)
/* Sanity bound for the RTC-timer delta: a stale/corrupt anchor must not
 * fling the clock years into the future. */
#define RTC_GAP_MAX_US  (30ULL * 24ULL * 3600ULL * 1000000ULL)
#define JUMP_LOG_SEC    10

/* One NVS blob, so the epoch and the RTC counter that go with it cannot be
 * torn apart by a reset in the middle of a save. */
typedef struct {
    uint32_t magic;
    uint32_t epoch;    /* UTC Unix seconds at save time */
    uint64_t rtc_us;   /* esp_rtc_get_time_us() at save time */
} time_anchor_t;

static Preferences s_prefs;

static uint32_t s_epoch;       /* anchor: UTC seconds */
static uint32_t s_anchor_ms;   /* millis() when the anchor was taken */
static uint32_t s_saved_ms;    /* millis() of the last NVS write */
static bool s_valid;
static bool s_saved_once;
static bool s_inited;

static void write_system_clock(uint32_t epoch)
{
    struct timeval tv;
    tv.tv_sec = (time_t)epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
}

/* Short, local-time log line: the serial console is the only way to tell
 * which layer the boot clock came from. */
static void log_clock(const char *tag, uint32_t epoch)
{
    time_t local = (time_t)epoch + (time_t)NET_TZ_OFFSET_MIN * 60;
    struct tm tm;
    gmtime_r(&local, &tm);

    Serial0.printf("[Time] %s %04d-%02d-%02d %02d:%02d:%02d\n",
                   tag, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                   tm.tm_hour, tm.tm_min, tm.tm_sec);
}

void time_keeper_init(void)
{
    if (s_inited) return;
    s_inited = true;

    s_prefs.begin("clock", false);

    time_anchor_t a;
    size_t len = s_prefs.getBytesLength("anchor");
    bool restored = (len == sizeof(a)) &&
                    (s_prefs.getBytes("anchor", &a, sizeof(a)) == sizeof(a)) &&
                    (a.magic == ANCHOR_MAGIC) &&
                    (a.epoch >= TIME_KEEPER_MIN_VALID);

    if (restored) {
        uint32_t epoch = a.epoch;

        /* The RTC timer keeps counting across CPU/external resets as long
         * as the RTC domain stays powered — which covers a reflash over
         * USB. Power-on and brown-out resets restart it, so the NVS anchor
         * is used as-is there (the clock was frozen while unplugged). */
        esp_reset_reason_t reason = esp_reset_reason();
        if (reason != ESP_RST_POWERON && reason != ESP_RST_BROWNOUT &&
            a.rtc_us != 0) {
            uint64_t now_us = esp_rtc_get_time_us();
            if (now_us >= a.rtc_us) {
                uint64_t gap_us = now_us - a.rtc_us;
                if (gap_us <= RTC_GAP_MAX_US) {
                    epoch += (uint32_t)(gap_us / 1000000ULL);
                }
            }
        }

        s_epoch = epoch;
        s_anchor_ms = millis();
        s_saved_ms = millis();
        s_valid = true;
        s_saved_once = true;   /* the anchor is already in NVS */

        write_system_clock(epoch);
        log_clock("restored", epoch);
    }
    else {
        Serial0.println("[Time] no saved time yet - waiting for PC/SNTP");
    }
}

bool time_keeper_now(uint32_t *unix_ts)
{
    if (!s_valid) return false;

    uint32_t now = s_epoch + (millis() - s_anchor_ms) / 1000U;
    if (unix_ts != NULL) *unix_ts = now;
    return true;
}

void time_keeper_sync(uint32_t unix_ts)
{
    if (unix_ts < TIME_KEEPER_MIN_VALID) return;

    uint32_t old = 0;
    bool had = time_keeper_now(&old);

    s_epoch = unix_ts;
    s_anchor_ms = millis();
    s_valid = true;

    /* Keep newlib's clock in step: SNTP users and future code see the same
     * time as the dial. */
    write_system_clock(unix_ts);

    int32_t delta = (int32_t)(unix_ts - old);
    if (!had || delta > JUMP_LOG_SEC || delta < -JUMP_LOG_SEC) {
        log_clock(had ? "resync" : "sync", unix_ts);
    }
}

void time_keeper_poll(void)
{
    if (!s_inited) return;

    /* SNTP writes the newlib clock directly, so treat a valid system time
     * as a time source: adopt it on the first sync after boot (the keeper
     * may still be empty) or whenever it corrects the keeper by more than
     * the ±1 s tick rounding. */
    time_t sys = time(NULL);
    if (sys > (time_t)TIME_KEEPER_MIN_VALID &&
        sys < (time_t)4102444800LL) {   /* year 2100 sanity bound */
        uint32_t kept;
        if (!time_keeper_now(&kept)) {
            time_keeper_sync((uint32_t)sys);
        }
        else {
            int32_t diff = (int32_t)((uint32_t)sys - kept);
            if (diff > 2 || diff < -2) {
                time_keeper_sync((uint32_t)sys);
            }
        }
    }

    if (!s_valid) return;

    uint32_t now = millis();
    if (s_saved_once && (uint32_t)(now - s_saved_ms) < SAVE_PERIOD_MS) {
        return;
    }

    uint32_t epoch;
    time_keeper_now(&epoch);

    time_anchor_t a = {
        .magic = ANCHOR_MAGIC,
        .epoch = epoch,
        .rtc_us = esp_rtc_get_time_us(),
    };
    s_prefs.putBytes("anchor", &a, sizeof(a));

    s_saved_ms = now;
    s_saved_once = true;
}

void time_keeper_start_sntp(void)
{
    /* Called on every transition to WL_CONNECTED: configTime() restarts
     * SNTP, which also recovers the client after WiFi was switched off.
     * The servers return UTC; the INFO page adds NET_TZ_OFFSET_MIN itself,
     * so the process timezone stays at UTC. */
    configTime(0, 0, "ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org");
    Serial0.println("[Time] SNTP started");
}
