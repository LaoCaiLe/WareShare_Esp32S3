/**
 * @file time_keeper.h
 * @brief Persistent wall clock for the watch face.
 *
 * The ESP32-S3-Touch-LCD-1.28 carries no battery-backed RTC chip, so wall
 * time is kept in three layers:
 *
 *   - while running: a Unix epoch anchored to millis();
 *   - across a reset: the ESP32 RTC timer keeps counting (power-on resets
 *     excepted), so the time since the last stored anchor is added back;
 *   - across a power cycle: the anchor (epoch + RTC timer value) is written
 *     to NVS once a minute and reloaded at boot. The clock stands still
 *     while the board is off — only an external RTC could avoid that.
 *
 * PC frames and SNTP both feed time_keeper_sync(). The INFO page reads
 * time_keeper_now(), so the dial keeps a running clock even when the PC
 * companion never sends a frame after a reflash.
 */
#ifndef TIME_KEEPER_H
#define TIME_KEEPER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Anything before 2020-01-01 is treated as "not a real time". */
#define TIME_KEEPER_MIN_VALID 1577836800UL

/* Restore the saved anchor. Call once after NVS and Serial are ready. */
void time_keeper_init(void);

/* One-second housekeeping: adopt SNTP corrections, persist the anchor. */
void time_keeper_poll(void);

/* Feed an authoritative Unix timestamp (UTC seconds). */
void time_keeper_sync(uint32_t unix_ts);

/* Current best-effort UTC seconds. False until a time source is known. */
bool time_keeper_now(uint32_t *unix_ts);

/* Start SNTP. Safe to call repeatedly; intended for WiFi connect events. */
void time_keeper_start_sntp(void);

#ifdef __cplusplus
}
#endif

#endif /* TIME_KEEPER_H */
