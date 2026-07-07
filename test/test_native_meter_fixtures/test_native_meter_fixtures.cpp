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

void test_radian_parse_primary_data_edge_cases(void)
{
    uint8_t buf[120];
    struct radian_primary_data out;

    // out == NULL must fail regardless of buffer contents.
    make_test_buf(buf, sizeof(buf), 774431UL, 6, 18);
    TEST_ASSERT_FALSE(radian_parse_primary_data(buf, sizeof(buf), nullptr));

    // decoded_buffer == NULL must fail.
    TEST_ASSERT_FALSE(radian_parse_primary_data(nullptr, sizeof(buf), &out));

    // size < 30 must fail (not enough bytes for the volume field).
    make_test_buf(buf, sizeof(buf), 774431UL, 6, 18);
    TEST_ASSERT_FALSE(radian_parse_primary_data(buf, 20, &out));

    // battery_left == 0xFF must be rejected (invalid sentinel).
    make_test_buf(buf, sizeof(buf), 774431UL, 6, 18);
    buf[31] = 0xFF;
    TEST_ASSERT_FALSE(radian_parse_primary_data(buf, sizeof(buf), &out));

    // reads_counter == 0xFF must be rejected (invalid sentinel).
    make_test_buf(buf, sizeof(buf), 774431UL, 6, 18);
    buf[48] = 0xFF;
    TEST_ASSERT_FALSE(radian_parse_primary_data(buf, sizeof(buf), &out));

    // 30 <= size < 49: volume is parsed but the extended fields (battery,
    // counter, wake window) and history are skipped and remain zero.
    make_test_buf(buf, sizeof(buf), 774431UL, 6, 18);
    TEST_ASSERT_TRUE(radian_parse_primary_data(buf, 40, &out));
    TEST_ASSERT_EQUAL_UINT32(774431UL, out.volume);
    TEST_ASSERT_EQUAL_UINT32(0, out.battery_left);
    TEST_ASSERT_EQUAL_UINT32(0, out.reads_counter);
    TEST_ASSERT_EQUAL_UINT32(0, out.time_start);
    TEST_ASSERT_EQUAL_UINT32(0, out.time_end);
    TEST_ASSERT_FALSE(out.history_available);

    // size >= 118 sets history_available.
    make_test_buf(buf, sizeof(buf), 774431UL, 6, 18);
    TEST_ASSERT_TRUE(radian_parse_primary_data(buf, sizeof(buf), &out));
    TEST_ASSERT_TRUE(out.history_available);
}

