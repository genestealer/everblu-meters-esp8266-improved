/**
 * @file meter_history.h
 * @brief Historical meter data processing and analysis
 *
 * Handles processing of historical meter readings, calculates monthly usage patterns,
 * and generates JSON representations for MQTT/Home Assistant integration.
 *
 * This module is designed to be reusable across different projects (Arduino, ESPHome, etc.)
 * and is independent of MQTT or WiFi dependencies. It works with any meter data structure
 * that provides a 13-month history array.
 */

#ifndef METER_HISTORY_H
#define METER_HISTORY_H

#include <Arduino.h>

/**
 * @struct HistoryStats
 * @brief Statistics calculated from historical meter data
 */
struct HistoryStats
{
    int monthCount;               // Number of valid historical months (1-13)
    uint32_t currentVolume;       // Current meter reading (this month)
    uint32_t currentMonthUsage;   // Usage in current month (current - previous)
    uint32_t monthlyUsage[13];    // Monthly usage for each historical month
    uint32_t totalUsage;          // Sum of all months
    uint32_t averageMonthlyUsage; // Average usage per month
};

/**
 * @class MeterHistory
 * @brief Processes and analyzes historical meter data
 *
 * Calculates monthly usage patterns, generates JSON representations,
 * and provides statistics from 13-month historical records.
 */
class MeterHistory
{
public:
    /**
     * @brief Calculate statistics from historical data
     *
     * Processes a 13-month history array and calculates:
     * - Number of valid months
     * - Monthly usage (consumption per month)
     * - Total and average usage
     *
     * @param history Array of 13 uint32_t values (oldest to most recent)
     * @param currentVolume Current meter reading
     * @return HistoryStats structure with calculated values
     */
    static HistoryStats calculateStats(const uint32_t history[13], uint32_t currentVolume);

    /**
     * @brief Generate JSON representation of history and monthly usage
     *
     * Creates a JSON object with historical volumes and calculated monthly usage.
     * Format: {"history":[...], "monthly_usage":[...], "current_month_usage":X, "months_available":Y}
     *
     * @param history Array of 13 uint32_t values
     * @param currentVolume Current meter reading
     * @param outputBuffer Buffer to write JSON to
     * @param bufferSize Size of output buffer
     * @return Number of bytes written (including null terminator), or 0 if buffer too small
     */
    static int generateHistoryJson(const uint32_t history[13], uint32_t currentVolume,
                                   char *outputBuffer, int bufferSize);

    /**
     * @brief Get string description of a history month (relative to current)
     *
     * Returns human-readable string like "-03" for 3 months ago, "Now" for current month.
     *
     * @param monthIndex Index in history array (0 = oldest, 12 = most recent)
     * @param totalMonths Total number of valid months
     * @param outputBuffer Buffer to write string to (recommend 6 bytes)
     * @param bufferSize Size of output buffer
     */
    static void getMonthLabel(int monthIndex, int totalMonths, char *outputBuffer, int bufferSize);

    /**
     * @brief Print history and monthly usage to serial console
     *
     * Formats and prints calculated statistics with human-readable month labels.
     *
     * @param history Array of 13 uint32_t values
     * @param currentVolume Current meter reading
     * @param headerPrefix Optional prefix for serial output (e.g., "[HISTORY]")
     */
    static void printToSerial(const uint32_t history[13], uint32_t currentVolume,
                              const char *headerPrefix = "[HISTORY]");

    /**
     * @brief Validate history data
     *
     * Checks if history contains valid data (non-zero entries).
     *
     * @param history Array of 13 uint32_t values
     * @return true if at least one non-zero entry exists
     */
    static bool isHistoryValid(const uint32_t history[13]);

    /**
     * @brief Count valid history entries
     *
     * Counts consecutive non-zero entries from start of history array.
     *
     * @param history Array of 13 uint32_t values
     * @return Number of valid entries (0-13)
     */
    static int countValidMonths(const uint32_t history[13]);

private:
    // Helper: Calculate usage between two meter readings (with underflow protection)
    static uint32_t calculateUsage(uint32_t current, uint32_t previous);

    // Private constructor - static-only class
    MeterHistory() = delete;
};

#endif // METER_HISTORY_H
