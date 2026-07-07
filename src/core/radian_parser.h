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

/**
 * @brief Plausibility check of a reading against its own monthly history.
 *
 * Computes the implied current-month usage (@p volume minus the newest history
 * snapshot) and the largest historical monthly usage (max delta between
 * consecutive months), then rejects the reading when the current-month usage
 * exceeds @p spike_factor times the largest historical monthly usage. A
 * corrupted current volume shows up as an absurd jump versus the meter's own
 * history, so such frames are discarded rather than published.
 *
 * The check is intentionally skipped (returns true = accept) when there is
 * insufficient history to judge: @p history is NULL, fewer than 2 valid
 * months, a @p spike_factor of 0, a largest monthly usage of 0, or a @p volume
 * that predates the newest snapshot.
 *
 * @param volume        Current cumulative meter reading.
 * @param history       Array of monthly cumulative snapshots (oldest first).
 * @param num_months    Number of valid entries in @p history.
 * @param spike_factor  Multiplier applied to the largest monthly usage (e.g. 100).
 * @return true to accept the reading, false to reject it as an implausible spike.
 */
bool radian_reading_within_history_bounds(uint32_t volume, const uint32_t *history,
                                          int num_months, uint32_t spike_factor);

#endif // RADIAN_PARSER_H
