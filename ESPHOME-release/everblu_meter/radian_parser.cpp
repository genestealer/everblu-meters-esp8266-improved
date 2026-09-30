/**
 * @file radian_parser.cpp
 * @brief Implementation of RADIAN protocol frame parsing and CRC validation
 *
 * Validates the CRC-16/KERMIT trailer of RADIAN frames and decodes the
 * primary meter reading fields from the decoded payload buffer.
 */

#include "radian_parser.h"
#include "crc_kermit.h"

#include <string.h>

// Upper bound on a plausible meter reading. 1 billion litres (1 million m³) is
// far beyond any meter this protocol is used with, so anything above it
// indicates corrupted decode alignment rather than real data.
#define RADIAN_MAX_PLAUSIBLE_VOLUME_LITRES 1000000000UL

uint16_t radian_crc_kermit(const uint8_t *input_ptr, size_t num_bytes)
{
    return crc_kermit(input_ptr, num_bytes);
}

bool radian_validate_crc(const uint8_t *decoded_buffer, size_t size)
{
    if (decoded_buffer == NULL || size < 4)
    {
        return false;
    }

    const uint8_t length_field = decoded_buffer[0];
    size_t expected_len = length_field ? length_field : size;

    if (expected_len > size)
    {
        // The length byte claims more bytes than were decoded, so the CRC
        // trailer is not in the buffer and the frame cannot be verified at all.
        // In practice this means a truncated or misaligned capture. Reject it:
        // accepting it unchecked let corrupt frames through the one integrity
        // gate the radio path has.
        return false;
    }

    if (expected_len < 4)
    {
        return false;
    }

    const size_t crc_offset = expected_len - 2;
    const uint16_t received_crc = ((uint16_t)decoded_buffer[crc_offset] << 8) |
                                  (uint16_t)decoded_buffer[crc_offset + 1];
    // CRC-16/KERMIT covers the whole frame up to the trailer, INCLUDING the
    // length byte [0] (proven against known-good captures: crc over [0..L-3]
    // matches the [L-2],[L-1] trailer). expected_len - 2 = bytes [0 .. L-3].
    const uint16_t computed_crc = radian_crc_kermit(&decoded_buffer[0], expected_len - 2);
    return computed_crc == received_crc;
}

bool radian_parse_primary_data(const uint8_t *decoded_buffer, size_t size, struct radian_primary_data *out)
{
    if (out == NULL)
    {
        return false;
    }

    memset(out, 0, sizeof(*out));

    if (decoded_buffer == NULL || size < 30)
    {
        return false;
    }

    out->volume = ((uint32_t)decoded_buffer[18]) |
                  ((uint32_t)decoded_buffer[19] << 8) |
                  ((uint32_t)decoded_buffer[20] << 16) |
                  ((uint32_t)decoded_buffer[21] << 24);

    if (out->volume == 0 || out->volume == 0xFFFFFFFFUL)
    {
        memset(out, 0, sizeof(*out));
        return false;
    }

    // Reject physically impossible volumes (see RADIAN_MAX_PLAUSIBLE_VOLUME_LITRES):
    // values above this threshold indicate corrupted decode alignment, not real data.
    if (out->volume > RADIAN_MAX_PLAUSIBLE_VOLUME_LITRES)
    {
        memset(out, 0, sizeof(*out));
        return false;
    }

    if (size >= 49)
    {
        out->reads_counter = decoded_buffer[48];
        out->battery_left = decoded_buffer[31];
        out->time_start = decoded_buffer[44];
        out->time_end = decoded_buffer[45];

        if (out->time_start > 23 || out->time_end > 23)
        {
            memset(out, 0, sizeof(*out));
            return false;
        }

        if (out->battery_left == 0xFF || out->reads_counter == 0xFF)
        {
            memset(out, 0, sizeof(*out));
            return false;
        }
    }

    // Meter real-time clock and identifier string. Byte offsets are taken
    // directly from the RADIAN reference display_meter_report():
    //   [24]=day [25]=month [26]=year(20xx) [28]=hour [29]=minute [30]=second
    //   [32..42]=ASCII meter type/identifier (NUL-terminated)
    // Both are best-effort extras: a meter with an unset clock or a blank
    // identifier must not cause an otherwise valid reading to be discarded, so
    // failures only leave clock_valid false / meter_type empty.
    if (size >= 31)
    {
        const uint8_t day = decoded_buffer[24];
        const uint8_t month = decoded_buffer[25];
        const uint8_t year = decoded_buffer[26];
        const uint8_t hour = decoded_buffer[28];
        const uint8_t minute = decoded_buffer[29];
        const uint8_t second = decoded_buffer[30];

        if (day >= 1 && day <= 31 && month >= 1 && month <= 12 &&
            hour <= 23 && minute <= 59 && second <= 59)
        {
            out->clock_day = day;
            out->clock_month = month;
            out->clock_year = year;
            out->clock_hour = hour;
            out->clock_minute = minute;
            out->clock_second = second;
            out->clock_valid = true;
        }
    }

    if (size >= 33)
    {
        size_t n = 0;
        const size_t max_chars = sizeof(out->meter_type) - 1;
        for (size_t idx = 32; idx < size && idx <= 42 && n < max_chars; idx++)
        {
            const uint8_t c = decoded_buffer[idx];
            if (c == 0x00)
            {
                break; // NUL terminates the string
            }
            if (c < 0x20 || c > 0x7E)
            {
                // Non-printable byte: not a real identifier, discard partial.
                n = 0;
                break;
            }
            out->meter_type[n++] = (char)c;
        }
        out->meter_type[n] = '\0';
    }

    out->history_available = size >= 118;
    return true;
}

