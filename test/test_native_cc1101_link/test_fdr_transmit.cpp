#include <unity.h>
#include <cstring>
#include "native_cc1101_device.h"
#include "core/cc1101.h"
#include "core/radian_parser.h"
#include "core/utils.h"

namespace {
enum class TxFault { None, StallBefore, StallBetween, StallDrain, UnderflowWake, UnderflowBefore, UnderflowBetween, UnderflowFinalWrite, UnderflowFinalWriteFalseStatus, InvalidOccupancy, TransientFalseLow, UnstableCount };
TxFault tx_fault;
size_t wake_bytes, request_size;
uint8_t request[64];
unsigned request_space_polls, rx_strobes, tx_strobes;
std::vector<size_t> request_chunks;

void capture_transmit(uint8_t *buffer, size_t length) {
    const uint8_t header = buffer[0];
    auto &radio = nativeCC1101();
    if (length == 1 && header == 0x35) ++tx_strobes;
    if (length == 1 && header == 0x34) ++rx_strobes;
    if (header == 0xFA && length == 2 && wake_bytes == 77 * 8 && request_size < 54) {
        ++request_space_polls;
        radio.txTenthsPerMs = 0;
        if (tx_fault == TxFault::UnderflowBefore ||
            (tx_fault == TxFault::UnderflowBetween && request_size)) {
            radio.txUnderflow = true;
            radio.txActive = false;
            radio.marcstate = 0x16;
            radio.chipState = kChipStateTxUnderflow;
        } else if (tx_fault == TxFault::InvalidOccupancy) {
            radio.txBytes = 65;
        } else if (tx_fault == TxFault::StallBefore ||
                   (tx_fault == TxFault::StallBetween && request_size)) {
            radio.txBytes = 64;
        } else {
            // Force 39 + 8 + 7 byte chunks; an eight-byte request write is not WUP.
            radio.txBytes = request_size == 0 ? 25 : (request_size == 39 ? 56 : 57);
        }
    }
    if (header == 0x7F && length > 1) {
        if ((tx_fault == TxFault::UnderflowFinalWrite || tx_fault == TxFault::UnderflowFinalWriteFalseStatus) && request_size == 47) {
            // FIFO drains after its occupancy was read, before the final burst.
            radio.txBytes = 0;
            radio.txActive = false;
            radio.txUnderflow = true;
            radio.marcstate = 0x16;
            radio.chipState = kChipStateTxUnderflow;
        }
        TEST_ASSERT_LESS_OR_EQUAL_size_t(64, radio.txBytes + length - 1);
        if (wake_bytes < 77 * 8) {
            TEST_ASSERT_EQUAL_size_t(8, length - 1);
            wake_bytes += length - 1;
        } else {
            TEST_ASSERT_LESS_OR_EQUAL_size_t(sizeof(request), request_size + length - 1);
            memcpy(request + request_size, buffer + 1, length - 1);
            request_size += length - 1;
            request_chunks.push_back(length - 1);
            if (request_size == 54 && tx_fault != TxFault::StallDrain) radio.txTenthsPerMs = 3;
        }
    }
    nativeCC1101Transfer(buffer, length);
    if (header == 0xFA && length == 2 && wake_bytes == 77 * 8 && request_size < 54) {
        if (tx_fault == TxFault::TransientFalseLow && request_space_polls == 1) buffer[1] = 0;
        if (tx_fault == TxFault::UnstableCount) buffer[1] = request_space_polls % 2 ? 25 : 26;
    }
    if (tx_fault == TxFault::UnderflowFinalWriteFalseStatus && header == 0x7F && request_size == 54)
        buffer[length - 1] = 0x20; // Erratum can also corrupt the SPI STATE field.
    if (tx_fault == TxFault::UnderflowWake && header == 0x7F && wake_bytes == 77 * 8) {
        radio.txBytes = 0;
        radio.txActive = false;
        radio.txUnderflow = true;
        radio.marcstate = 0x16;
        radio.chipState = kChipStateTxUnderflow;
    }
}
void installTransmit(TxFault fault) {
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(433.82f));
    tx_fault = fault;
    wake_bytes = request_size = request_space_polls = rx_strobes = tx_strobes = 0;
    request_chunks.clear();
    nativeSpiSetHandler(capture_transmit);
}
}

void test_fdr_transmit_waits_for_fifo_space_and_disables_ats() {
    for (TxFault fault : {TxFault::None, TxFault::TransientFalseLow}) {
        for (uint8_t selector = 7; selector <= 8; ++selector) {
            installTransmit(fault);
            radian_fdr_data result{};
            TEST_ASSERT_FALSE(read_fdr_frame_for_meter(21, 123456, selector, &result)); // no RX reply
            TEST_ASSERT_EQUAL_size_t(77 * 8, wake_bytes);
            TEST_ASSERT_EQUAL_size_t(54, request_size);
            TEST_ASSERT_EQUAL_UINT(1, tx_strobes);
            TEST_ASSERT_EQUAL_size_t(3, request_chunks.size());
            TEST_ASSERT_EQUAL_size_t(39, request_chunks[0]);
            TEST_ASSERT_EQUAL_size_t(8, request_chunks[1]);
            TEST_ASSERT_EQUAL_size_t(7, request_chunks[2]);
            const uint8_t disabled_ats[7] = {};
            uint8_t raw[29], expected[64];
            radian_build_predefined_request(raw, sizeof(raw), 21, 123456, disabled_ats, 0, selector);
            const int size = encode_radian_request(raw, sizeof(raw), expected, sizeof(expected));
            TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, request, size);
        }
    }
    const auto transfers = nativeCC1101().transfers;
    radian_fdr_data result{};
    TEST_ASSERT_FALSE(read_fdr_frame_for_meter(21, 123456, 0x69, &result));
    TEST_ASSERT_EQUAL_UINT32(transfers, nativeCC1101().transfers);
}

