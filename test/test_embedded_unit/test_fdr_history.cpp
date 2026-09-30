#include <unity.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include "services/meter_history.h"
#include "core/radian_parser.h"

namespace {
radian_fdr_data sample() {
    radian_fdr_data data{};
    data.configuration.start_day = 1;
    data.pulse_medium[0] = 0x16; // Water, synthetic one-litre pulse weight
    data.pulse_medium[1] = 4;
    data.global_index = 12345;
    data.current_index = 12456;
    return data;
}
void put(uint8_t *out, uint32_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) out[i] = value >> (8 * i);
}
std::string json(const radian_fdr_data &data, const tm *clock = nullptr) {
    char output[16384];
    const int count = MeterHistory::generateFullFdrJson(data, output, sizeof(output), clock);
    TEST_ASSERT_GREATER_THAN_INT(0, count);
    TEST_ASSERT_EQUAL_INT(count, strlen(output));
    return output;
}
void contains(const std::string &text, const char *expected) {
    TEST_ASSERT_NOT_EQUAL_MESSAGE(std::string::npos, text.find(expected), expected);
}
}

void test_fdr_json_orders_fragments_and_scales_consumption() {
    auto data = sample();
    data.configuration.turn_factor = 1;
    put(data.consumptions + 84, 12, 4); // newest, end of first fragment
    put(data.consumptions + 80, 23, 4);
    put(data.consumptions, 34, 4);      // 22nd
    put(data.consumptions + 176, 45, 4); // 23rd, end of second fragment
    put(data.consumptions + 88, 56, 4); // oldest
    const auto out = json(data);
    contains(out, "\"order\":\"newest_first\"");
    contains(out, "\"interval_count\":45");
    contains(out, "\"consumptions\":[120,230,");
    contains(out, ",340,450,");
    contains(out, ",560]");
    contains(out, "\"global_index\":12340"); // quantize anchor, do not multiply it
    contains(out, "\"interval_end\":null");
}

void test_fdr_json_widths_signs_sentinels_and_zero_validity() {
    const unsigned widths[] = {4,2,2,1,1};
    for (unsigned resolution = 0; resolution < 5; ++resolution) {
        auto data = sample();
        data.configuration.resolution = resolution;
        const unsigned width = widths[resolution];
        put(data.consumptions + 88 - width, 9, width);
        put(data.consumptions + 88 - width * 2, resolution == 1 ? 65534 : resolution == 3 ? 254 : uint32_t(-7), width);
        put(data.consumptions + 88 - width * 3, resolution == 0 ? uint32_t(-2147483647 - 1) : resolution == 1 ? 65535 : resolution == 2 ? 32768 : resolution == 3 ? 255 : 128, width);
        put(data.consumptions, 21, width); // last record of first fragment
        put(data.consumptions + 180 - width, 22, width); // first record of second fragment
        put(data.consumptions + 88, 23, width); // oldest record
        const auto out = json(data);
        contains(out, resolution == 1 || resolution == 3 ? "\"consumptions\":[9,null,null,0," : resolution == 0 ? "\"consumptions\":[9,-7,-2147483648,0," : "\"consumptions\":[9,-7,null,0,");
        contains(out, resolution == 1 || resolution == 3 ? "\"validity\":[\"valid\",\"negative\",\"overflow\",\"unknown\"" : resolution == 0 ? "\"validity\":[\"valid\",\"valid\",\"valid\",\"unknown\"" : "\"validity\":[\"valid\",\"valid\",\"overflow\",\"unknown\"");
        contains(out, resolution == 0 ? "\"interval_count\":45" : width == 2 ? "\"interval_count\":90" : "\"interval_count\":180");
        contains(out, ",21,22,");
        contains(out, ",23]");
    }
}

