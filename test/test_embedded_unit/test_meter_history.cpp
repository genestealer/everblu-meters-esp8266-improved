/**
 * @file test_meter_history.cpp
 * @brief Unit tests for MeterHistory - monthly usage maths and JSON payload
 *
 * Test registration lives in test_runner.cpp.
 */

#include <unity.h>
#include <cstring>
#include <stdint.h>
#include "services/meter_history.h"

namespace
{
    // Builds a 13-slot history array from the leading values given. Remaining
    // slots are zeroed, which is how the firmware marks "no data": countValidMonths()
    // stops at the first zero.
    void makeHistory(uint32_t out[13], const uint32_t *values, int count)
    {
        memset(out, 0, sizeof(uint32_t) * 13);
        for (int i = 0; i < count; i++)
        {
            out[i] = values[i];
        }
    }
}

/**
 * Test: countValidMonths stops at the first zero entry
 */
void test_history_count_valid_months(void)
{
    uint32_t history[13];

    const uint32_t three[] = {100, 150, 220};
    makeHistory(history, three, 3);
    TEST_ASSERT_EQUAL_INT(3, MeterHistory::countValidMonths(history));

    // A zero in the middle terminates the count: later entries are ignored even
    // though they are non-zero.
    const uint32_t gap[] = {100, 150, 0, 400, 500};
    makeHistory(history, gap, 5);
    TEST_ASSERT_EQUAL_INT(2, MeterHistory::countValidMonths(history));

    memset(history, 0, sizeof(history));
    TEST_ASSERT_EQUAL_INT(0, MeterHistory::countValidMonths(history));

    for (int i = 0; i < 13; i++)
    {
        history[i] = (uint32_t)(i + 1) * 100u;
    }
    TEST_ASSERT_EQUAL_INT(13, MeterHistory::countValidMonths(history));

    TEST_ASSERT_EQUAL_INT(0, MeterHistory::countValidMonths(nullptr));
}

/**
 * Test: isHistoryValid detects any non-zero entry, unlike countValidMonths
 */
void test_history_is_valid(void)
{
    uint32_t history[13];

    memset(history, 0, sizeof(history));
    TEST_ASSERT_FALSE(MeterHistory::isHistoryValid(history));

    // Non-zero only after a gap: no usable months, but the array is not empty
    history[4] = 999;
    TEST_ASSERT_TRUE(MeterHistory::isHistoryValid(history));
    TEST_ASSERT_EQUAL_INT(0, MeterHistory::countValidMonths(history));

    TEST_ASSERT_FALSE(MeterHistory::isHistoryValid(nullptr));
}

/**
 * Test: calculateStats derives month-over-month usage
 */
void test_history_stats_typical(void)
{
    uint32_t history[13];
    const uint32_t values[] = {100, 150, 220};
    makeHistory(history, values, 3);

    const HistoryStats stats = MeterHistory::calculateStats(history, 260);

    TEST_ASSERT_EQUAL_INT(3, stats.monthCount);
    TEST_ASSERT_EQUAL_UINT32(260, stats.currentVolume);

    // The oldest month has no earlier baseline, so its usage is reported as 0
    TEST_ASSERT_EQUAL_UINT32(0, stats.monthlyUsage[0]);
    TEST_ASSERT_EQUAL_UINT32(50, stats.monthlyUsage[1]);
    TEST_ASSERT_EQUAL_UINT32(70, stats.monthlyUsage[2]);

    TEST_ASSERT_EQUAL_UINT32(40, stats.currentMonthUsage);
    TEST_ASSERT_EQUAL_UINT32(160, stats.totalUsage);            // 0 + 50 + 70 + 40
    TEST_ASSERT_EQUAL_UINT32(40, stats.averageMonthlyUsage);    // 160 / (3 + 1)
}

/**
 * Test: calculateStats returns a zeroed struct when there is no history
 */
void test_history_stats_empty(void)
{
    uint32_t history[13];
    memset(history, 0, sizeof(history));

    const HistoryStats stats = MeterHistory::calculateStats(history, 500);

    TEST_ASSERT_EQUAL_INT(0, stats.monthCount);
    TEST_ASSERT_EQUAL_UINT32(500, stats.currentVolume);
    TEST_ASSERT_EQUAL_UINT32(0, stats.currentMonthUsage);
    TEST_ASSERT_EQUAL_UINT32(0, stats.totalUsage);
    TEST_ASSERT_EQUAL_UINT32(0, stats.averageMonthlyUsage);
}

/**
 * Test: a decreasing reading (meter reset or replacement) yields zero usage
 * rather than a huge unsigned underflow
 */
void test_history_stats_handles_meter_reset(void)
{
    uint32_t history[13];
    const uint32_t values[] = {900, 950, 10}; // meter replaced, counter restarts
    makeHistory(history, values, 3);

    const HistoryStats stats = MeterHistory::calculateStats(history, 60);

    TEST_ASSERT_EQUAL_UINT32(50, stats.monthlyUsage[1]);
    TEST_ASSERT_EQUAL_UINT32(0, stats.monthlyUsage[2]); // would underflow
    TEST_ASSERT_EQUAL_UINT32(50, stats.currentMonthUsage);
}

/**
 * Test: a current reading below the newest history entry yields zero usage
 */
