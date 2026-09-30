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

    // Meter real-time clock, decoded from the frame per the RADIAN reference
    // implementation (display_meter_report in the radianprotocol.com sources):
    //   byte [24]=day  [25]=month  [26]=year (add 2000)  [28]=hour  [29]=minute  [30]=second
    // clock_valid is false when the bytes are out of range (e.g. an unset
    // clock); an invalid clock never causes the whole reading to be discarded.
    uint8_t clock_day;
    uint8_t clock_month;
    uint8_t clock_year; // two-digit year, full year = 2000 + clock_year
    uint8_t clock_hour;
    uint8_t clock_minute;
    uint8_t clock_second;
    bool clock_valid;

    // ASCII meter type / identifier string, bytes [32..42] of the frame
    // (NUL-terminated in the reference). Empty when the bytes are not printable.
    char meter_type[12]; // up to 11 printable chars + NUL
};

uint16_t radian_crc_kermit(const uint8_t *input_ptr, size_t num_bytes);
bool radian_validate_crc(const uint8_t *decoded_buffer, size_t size);
bool radian_parse_primary_data(const uint8_t *decoded_buffer, size_t size, struct radian_primary_data *out);

// Raw request builders share addressing, length and CRC framing. ATS bytes are
// supplied explicitly: a nonzero ATS can synchronize the meter's clock.
size_t radian_build_standard_request(uint8_t *out, size_t capacity, uint8_t year, uint32_t serial);
size_t radian_build_predefined_request(uint8_t *out, size_t capacity, uint8_t year, uint32_t serial,
                                      const uint8_t ats[7], uint16_t access_code, uint8_t frame_number);

struct radian_fdr_configuration
{
    uint8_t raw[3];
    uint8_t start_hour, start_day, period, turn_factor, resolution;
};

struct radian_fdr_data
{
    uint8_t communication_status[2]; // Retained raw; status semantics are not assumed.
    uint32_t current_index, global_index;
    uint8_t pulse_medium[2];
    radian_fdr_configuration configuration;
    uint8_t enhanced_alarms[3], backflow[6], water_intelligence_alarms[6];
    uint8_t miu_group, battery_lifetime;
    uint8_t leakage_threshold[2], rf_counters[2], leakage_history[14], billing_indexes[8];
    uint8_t consumptions[180]; // Encoded deltas, not cumulative readings or timestamps.
};

// Captures confirm a 16-byte application envelope and a two-byte CRC.
// Frame 7 also has two uninterpreted bytes after its schema; frame 8 does not.
constexpr size_t RADIAN_FDR_FRAME_7_SIZE = 16 + 117 + 2 + 2;
constexpr size_t RADIAN_FDR_FRAME_8_SIZE = 16 + 121 + 2;

// Each successful parse updates only that frame's fields. Failure leaves out unchanged.
bool radian_parse_fdr_frame(const uint8_t *frame, size_t size, uint8_t frame_number, radian_fdr_data *out);

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
