/**
 * @file test_transmit_frame.cpp
 * @brief Round-trip tests for the RADIAN request frame the device transmits
 *
 * Everything before this file tested the receive path. The transmit path had no
 * coverage at all, yet a wrong interrogation frame is indistinguishable from a
 * meter that is out of range: the device simply never gets an answer.
 *
 * Make_Radian_Master_req() builds the frame and encode2serial_1_3() applies the
 * serial framing. Rather than assert the encoder's output byte by byte, which
 * would only restate the implementation, these tests push the encoded frame
 * back through radian_decode_4bitpbit() (the receiver used against real
 * captures) and check the original payload comes back out. The two halves were
 * written for opposite directions, so agreeing on framing, bit order and CRC is
 * real evidence rather than a tautology.
 */

#include <unity.h>

#include <cstring>

#include "core/crc_kermit.h"
#include "core/radian_decoder.h"
#include "core/utils.h"

namespace
{
    // Length of the literal preamble Make_Radian_Master_req() prepends. It is
    // sent as raw bytes, not through the serial encoder, so the round trip
    // starts after it.
    constexpr int SYNC_PATTERN_LEN = 9;

    // The unencoded request payload: 17 bytes of fields plus a 2-byte CRC.
    constexpr int PAYLOAD_LEN = 19;

    /**
     * @brief Turn an encode2serial_1_3() frame into the 4x oversampled sample
     *        stream a receiver sees on air.
     *
     * The radio transmits at 9.6 kbps while the protocol signals at 2.4 kbps,
     * so every logical bit occupies four identical samples, packed MSB-first
     * exactly as the CC1101 delivers them.
     *
     * Two adjustments are needed to line the encoder up with the receiver:
     *
     *  - encode2serial_1_3() emits the first byte's start bit before any data,
     *    whereas the receiver locks onto the stream mid-byte and treats the
     *    first sample run as data. On air that leading bit falls inside the
     *    preamble the receiver has already skipped, so it is dropped here.
     *  - the frame ends in stop bits, and the receiver commits a byte only when
     *    it processes the following separator. A separator plus an idle run is
     *    appended so the last byte is emitted.
     *
     * @return Number of sample bytes written, or 0 if they would not fit.
     */
    int frameToAir(const uint8_t *encoded, int encodedLen, uint8_t *out, int outMax)
    {
        const int bitCount = encodedLen * 8 - 1 + 1 + 8; // drop start, add separator + idle
        const int needed = bitCount * 4 / 8;
        if (needed > outMax)
        {
            return 0;
        }

        std::memset(out, 0, (size_t)needed);
        int outBit = 0;

        auto emit = [&](bool value) {
            for (int repeat = 0; repeat < 4; repeat++)
            {
                if (value)
                {
                    out[outBit / 8] |= (uint8_t)(1 << (7 - (outBit % 8)));
                }
                outBit++;
            }
        };

        for (int bit = 1; bit < encodedLen * 8; bit++)
        {
            emit((encoded[bit / 8] >> (7 - (bit % 8))) & 1);
        }

        emit(false); // separator terminating the last byte
        for (int i = 0; i < 8; i++)
        {
            emit(true); // idle, so the separator run is flushed
        }

        return needed;
    }

    /// Build a request frame and decode it back, returning the payload length.
    int roundTrip(uint8_t year, uint32_t serial, uint8_t *payload, int payloadMax)
    {
        uint8_t frame[128] = {0};
        const int frameLen = Make_Radian_Master_req(frame, year, serial);
        TEST_ASSERT_TRUE(frameLen > SYNC_PATTERN_LEN);
        TEST_ASSERT_TRUE(frameLen <= (int)sizeof(frame));

        uint8_t air[1024];
        const int airLen = frameToAir(frame + SYNC_PATTERN_LEN,
                                      frameLen - SYNC_PATTERN_LEN,
                                      air, (int)sizeof(air));
        TEST_ASSERT_TRUE(airLen > 0);

        return (int)radian_decode_4bitpbit(air, airLen, payload, payloadMax);
    }
}

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

