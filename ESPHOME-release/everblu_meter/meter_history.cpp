/**
 * @file meter_history.cpp
 * @brief Implementation of historical meter data processing
 */

#include "meter_history.h"
#include "logging.h"
#include <cstring>

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

    int monthCount = countValidMonths(history);
    if (monthCount == 0)
    {
        return 0; // No valid history
    }

    int pos = 0;
    int remaining = bufferSize;

    // Start JSON object
    pos += snprintf(outputBuffer + pos, remaining, "{\"history\":[");
    remaining = bufferSize - pos;

    if (remaining <= 1)
    {
        return 0;
    }

    // Add historical volumes
    for (int i = 0; i < monthCount; i++)
    {
        remaining = bufferSize - pos;
        if (remaining <= 1)
        {
            break;
        }
        pos += snprintf(outputBuffer + pos, remaining, "%s%u",
                        (i > 0 ? "," : ""), history[i]);
    }

    // Add monthly usage calculations
    remaining = bufferSize - pos;
    if (remaining <= 1)
    {
        // Buffer full before monthly_usage - close and return best-effort
        outputBuffer[bufferSize - 1] = '\0';
        return pos;
    }

    pos += snprintf(outputBuffer + pos, remaining, "],\"monthly_usage\":[");

    for (int i = 0; i < monthCount; i++)
    {
        uint32_t usage = calculateUsage(history[i], (i > 0) ? history[i - 1] : 0);

        remaining = bufferSize - pos;
        if (remaining <= 1)
        {
            break;
        }
        pos += snprintf(outputBuffer + pos, remaining, "%s%u",
                        (i > 0 ? "," : ""), usage);
    }

    // Calculate current month usage
    uint32_t currentMonthUsage = calculateUsage(currentVolume, (monthCount > 0) ? history[monthCount - 1] : 0);

    remaining = bufferSize - pos;
    if (remaining <= 1)
    {
        // Buffer full - close and return best-effort
        outputBuffer[bufferSize - 1] = '\0';
        return pos;
    }

    pos += snprintf(outputBuffer + pos, remaining,
                    "],\"current_month_usage\":%u,\"months_available\":%d}",
                    currentMonthUsage, monthCount);

    // Ensure null termination
    outputBuffer[bufferSize - 1] = '\0';

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

    // Print each historical month
    for (int i = 0; i < monthCount; i++)
    {
        char monthLabel[6];
        getMonthLabel(i, monthCount, monthLabel, sizeof(monthLabel));

        uint32_t usage = calculateUsage(history[i], (i > 0) ? history[i - 1] : 0);
        LOG_I("everblu_meter", "%s  %s   %10u  %9u", headerPrefix, monthLabel, history[i], usage);
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
