/**
 * @file udp_reader.h
 * @brief WiFi + UDP broadcast receiver for PC monitor data
 */
#ifndef UDP_READER_H
#define UDP_READER_H

#include <Arduino.h>
#include "data_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the WiFi station. Non-blocking: the connection is completed in the
 * background while the UI keeps running.
 */
void udp_reader_init(void);

/**
 * Service the WiFi link and parse any datagrams received on NET_UDP_PORT.
 * Call this from loop() before lv_task_handler().
 * Sets pc_data.updated = true when a valid frame is parsed.
 */
void udp_reader_poll(pc_data_t *data);

/**
 * Turn the WiFi radio on or off at runtime (SETTINGS screen).
 * Enabling starts the station again; disabling stops UDP and powers the
 * radio down. The choice is persisted by app_settings.
 */
void udp_reader_set_enabled(bool on);
bool udp_reader_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* UDP_READER_H */
