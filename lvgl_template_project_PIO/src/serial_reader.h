/**
 * @file serial_reader.h
 * @brief Serial JSON frame receiver for PC monitor data
 */
#ifndef SERIAL_READER_H
#define SERIAL_READER_H

#include <Arduino.h>
#include "data_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the serial reader.
 * @param baud  baud rate (must match PC side)
 */
void serial_reader_init(unsigned long baud);

/**
 * Read available bytes from Serial and try to parse a complete JSON frame.
 * Call this from loop() before lv_task_handler().
 * Sets pc_data.updated = true when a valid frame is parsed.
 */
void serial_reader_poll(pc_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_READER_H */