void test_fdr_transmit_failures_restore_radio_without_receiving() {
    for (TxFault fault : {TxFault::StallBefore, TxFault::StallBetween, TxFault::StallDrain, TxFault::UnderflowWake, TxFault::UnderflowBefore,
                         TxFault::UnderflowBetween, TxFault::UnderflowFinalWrite, TxFault::UnderflowFinalWriteFalseStatus,
                         TxFault::InvalidOccupancy, TxFault::UnstableCount}) {
        installTransmit(fault);
        const auto start = millis();
        radian_fdr_data result{};
        std::string log;
        NativeSerial::capture() = &log;
        const bool ok = read_fdr_frame_for_meter(21, 123456, 7, &result);
        NativeSerial::capture() = nullptr;
        TEST_ASSERT_FALSE(ok);
        TEST_ASSERT_TRUE(log.find("FDR transmission failed") != std::string::npos);
        TEST_ASSERT_TRUE(log.find("normal end of transmit") == std::string::npos);
        if (fault == TxFault::UnderflowFinalWrite || fault == TxFault::UnderflowFinalWriteFalseStatus)
            TEST_ASSERT_TRUE(log.find("47/54 request bytes queued") != std::string::npos);
        TEST_ASSERT_EQUAL_UINT(0, rx_strobes);
        TEST_ASSERT_EQUAL_UINT(1, tx_strobes);
        TEST_ASSERT_LESS_OR_EQUAL_UINT(fault == TxFault::UnstableCount ? 4 : 400, request_space_polls);
        TEST_ASSERT_LESS_THAN_UINT32(4000, millis() - start);
        const size_t expected_size = fault == TxFault::StallDrain || fault == TxFault::UnderflowFinalWrite || fault == TxFault::UnderflowFinalWriteFalseStatus ? 54 :
            (fault == TxFault::StallBetween || fault == TxFault::UnderflowBetween ? 39 : 0);
        TEST_ASSERT_EQUAL_size_t(expected_size, request_size);
        TEST_ASSERT_EQUAL_UINT8(kChipStateIdle, nativeCC1101().chipState);
        TEST_ASSERT_EQUAL_UINT8(0, nativeCC1101().txBytes);
        TEST_ASSERT_FALSE(nativeCC1101().txUnderflow);
        TEST_ASSERT_EQUAL_HEX8(0x02, nativeCC1101().config[0x12]); // MDMCFG2: sync restored
        TEST_ASSERT_EQUAL_HEX8(0x00, nativeCC1101().config[0x08]); // PKTCTRL0: fixed length
    }
}

void test_standard_request_remains_exactly_39_bytes() {
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(433.82f));
    nativeCC1101().txLog.clear();
    get_meter_data_for_meter(21, 123456); // synthetic identity, no reply needed
    const auto &sent = nativeCC1101().txLog;
    TEST_ASSERT_EQUAL_size_t(77 * 8 + 39, sent.size());
    for (size_t i = 0; i < 77 * 8; ++i) TEST_ASSERT_EQUAL_HEX8(0x55, sent[i]);
    uint8_t expected[100]{};
    TEST_ASSERT_EQUAL_INT(39, Make_Radian_Master_req(expected, 21, 123456));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, sent.data() + 77 * 8, 39);
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
size_t pair_wake_bytes;
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
    if (length == 1 && buffer[0] == 0x3B) pair_wake_bytes = 0; // SFTX starts a new phase
    if (length == 1 && buffer[0] == 0x35) {
        nativeCC1101ArmReply(exchanges < replies.size() ? replies[exchanges] : std::vector<uint8_t>{});
        ++exchanges;
        requests.emplace_back();
    }
    if (length > 1 && buffer[0] == 0x7F) {
        TEST_ASSERT_LESS_OR_EQUAL_size_t(64, nativeCC1101().txBytes + length - 1);
        if (pair_wake_bytes < 77 * 8) {
            pair_wake_bytes += length - 1;
        } else {
            requests.back().insert(requests.back().end(), buffer + 1, buffer + length);
        }
    }
    nativeCC1101Transfer(buffer, length);
}
void installPair() {
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(433.82f));
    exchanges = 0; pair_wake_bytes = 0; requests.clear();
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
            TEST_ASSERT_EQUAL_size_t(size, requests[i].size());
            TEST_ASSERT_EQUAL_UINT8_ARRAY(encoded, requests[i].data(), size);
        }
    }
}
