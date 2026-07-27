/**
 * @file test_utils.cpp
 * @brief Unit tests for the CRC-16/KERMIT checksum used by the RADIAN protocol
 *
 * Test registration lives in test_runner.cpp.
 */

#include <unity.h>
#include <stdint.h>
#include "core/crc_kermit.h"

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
