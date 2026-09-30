/**
 * @file meter_history.cpp
 * @brief Implementation of historical meter data processing
 */

#include "meter_history.h"
#include "../core/radian_parser.h"
#include <algorithm>
#include "../core/logging.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{
    /**
     * @brief Append printf-style text at @p pos, refusing to truncate
     *
     * Returns false if the formatted text does not fit in the remaining space,
     * leaving @p pos unchanged. A half-written JSON document is never useful to
     * a caller, so truncation is treated as an error rather than best-effort.
     */
    bool appendFormatted(char *buffer, int bufferSize, int &pos, const char *fmt, ...)
    {
        if (pos < 0 || pos >= bufferSize)
        {
            return false;
        }

        va_list args;
        va_start(args, fmt);
        const int written = vsnprintf(buffer + pos, (size_t)(bufferSize - pos), fmt, args);
        va_end(args);

        if (written < 0 || written >= bufferSize - pos)
        {
            return false;
        }

        pos += written;
        return true;
    }
}

HistoryStats MeterHistory::calculateStats(const uint32_t history[13], uint32_t currentVolume)
{
    HistoryStats stats = {};
    stats.currentVolume = currentVolume;

    // Count valid months
    stats.monthCount = countValidMonths(history);

    if (stats.monthCount == 0)
    {
        return stats; // No valid history
    }

    // Calculate monthly usage for each valid month
    uint32_t totalUsage = 0;
    for (int i = 0; i < stats.monthCount; i++)
    {
        if (i == 0)
        {
            // First month: can't calculate without older baseline
            stats.monthlyUsage[i] = 0;
        }
        else if (history[i] >= history[i - 1])
        {
            stats.monthlyUsage[i] = history[i] - history[i - 1];
        }
        else
        {
            stats.monthlyUsage[i] = 0; // Meter reset or underflow
        }
        totalUsage += stats.monthlyUsage[i];
    }

    // Calculate current month usage
    if (stats.monthCount > 0 && currentVolume >= history[stats.monthCount - 1])
    {
        stats.currentMonthUsage = currentVolume - history[stats.monthCount - 1];
    }
    else
    {
        stats.currentMonthUsage = 0;
    }

    stats.totalUsage = totalUsage + stats.currentMonthUsage;

    return stats;
}

int MeterHistory::generateHistoryJson(const uint32_t history[13], uint32_t currentVolume,
                                      char *outputBuffer, int bufferSize)
{
    if (!outputBuffer || bufferSize <= 1)
    {
        return 0;
    }

    outputBuffer[0] = '\0';

    const int monthCount = countValidMonths(history);
    if (monthCount == 0)
    {
        return 0; // No valid history
    }

    int pos = 0;
    bool ok = appendFormatted(outputBuffer, bufferSize, pos, "{\"history\":[");

    // Add historical volumes
    for (int i = 0; ok && i < monthCount; i++)
    {
        ok = appendFormatted(outputBuffer, bufferSize, pos, "%s%u", (i > 0 ? "," : ""), history[i]);
    }

    if (ok)
    {
        ok = appendFormatted(outputBuffer, bufferSize, pos, "],\"monthly_usage\":[");
    }

    // Start at the second month: the oldest month has no earlier baseline, so we
    // omit it entirely rather than publishing a misleading value. monthly_usage
    // holds (monthCount - 1) real month-over-month deltas, aligned so
    // monthly_usage[k] pairs with history[k+1].
    for (int i = 1; ok && i < monthCount; i++)
    {
        const uint32_t usage = calculateUsage(history[i], history[i - 1]);
        ok = appendFormatted(outputBuffer, bufferSize, pos, "%s%u", (i > 1 ? "," : ""), usage);
    }

    if (ok)
    {
        const uint32_t currentMonthUsage = calculateUsage(currentVolume, history[monthCount - 1]);
        ok = appendFormatted(outputBuffer, bufferSize, pos,
                             "],\"current_month_usage\":%u,\"months_available\":%d}",
                             currentMonthUsage, monthCount);
    }

    if (!ok)
    {
        // Report failure rather than publishing a truncated, unparseable payload.
        outputBuffer[0] = '\0';
        return 0;
    }

    return pos;
}

