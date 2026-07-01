/**
 * @file test_native_hal_meter_read.cpp
 * @brief End-to-end (host) simulation of a full meter read over the MQTT/standalone
 *        firmware path, driving the REAL src/core/cc1101.cpp against a simulated
 *        CC1101 chip mocked at the SPI/GPIO seam (see fake_cc1101.cpp).
 *
 * Two tests:
 *   1. test_onair_roundtrip: the synthesized on-air 4x-oversampled stream, when
 *      run through the firmware's real decode_4bitpbit_serial(), reproduces the
 *      captured decoded frame byte-for-byte. This validates the RX DSP path and
 *      the test's encoder.
 *   2. test_full_meter_read: cc1101_init() + get_meter_data_for_meter() run the
 *      complete request build -> wake-up/interrogation TX -> ACK RX -> data RX ->
 *      decode -> CRC -> parse pipeline against the fake meter, and the returned
 *      tmeter_data must match the captured fixture's expected values.
 */

#include <unity.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/cc1101.h" // struct tmeter_data, cc1101_init, get_meter_data_for_meter

#include "fake_cc1101.h"
#include "radian_encode.h"

// decode_4bitpbit_serial is an internal (non-static) firmware function not
// declared in cc1101.h; declare its prototype to exercise it directly.
uint8_t decode_4bitpbit_serial(uint8_t *rxBuffer, int l_total_byte, uint8_t *decoded_buffer);

// ---------------------------------------------------------------------------
// Fixture loading (home_001 from test/fixtures/meter_frames/fixtures.lst)
// ---------------------------------------------------------------------------
namespace
{
struct Fixture
{
  std::vector<uint8_t> decoded;
  uint32_t volume = 0;
  uint32_t battery = 0;
  uint32_t counter = 0;
  uint32_t time_start = 0;
  uint32_t time_end = 0;
  bool history_available = false;
  bool loaded = false;
};

std::string trim(const std::string &s)
{
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
    ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    --b;
  return s.substr(a, b - a);
}

std::ifstream open_fixture_list()
{
  const char *candidates[] = {
      "test/fixtures/meter_frames/fixtures.lst",
      "../test/fixtures/meter_frames/fixtures.lst",
      "../../test/fixtures/meter_frames/fixtures.lst",
      "../../../test/fixtures/meter_frames/fixtures.lst",
  };
  for (const char *path : candidates)
  {
    std::ifstream f(path);
    if (f.good())
      return f;
  }
  return std::ifstream();
}

Fixture load_home_001()
{
  Fixture fx;
  std::ifstream in = open_fixture_list();
  if (!in.good())
    return fx;

  std::string line;
  while (std::getline(in, line))
  {
    std::string t = trim(line);
    if (t.empty() || t[0] == '#')
      continue;
    if (t.rfind("home_001", 0) != 0)
      continue;

    std::vector<std::string> parts;
    std::stringstream ss(t);
    std::string part;
    while (std::getline(ss, part, '|'))
      parts.push_back(part);
    if (parts.size() != 9)
      return fx;

    std::stringstream hs(trim(parts[1]));
    std::string tok;
    while (hs >> tok)
      fx.decoded.push_back(static_cast<uint8_t>(std::strtol(tok.c_str(), nullptr, 16)));

    fx.volume = static_cast<uint32_t>(std::strtoul(trim(parts[2]).c_str(), nullptr, 10));
    fx.battery = static_cast<uint32_t>(std::strtoul(trim(parts[3]).c_str(), nullptr, 10));
    fx.counter = static_cast<uint32_t>(std::strtoul(trim(parts[4]).c_str(), nullptr, 10));
    fx.time_start = static_cast<uint32_t>(std::strtoul(trim(parts[5]).c_str(), nullptr, 10));
    fx.time_end = static_cast<uint32_t>(std::strtoul(trim(parts[6]).c_str(), nullptr, 10));
    fx.history_available = std::strtoul(trim(parts[7]).c_str(), nullptr, 10) != 0;
    fx.loaded = true;
    return fx;
  }
  return fx;
}

// Raw stream sizes the firmware expects for each receive_radian_frame() call:
//   expected = ((size_byte * 11) / 8 + 1) * 4
constexpr size_t kAckRawLen = ((0x12 * 11) / 8 + 1) * 4;  // 100
constexpr size_t kDataRawLen = ((0x7C * 11) / 8 + 1) * 4; // 684
} // namespace

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Test 1: on-air encode -> real firmware decode round trips to the fixture.
// ---------------------------------------------------------------------------
void test_onair_roundtrip(void)
{
  Fixture fx = load_home_001();
  TEST_ASSERT_TRUE_MESSAGE(fx.loaded, "home_001 fixture not found");
  TEST_ASSERT_EQUAL_UINT32(120, static_cast<uint32_t>(fx.decoded.size()));

  std::vector<uint8_t> raw(kDataRawLen + 64, 0);
  size_t raw_len = nativehal::radian_encode_onair(fx.decoded.data(), fx.decoded.size(),
                                                  raw.data(), raw.size(), kDataRawLen);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(kDataRawLen), static_cast<uint32_t>(raw_len));

  uint8_t decoded[200];
  memset(decoded, 0, sizeof(decoded));
  uint8_t n = decode_4bitpbit_serial(raw.data(), static_cast<int>(raw_len), decoded);

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(120, n, "decoded byte count mismatch");
  TEST_ASSERT_EQUAL_UINT8_ARRAY(fx.decoded.data(), decoded, fx.decoded.size());
}

// ---------------------------------------------------------------------------
// Test 2: full get_meter_data_for_meter() against the simulated meter.
// ---------------------------------------------------------------------------
void test_full_meter_read(void)
{
  Fixture fx = load_home_001();
  TEST_ASSERT_TRUE_MESSAGE(fx.loaded, "home_001 fixture not found");

  fake_cc1101_reset();

  // Queue the ACK frame (content is not parsed) then the DATA frame.
  std::vector<uint8_t> ack_raw(kAckRawLen, 0xF0);
  fake_cc1101_queue_stage2(ack_raw.data(), ack_raw.size());

  std::vector<uint8_t> data_raw(kDataRawLen + 64, 0);
  size_t data_len = nativehal::radian_encode_onair(fx.decoded.data(), fx.decoded.size(),
                                                   data_raw.data(), data_raw.size(), kDataRawLen);
  fake_cc1101_queue_stage2(data_raw.data(), data_len);

  TEST_ASSERT_TRUE_MESSAGE(cc1101_init(433.82f), "cc1101_init failed");

  struct tmeter_data md = get_meter_data_for_meter(15, 123456);

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(fx.volume, static_cast<uint32_t>(md.volume), "volume");
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(fx.battery), md.battery_left, "battery_left");
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(fx.counter), md.reads_counter, "reads_counter");
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(fx.time_start), md.time_start, "time_start");
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(fx.time_end), md.time_end, "time_end");
  TEST_ASSERT_TRUE_MESSAGE(md.history_available, "history_available");
}

int main(int, char **)
{
  UNITY_BEGIN();
  RUN_TEST(test_onair_roundtrip);
  RUN_TEST(test_full_meter_read);
  return UNITY_END();
}