// ---------------------------------------------------------------------------
// CRC validation coverage (radian_validate_crc)
//
// Exercised end-to-end elsewhere only through captured fixtures, which may be
// absent in CI. These direct tests keep the function covered unconditionally.
// ---------------------------------------------------------------------------
void test_radian_validate_crc(void)
{
    // Build a well-formed frame: buf[0] = length, buf[1..len-3] = payload,
    // buf[len-2..len-1] = CRC-16/KERMIT (big-endian) over the payload.
    uint8_t buf[8] = {8, 0x11, 0x22, 0x33, 0x44, 0x55, 0, 0};
    const uint16_t crc = radian_crc_kermit(&buf[1], 5);
    buf[6] = static_cast<uint8_t>(crc >> 8);
    buf[7] = static_cast<uint8_t>(crc & 0xFF);
    TEST_ASSERT_TRUE(radian_validate_crc(buf, sizeof(buf)));

    // A corrupted payload byte must fail the CRC check.
    uint8_t bad[8];
    memcpy(bad, buf, sizeof(buf));
    bad[3] ^= 0xFF;
    TEST_ASSERT_FALSE(radian_validate_crc(bad, sizeof(bad)));

    // NULL buffer and undersized buffers are rejected.
    TEST_ASSERT_FALSE(radian_validate_crc(nullptr, sizeof(buf)));
    uint8_t tiny[3] = {3, 0, 0};
    TEST_ASSERT_FALSE(radian_validate_crc(tiny, sizeof(tiny)));

    // length_field advertising fewer than 4 bytes is rejected.
    uint8_t short_len[8] = {3, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(radian_validate_crc(short_len, sizeof(short_len)));

    // length_field larger than the buffer is accepted (compat shim).
    uint8_t long_len[8] = {200, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_TRUE(radian_validate_crc(long_len, sizeof(long_len)));

    // length_field == 0 falls back to the actual buffer size.
    uint8_t implicit[8] = {0, 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0, 0};
    const uint16_t crc2 = radian_crc_kermit(&implicit[1], sizeof(implicit) - 3);
    implicit[6] = static_cast<uint8_t>(crc2 >> 8);
    implicit[7] = static_cast<uint8_t>(crc2 & 0xFF);
    TEST_ASSERT_TRUE(radian_validate_crc(implicit, sizeof(implicit)));
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

    // The framing yields 12 logical bits per byte plus an 8-bit trailing run,
    // so the sample count (4 * (12*N + 8)) is always a multiple of 8 - no
    // padding is required before packing.

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
    // A full 4-byte buffer so rx_len=4 never implies an out-of-bounds read,
    // even if a future change reordered the guard checks.
    uint8_t sample[4] = {0xF0, 0xF0, 0xF0, 0xF0};

    TEST_ASSERT_EQUAL_UINT32(0, radian_decode_4bitpbit(nullptr, 4, decoded, sizeof(decoded)));
    TEST_ASSERT_EQUAL_UINT32(0, radian_decode_4bitpbit(sample, 0, decoded, sizeof(decoded)));
    TEST_ASSERT_EQUAL_UINT32(0, radian_decode_4bitpbit(sample, 4, nullptr, sizeof(decoded)));
    TEST_ASSERT_EQUAL_UINT32(0, radian_decode_4bitpbit(sample, 4, decoded, 0));
}

// ---------------------------------------------------------------------------
// Sample-level helpers: expose the oversampled bitstream so tests can inject
// glitches / corruption that the byte-level encode helper cannot express.
// Framing matches encode_4x_oversampled(): per byte, 8 data bits (LSB first)
// + 3 stop bits (1) + 1 separator (0), each logical bit repeated 4x, plus a
// trailing 8-bit high run so the decoder flushes the final separator.
// ---------------------------------------------------------------------------
static void oversample_bits(const std::vector<uint8_t> &msg,
                            std::vector<uint8_t> &samples)
{
    std::vector<uint8_t> bits;
    for (uint8_t value : msg)
    {
        for (int i = 0; i < 8; i++)
            bits.push_back(static_cast<uint8_t>((value >> i) & 1U));
        bits.push_back(1);
        bits.push_back(1);
        bits.push_back(1);
        bits.push_back(0);
    }
    for (int i = 0; i < 8; i++)
        bits.push_back(1);

    samples.clear();
    samples.reserve(bits.size() * 4);
    for (uint8_t b : bits)
        for (int s = 0; s < 4; s++)
            samples.push_back(static_cast<uint8_t>(b & 1U));
}

// Pack a flat sample vector (0/1) MSB-first into bytes, zero-padding the tail.
static void pack_samples(const std::vector<uint8_t> &samples,
                         std::vector<uint8_t> &out)
{
    out.clear();
    out.reserve((samples.size() + 7) / 8);
    for (size_t i = 0; i < samples.size(); i += 8)
    {
        uint8_t v = 0;
        for (int b = 0; b < 8; b++)
        {
            uint8_t s = (i + b < samples.size())
                            ? static_cast<uint8_t>(samples[i + b] & 1U)
                            : 0;
            v = static_cast<uint8_t>((v << 1) | s);
        }
        out.push_back(v);
    }
}

// A single-sample polarity flip inside an isolated bit run must be tolerated
// (the decoder's glitch-recovery path, `bit_cnt == 1`) and still decode to the
// original bytes. An alternating message (0x55 / 0xAA) makes each data bit its
// own 4-sample run, so flipping one interior sample produces a lone 1-sample
// run at the transition that the decoder absorbs.
void test_radian_decode_tolerates_single_sample_glitch(void)
{
    const std::vector<uint8_t> message = {0x55, 0xAA};

    std::vector<uint8_t> samples;
    oversample_bits(message, samples);

    // Index 5 sits inside the second data bit's run (an isolated run of four),
    // exercising the single-sample glitch branch without desyncing the frame.
    samples[5] ^= 1U;

    std::vector<uint8_t> rx;
    pack_samples(samples, rx);

    uint8_t decoded[64];
    uint8_t count = radian_decode_4bitpbit(
        rx.data(), static_cast<int>(rx.size()), decoded, sizeof(decoded));

    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(message.size()), count);
    TEST_ASSERT_EQUAL_HEX8(message[0], decoded[0]);
    TEST_ASSERT_EQUAL_HEX8(message[1], decoded[1]);
}

// When the destination buffer fills before the frame ends, the decoder must
// stop and return the number of bytes written (exercises the
// `dest_byte_cnt >= decoded_max` early-return guards).
void test_radian_decode_truncates_on_full_buffer(void)
{
    std::vector<uint8_t> message;
    for (int i = 0; i < 8; i++)
        message.push_back(static_cast<uint8_t>(0x11 * (i + 1)));

    std::vector<uint8_t> oversampled;
    encode_4x_oversampled(message, oversampled);

    uint8_t decoded[3];
    uint8_t count = radian_decode_4bitpbit(
        oversampled.data(), static_cast<int>(oversampled.size()),
        decoded, static_cast<int>(sizeof(decoded)));

    // Never writes past the buffer, and the bytes it did emit are correct.
    TEST_ASSERT_TRUE(count <= sizeof(decoded));
    TEST_ASSERT_EQUAL_HEX8(message[0], decoded[0]);
    TEST_ASSERT_EQUAL_HEX8(message[1], decoded[1]);
}

// A frame whose stop bits are corrupted to 0 accumulates framing errors; once
// they exceed half the decoded byte count the decoder rejects the frame
// (returns 0). Corrupting every byte's stop region guarantees rejection.
void test_radian_decode_rejects_framing_errors(void)
{
    const std::vector<uint8_t> message = {0x00, 0x00, 0x00, 0x00};

    std::vector<uint8_t> samples;
    oversample_bits(message, samples);

    // Each byte occupies 12 logical bits (48 samples): data[0..31] then
    // stop/separator[32..47]. Force the three stop bits of every byte low so
    // the stop-bit check at dest_bit_cnt == 10 flags a framing error.
    const size_t bits_per_byte = 12;
    for (size_t b = 0; b < message.size(); b++)
    {
        size_t base = b * bits_per_byte * 4;
        for (size_t s = base + 32; s < base + 44 && s < samples.size(); s++)
            samples[s] = 0;
    }

    std::vector<uint8_t> rx;
    pack_samples(samples, rx);

    uint8_t decoded[64];
    uint8_t count = radian_decode_4bitpbit(
        rx.data(), static_cast<int>(rx.size()), decoded, sizeof(decoded));

    TEST_ASSERT_EQUAL_UINT32(0, count);
}

// A buffer with no polarity transitions (all samples identical) never flushes
// a run, so no bytes are decoded: the decoder returns 0 via the
// `dest_byte_cnt == 0` path at the end.
void test_radian_decode_returns_zero_without_transitions(void)
{
    uint8_t all_high[16];
    memset(all_high, 0xFF, sizeof(all_high));
    uint8_t decoded[16];
    TEST_ASSERT_EQUAL_UINT32(
        0, radian_decode_4bitpbit(all_high, sizeof(all_high), decoded, sizeof(decoded)));

    uint8_t all_low[16];
    memset(all_low, 0x00, sizeof(all_low));
    TEST_ASSERT_EQUAL_UINT32(
        0, radian_decode_4bitpbit(all_low, sizeof(all_low), decoded, sizeof(decoded)));
}

// A framing error that fills the destination buffer must stop immediately and
// return the bytes written so far (the framing-error branch's buffer-full
// guard), rather than running on to the end-of-frame rejection.
void test_radian_decode_framing_error_truncates(void)
{
    const std::vector<uint8_t> message = {0x00, 0x00, 0x00, 0x00};

    std::vector<uint8_t> samples;
    oversample_bits(message, samples);

    const size_t bits_per_byte = 12;
    for (size_t b = 0; b < message.size(); b++)
    {
        size_t base = b * bits_per_byte * 4;
        for (size_t s = base + 32; s < base + 44 && s < samples.size(); s++)
            samples[s] = 0;
    }

    std::vector<uint8_t> rx;
    pack_samples(samples, rx);

    // decoded_max = 2 fills after the second framing-error byte, forcing the
    // early return from inside the framing-error path.
    uint8_t decoded[2];
    uint8_t count = radian_decode_4bitpbit(
        rx.data(), static_cast<int>(rx.size()), decoded, sizeof(decoded));

    TEST_ASSERT_EQUAL_UINT32(2, count);
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

    // A decreasing month (meter reset/rollover) is skipped when computing the
    // largest usage; the remaining rising months (all 1000 L) still bound the
    // check. Newest = 2500.
    const uint32_t with_reset[] = {10000, 11000, 500, 1500, 2500};
    TEST_ASSERT_TRUE(radian_reading_within_history_bounds(2500 + 1000, with_reset, 5, 100UL));
    TEST_ASSERT_FALSE(radian_reading_within_history_bounds(2500 + 100001, with_reset, 5, 100UL));
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
    RUN_TEST(test_radian_parse_primary_data_edge_cases);
    RUN_TEST(test_radian_validate_crc);
    RUN_TEST(test_radian_decode_roundtrip);
    RUN_TEST(test_radian_decode_rejects_empty_and_null);
    RUN_TEST(test_radian_decode_tolerates_single_sample_glitch);
    RUN_TEST(test_radian_decode_truncates_on_full_buffer);
    RUN_TEST(test_radian_decode_rejects_framing_errors);
    RUN_TEST(test_radian_decode_returns_zero_without_transitions);
    RUN_TEST(test_radian_decode_framing_error_truncates);
    RUN_TEST(test_radian_reading_within_history_bounds);
    RUN_TEST(test_radian_reading_within_history_bounds_skips_when_insufficient);
    RUN_TEST(test_replay_meter_fixtures);
    return UNITY_END();
}