int MeterHistory::generateHistoryJsonCompact(const uint32_t history[13], uint32_t currentVolume,
                                             char *outputBuffer, int bufferSize)
{
    if (!outputBuffer || bufferSize <= 1)
    {
        return 0;
    }

    outputBuffer[0] = '\0';

    // Unlike generateHistoryJson(), an empty history is not an error here: emit a
    // valid empty document so Home Assistant always receives a parseable state.
    const int monthCount = countValidMonths(history);

    int pos = 0;
    bool ok = appendFormatted(outputBuffer, bufferSize, pos, "{\"monthly_usage\":[");

    // Month-over-month deltas, omitting the oldest month (no earlier baseline),
    // aligned so monthly_usage[k] pairs with history[k+1] - same as the full form.
    for (int i = 1; ok && i < monthCount; i++)
    {
        const uint32_t usage = calculateUsage(history[i], history[i - 1]);
        ok = appendFormatted(outputBuffer, bufferSize, pos, "%s%u", (i > 1 ? "," : ""), usage);
    }

    if (ok)
    {
        const uint32_t currentMonthUsage =
            (monthCount > 0) ? calculateUsage(currentVolume, history[monthCount - 1]) : 0;
        ok = appendFormatted(outputBuffer, bufferSize, pos,
                             "],\"current_month_usage\":%u,\"months_available\":%d}",
                             currentMonthUsage, monthCount);
    }

    if (!ok)
    {
        // Report failure rather than publishing a truncated, unparseable payload.
        outputBuffer[0] = '\0';
        return 0;
    }

    return pos;
}

void MeterHistory::getMonthLabel(int monthIndex, int totalMonths, char *outputBuffer, int bufferSize)
{
    if (!outputBuffer || bufferSize <= 1)
    {
        return;
    }

    if (monthIndex == totalMonths - 1)
    {
        snprintf(outputBuffer, bufferSize, "Now");
    }
    else if (monthIndex < totalMonths)
    {
        int monthsAgo = totalMonths - 1 - monthIndex;
        snprintf(outputBuffer, bufferSize, "-%02d", monthsAgo);
    }
    else
    {
        snprintf(outputBuffer, bufferSize, "???");
    }
}

void MeterHistory::printToSerial(const uint32_t history[13], uint32_t currentVolume,
                                 const char *headerPrefix)
{
    int monthCount = countValidMonths(history);

    if (monthCount == 0)
    {
        LOG_I("everblu_meter", "%s No historical data available", headerPrefix);
        return;
    }

    LOG_I("everblu_meter", "=== HISTORICAL DATA (%d months) ===", monthCount);
    LOG_I("everblu_meter", "%s Month  Volume (L)  Usage (L)", headerPrefix);
    LOG_I("everblu_meter", "%s -----  ----------  ---------", headerPrefix);

    // Print each historical month. The oldest month has no earlier baseline, so
    // its usage is unknown and shown as 0.
    // Note: the JSON monthly_usage array omits this oldest-month usage value.
    // Rows are labelled "-NN" months-ago; the live reading is printed separately
    // below as "Now".
    for (int i = 0; i < monthCount; i++)
    {
        uint32_t usage = (i > 0) ? calculateUsage(history[i], history[i - 1]) : 0;
        LOG_I("everblu_meter", "%s  -%02d   %10u  %9u", headerPrefix, monthCount - 1 - i, history[i], usage);
    }

    // Print current month usage
    uint32_t currentMonthUsage = calculateUsage(currentVolume, history[monthCount - 1]);
    LOG_I("everblu_meter", "%s   Now  %10u  %9u (current month usage: %u L)",
          headerPrefix, currentVolume, currentMonthUsage, currentMonthUsage);

    LOG_I("everblu_meter", "===================================");
}

bool MeterHistory::isHistoryValid(const uint32_t history[13])
{
    if (!history)
    {
        return false;
    }

    for (int i = 0; i < 13; i++)
    {
        if (history[i] != 0)
        {
            return true;
        }
    }

    return false;
}

int MeterHistory::countValidMonths(const uint32_t history[13])
{
    if (!history)
    {
        return 0;
    }

    // A zero entry marks the end of the stored history. Meter volume is a
    // cumulative counter, so an operational meter never legitimately reports 0,
    // which makes 0 usable as the "no more data" sentinel.
    for (int i = 0; i < 13; i++)
    {
        if (history[i] == 0)
        {
            return i;
        }
    }

    return 13;
}

uint32_t MeterHistory::calculateUsage(uint32_t current, uint32_t previous)
{
    if (current >= previous)
    {
        return current - previous;
    }
    else
    {
        return 0; // Meter reset or underflow
    }
}

namespace
{
    bool isLeapYear(int year)
    {
        return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    }

    int monthDays(const tm &date)
    {
        static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        return days[date.tm_mon] + (date.tm_mon == 1 && isLeapYear(date.tm_year + 1900));
    }