void test_history_stats_current_below_history(void)
{
    uint32_t history[13];
    const uint32_t values[] = {100, 200};
    makeHistory(history, values, 2);

    const HistoryStats stats = MeterHistory::calculateStats(history, 150);

    TEST_ASSERT_EQUAL_UINT32(0, stats.currentMonthUsage);
}

/**
 * Test: exact JSON payload published to Home Assistant
 */
void test_history_json_exact_payload(void)
{
    uint32_t history[13];
    const uint32_t values[] = {100, 150, 220};
    makeHistory(history, values, 3);

    char buffer[256];
    const int written = MeterHistory::generateHistoryJson(history, 260, buffer, sizeof(buffer));

    const char *expected =
        "{\"history\":[100,150,220],"
        "\"monthly_usage\":[50,70],"
        "\"current_month_usage\":40,"
        "\"months_available\":3}";

    TEST_ASSERT_EQUAL_STRING(expected, buffer);
    TEST_ASSERT_EQUAL_INT((int)strlen(expected), written);
}

/**
 * Test: monthly_usage omits the oldest month, so it is one shorter than history
 */
void test_history_json_single_month(void)
{
    uint32_t history[13];
    const uint32_t values[] = {100};
    makeHistory(history, values, 1);

    char buffer[256];
    const int written = MeterHistory::generateHistoryJson(history, 130, buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(written > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"history\":[100],\"monthly_usage\":[],"
        "\"current_month_usage\":30,\"months_available\":1}",
        buffer);
}

/**
 * Test: a full 13-month history fits the 512-byte buffer used by the publishers
 */
void test_history_json_full_thirteen_months(void)
{
    uint32_t history[13];
    for (int i = 0; i < 13; i++)
    {
        history[i] = 1000000u + (uint32_t)i * 4321u;
    }

    char buffer[512]; // matches esphome_data_publisher.cpp
    const int written = MeterHistory::generateHistoryJson(history, 1060000u, buffer, sizeof(buffer));

    TEST_ASSERT_TRUE(written > 0);
    TEST_ASSERT_TRUE(written < (int)sizeof(buffer));
    TEST_ASSERT_EQUAL_INT((int)strlen(buffer), written);
    TEST_ASSERT_EQUAL_CHAR('}', buffer[written - 1]);
}

/**
 * Test: no history produces no payload
 */
void test_history_json_empty_history(void)
{
    uint32_t history[13];
    memset(history, 0, sizeof(history));

    char buffer[256];
    memset(buffer, 'x', sizeof(buffer));

    TEST_ASSERT_EQUAL_INT(0, MeterHistory::generateHistoryJson(history, 500, buffer, sizeof(buffer)));
}

/**
 * Test: an undersized buffer reports failure instead of emitting truncated JSON
 *
 * Callers treat a positive return as "publish this", so a partial payload would
 * reach Home Assistant as unparseable state.
 */
void test_history_json_rejects_undersized_buffer(void)
{
    uint32_t history[13];
    const uint32_t values[] = {100, 150, 220};
    makeHistory(history, values, 3);

    // Every buffer size too small to hold the whole document must fail, and must
    // never report writing more than the buffer holds.
    for (int size = 1; size < 90; size++)
    {
        char buffer[128];
        memset(buffer, '\xAA', sizeof(buffer));

        const int written = MeterHistory::generateHistoryJson(history, 260, buffer, size);

        TEST_ASSERT_EQUAL_INT(0, written);
        // Nothing beyond the declared buffer size may be touched
        TEST_ASSERT_EQUAL_HEX8('\xAA', (unsigned char)buffer[size]);
    }
}

/**
 * Test: null output buffer is handled
 */
void test_history_json_null_buffer(void)
{
    uint32_t history[13];
    const uint32_t values[] = {100, 150};
    makeHistory(history, values, 2);

    TEST_ASSERT_EQUAL_INT(0, MeterHistory::generateHistoryJson(history, 200, nullptr, 256));
}

/**
 * Test: month labels are relative to the newest entry
 */
void test_history_month_labels(void)
{
    char label[6];

    MeterHistory::getMonthLabel(4, 5, label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("Now", label);

    MeterHistory::getMonthLabel(0, 5, label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("-04", label);

    MeterHistory::getMonthLabel(3, 5, label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("-01", label);

    // Index beyond the valid range
    MeterHistory::getMonthLabel(9, 5, label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("???", label);

    // Must not write to a zero-sized buffer
    char guarded[2] = {'\x7F', '\x7F'};
    MeterHistory::getMonthLabel(0, 5, guarded, 1);
    TEST_ASSERT_EQUAL_HEX8('\x7F', (unsigned char)guarded[0]);

    MeterHistory::getMonthLabel(0, 5, nullptr, sizeof(label));
}

/**
 * Test: the serial dump handles both populated and empty history without crashing
 */
void test_history_print_to_serial_is_safe(void)
{
    uint32_t history[13];
    const uint32_t values[] = {100, 150, 220};
    makeHistory(history, values, 3);
    MeterHistory::printToSerial(history, 260, "[HISTORY]");

    memset(history, 0, sizeof(history));
    MeterHistory::printToSerial(history, 260, "[HISTORY]");

    TEST_PASS();
}