void test_fdr_json_rejects_unsupported_and_truncated_output() {
    auto data = sample();
    char output[16384];
    for (unsigned code : {5u, 15u}) {
        data.configuration.resolution = code;
        TEST_ASSERT_EQUAL_INT(0, MeterHistory::generateFullFdrJson(data, output, sizeof(output)));
        TEST_ASSERT_EQUAL_STRING("", output);
    }
    data = sample(); data.communication_status[1] = 1;
    TEST_ASSERT_EQUAL_INT(0, MeterHistory::generateFullFdrJson(data, output, sizeof(output)));
    data = sample(); data.configuration.turn_factor = 5;
    TEST_ASSERT_EQUAL_INT(0, MeterHistory::generateFullFdrJson(data, output, sizeof(output)));
    data = sample(); data.configuration.start_hour = 24;
    TEST_ASSERT_EQUAL_INT(0, MeterHistory::generateFullFdrJson(data, output, sizeof(output)));
    for (unsigned field = 0; field < 5; ++field) {
        data = sample();
        if (field == 0) data.configuration.period = 4;
        if (field == 1) data.configuration.start_day = 32;
        if (field == 2) { data.configuration.period = 1; data.configuration.start_day = 8; }
        if (field == 3) data.pulse_medium[0] = 0;
        if (field == 4) data.pulse_medium[1] = 8;
        TEST_ASSERT_EQUAL_INT(0, MeterHistory::generateFullFdrJson(data, output, sizeof(output)));
        TEST_ASSERT_EQUAL_STRING("", output);
    }
    data = sample();
    const auto valid = json(data);
    TEST_ASSERT_EQUAL_INT(0, MeterHistory::generateFullFdrJson(data, output, valid.size()));
    TEST_ASSERT_EQUAL_STRING("", output);
    TEST_ASSERT_EQUAL_INT(valid.size(), MeterHistory::generateFullFdrJson(data, output, valid.size() + 1));
    TEST_ASSERT_EQUAL_INT(0, MeterHistory::generateFullFdrJson(data, nullptr, 100));
}

void test_fdr_meter_clock_rollover_and_monthly_dates() {
    tm clock{}; clock.tm_year = 124; clock.tm_mon = 1; clock.tm_mday = 29;
    clock.tm_hour = 23; clock.tm_min = 59; clock.tm_sec = 59;
    tm advanced{};
    TEST_ASSERT_TRUE(MeterHistory::advanceMeterTime(clock, 1, advanced));
    TEST_ASSERT_EQUAL_INT(2, advanced.tm_mon);
    TEST_ASSERT_EQUAL_INT(1, advanced.tm_mday);
    TEST_ASSERT_EQUAL_INT(0, advanced.tm_hour);
    auto data = sample();
    const auto out = json(data, &advanced);
    contains(out, "\"timestamp_basis\":\"meter_clock\"");
    contains(out, "\"interval_end\":[\"2024-03-01T00:00:00\",\"2024-02-01T00:00:00\",\"2024-01-01T00:00:00\"");
    clock.tm_year = 123; // Not a leap year
    TEST_ASSERT_FALSE(MeterHistory::advanceMeterTime(clock, 0, advanced));
}