    // Civil arithmetic deliberately avoids mktime: the device timezone/DST
    // must not alter dates reported by the meter's own clock.
    int64_t civilSeconds(const tm &date)
    {
        int64_t days = 0;
        tm cursor{};
        for (cursor.tm_year = 70; cursor.tm_year < date.tm_year; ++cursor.tm_year)
        {
            days += isLeapYear(cursor.tm_year + 1900) ? 366 : 365;
        }
        for (cursor.tm_mon = 0; cursor.tm_mon < date.tm_mon; ++cursor.tm_mon)
            days += monthDays(cursor);
        return ((days + date.tm_mday - 1) * 24 + date.tm_hour) * 3600 + date.tm_min * 60 + date.tm_sec;
    }

    tm shiftSeconds(const tm &date, int64_t offset)
    {
        const int64_t seconds = civilSeconds(date) + offset;
        tm result{};
        result.tm_year = 70;
        int64_t days = seconds / 86400;
        result.tm_wday = (days + 4) % 7;
        result.tm_hour = seconds % 86400 / 3600;
        result.tm_min = seconds % 3600 / 60;
        result.tm_sec = seconds % 60;
        while (days >= (isLeapYear(result.tm_year + 1900) ? 366 : 365))
        {
            days -= isLeapYear(result.tm_year + 1900) ? 366 : 365;
            ++result.tm_year;
        }
        result.tm_mon = 0;
        while (days >= monthDays(result)) days -= monthDays(result), ++result.tm_mon;
        result.tm_mday = days + 1;
        return result;
    }

    tm shiftMonths(tm date, int months)
    {
        const int month = date.tm_year * 12 + date.tm_mon + months;
        date.tm_year = month / 12;
        date.tm_mon = month % 12;
        date.tm_mday = std::min(date.tm_mday, monthDays(date));
        return date;
    }

    tm fdrBoundary(const tm &clock, const radian_fdr_configuration &config)
    {
        tm boundary = clock;
        boundary.tm_min = boundary.tm_sec = 0;
        if (config.period == 3) return boundary;
        boundary.tm_hour = config.start_hour;
        const int startDay = config.start_day ? config.start_day : 1;
        if (config.period == 0)
        {
            boundary.tm_mday = std::min(startDay, monthDays(boundary));
            if (civilSeconds(clock) < civilSeconds(boundary))
            {
                boundary = shiftMonths(boundary, -1);
                boundary.tm_mday = std::min(startDay, monthDays(boundary));
            }
        }
        else if (config.period == 2)
        {
            // Last completed civil boundary. Daily/weekly rules have synthetic
            // coverage only; field validation currently covers monthly meters.
            if (civilSeconds(clock) < civilSeconds(boundary))
                boundary = shiftSeconds(boundary, -86400);
        }
        else
        {
            const int weekday = clock.tm_wday ? clock.tm_wday : 7;
            boundary = shiftSeconds(boundary, int64_t(startDay - weekday) * 86400);
            if (civilSeconds(boundary) > civilSeconds(clock))
                boundary = shiftSeconds(boundary, -7 * 86400);
        }
        return boundary;
    }

    tm intervalEnd(const tm &boundary, const radian_fdr_configuration &config, unsigned index)
    {
        if (config.period == 0)
        {
            tm end = shiftMonths(boundary, -int(index));
            // Reapply the configured day independently in every month: a
            // February clamp of day 29/30 must not turn January into day 31.
            end.tm_mday = std::min(config.start_day ? int(config.start_day) : 1, monthDays(end));
            return end;
        }
        const int seconds = config.period == 1 ? 7 * 86400 : config.period == 2 ? 86400 : 3600;
        return shiftSeconds(boundary, -int64_t(index) * seconds);
    }

    enum class FdrValidity : uint8_t { Valid, Unknown, Overflow, Negative, NotDone };
    FdrValidity fdrValidity(int64_t value, unsigned resolution)
    {
        if (!value) return FdrValidity::Unknown;
        if ((resolution == 1 && value == 65534) || (resolution == 3 && value == 254)) return FdrValidity::Negative;
        if ((resolution == 1 && value == 65535) || (resolution == 2 && value == -32768) ||
            (resolution == 3 && value == 255) || (resolution == 4 && value == -128)) return FdrValidity::Overflow;
        return FdrValidity::Valid;
    }