void test_request_frame_round_trips_through_the_receiver(void)
{
    uint8_t payload[64] = {0};
    const int len = roundTrip(21, 123456, payload, (int)sizeof(payload));

    TEST_ASSERT_EQUAL_INT(PAYLOAD_LEN, len);

    // The fixed RADIAN request header, as built by Make_Radian_Master_req().
    static const uint8_t expectedHeader[4] = {0x13, 0x10, 0x00, 0x45};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedHeader, payload, 4);
}

void test_request_frame_carries_the_meter_identity(void)
{
    uint8_t payload[64] = {0};
    const int len = roundTrip(21, 123456, payload, (int)sizeof(payload));
    TEST_ASSERT_EQUAL_INT(PAYLOAD_LEN, len);

    // Year then serial, big-endian across bytes 4..7.
    TEST_ASSERT_EQUAL_UINT8(21, payload[4]);
    TEST_ASSERT_EQUAL_UINT8((123456 >> 16) & 0xFF, payload[5]);
    TEST_ASSERT_EQUAL_UINT8((123456 >> 8) & 0xFF, payload[6]);
    TEST_ASSERT_EQUAL_UINT8(123456 & 0xFF, payload[7]);
}

void test_request_frame_crc_is_valid_after_the_round_trip(void)
{
    uint8_t payload[64] = {0};
    const int len = roundTrip(21, 123456, payload, (int)sizeof(payload));
    TEST_ASSERT_EQUAL_INT(PAYLOAD_LEN, len);

    const uint16_t crc = crc_kermit(payload, PAYLOAD_LEN - 2);
    TEST_ASSERT_EQUAL_UINT8((crc >> 8) & 0xFF, payload[PAYLOAD_LEN - 2]);
    TEST_ASSERT_EQUAL_UINT8(crc & 0xFF, payload[PAYLOAD_LEN - 1]);
}

void test_request_frame_round_trips_for_boundary_identities(void)
{
    struct Identity
    {
        uint8_t year;
        uint32_t serial;
    };

    // Serial is carried in three bytes, so the top of the range is 0xFFFFFF.
    static const Identity identities[] = {
        {0, 0},
        {0, 1},
        {99, 0xFFFFFF},
        {21, 0x00FF00},
        {17, 501090},
        {24, 1234567 & 0xFFFFFF},
    };

    for (const Identity &id : identities)
    {
        uint8_t payload[64] = {0};
        const int len = roundTrip(id.year, id.serial, payload, (int)sizeof(payload));

        TEST_ASSERT_EQUAL_INT(PAYLOAD_LEN, len);
        TEST_ASSERT_EQUAL_UINT8(id.year, payload[4]);
        TEST_ASSERT_EQUAL_UINT8((id.serial >> 16) & 0xFF, payload[5]);
        TEST_ASSERT_EQUAL_UINT8((id.serial >> 8) & 0xFF, payload[6]);
        TEST_ASSERT_EQUAL_UINT8(id.serial & 0xFF, payload[7]);

        const uint16_t crc = crc_kermit(payload, PAYLOAD_LEN - 2);
        TEST_ASSERT_EQUAL_UINT8((crc >> 8) & 0xFF, payload[PAYLOAD_LEN - 2]);
        TEST_ASSERT_EQUAL_UINT8(crc & 0xFF, payload[PAYLOAD_LEN - 1]);
    }
}

// ---------------------------------------------------------------------------
// Frame construction
// ---------------------------------------------------------------------------

void test_request_frame_starts_with_the_sync_pattern(void)
{
    static const uint8_t expectedSync[SYNC_PATTERN_LEN] = {
        0x50, 0x00, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0xFF, 0xFF};

    uint8_t frame[128] = {0};
    Make_Radian_Master_req(frame, 21, 123456);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedSync, frame, SYNC_PATTERN_LEN);
}

