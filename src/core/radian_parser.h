/**
 * @file radian_parser.h
 * @brief Parsing and CRC validation for RADIAN protocol meter frames
 *
 * Decodes and validates RADIAN protocol payloads received from Everblu Cyble
 * water/gas meters, extracting the primary reading (volume, read counter,
 * battery, daily wake window) and history availability.
 *
 * IMPORTANT LICENSING NOTICE:
 * The RADIAN protocol implementation shall not be distributed nor used for
 * commercial products. It is exposed only to demonstrate CC1101 capability to
 * read water meter indexes. There is no warranty on this software.
 */

#ifndef RADIAN_PARSER_H
#define RADIAN_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct radian_primary_data
{
    uint32_t volume;
    uint8_t reads_counter;
    uint8_t battery_left;
    uint8_t time_start;
    uint8_t time_end;
    bool history_available;
};

uint16_t radian_crc_kermit(const uint8_t *input_ptr, size_t num_bytes);
bool radian_validate_crc(const uint8_t *decoded_buffer, size_t size);
bool radian_parse_primary_data(const uint8_t *decoded_buffer, size_t size, struct radian_primary_data *out);

#endif // RADIAN_PARSER_H