void test_fdr_json_calendar_boundaries_validity_and_fractional_units() {
    auto data = sample();
    tm clock{}; clock.tm_year = 125; clock.tm_mon = 2; clock.tm_mday = 5;
    clock.tm_hour = 6; clock.tm_min = 30; clock.tm_sec = 1;
    data.configuration.period = 2; data.configuration.start_hour = 6;
    contains(json(data, &clock), "\"interval_end\":[\"2025-03-05T06:00:00\""); // Boundary remains in the current day after the start hour.
    data.configuration.period = 1; data.configuration.start_day = 1;
    contains(json(data, &clock), "\"interval_end\":[\"2025-03-03T06:00:00\""); // Wednesday remains in the week starting Monday.
    clock.tm_sec = 0;
    contains(json(data, &clock), "\"interval_end\":[\"2025-03-03T06:00:00\"");
    data.configuration.period = 3;
    contains(json(data, &clock), "\"interval_end\":[\"2025-03-05T06:00:00\",\"2025-03-05T05:00:00\"");
    data.configuration.period = 0; data.configuration.start_day = 31;
    contains(json(data, &clock), "\"interval_end\":[\"2025-02-28T06:00:00\",\"2025-01-31T06:00:00\",\"2024-12-31T06:00:00\"");
    for (unsigned day : {28u, 29u, 30u, 31u}) {
        data.configuration.start_day = day;
        tm february = clock; february.tm_mon = 1; february.tm_mday = 28;
        char expected[180];
        snprintf(expected, sizeof(expected), "\"interval_end\":[\"2025-02-28T06:00:00\",\"2025-01-%02uT06:00:00\",\"2024-12-%02uT06:00:00\"", day, day);
        contains(json(data, &february), expected);
        february.tm_mday = 27;
        snprintf(expected, sizeof(expected), "\"interval_end\":[\"2025-01-%02uT06:00:00\",\"2024-12-%02uT06:00:00\"", day, day);
        contains(json(data, &february), expected);
        february.tm_year = 124; february.tm_mday = 29;
        snprintf(expected, sizeof(expected), "\"interval_end\":[\"2024-02-%02uT06:00:00\",\"2024-01-%02uT06:00:00\",\"2023-12-%02uT06:00:00\"", std::min(day, 29u), day, day);
        contains(json(data, &february), expected);
    }
    data = sample();
    data.leakage_history[13] = 0x80; // most recent completed month is valid zero
    data.leakage_history[12] = 0; // previous month's FDR was not done
    const auto out = json(data, &clock);
    contains(out, "\"consumptions\":[0,null,");
    contains(out, "\"validity\":[\"valid\",\"not_done\"");
    data = sample(); data.pulse_medium[1] = 0; // 0.0001 L/pulse
    put(data.consumptions + 84, uint32_t(-7), 4);
    contains(json(data), "\"consumptions\":[-0.0007,0,");
    data.pulse_medium[1] = 11; // alternate code for same pulse weight
    contains(json(data), "\"consumptions\":[-0.0007,0,");
    data.pulse_medium[1] = 7; data.configuration.turn_factor = 4;
    put(data.consumptions + 84, 2147483647, 4);
    contains(json(data), "\"consumptions\":[21474836470000000,");
    // Largest record count with dates remains within publisher's heap buffer.
    data = sample(); data.configuration.resolution = 3;
    memset(data.consumptions, 253, sizeof(data.consumptions));
    contains(json(data, &clock), "\"interval_count\":180");

    // All-zero daily archive: current month unknown, alternating completed
    // months valid/not_done. Compare every slot across six month boundaries.
    data = sample(); data.configuration.resolution = 3; data.configuration.period = 2;
    data.leakage_history[0] = data.leakage_history[13] = data.leakage_history[11] = data.leakage_history[9] = 0x80;
    std::string expected = "{\"period\":\"daily\",\"resolution\":3,\"start_day\":1,\"start_hour\":0,"
        "\"turn_factor\":1,\"pulse_value_code\":4,\"unit\":\"L\",\"order\":\"newest_first\","
        "\"interval_count\":180,\"timestamp_basis\":\"meter_clock\",\"global_index\":12345,"
        "\"current_index\":12456,\"consumptions\":[";
    std::string validity, dates;
    for (unsigned i = 0; i < 180; ++i) {
        // 4 current-month days, then February 28, January 31, December 31,
        // November 30, October 31, and the final 25 days in September.
        const bool unknown = i < 4;
        const bool valid = (i >= 4 && i < 32) || (i >= 63 && i < 94) || (i >= 124 && i < 155);
        if (i) { expected += ','; validity += ','; dates += ','; }
        expected += unknown || valid ? "0" : "null";
        validity += unknown ? "\"unknown\"" : valid ? "\"valid\"" : "\"not_done\"";
        const time_t utc = 1741132800 - time_t(i) * 86400; // 2025-03-05 00:00 UTC
        const tm *date = gmtime(&utc);
        TEST_ASSERT_NOT_NULL(date);
        char text[24];
        TEST_ASSERT_NOT_EQUAL(0, strftime(text, sizeof(text), "\"%Y-%m-%dT%H:%M:%S\"", date));
        dates += text;
    }
    expected += "],\"validity\":[" + validity + "],\"interval_end\":[" + dates + "],\"captured_at\":null}";
    char output[FULL_FDR_JSON_BUFFER_SIZE];
    TEST_ASSERT_GREATER_THAN_INT(0, MeterHistory::generateFullFdrJson(data, output, sizeof(output), &clock));
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), output);
}

