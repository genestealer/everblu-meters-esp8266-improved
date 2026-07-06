#include <unity.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "core/radian_parser.h"
#include "core/radian_decoder.h"

struct Fixture
{
    std::string name;
    std::vector<uint8_t> decoded;
    uint32_t expected_volume;
    uint32_t expected_battery;
    uint32_t expected_counter;
    uint32_t expected_time_start;
    uint32_t expected_time_end;
    bool expected_history_available;
    bool expected_crc_valid;
};

static std::string trim(const std::string &s)
{
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
    {
        start++;
    }

    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
    {
        end--;
    }

    return s.substr(start, end - start);
}

static std::vector<std::string> split(const std::string &s, char delim)
{
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, delim))
    {
        out.push_back(part);
    }
    return out;
}

static bool parse_hex_bytes(const std::string &hex_string, std::vector<uint8_t> &out)
{
    out.clear();
    std::stringstream ss(hex_string);
    std::string tok;

    while (ss >> tok)
    {
        if (tok.size() != 2)
        {
            return false;
        }
        char *end = nullptr;
        long value = std::strtol(tok.c_str(), &end, 16);
        if (!(end && *end == '\0' && value >= 0 && value <= 255))
        {
            return false;
        }
        out.push_back(static_cast<uint8_t>(value));
    }

    return !out.empty();
}

