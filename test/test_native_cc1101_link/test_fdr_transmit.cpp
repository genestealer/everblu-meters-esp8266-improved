#include <unity.h>
#include <cstring>
#include "native_cc1101_device.h"
#include "core/cc1101.h"
#include "core/radian_parser.h"
#include "core/utils.h"

namespace {
bool transmitting;
size_t wake_bytes, request_size;
uint8_t request[64], occupancy;
unsigned request_space_polls;

void capture_transmit(uint8_t *buffer, size_t length) {
    const uint8_t header = buffer[0];
    if (length == 1 && header == 0x35) transmitting = true; // STX
    if (length == 1 && header == 0x36) transmitting = false; // SIDLE
    if (header == 0x7F && length > 1) { // TX FIFO burst write
        if (length == 9) {
            wake_bytes += 8;
        } else {
            TEST_ASSERT_LESS_OR_EQUAL_size_t(64, occupancy + length - 1);
            request_size = length - 1;
            memcpy(request, buffer + 1, request_size);
        }
    }
    nativeCC1101Transfer(buffer, length);
    if (transmitting) {
        buffer[0] = 0x20; // Chip status: TX
        if (header == 0xF5 && length == 2) { // MARCSTATE
            buffer[1] = request_size ? 0x16 : 0x13;
        }
        if (header == 0xFA && length == 2) { // TXBYTES
            if (wake_bytes == 77 * 8) {
                ++request_space_polls;
                if (occupancy > 0) --occupancy;
                buffer[1] = occupancy;
            } else {
                buffer[1] = 8;
            }
        }
    }
}
}

void test_fdr_transmit_waits_for_fifo_space_and_disables_ats() {
    for (uint8_t selector = 7; selector <= 8; ++selector) {
        nativeCC1101Install();
        TEST_ASSERT_TRUE(cc1101_init(433.82f));
        transmitting = false; wake_bytes = request_size = request_space_polls = 0;
        occupancy = 25; // Enough space for standard39, too little for FDR54.
        digitalWrite(4, LOW); digitalWrite(5, LOW);
        nativeSpiSetHandler(capture_transmit);
        radian_fdr_data result{};
        TEST_ASSERT_FALSE(read_fdr_frame_for_meter(21, 123456, selector, &result)); // no RX reply
        TEST_ASSERT_EQUAL_size_t(77 * 8, wake_bytes);
        TEST_ASSERT_EQUAL_size_t(54, request_size);
        TEST_ASSERT_GREATER_THAN_UINT(1, request_space_polls);
        const uint8_t disabled_ats[7] = {};
        uint8_t raw[29], expected[64];
        radian_build_predefined_request(raw, sizeof(raw), 21, 123456, disabled_ats, 0, selector);
        const int size = encode_radian_request(raw, sizeof(raw), expected, sizeof(expected));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, request, size);
    }
    const auto transfers = nativeCC1101().transfers;
    radian_fdr_data result{};
    TEST_ASSERT_FALSE(read_fdr_frame_for_meter(21, 123456, 0x69, &result));
    TEST_ASSERT_EQUAL_UINT32(transfers, nativeCC1101().transfers);
}

// Exercise the actual receive guard: an odd-length response needs a rounded-up
// serial byte, so insufficient capacity must be rejected before touching SPI.
extern int receive_radian_frame(int size_byte, int timeout_ms, uint8_t *buffer, int capacity, int8_t *frequency_estimate = nullptr);
void test_fdr_receive_capacity_rounds_up_partial_serial_bytes() {
    TEST_ASSERT_TRUE(cc1101_init(433.82f));
    const struct { int decoded; int raw; } sizes[] = {{18, 112}, {124, 748}, {137, 828}, {139, 840}};
    uint8_t raw[840]{};
    for (const auto &size : sizes) {
        const auto before = nativeCC1101().transfers;
        TEST_ASSERT_EQUAL_INT(0, receive_radian_frame(size.decoded, 1, raw, size.raw - 1));
        TEST_ASSERT_EQUAL_UINT32(before, nativeCC1101().transfers);
        // With sufficient space, enter RX (the fake supplies no response).
        TEST_ASSERT_EQUAL_INT(0, receive_radian_frame(size.decoded, 1, raw, size.raw));
        TEST_ASSERT_GREATER_THAN_UINT32(before, nativeCC1101().transfers);
    }
}