    FdrValidity zeroValidity(const int64_t monthStarts[14], const tm &end, const uint8_t leakage[14])
    {
        const int64_t last = civilSeconds(end) - 1; // APK intervals end inclusively
        // Byte zero's current-month leakage semantics are unverified; do not
        // certify a zero merely because it lies in the current month.
        if (last >= monthStarts[0]) return FdrValidity::Unknown;
        for (int ago = 1; ago <= 13; ++ago)
        {
            if (last >= monthStarts[ago] && last < monthStarts[ago - 1])
                return leakage[14 - ago] & 0x80 ? FdrValidity::Valid : FdrValidity::NotDone;
        }
        return FdrValidity::Unknown;
    }

    bool appendLitres(char *buffer, int size, int &pos, int64_t pulses, int exponent)
    {
        // Integer decimal formatting preserves sub-litre values and avoids
        // float rounding of large indexes or scaled signed consumption.
        if (exponent >= 0)
        {
            while (exponent--) pulses *= 10;
            return appendFormatted(buffer, size, pos, "%lld", (long long)pulses);
        }
        int64_t divisor = 1;
        for (int i = 0; i < -exponent; ++i) divisor *= 10;
        const int64_t magnitude = pulses < 0 ? -pulses : pulses;
        if (magnitude % divisor == 0)
            return appendFormatted(buffer, size, pos, "%lld", (long long)(pulses / divisor));
        return appendFormatted(buffer, size, pos, "%s%lld.%0*lld", pulses < 0 ? "-" : "",
            (long long)(magnitude / divisor), -exponent, (long long)(magnitude % divisor));
    }
}

bool MeterHistory::advanceMeterTime(const tm &base, uint32_t elapsedSeconds, tm &out)
{
    if (base.tm_year < 100 || base.tm_year > 199 || base.tm_mon < 0 || base.tm_mon > 11 ||
        base.tm_mday < 1 || base.tm_mday > monthDays(base) || base.tm_hour < 0 || base.tm_hour > 23 ||
        base.tm_min < 0 || base.tm_min > 59 || base.tm_sec < 0 || base.tm_sec > 59) return false;
    out = shiftSeconds(base, elapsedSeconds);
    return out.tm_year <= 199;
}