void test_fdr_strict_clock_capture_rollovers_and_capture_time() {
    tm clock{};
    for (const char *bad : {"", "2025-02-29 00:00:00", "2024-02-29 24:00:00", "2024-02-29 12:60:00", "2024-02-29 12:00:60", "2024-2-29 00:00:00", "2024-02-29T00:00:00", "2024-02-29 00:00:00Z", "1999-12-31 00:00:00", "2100-01-01 00:00:00", "202a-01-01 00:00:00"})
        TEST_ASSERT_FALSE_MESSAGE(MeterHistory::parseMeterTime(bad, clock), bad);
    TEST_ASSERT_FALSE(MeterHistory::parseMeterTime(nullptr, clock));
    TEST_ASSERT_TRUE(MeterHistory::parseMeterTime("2024-02-29 23:59:59", clock));
    auto data = sample();
    TEST_ASSERT_TRUE(MeterHistory::captureWithinFdrInterval(data, clock, 999));
    TEST_ASSERT_FALSE(MeterHistory::captureWithinFdrInterval(data, clock, 1000));
    TEST_ASSERT_TRUE(MeterHistory::parseMeterTime("2025-12-31 23:59:59", clock));
    TEST_ASSERT_FALSE(MeterHistory::captureWithinFdrInterval(data, clock, 1000));
    for (unsigned period : {1u, 2u, 3u}) {
        data.configuration.period = period;
        data.configuration.start_hour = 6;
        TEST_ASSERT_TRUE(MeterHistory::parseMeterTime("2025-03-03 05:59:59", clock)); // Monday
        TEST_ASSERT_FALSE(MeterHistory::captureWithinFdrInterval(data, clock, 1000));
        TEST_ASSERT_TRUE(MeterHistory::parseMeterTime("2025-03-03 06:00:00", clock));
        TEST_ASSERT_TRUE(MeterHistory::captureWithinFdrInterval(data, clock, 1000));
        contains(json(data, &clock), "\"interval_end\":[\"2025-03-03T06:00:00\"");
        contains(json(data, &clock), "\"validity\":[\"unknown\""); // Current month leakage semantics unverified.
    }
    char output[FULL_FDR_JSON_BUFFER_SIZE];
    TEST_ASSERT_GREATER_THAN_INT(0, MeterHistory::generateFullFdrJson(data, output, sizeof(output), &clock, 1740988800));
    contains(output, "\"captured_at\":\"2025-03-03T08:00:00Z\"");
    contains(json(data), "\"captured_at\":null");
    for (time_t unavailable : {time_t(-1), time_t(0), time_t(946684799), time_t(4102444800LL)}) {
        TEST_ASSERT_GREATER_THAN_INT(0, MeterHistory::generateFullFdrJson(data, output, sizeof(output), &clock, unavailable));
        contains(output, "\"captured_at\":null");
    }
    TEST_ASSERT_GREATER_THAN_INT(0, MeterHistory::generateFullFdrJson(data, output, sizeof(output), &clock, 946684800));
    contains(output, "\"captured_at\":\"2000-01-01T00:00:00Z\"");
    if (sizeof(time_t) > 4) {
        TEST_ASSERT_GREATER_THAN_INT(0, MeterHistory::generateFullFdrJson(data, output, sizeof(output), &clock, std::numeric_limits<time_t>::max()));
        contains(output, "\"captured_at\":null");
    }
    TEST_ASSERT_TRUE(MeterHistory::parseMeterTime("2000-02-29 23:59:59", clock));
    tm advanced{};
    TEST_ASSERT_TRUE(MeterHistory::advanceMeterTime(clock, 1, advanced));
    TEST_ASSERT_EQUAL_INT(2, advanced.tm_mon);
    TEST_ASSERT_EQUAL_INT(1, advanced.tm_mday);
    TEST_ASSERT_TRUE(MeterHistory::parseMeterTime("2099-12-31 23:59:59", clock));
    TEST_ASSERT_FALSE(MeterHistory::advanceMeterTime(clock, 1, advanced));
    clock.tm_year = 200;
    contains(json(data, &clock), "\"timestamp_basis\":\"unavailable\"");
    contains(json(data, &clock), "\"interval_end\":null");
}