static bool parse_u32_field(const std::string &input, uint32_t &out)
{
    const std::string cleaned = trim(input);
    if (cleaned.empty())
    {
        return false;
    }

    errno = 0;
    char *end = nullptr;
    unsigned long value = std::strtoul(cleaned.c_str(), &end, 10);
    if (errno != 0 || end == nullptr || *end != '\0' ||
        value > static_cast<unsigned long>(std::numeric_limits<uint32_t>::max()))
    {
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

static std::ifstream open_fixture_list()
{
    const char *candidates[] = {
        "test/fixtures/meter_frames/fixtures.lst",
        "../test/fixtures/meter_frames/fixtures.lst",
        "../../test/fixtures/meter_frames/fixtures.lst",
        "../../../test/fixtures/meter_frames/fixtures.lst",
    };

    for (const char *path : candidates)
    {
        std::ifstream file(path);
        if (file.good())
        {
            return file;
        }
    }

    return std::ifstream();
}

struct FixtureLoadResult
{
    std::vector<Fixture> fixtures;
    bool fixture_file_found;
    size_t data_line_count;
    size_t parse_error_count;
};

static FixtureLoadResult load_fixtures()
{
    FixtureLoadResult result{};
    std::ifstream in = open_fixture_list();
    if (!in.good())
    {
        result.fixture_file_found = false;
        return result;
    }
    result.fixture_file_found = true;

    std::string line;
    size_t line_number = 0;
    while (std::getline(in, line))
    {
        line_number++;
        line = trim(line);
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        result.data_line_count++;

        std::vector<std::string> parts = split(line, '|');
        if (parts.size() != 9)
        {
            result.parse_error_count++;
            continue;
        }

        Fixture fx;
        fx.name = trim(parts[0]);
        uint32_t history_u32 = 0;
        uint32_t crc_u32 = 0;
        bool ok = true;
        ok = ok && parse_hex_bytes(parts[1], fx.decoded);
        ok = ok && parse_u32_field(parts[2], fx.expected_volume);
        ok = ok && parse_u32_field(parts[3], fx.expected_battery);
        ok = ok && parse_u32_field(parts[4], fx.expected_counter);
        ok = ok && parse_u32_field(parts[5], fx.expected_time_start);
        ok = ok && parse_u32_field(parts[6], fx.expected_time_end);
        ok = ok && parse_u32_field(parts[7], history_u32);
        ok = ok && parse_u32_field(parts[8], crc_u32);

        if (!ok)
        {
            (void)line_number;
            result.parse_error_count++;
            continue;
        }

        fx.expected_history_available = history_u32 != 0;
        fx.expected_crc_valid = crc_u32 != 0;
        result.fixtures.push_back(fx);
    }

    return result;
}

void setUp(void) {}

void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers for inline validation tests (no fixture file required)
// ---------------------------------------------------------------------------

static void make_test_buf(uint8_t *buf, size_t size, uint32_t volume,
                          uint8_t time_start, uint8_t time_end)
{
    memset(buf, 0, size);
    buf[18] = volume & 0xFFU;
    buf[19] = (volume >> 8) & 0xFFU;
    buf[20] = (volume >> 16) & 0xFFU;
    buf[21] = (volume >> 24) & 0xFFU;
    buf[31] = 10; // battery_left: valid (not 0xFF)
    buf[44] = time_start;
    buf[45] = time_end;
    buf[48] = 5; // reads_counter: valid (not 0xFF)
}

void test_radian_parse_primary_volume_rejection(void)
{
    uint8_t buf[120];
    struct radian_primary_data out;

    // volume == 0 must fail
    make_test_buf(buf, sizeof(buf), 0UL, 6, 18);
    TEST_ASSERT_FALSE(radian_parse_primary_data(buf, sizeof(buf), &out));

    // volume == 0xFFFFFFFF must fail
    make_test_buf(buf, sizeof(buf), 0xFFFFFFFFUL, 6, 18);
    TEST_ASSERT_FALSE(radian_parse_primary_data(buf, sizeof(buf), &out));

    // volume > 1,000,000,000 must fail (physically impossible)
    make_test_buf(buf, sizeof(buf), 1000000001UL, 6, 18);
    TEST_ASSERT_FALSE(radian_parse_primary_data(buf, sizeof(buf), &out));

    // volume exactly at the 1B limit must pass
    make_test_buf(buf, sizeof(buf), 1000000000UL, 6, 18);
    TEST_ASSERT_TRUE(radian_parse_primary_data(buf, sizeof(buf), &out));
    TEST_ASSERT_EQUAL_UINT32(1000000000UL, out.volume);

    // normal volume must pass
    make_test_buf(buf, sizeof(buf), 774431UL, 6, 18);
    TEST_ASSERT_TRUE(radian_parse_primary_data(buf, sizeof(buf), &out));
    TEST_ASSERT_EQUAL_UINT32(774431UL, out.volume);
}

void test_radian_parse_primary_time_rejection(void)
{
    uint8_t buf[120];
    struct radian_primary_data out;

    // time_start out of range: frame must be rejected
    make_test_buf(buf, sizeof(buf), 774431UL, 128, 6);
    TEST_ASSERT_FALSE(radian_parse_primary_data(buf, sizeof(buf), &out));

    // time_end out of range: frame must be rejected
    make_test_buf(buf, sizeof(buf), 774431UL, 6, 255);
    TEST_ASSERT_FALSE(radian_parse_primary_data(buf, sizeof(buf), &out));

    // both out of range: frame must be rejected
    make_test_buf(buf, sizeof(buf), 774431UL, 200, 200);
    TEST_ASSERT_FALSE(radian_parse_primary_data(buf, sizeof(buf), &out));

    // valid time values: frame accepted and values passed through unchanged
    make_test_buf(buf, sizeof(buf), 774431UL, 6, 18);
    TEST_ASSERT_TRUE(radian_parse_primary_data(buf, sizeof(buf), &out));
    TEST_ASSERT_EQUAL_UINT32(6, out.time_start);
    TEST_ASSERT_EQUAL_UINT32(18, out.time_end);
}

void test_replay_meter_fixtures(void)
{
    FixtureLoadResult loaded = load_fixtures();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0,
        static_cast<uint32_t>(loaded.parse_error_count),
        "Malformed fixture line(s) found in fixtures.lst");

    std::vector<Fixture> fixtures = loaded.fixtures;

    if (!loaded.fixture_file_found || fixtures.empty())
    {
        TEST_PASS_MESSAGE("No meter fixtures present yet. Capture frames and append fixtures.lst.");
        return;
    }

    for (const Fixture &fx : fixtures)
    {
        TEST_ASSERT_TRUE_MESSAGE(!fx.decoded.empty(), fx.name.c_str());

        bool crc_ok = radian_validate_crc(fx.decoded.data(), fx.decoded.size());
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            fx.expected_crc_valid ? 1 : 0,
            crc_ok ? 1 : 0,
            fx.name.c_str());

        if (!crc_ok)
        {
            continue;
        }

        struct radian_primary_data parsed;
        bool parsed_ok = radian_parse_primary_data(fx.decoded.data(), fx.decoded.size(), &parsed);
        TEST_ASSERT_TRUE_MESSAGE(parsed_ok, fx.name.c_str());

        TEST_ASSERT_EQUAL_UINT32_MESSAGE(fx.expected_volume, parsed.volume, fx.name.c_str());
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(fx.expected_battery, parsed.battery_left, fx.name.c_str());
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(fx.expected_counter, parsed.reads_counter, fx.name.c_str());
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(fx.expected_time_start, parsed.time_start, fx.name.c_str());
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(fx.expected_time_end, parsed.time_end, fx.name.c_str());
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            fx.expected_history_available ? 1 : 0,
            parsed.history_available ? 1 : 0,
            fx.name.c_str());
    }
}