int MeterHistory::generateFullFdrJson(const radian_fdr_data &data, char *output, int size, const tm *meterTime, time_t capturedAt)
{
    if (!output || size < 1) return 0;
    output[0] = '\0';
    const auto &config = data.configuration;
    const unsigned pulse = data.pulse_medium[1];
    const unsigned medium = data.pulse_medium[0];
    if (data.communication_status[0] || data.communication_status[1] || config.resolution > 4 ||
        config.turn_factor > 4 || config.period > 3 || config.start_hour > 23 || config.start_day > 31 ||
        (config.period == 1 && config.start_day > 7) ||
        (medium != 6 && medium != 7 && medium != 22) || (pulse > 7 && (pulse < 11 || pulse > 18))) return 0;
    const int exponent = pulse <= 7 ? int(pulse) - 4 : int(pulse) - 15;
    static const unsigned widths[] = {4,2,2,1,1};
    static const char *periods[] = {"monthly", "weekly", "daily", "hourly"};
    static const char *validityNames[] = {"valid", "unknown", "overflow", "negative", "not_done"};
    const unsigned width = widths[config.resolution], count = 180 / width;
    unsigned multiplier = 1;
    for (unsigned i = 0; i < config.turn_factor; ++i) multiplier *= 10;
    tm clock{}, boundary{};
    const bool dated = meterTime && advanceMeterTime(*meterTime, 0, clock);
    int64_t monthStarts[14];
    if (dated)
    {
        boundary = fdrBoundary(clock, config);
        tm month = clock;
        month.tm_mday = 1;
        month.tm_hour = month.tm_min = month.tm_sec = 0;
        for (int ago = 0; ago <= 13; ++ago)
            monthStarts[ago] = civilSeconds(shiftMonths(month, -ago));
    }
    FdrValidity validity[180];
    int pos = 0;
    bool ok = appendFormatted(output, size, pos,
        "{\"period\":\"%s\",\"resolution\":%u,\"start_day\":%u,\"start_hour\":%u,"
        "\"turn_factor\":%u,\"pulse_value_code\":%u,\"unit\":\"L\",\"order\":\"newest_first\","
        "\"interval_count\":%u,\"timestamp_basis\":\"%s\",\"global_index\":",
        periods[config.period], config.resolution, config.start_day, config.start_hour, multiplier, pulse, count,
        dated ? "meter_clock" : "unavailable");
    ok = ok && appendLitres(output, size, pos, data.global_index - data.global_index % multiplier, exponent);
    ok = ok && appendFormatted(output, size, pos, ",\"current_index\":");
    ok = ok && appendLitres(output, size, pos, data.current_index, exponent);
    ok = ok && appendFormatted(output, size, pos, ",\"consumptions\":[");
    for (unsigned i = 0; ok && i < count; ++i)
    {
        // APK CommonCyblePulse joins second+first, EnhancedFdr reverses records.
        const unsigned offset = i < 88 / width ? 88 - (i + 1) * width : 180 - (i - 88 / width + 1) * width;
        uint32_t raw = 0;
        for (unsigned byte = 0; byte < width; ++byte) raw |= uint32_t(data.consumptions[offset + byte]) << (8 * byte);
        int64_t value = raw;
        if (config.resolution != 1 && config.resolution != 3 && (raw & (uint32_t(1) << (width * 8 - 1))))
            value -= int64_t(1) << (width * 8);
        validity[i] = fdrValidity(value, config.resolution);
        if (validity[i] == FdrValidity::Unknown && dated)
            validity[i] = zeroValidity(monthStarts, intervalEnd(boundary, config, i), data.leakage_history);
        ok = appendFormatted(output, size, pos, "%s", i ? "," : "");
        if (validity[i] == FdrValidity::Valid || validity[i] == FdrValidity::Unknown)
            ok = ok && appendLitres(output, size, pos, value * multiplier, exponent);
        else ok = ok && appendFormatted(output, size, pos, "null");
    }
    ok = ok && appendFormatted(output, size, pos, "],\"validity\":[");
    for (unsigned i = 0; ok && i < count; ++i)
        ok = appendFormatted(output, size, pos, "%s\"%s\"", i ? "," : "", validityNames[unsigned(validity[i])]);
    ok = ok && appendFormatted(output, size, pos, "],\"interval_end\":");
    if (!dated) ok = ok && appendFormatted(output, size, pos, "null");
    else
    {
        ok = ok && appendFormatted(output, size, pos, "[");
        for (unsigned i = 0; ok && i < count; ++i)
        {
            const tm end = intervalEnd(boundary, config, i);
            ok = appendFormatted(output, size, pos, "%s\"%04d-%02d-%02dT%02d:%02d:%02d\"", i ? "," : "",
                end.tm_year + 1900, end.tm_mon + 1, end.tm_mday, end.tm_hour, end.tm_min, end.tm_sec);
        }
        ok = ok && appendFormatted(output, size, pos, "]");
    }
    tm capture{};
    const tm *utc = capturedAt > 0 ? gmtime(&capturedAt) : nullptr;
    if (utc) capture = *utc;
    if (utc && capture.tm_year >= 100 && capture.tm_year <= 199)
        ok = ok && appendFormatted(output, size, pos, ",\"captured_at\":\"%04d-%02d-%02dT%02d:%02d:%02dZ\"}",
            capture.tm_year + 1900, capture.tm_mon + 1, capture.tm_mday, capture.tm_hour, capture.tm_min, capture.tm_sec);
    else
        ok = ok && appendFormatted(output, size, pos, ",\"captured_at\":null}");
    if (!ok) output[0] = '\0';
    return ok ? pos : 0;
}

bool MeterHistory::parseMeterTime(const char *text, tm &out)
{
    if (!text || strlen(text) != 19) return false;
    for (unsigned i = 0; i < 19; ++i)
    {
        const char delimiter = (i == 4 || i == 7) ? '-' : i == 10 ? ' ' : (i == 13 || i == 16) ? ':' : 0;
        if (delimiter ? text[i] != delimiter : (text[i] < '0' || text[i] > '9')) return false;
    }
    tm parsed{};
    if (sscanf(text, "%4d-%2d-%2d %2d:%2d:%2d", &parsed.tm_year, &parsed.tm_mon,
               &parsed.tm_mday, &parsed.tm_hour, &parsed.tm_min, &parsed.tm_sec) != 6) return false;
    parsed.tm_year -= 1900;
    --parsed.tm_mon;
    return advanceMeterTime(parsed, 0, out);
}

bool MeterHistory::captureWithinFdrInterval(const radian_fdr_data &data, const tm &sampledClock, uint32_t elapsedMs)
{
    const auto &config = data.configuration;
    if (config.period > 3 || config.start_hour > 23 || config.start_day > 31 ||
        (config.period == 1 && config.start_day > 7)) return false;
    tm start{}, finish{};
    if (!advanceMeterTime(sampledClock, 0, start) ||
        !advanceMeterTime(start, elapsedMs / 1000, finish)) return false;
    // Exact measured duration at the clock's second precision; no invented
    // processing margin. Meter sampling/closure uncertainty still remains.
    return civilSeconds(fdrBoundary(start, config)) == civilSeconds(fdrBoundary(finish, config));
}