void test_fdr_maximum_payload_bound_and_escaped_envelopes() {
    tm clock{};
    TEST_ASSERT_TRUE(MeterHistory::parseMeterTime("2099-12-31 23:59:59", clock));
    const unsigned widths[] = {4, 2, 2, 1, 1};
    const uint32_t cases[][7] = {
        {0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 99999999, 0x80000001},
        {0, 1, 65533, 65534, 65535, 9999, 32768},
        {0, 1, 32767, 32768, 32769, 65535, 55536},
        {0, 1, 253, 254, 255, 99, 128},
        {0, 1, 127, 128, 129, 255, 156},
    };
    size_t maximum = 0, escapedMaximum = 0;
    char output[FULL_FDR_JSON_BUFFER_SIZE];
    for (unsigned resolution = 0; resolution <= 4; ++resolution)
        for (unsigned pulse = 0; pulse <= 18; ++pulse) {
            if (pulse > 7 && pulse < 11) continue;
            for (unsigned factor = 0; factor <= 4; ++factor)
                for (uint32_t raw : cases[resolution]) {
                    auto data = sample();
                    data.configuration = {{}, 23, 31, 0, uint8_t(factor), uint8_t(resolution)};
                    data.pulse_medium[1] = pulse;
                    data.current_index = data.global_index = UINT32_MAX;
                    for (unsigned i = 0; i < 180; i += widths[resolution])
                        put(data.consumptions + i, raw, widths[resolution]);
                    const int size = MeterHistory::generateFullFdrJson(data, output, sizeof(output), &clock, 4102444799LL);
                    TEST_ASSERT_GREATER_THAN_INT(0, size);
                    TEST_ASSERT_EQUAL_INT(size, strlen(output));
                    size_t escaped = size;
                    for (const char *p = output; *p; ++p) if (*p == '"' || *p == '\\') ++escaped;
                    maximum = std::max(maximum, size_t(size));
                    escapedMaximum = std::max(escapedMaximum, escaped);
                }
        }
    TEST_ASSERT_LESS_THAN_size_t(FULL_FDR_JSON_BUFFER_SIZE, maximum);
    TEST_ASSERT_EQUAL_INT(0, MeterHistory::generateFullFdrJson(sample(), output, 1));
    TEST_ASSERT_EQUAL_STRING("", output);
    // Exact bound regression: update only alongside a reviewed contract change.
    TEST_ASSERT_EQUAL_size_t(7898, maximum);
    printf("FDR maximum raw=%zu, getter JSON envelope=%zu, event data envelope=%zu, duplicated web state/value=%zu bytes\n",
           maximum, escapedMaximum + strlen("{\"json\":\"\"}"),
           escapedMaximum + strlen("{\"archive\":\"\"}"), 2 * escapedMaximum + strlen("{\"state\":\"\",\"value\":\"\"}"));
}