// ---------------------------------------------------------------------------
// 4-bit-per-bit decoder coverage (issue #118)
//
// radian_decode_4bitpbit() is the single, shared implementation of the RADIAN
// 4x-oversampled bit-recovery algorithm; the firmware wrapper
// decode_4bitpbit_serial() in cc1101.cpp now delegates to it. This encoder is
// an *independent* implementation of the on-air framing, so a clean round-trip
// (encode -> decode == original) validates the shared decode path against a
// known-good oversampled bitstream.
//
// On-air framing per byte: 8 data bits (LSB first) + 3 stop bits (1), with a
// single start/separator bit (0) between bytes. Each logical bit is
// transmitted as four identical samples; samples are packed MSB-first into the
// RX buffer bytes exactly as the CC1101 delivers them.
// ---------------------------------------------------------------------------
static void encode_4x_oversampled(const std::vector<uint8_t> &msg,
                                  std::vector<uint8_t> &out)
{
    std::vector<uint8_t> bits; // logical bits in transmission order

    for (uint8_t value : msg)
    {
        for (int i = 0; i < 8; i++) // 8 data bits, LSB first
        {
            bits.push_back(static_cast<uint8_t>((value >> i) & 1U));
        }
        bits.push_back(1); // 3 stop bits
        bits.push_back(1);
        bits.push_back(1);
        bits.push_back(0); // start/separator bit terminating this byte
    }

    // Trailing high run so the decoder flushes (and therefore counts) the final
    // separator bit; the decoder only emits a run when the polarity changes.
    for (int i = 0; i < 8; i++)
    {
        bits.push_back(1);
    }

    // Each logical bit -> four identical samples.
    std::vector<uint8_t> samples;
    samples.reserve(bits.size() * 4);
    for (uint8_t b : bits)
    {
        for (int s = 0; s < 4; s++)
        {
            samples.push_back(b);
        }
    }

    // Pad to a byte boundary with 1s (harmlessly extends the trailing run).
    while (samples.size() % 8 != 0)
    {
        samples.push_back(1);
    }

    // Pack MSB-first, matching how the CC1101 FIFO bytes are consumed.
    out.clear();
    out.reserve(samples.size() / 8);
    for (size_t i = 0; i < samples.size(); i += 8)
    {
        uint8_t v = 0;
        for (int b = 0; b < 8; b++)
        {
            v = static_cast<uint8_t>((v << 1) | (samples[i + b] & 1U));
        }
        out.push_back(v);
    }
}

static void assert_decode_roundtrip(const std::vector<uint8_t> &message)
{
    std::vector<uint8_t> oversampled;
    encode_4x_oversampled(message, oversampled);

    uint8_t decoded[256];
    uint8_t count = radian_decode_4bitpbit(
        oversampled.data(), static_cast<int>(oversampled.size()),
        decoded, static_cast<int>(sizeof(decoded)));

    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(message.size()),
                             static_cast<uint32_t>(count));
    for (size_t i = 0; i < message.size(); i++)
    {
        TEST_ASSERT_EQUAL_HEX8(message[i], decoded[i]);
    }
}

