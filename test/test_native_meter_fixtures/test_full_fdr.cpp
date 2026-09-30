#include <unity.h>
#include <cstring>
#include <initializer_list>
#include "core/radian_parser.h"
#include "core/utils.h"

// All meter identities in these fixtures are synthetic.
namespace {
void seal(uint8_t *frame, size_t size) {
    frame[0] = size;
    const uint16_t crc = radian_crc_kermit(frame, size - 2);
    frame[size - 2] = crc >> 8;
    frame[size - 1] = crc;
}
void fixture(uint8_t *frame, size_t size) {
    for (size_t i = 0; i < size; ++i) frame[i] = i;
    frame[1] = 0x11;
    frame[15] = 1; // Application envelope byte, not communication status
    frame[16] = 0;
    seal(frame, size);
}
}

void test_fdr_requests() {
    const uint8_t ats[7] = {28, 9, 26, 1, 12, 34, 56};
    uint8_t first[29], second[29];
    TEST_ASSERT_EQUAL_INT(29, radian_build_predefined_request(first, sizeof(first), 21, 123456, ats, 0, 7));
    TEST_ASSERT_EQUAL_INT(29, radian_build_predefined_request(second, sizeof(second), 21, 123456, ats, 0, 8));
    const uint8_t expected[] = {29,0x10,0,0x45,0x15,0x01,0xE2,0x40,0,0x45,0x20,0x0A,0x50,0x14,0,0x0A,0x70,
                               28,9,26,1,12,34,56,0,0,7};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, first, sizeof(expected));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first, second, 26);
    TEST_ASSERT_EQUAL_UINT8(8, second[26]);
    TEST_ASSERT_TRUE(radian_validate_crc(first, sizeof(first)));
    TEST_ASSERT_TRUE(radian_validate_crc(second, sizeof(second)));
    TEST_ASSERT_NOT_EQUAL(first[27] * 256 + first[28], second[27] * 256 + second[28]);
    TEST_ASSERT_EQUAL_INT(0, radian_build_predefined_request(first, 28, 21, 123456, ats, 0, 7));
    TEST_ASSERT_EQUAL_INT(0, radian_build_predefined_request(nullptr, 29, 21, 123456, ats, 0, 7));
    TEST_ASSERT_EQUAL_INT(0, radian_build_predefined_request(first, 29, 22, 0x1000000, ats, 0, 7));
    TEST_ASSERT_EQUAL_INT(0, radian_build_predefined_request(first, 29, 21, 123456, nullptr, 0, 7));
    TEST_ASSERT_EQUAL_INT(29, radian_build_predefined_request(first, 29, 21, 123456, ats, 0x1234, 7));
    TEST_ASSERT_EQUAL_UINT8(0x34, first[24]);
    TEST_ASSERT_EQUAL_UINT8(0x12, first[25]);
}

void test_standard_request_unchanged() {
    // Golden raw request independently specified before refactoring the builder.
    uint8_t raw[] = {19,0x10,0,0x45,0x15,0x01,0xE2,0x40,0,0x45,0x20,0x0A,0x50,0x14,0,0x0A,0x40,0,0};
    seal(raw, sizeof(raw));
    uint8_t expected[100] = {0x50,0,0,0,3,0xFF,0xFF,0xFF,0xFF}, actual[100] = {};
    const int expected_size = 9 + encode2serial_1_3(raw, sizeof(raw), expected + 9);
    const int size = Make_Radian_Master_req(actual, 21, 123456);
    TEST_ASSERT_EQUAL_INT(expected_size, size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, size);
}

void test_fdr_parse_and_join() {
    uint8_t first[137], second[139];
    fixture(first, sizeof(first)); fixture(second, sizeof(second));
    first[23] = 6; first[24] = 17 | (3 << 5); first[25] = 2 | (4 << 4);
    seal(first, sizeof(first));
    radian_fdr_data data{};
    TEST_ASSERT_TRUE(radian_parse_fdr_frame(first, sizeof(first), 7, &data));
    TEST_ASSERT_TRUE(radian_parse_fdr_frame(second, sizeof(second), 8, &data));
    TEST_ASSERT_EQUAL_UINT8(0, data.communication_status[0]);
    TEST_ASSERT_EQUAL_UINT8(0, data.communication_status[1]);
    TEST_ASSERT_EQUAL_UINT32(0x14131211, data.current_index);
    TEST_ASSERT_EQUAL_UINT32(0x26252423, data.global_index);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first + 23, data.configuration.raw, 3);
    TEST_ASSERT_EQUAL_UINT8(6, data.configuration.start_hour);
    TEST_ASSERT_EQUAL_UINT8(17, data.configuration.start_day);
    TEST_ASSERT_EQUAL_UINT8(3, data.configuration.period);
    TEST_ASSERT_EQUAL_UINT8(2, data.configuration.turn_factor);
    TEST_ASSERT_EQUAL_UINT8(4, data.configuration.resolution);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first + 39, data.consumptions, 88);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(second + 37, data.consumptions + 88, 92);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first + 127, data.water_intelligence_alarms, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(second + 129, data.billing_indexes, 8);
    TEST_ASSERT_EQUAL_UINT8(17, data.miu_group);
    TEST_ASSERT_EQUAL_UINT8(18, data.battery_lifetime);
}