void test_request_frame_length_is_stable(void)
{
    // The frame is fixed-format, so its length must not depend on the identity.
    // A shorter frame for some meters would mean the CC1101 was fed a different
    // number of bytes than the TX loop expects.
    uint8_t frame[128] = {0};
    const int reference = Make_Radian_Master_req(frame, 21, 123456);

    TEST_ASSERT_EQUAL_INT(reference, Make_Radian_Master_req(frame, 0, 0));
    TEST_ASSERT_EQUAL_INT(reference, Make_Radian_Master_req(frame, 99, 0xFFFFFF));
    TEST_ASSERT_TRUE(reference > SYNC_PATTERN_LEN);
}

void test_request_frames_differ_between_meters(void)
{
    // Two meters must never be interrogated by the same bytes: identical frames
    // would mean the identity was dropped somewhere in the encoding.
    uint8_t frameA[128] = {0};
    uint8_t frameB[128] = {0};

    const int lenA = Make_Radian_Master_req(frameA, 21, 123456);
    const int lenB = Make_Radian_Master_req(frameB, 21, 123457);

    TEST_ASSERT_EQUAL_INT(lenA, lenB);
    TEST_ASSERT_TRUE(std::memcmp(frameA, frameB, (size_t)lenA) != 0);
}

void test_request_frame_is_deterministic(void)
{
    uint8_t first[128] = {0};
    uint8_t second[128] = {0};

    const int lenFirst = Make_Radian_Master_req(first, 17, 501090);
    const int lenSecond = Make_Radian_Master_req(second, 17, 501090);

    TEST_ASSERT_EQUAL_INT(lenFirst, lenSecond);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first, second, lenFirst);
}

// ---------------------------------------------------------------------------
// Serial framing
// ---------------------------------------------------------------------------

void test_serial_encoding_round_trips_arbitrary_payloads(void)
{
    // encode2serial_1_3() is the framing layer on its own: whatever goes in
    // must come back out of the receiver unchanged.
    uint8_t input[16];
    for (int i = 0; i < (int)sizeof(input); i++)
    {
        input[i] = (uint8_t)(i * 17 + 3);
    }

    uint8_t encoded[128] = {0};
    const int encodedLen = encode2serial_1_3(input, (int)sizeof(input), encoded);
    TEST_ASSERT_TRUE(encodedLen > (int)sizeof(input));

    uint8_t air[1024];
    const int airLen = frameToAir(encoded, encodedLen, air, (int)sizeof(air));
    TEST_ASSERT_TRUE(airLen > 0);

    uint8_t decoded[64] = {0};
    const int decodedLen = (int)radian_decode_4bitpbit(air, airLen, decoded, (int)sizeof(decoded));

    TEST_ASSERT_EQUAL_INT((int)sizeof(input), decodedLen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(input, decoded, (int)sizeof(input));
}

void test_serial_encoding_round_trips_all_byte_values(void)
{
    uint8_t input[256];
    for (int i = 0; i < 256; i++)
    {
        input[i] = (uint8_t)i;
    }

    // Encode in blocks so the decoder's output buffer stays a sane size.
    for (int block = 0; block < 256; block += 16)
    {
        uint8_t encoded[128] = {0};
        const int encodedLen = encode2serial_1_3(&input[block], 16, encoded);

        uint8_t air[1024];
        const int airLen = frameToAir(encoded, encodedLen, air, (int)sizeof(air));
        TEST_ASSERT_TRUE(airLen > 0);

        uint8_t decoded[64] = {0};
        const int decodedLen = (int)radian_decode_4bitpbit(air, airLen, decoded, (int)sizeof(decoded));

        TEST_ASSERT_EQUAL_INT(16, decodedLen);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(&input[block], decoded, 16);
    }
}
