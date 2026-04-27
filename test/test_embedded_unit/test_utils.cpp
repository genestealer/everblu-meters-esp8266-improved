/**
 * @file test_utils.cpp
 * @brief Unit tests for utility functions (CRC, hex display, encoding)
 */

#include <unity.h>
#include <Arduino.h>

// Forward declarations
extern unsigned long crc(unsigned char *data, int len);
extern void display_hex(unsigned char *buf, int length);

// Note: Global Unity setUp/tearDown hooks are defined in a single test runner
// (see test_config_validation.cpp) to avoid multiple definition linker errors.

/**
 * Test: CRC calculation with known data
 */
void test_crc_known_data(void)
{
    // Test with a simple known pattern
    unsigned char testData[] = {0x01, 0x02, 0x03, 0x04};
    unsigned long result = crc(testData, 4);

    // CRC should be consistent for same data
    unsigned long result2 = crc(testData, 4);
    TEST_ASSERT_EQUAL_UINT32(result, result2);
}

/**
 * Test: CRC with empty data
 */
void test_crc_empty_data(void)
{
    unsigned char emptyData[] = {};
    unsigned long result = crc(emptyData, 0);

    // Empty data should produce a specific CRC value
    TEST_ASSERT_NOT_EQUAL(0, result);
}

/**
 * Test: CRC with different data produces different results
 */
void test_crc_different_data(void)
{
    unsigned char data1[] = {0x01, 0x02, 0x03};
    unsigned char data2[] = {0x04, 0x05, 0x06};

    unsigned long crc1 = crc(data1, 3);
    unsigned long crc2 = crc(data2, 3);

    // Different data should produce different CRCs
    TEST_ASSERT_NOT_EQUAL(crc1, crc2);
}

/**
 * Test: CRC is deterministic
 */
void test_crc_deterministic(void)
{
    unsigned char testData[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    // Run CRC multiple times
    unsigned long results[5];
    for (int i = 0; i < 5; i++)
    {
        results[i] = crc(testData, 6);
    }

    // All results should be identical
    for (int i = 1; i < 5; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(results[0], results[i]);
    }
}

/**
 * Test: Display hex doesn't crash with null pointer
 */
void test_display_hex_null_safe(void)
{
    // This should not crash
    display_hex(NULL, 0);
    TEST_PASS();
}

/**
 * Test: Display hex with valid data
 */
void test_display_hex_valid_data(void)
{
    unsigned char testData[] = {0x12, 0x34, 0x56, 0x78};

    // This should not crash
    display_hex(testData, 4);
    TEST_PASS();
}

// Test registration and Arduino hooks are centralized in test_config_validation.cpp.
