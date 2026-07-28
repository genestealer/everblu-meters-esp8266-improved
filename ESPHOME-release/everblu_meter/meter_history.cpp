/**
 * @file meter_history.cpp
 * @brief Implementation of historical meter data processing
 */

#include "meter_history.h"
#include "logging.h"
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
    stats.averageMonthlyUsage = (stats.monthCount > 0) ? (stats.totalUsage / (stats.monthCount + 1)) : 0;

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