void test_fdr_rejects_bad_frames() {
    for (uint8_t selector : {7, 8}) {
        uint8_t frame[141];
        const size_t size = selector == 7 ? 137 : 139;
        fixture(frame, size);
        radian_fdr_data data{}, original{};
        for (size_t n = 0; n < size; ++n)
            TEST_ASSERT_FALSE(radian_parse_fdr_frame(frame, n, selector, &data));
        for (size_t bit = 0; bit < size * 8; ++bit) {
            frame[bit / 8] ^= 1 << (bit % 8);
            TEST_ASSERT_FALSE(radian_parse_fdr_frame(frame, size, selector, &data));
            frame[bit / 8] ^= 1 << (bit % 8);
        }
        // A CRC-valid legacy assumed length must not bypass the envelope check.
        seal(frame, selector == 7 ? 134 : 138);
        TEST_ASSERT_FALSE(radian_parse_fdr_frame(frame, size, selector, &data));
        if (selector == 8) {
            // Frame 8 has no extra suffix: reject the old 141-byte assumption.
            seal(frame, sizeof(frame));
            TEST_ASSERT_FALSE(radian_parse_fdr_frame(frame, sizeof(frame), selector, &data));
        }
        frame[1] = 0x10; seal(frame, size);
        TEST_ASSERT_FALSE(radian_parse_fdr_frame(frame, size, selector, &data));
        frame[1] = 0x11; seal(frame, size);
        TEST_ASSERT_FALSE(radian_parse_fdr_frame(frame, size, 6, &data));
        TEST_ASSERT_FALSE(radian_parse_fdr_frame(frame, size, selector, nullptr));
        TEST_ASSERT_FALSE(radian_parse_fdr_frame(nullptr, size, selector, &data));
        TEST_ASSERT_EQUAL_MEMORY(&original, &data, sizeof(data));
    }
}

// Consume the shareable, entirely synthetic fixture with the real parser and
// formatter. Regex only extracts this fixed test document's hex/string fields.
#include <fstream>
#include <sstream>
#include <regex>
#include <string>
#include <vector>
#include "services/meter_history.h"
void test_shared_synthetic_fdr_fixture() {
    std::ifstream file;
    for (const char *prefix : {"", "../", "../../", "../../../"}) {
        file.open(std::string(prefix) + "test/fixtures/full_fdr/synthetic_pair.json");
        if (file.good()) break;
        file.clear();
    }
    TEST_ASSERT_TRUE_MESSAGE(file.good(), "Synthetic FDR fixture missing");
    const std::string document((std::istreambuf_iterator<char>(file)), {});
    TEST_ASSERT_NOT_EQUAL(std::string::npos, document.find("fully_synthetic"));
    const std::regex hexField("\"(decoded_hex|request_hex)\": \\\"([0-9A-F ]+)\\\"");
    radian_fdr_data data{};
    unsigned selector = 7, fields = 0;
    for (auto it = std::sregex_iterator(document.begin(), document.end(), hexField);
         it != std::sregex_iterator(); ++it) {
        std::vector<uint8_t> bytes;
        std::istringstream hex((*it)[2].str());
        unsigned byte;
        while (hex >> std::hex >> byte) bytes.push_back(byte);
        TEST_ASSERT_TRUE(radian_validate_crc(bytes.data(), bytes.size()));
        if ((*it)[1].str() == "decoded_hex") {
            TEST_ASSERT_TRUE(radian_parse_fdr_frame(bytes.data(), bytes.size(), selector, &data));
        } else {
            uint8_t request[29], ats[7]{};
            TEST_ASSERT_EQUAL_INT(bytes.size(), radian_build_predefined_request(request, sizeof(request), 21, 123456, ats, 0, selector));
            TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), request, sizeof(request));
            ++selector;
        }
        ++fields;
    }
    TEST_ASSERT_EQUAL_UINT(4, fields);
    TEST_ASSERT_EQUAL_UINT32(123456, data.current_index);
    TEST_ASSERT_EQUAL_UINT32(120000, data.global_index);
    char output[16384];
    TEST_ASSERT_GREATER_THAN_INT(0, MeterHistory::generateFullFdrJson(data, output, sizeof(output)));
    std::smatch match;
    TEST_ASSERT_TRUE(std::regex_search(document, match, std::regex("\"consumptions_newest_first\": \\[([^\\]]+)\\]")));
    const std::string expected = std::regex_replace(match[1].str(), std::regex("\\s+"), "");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, std::string(output).find("\"consumptions\":[" + expected + "]"));
}
