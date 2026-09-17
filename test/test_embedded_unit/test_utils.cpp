/**
 * @file test_utils.cpp
 * @brief Unit tests for core/utils - CRC-16/KERMIT and the log/report helpers
 *
 * Test registration lives in test_runner.cpp.
 */

#include <unity.h>
#include <stdint.h>
#include <string>

#include <Arduino.h>

#include "core/cc1101.h"
#include "core/crc_kermit.h"
#include "core/logging.h"
#include "core/utils.h"

namespace
{
    std::string g_serialLog;

    /// Redirect firmware log output into g_serialLog for the current scope.
    struct SerialCapture
    {
        SerialCapture()
        {
            g_serialLog.clear();
            NativeSerial::capture() = &g_serialLog;
        }
        ~SerialCapture() { NativeSerial::capture() = nullptr; }
    };

    bool logContains(const char *needle) { return g_serialLog.find(needle) != std::string::npos; }
}

/**
 * Test: CRC calculation with known data
 */
void test_crc_known_data(void)
{
    // The standard CRC-16/KERMIT check value over the ASCII string "123456789"
    // is 0x2189. crc_kermit() returns the bytes swapped, matching the order the
    // checksum appears in on the wire, so the expected value here is 0x8921.
    const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(0x8921, crc_kermit(check, sizeof(check)));
}

/**
 * Test: CRC over an empty buffer returns the init value
 */
void test_crc_empty_data(void)
{
    const uint8_t empty[1] = {0};
    TEST_ASSERT_EQUAL_HEX16(0x0000, crc_kermit(empty, 0));
    TEST_ASSERT_EQUAL_HEX16(0x0000, crc_kermit(nullptr, 0));
}

/**
 * Test: CRC with different data produces different results
 */
void test_crc_different_data(void)
{
    const uint8_t data1[] = {0x01, 0x02, 0x03};
    const uint8_t data2[] = {0x04, 0x05, 0x06};

    TEST_ASSERT_NOT_EQUAL(crc_kermit(data1, sizeof(data1)), crc_kermit(data2, sizeof(data2)));
}

/**
 * Test: CRC is deterministic
 */
void test_crc_deterministic(void)
{
    const uint8_t data[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const uint16_t expected = crc_kermit(data, sizeof(data));

    for (int i = 0; i < 5; i++)
    {
        TEST_ASSERT_EQUAL_HEX16(expected, crc_kermit(data, sizeof(data)));
    }
}

/**
 * Test: a single flipped bit changes the checksum
 *
 * This is the property the meter frame validation relies on to reject
 * corrupted receptions.
 */
void test_crc_detects_single_bit_flip(void)
{
    uint8_t frame[] = {0x2F, 0x2F, 0x1E, 0x11, 0x03, 0xF1, 0x39, 0x00};
    const uint16_t original = crc_kermit(frame, sizeof(frame));

    for (size_t byte = 0; byte < sizeof(frame); byte++)
    {
        for (int bit = 0; bit < 8; bit++)
        {
            frame[byte] ^= (uint8_t)(1u << bit);
            TEST_ASSERT_NOT_EQUAL(original, crc_kermit(frame, sizeof(frame)));
            frame[byte] ^= (uint8_t)(1u << bit); // restore
        }
    }
}

/**
 * Test: byte order affects the checksum
 */
void test_crc_is_order_sensitive(void)
{
    const uint8_t forward[] = {0x11, 0x22, 0x33, 0x44};
    const uint8_t reversed[] = {0x44, 0x33, 0x22, 0x11};

    TEST_ASSERT_NOT_EQUAL(crc_kermit(forward, sizeof(forward)),
                          crc_kermit(reversed, sizeof(reversed)));
}

// ---------------------------------------------------------------------------
// Meter data summary
// ---------------------------------------------------------------------------

void test_meter_summary_ignores_a_null_reading(void)
{
    SerialCapture capture;
    printMeterDataSummary(nullptr, false, 100);

    TEST_ASSERT_TRUE(g_serialLog.empty());
}

void test_meter_summary_falls_back_to_the_default_gas_divisor(void)
{
    // A misconfigured divisor must not divide by zero or report raw litres as m3.
    struct tmeter_data data = {};
    data.volume = 12345;

    SerialCapture capture;
    printMeterDataSummary(&data, true, 0);

    TEST_ASSERT_TRUE(logContains("Volume (m3)"));
    TEST_ASSERT_TRUE(logContains("123.450"));
}

void test_meter_summary_reports_litres_for_a_water_meter(void)
{
    struct tmeter_data data = {};
    data.volume = 12345;
    data.time_start = 6;
    data.time_end = 18;

    SerialCapture capture;
    printMeterDataSummary(&data, false, 100);

    TEST_ASSERT_TRUE(logContains("Volume (L)"));
    TEST_ASSERT_TRUE(logContains("12345"));
    TEST_ASSERT_TRUE(logContains("06:00"));
    TEST_ASSERT_TRUE(logContains("18:00"));
}

void test_meter_summary_clamps_an_out_of_range_time_window(void)
{
    struct tmeter_data data = {};
    data.volume = 1;
    data.time_start = -3;
    data.time_end = 41;

    SerialCapture capture;
    printMeterDataSummary(&data, false, 100);

    TEST_ASSERT_TRUE(logContains("00:00"));
    TEST_ASSERT_TRUE(logContains("23:00"));
}

// ---------------------------------------------------------------------------
// echo_debug routing
// ---------------------------------------------------------------------------

void test_echo_debug_is_silent_when_the_caller_disables_it(void)
{
    SerialCapture capture;
    echo_debug(false, "[METER] should not appear\n");

    TEST_ASSERT_TRUE(g_serialLog.empty());
}

void test_echo_debug_is_silent_inside_a_quiet_guard(void)
{
    // The guard is what stops a frequency scan dumping a frame per step.
    SerialCapture capture;
    {
        EchoDebugQuietGuard quiet;
        echo_debug(true, "[METER] should not appear\n");
    }
    TEST_ASSERT_TRUE(g_serialLog.empty());

    echo_debug(true, "[METER] back again\n");
    TEST_ASSERT_TRUE(logContains("back again"));
}

void test_echo_debug_colourises_a_recognised_tag(void)
{
    SerialCapture capture;
    echo_debug(true, "[METER] tagged line\n");

    TEST_ASSERT_TRUE(logContains("tagged line"));
    TEST_ASSERT_TRUE(logContains(EVB_ANSI_CYAN));
    TEST_ASSERT_TRUE(logContains(EVB_ANSI_RESET));
}

void test_echo_debug_leaves_an_unrecognised_line_uncoloured(void)
{
    // Hex dumps and other machine-parsed output must stay free of escape codes.
    SerialCapture capture;
    echo_debug(true, "[000-015]: 01 02 03\n");

    TEST_ASSERT_TRUE(logContains("[000-015]: 01 02 03"));
    TEST_ASSERT_FALSE(logContains("\033["));
}

void test_print_time_emits_a_formatted_timestamp(void)
{
    SerialCapture capture;
    print_time();

    // "%d/%m/%Y %X" always produces two date separators.
    TEST_ASSERT_TRUE(g_serialLog.find('/') != std::string::npos);
    TEST_ASSERT_TRUE(g_serialLog.find(':') != std::string::npos);
}