void test_radian_decode_roundtrip(void)
{
    // Bit patterns chosen to exercise all-zero, all-one, alternating and
    // sequential bytes through the run-length recovery.
    assert_decode_roundtrip({0xA5});
    assert_decode_roundtrip({0x00, 0xFF, 0xA5, 0x5A, 0x01, 0x80, 0x7F, 0xFE});

    std::vector<uint8_t> sequential;
    for (int i = 0; i < 32; i++)
    {
        sequential.push_back(static_cast<uint8_t>(i * 7 + 1));
    }
    assert_decode_roundtrip(sequential);
}

void test_radian_decode_rejects_empty_and_null(void)
{
    uint8_t decoded[16];
    uint8_t sample = 0xF0;

    TEST_ASSERT_EQUAL_UINT32(0, radian_decode_4bitpbit(nullptr, 4, decoded, sizeof(decoded)));
    TEST_ASSERT_EQUAL_UINT32(0, radian_decode_4bitpbit(&sample, 0, decoded, sizeof(decoded)));
    TEST_ASSERT_EQUAL_UINT32(0, radian_decode_4bitpbit(&sample, 4, nullptr, sizeof(decoded)));
    TEST_ASSERT_EQUAL_UINT32(0, radian_decode_4bitpbit(&sample, 4, decoded, 0));
}

// ---------------------------------------------------------------------------
// Reading-vs-history plausibility guard
//
// radian_reading_within_history_bounds() rejects a reading whose implied
// current-month usage exceeds 100x the largest historical monthly usage. The
// check is skipped (accepts) when history is insufficient to judge.
// ---------------------------------------------------------------------------
void test_radian_reading_within_history_bounds(void)
{
    // Steady ~1000 L/month history; largest monthly usage = 1000 L.
    const uint32_t history[] = {10000, 11000, 12000, 13000, 14000};
    const int months = 5;

    // Normal current-month usage (~1000 L) is well within bounds -> accept.
    TEST_ASSERT_TRUE(radian_reading_within_history_bounds(15000, history, months, 100UL));

    // Exactly 100x the largest monthly usage (100000 L) is the boundary and
    // must still be accepted (only a strictly greater jump is rejected).
    TEST_ASSERT_TRUE(radian_reading_within_history_bounds(14000 + 100000, history, months, 100UL));

    // One litre beyond 100x is an implausible spike -> reject.
    TEST_ASSERT_FALSE(radian_reading_within_history_bounds(14000 + 100001, history, months, 100UL));

    // A grossly corrupted reading -> reject.
    TEST_ASSERT_FALSE(radian_reading_within_history_bounds(500000000UL, history, months, 100UL));
}

void test_radian_reading_within_history_bounds_skips_when_insufficient(void)
{
    const uint32_t history[] = {10000, 11000, 12000};

    // NULL history -> accept (skip).
    TEST_ASSERT_TRUE(radian_reading_within_history_bounds(999999999UL, nullptr, 3, 100UL));

    // Fewer than 2 months -> accept (skip).
    TEST_ASSERT_TRUE(radian_reading_within_history_bounds(999999999UL, history, 1, 100UL));

    // spike_factor 0 -> accept (skip).
    TEST_ASSERT_TRUE(radian_reading_within_history_bounds(999999999UL, history, 3, 0UL));

    // Flat history (largest monthly usage == 0) -> accept (skip), no baseline.
    const uint32_t flat[] = {5000, 5000, 5000};
    TEST_ASSERT_TRUE(radian_reading_within_history_bounds(999999999UL, flat, 3, 100UL));

    // Current volume predates the newest snapshot -> accept (skip).
    TEST_ASSERT_TRUE(radian_reading_within_history_bounds(11500, history, 3, 100UL));
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_radian_parse_primary_volume_rejection);
    RUN_TEST(test_radian_parse_primary_time_rejection);
    RUN_TEST(test_radian_decode_roundtrip);
    RUN_TEST(test_radian_decode_rejects_empty_and_null);
    RUN_TEST(test_radian_reading_within_history_bounds);
    RUN_TEST(test_radian_reading_within_history_bounds_skips_when_insufficient);
    RUN_TEST(test_replay_meter_fixtures);
    return UNITY_END();
}