bool radian_reading_within_history_bounds(uint32_t volume, const uint32_t *history,
                                          int num_months, uint32_t spike_factor)
{
    // Insufficient data to judge: accept the reading.
    if (history == NULL || num_months < 2 || spike_factor == 0)
    {
        return true;
    }

    // Largest historical monthly usage (max delta between consecutive months).
    uint32_t max_monthly_usage = 0;
    for (int i = 1; i < num_months; i++)
    {
        if (history[i] >= history[i - 1])
        {
            uint32_t usage = history[i] - history[i - 1];
            if (usage > max_monthly_usage)
            {
                max_monthly_usage = usage;
            }
        }
    }

    const uint32_t newest = history[num_months - 1];

    // No consumption baseline, or the current volume predates the newest
    // snapshot: nothing sensible to compare against, so accept.
    if (max_monthly_usage == 0 || volume < newest)
    {
        return true;
    }

    const uint32_t current_month_usage = volume - newest;

    // 64-bit math avoids overflow when scaling the usage by the factor.
    if ((uint64_t)current_month_usage > (uint64_t)max_monthly_usage * (uint64_t)spike_factor)
    {
        return false; // implausible spike -> reject
    }

    return true;
}

static size_t build_read_request(uint8_t *out, size_t capacity, uint8_t year, uint32_t serial,
                                 const uint8_t *payload, size_t payload_size)
{
    const uint8_t header[] = {0, 0x10, 0, 0x45, year, (uint8_t)(serial >> 16),
                              (uint8_t)(serial >> 8), (uint8_t)serial, 0,
                              0x45, 0x20, 0x0A, 0x50, 0x14, 0, 0x0A};
    const size_t size = sizeof(header) + payload_size + 2;
    if (!out || capacity < size || serial > 0xFFFFFF) return 0;
    memcpy(out, header, sizeof(header));
    memcpy(out + sizeof(header), payload, payload_size);
    out[0] = size;
    const uint16_t crc = radian_crc_kermit(out, size - 2);
    out[size - 2] = crc >> 8;
    out[size - 1] = crc;
    return size;
}

size_t radian_build_standard_request(uint8_t *out, size_t capacity, uint8_t year, uint32_t serial)
{
    const uint8_t payload[] = {0x40};
    return build_read_request(out, capacity, year, serial, payload, sizeof(payload));
}

size_t radian_build_predefined_request(uint8_t *out, size_t capacity, uint8_t year, uint32_t serial,
                                      const uint8_t ats[7], uint16_t access_code, uint8_t frame_number)
{
    if (!ats) return 0;
    uint8_t payload[11] = {0x70};
    memcpy(payload + 1, ats, 7);
    payload[8] = access_code;
    payload[9] = access_code >> 8;
    payload[10] = frame_number;
    return build_read_request(out, capacity, year, serial, payload, sizeof(payload));
}

static uint32_t fdr_uint32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool radian_parse_fdr_frame(const uint8_t *frame, size_t size, uint8_t frame_number, radian_fdr_data *out)
{
    // A capture may include idle/noise after the declared frame; those bytes
    // are not payload and are not part of its CRC (same as the standard reader).
    const size_t expected = frame_number == 7 ? RADIAN_FDR_FRAME_7_SIZE :
                            frame_number == 8 ? RADIAN_FDR_FRAME_8_SIZE : 0;
    if (!frame || !out || !expected || size < expected || frame[0] != expected ||
        frame[1] != 0x11 || !radian_validate_crc(frame, size)) return false;

    // The schema starts after byte 15 (application envelope), not at it.
    const uint8_t *payload = frame + 16;
    if (frame_number == 7)
    {
        out->communication_status[0] = payload[0];
        out->current_index = fdr_uint32(payload + 1);
        memcpy(out->pulse_medium, payload + 5, 2);
        auto &config = out->configuration;
        memcpy(config.raw, payload + 7, 3);
        // APK BitsDataBlock reads fields least-significant bit first.
        config.start_hour = payload[7];
        config.start_day = payload[8] & 0x1F;
        config.period = (payload[8] >> 5) & 3;
        config.turn_factor = payload[9] & 0x0F;
        config.resolution = payload[9] >> 4;
        memcpy(out->enhanced_alarms, payload + 10, 3);
        memcpy(out->backflow, payload + 13, 6);
        out->global_index = fdr_uint32(payload + 19);
        memcpy(out->consumptions, payload + 23, 88);
        memcpy(out->water_intelligence_alarms, payload + 111, 6);
    }
    else
    {
        out->communication_status[1] = payload[0];
        out->miu_group = payload[1];
        out->battery_lifetime = payload[2];
        memcpy(out->leakage_threshold, payload + 3, 2);
        memcpy(out->rf_counters, payload + 5, 2);
        memcpy(out->leakage_history, payload + 7, 14);
        memcpy(out->consumptions + 88, payload + 21, 92);
        memcpy(out->billing_indexes, payload + 113, 8);
    }
    return true;
}