namespace {
std::vector<std::vector<uint8_t>> replies;
unsigned exchanges;
std::vector<std::vector<uint8_t>> requests;

std::vector<uint8_t> response(uint8_t selector, uint8_t year = 21, uint32_t serial = 123456) {
    std::vector<uint8_t> frame(selector == 7 ? 137 : 139, 0);
    uint8_t requestRaw[29], ats[7]{};
    radian_build_predefined_request(requestRaw, sizeof(requestRaw), year, serial, ats, 0, selector);
    frame[0] = frame.size(); frame[1] = 0x11;
    memcpy(frame.data() + 3, requestRaw + 9, 5);
    memcpy(frame.data() + 9, requestRaw + 3, 5);
    if (selector == 7) {
        frame[17] = 42;
        frame[21] = 22; frame[22] = 4;
        frame[24] = 1; frame[25] = 3 << 4;
        memset(frame.data() + 39, 10, 88);
    } else {
        memset(frame.data() + 37, 20, 92);
    }
    const uint16_t crc = radian_crc_kermit(frame.data(), frame.size() - 2);
    frame[frame.size() - 2] = crc >> 8; frame.back() = crc;
    return frame;
}
std::vector<uint8_t> oversample(const std::vector<uint8_t> &frame) {
    // Stage-two sync has consumed the first start bit. Each data/stop/start
    // bit is four identical samples, packed MSB first, independently of the
    // production transmit encoder (which runs without oversampling).
    std::vector<uint8_t> nibbles;
    for (uint8_t byte : frame) {
        for (unsigned bit = 0; bit < 8; ++bit) nibbles.push_back(byte & (1 << bit) ? 15 : 0);
        nibbles.insert(nibbles.end(), {15, 15, 15, 0});
    }
    nibbles.insert(nibbles.end(), {15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15});
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < nibbles.size(); i += 2) bytes.push_back((nibbles[i] << 4) | nibbles[i + 1]);
    return bytes;
}
void pairTransfer(uint8_t *buffer, size_t length) {
    if (length == 1 && buffer[0] == 0x35) {
        nativeCC1101ArmReply(exchanges < replies.size() ? replies[exchanges] : std::vector<uint8_t>{});
        ++exchanges;
    }
    if (length > 1 && buffer[0] == 0x7F) {
        TEST_ASSERT_LESS_OR_EQUAL_size_t(64, nativeCC1101().txBytes + length - 1);
        if (length != 9) requests.emplace_back(buffer + 1, buffer + length);
    }
    nativeCC1101Transfer(buffer, length);
}
void installPair() {
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(433.82f));
    exchanges = 0; requests.clear();
    nativeSpiSetHandler(pairTransfer);
}
}

void test_fdr_complete_synthetic_pair_and_failures() {
    for (unsigned failure = 0; failure <= 6; ++failure) {
        installPair();
        auto first = response(7), second = response(8);
        if (failure == 1) first[50] ^= 1; // first CRC
        if (failure == 2) second[50] ^= 1; // second CRC
        if (failure == 3) second = response(8, 22); // valid CRC, wrong selected meter
        if (failure == 4) second.resize(80); // truncated response
        replies = {oversample(first), oversample(second)};
        if (failure == 5) replies[0].clear(); // no first reply
        if (failure == 6) replies[1].clear(); // no second reply
        radian_fdr_data data{};
        memset(&data, 0xAA, sizeof(data));
        const bool ok = read_full_fdr_for_meter(21, 123456, &data);
        TEST_ASSERT_EQUAL_MESSAGE(failure == 0, ok, "complete frame-pair outcome");
        TEST_ASSERT_EQUAL_UINT(failure == 1 || failure == 5 ? 1 : 2, exchanges);
        if (ok) {
            TEST_ASSERT_EQUAL_UINT32(42, data.current_index);
            for (unsigned i = 0; i < 180; ++i)
                TEST_ASSERT_EQUAL_UINT8(i < 88 ? 10 : 20, data.consumptions[i]);
        } else {
            radian_fdr_data empty{};
            TEST_ASSERT_EQUAL_MEMORY(&empty, &data, sizeof(data));
        }
        for (unsigned i = 0; i < requests.size(); ++i) {
            uint8_t raw[29], encoded[64], ats[7]{};
            radian_build_predefined_request(raw, sizeof(raw), 21, 123456, ats, 0, 7 + i);
            const int size = encode_radian_request(raw, sizeof(raw), encoded, sizeof(encoded));
            TEST_ASSERT_EQUAL_UINT8_ARRAY(encoded, requests[i].data(), size);
        }
    }
}
