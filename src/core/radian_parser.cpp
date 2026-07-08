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
        // Keep compatibility with frames that advertise a longer length than payload.
        return true;
    }

    if (expected_len < 4)
    {
        return false;
    }

    const size_t crc_offset = expected_len - 2;
    const uint16_t received_crc = ((uint16_t)decoded_buffer[crc_offset] << 8) |
                                  (uint16_t)decoded_buffer[crc_offset + 1];
    const uint16_t computed_crc = radian_crc_kermit(&decoded_buffer[1], expected_len - 3);
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
