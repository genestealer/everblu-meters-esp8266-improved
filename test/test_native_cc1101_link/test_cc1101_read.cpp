/**
 * @file test_cc1101_read.cpp
 * @brief The whole meter read, driven end to end against a simulated radio
 *
 * `get_meter_data_for_meter()` is the firmware's one indivisible RF transaction:
 * a ~2 s wake-up burst clocked out of a 64-byte FIFO, an interrogation frame,
 * then two receive windows drained through the RX FIFO, software-decoded from a
 * 4x-oversampled stream, CRC-checked and parsed. On hardware it is only
 * observable through the log.
 *
 * Here the simulated CC1101 in native_cc1101_device.h models the FIFOs, the
 * MARCSTATE transitions and the GDO threshold lines, and replays a real
 * oversampled capture from test/fixtures/meter_frames/raw_frames.lst as the
 * meter's reply. That puts the transmit loop, both receive stages, the decoder,
 * the CRC check and the field parser under test without a radio.
 *
 * Test registration lives in test_runner.cpp.
 */

#include <unity.h>

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "native_cc1101_device.h"
#include "core/cc1101.h"
#include "core/utils.h"

// Mirror the driver's own optional include, so METER_CODE is visible here
// exactly when get_meter_data() can see it.
#if __has_include("private.h")
#include "private.h"
#endif

namespace
{
    constexpr float kReadFrequency = 433.82f;

    /// The meter the fixtures below were captured from.
    constexpr uint8_t kFixtureMeterYear = 20;
    constexpr uint32_t kFixtureMeterSerial = 257750;

    struct RawCapture
    {
        std::string name;
        std::vector<uint8_t> oversampled;
        uint32_t volume = 0;
        uint32_t battery = 0;
        uint32_t counter = 0;
        uint32_t timeStart = 0;
        uint32_t timeEnd = 0;
    };

    std::ifstream openRawCaptures()
    {
        const char *candidates[] = {
            "test/fixtures/meter_frames/raw_frames.lst",
            "../test/fixtures/meter_frames/raw_frames.lst",
            "../../test/fixtures/meter_frames/raw_frames.lst",
            "../../../test/fixtures/meter_frames/raw_frames.lst",
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

    std::vector<uint8_t> parseHex(const std::string &text)
    {
        std::vector<uint8_t> bytes;
        unsigned value = 0;
        int digits = 0;
        for (char c : text)
        {
            int nibble = -1;
            if (c >= '0' && c <= '9') nibble = c - '0';
            else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;

            if (nibble < 0)
            {
                digits = 0;
                value = 0;
                continue;
            }
            value = (value << 4) | (unsigned)nibble;
            if (++digits == 2)
            {
                bytes.push_back((uint8_t)value);
                digits = 0;
                value = 0;
            }
        }
        return bytes;
    }

    /// Decode one `name|hex|volume|...` row into a capture.
    bool parseCaptureLine(const std::string &line, RawCapture &out)
    {
        std::vector<std::string> parts;
        std::string field;
        for (char c : line)
        {
            if (c == '|') { parts.push_back(field); field.clear(); }
            else { field.push_back(c); }
        }
        parts.push_back(field);
        if (parts.size() != 9)
        {
            return false;
        }
        out.name = parts[0];
        out.oversampled = parseHex(parts[1]);
        out.volume = (uint32_t)strtoul(parts[2].c_str(), nullptr, 10);
        out.battery = (uint32_t)strtoul(parts[3].c_str(), nullptr, 10);
        out.counter = (uint32_t)strtoul(parts[4].c_str(), nullptr, 10);
        out.timeStart = (uint32_t)strtoul(parts[5].c_str(), nullptr, 10);
        out.timeEnd = (uint32_t)strtoul(parts[6].c_str(), nullptr, 10);
        return true;
    }

    /// First capture in raw_frames.lst whose name starts with @p prefix.
    bool loadRawCapture(const char *prefix, RawCapture &out)
    {
        std::ifstream in = openRawCaptures();
        if (!in.good())
        {
            return false;
        }
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#' || line.compare(0, strlen(prefix), prefix) != 0)
            {
                continue;
            }
            if (parseCaptureLine(line, out))
            {
                return true;
            }
        }
        return false;
    }

    /// Bring the radio up with a captured reply waiting for it.
    bool armRadioWithCapture(RawCapture &capture)
    {
        if (!loadRawCapture("raw_257750_a", capture))
        {
            return false;
        }
        nativeCC1101Install();
        TEST_ASSERT_TRUE(cc1101_init(kReadFrequency));
        nativeCC1101ArmReply(capture.oversampled);
        return true;
    }

    tmeter_data readFixtureMeter()
    {
        return get_meter_data_for_meter(kFixtureMeterYear, kFixtureMeterSerial);
    }
}

// ---------------------------------------------------------------------------
// The happy path
// ---------------------------------------------------------------------------

void test_read_decodes_a_captured_reply_into_a_reading(void)
{
    RawCapture capture;
    if (!armRadioWithCapture(capture))
    {
        TEST_IGNORE_MESSAGE("raw_frames.lst not found; skipping radio replay");
    }

    const tmeter_data data = readFixtureMeter();

    TEST_ASSERT_TRUE(data.failure == ReadFailure::None);
    TEST_ASSERT_EQUAL_UINT32(capture.volume, (uint32_t)data.volume);
    TEST_ASSERT_EQUAL_UINT32(capture.battery, (uint32_t)data.battery_left);
    TEST_ASSERT_EQUAL_UINT32(capture.counter, (uint32_t)data.reads_counter);
    TEST_ASSERT_EQUAL_UINT32(capture.timeStart, (uint32_t)data.time_start);
    TEST_ASSERT_EQUAL_UINT32(capture.timeEnd, (uint32_t)data.time_end);
    TEST_ASSERT_TRUE(data.history_available);
}

void test_read_reports_the_link_quality_it_measured(void)
{
    RawCapture capture;
    if (!armRadioWithCapture(capture))
    {
        TEST_IGNORE_MESSAGE("raw_frames.lst not found; skipping radio replay");
    }
    nativeCC1101().rssiRaw = 0x14; // -64 dBm
    nativeCC1101().lqiRaw = 0x87;  // CRC_OK set, LQI 7
    nativeCC1101().freqEst = 0xFE; // -2 LSB

    const tmeter_data data = readFixtureMeter();

    TEST_ASSERT_EQUAL_UINT8(0x14, data.rssi);
    TEST_ASSERT_EQUAL_INT(-64, data.rssi_dbm);
    TEST_ASSERT_EQUAL_UINT8(0x07, data.lqi); // bit 7 is CRC_OK, not part of the LQI
    TEST_ASSERT_EQUAL_INT8(-2, data.freqest);
}

void test_read_transmits_the_wake_up_burst_then_the_interrogation(void)
{
    RawCapture capture;
    if (!armRadioWithCapture(capture))
    {
        TEST_IGNORE_MESSAGE("raw_frames.lst not found; skipping radio replay");
    }
    nativeCC1101().txLog.clear();

    readFixtureMeter();

    const std::vector<uint8_t> &sent = nativeCC1101().txLog;
    // 77 eight-byte wake-up buffers plus the 39-byte interrogation frame is the
    // full ~2 s burst. The exact total depends on how far the FIFO drained, so
    // assert the shape rather than a magic number.
    TEST_ASSERT_GREATER_THAN_UINT32(39, (uint32_t)sent.size());

    // The interrogation frame is what carries the meter identity, and it is the
    // only non-0x55 content in the stream.
    std::vector<uint8_t> expected(100, 0);
    Make_Radian_Master_req(expected.data(), kFixtureMeterYear, kFixtureMeterSerial);

    bool found = false;
    for (size_t start = 0; start + 39 <= sent.size() && !found; start++)
    {
        found = memcmp(&sent[start], expected.data(), 39) == 0;
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "interrogation frame was never clocked out");
}

// ---------------------------------------------------------------------------
// Failure modes
// ---------------------------------------------------------------------------

void test_read_reports_no_reply_when_the_meter_stays_silent(void)
{
    // Nothing armed: the wake-up burst goes out, both receive windows time out.
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(kReadFrequency));

    const tmeter_data data = readFixtureMeter();

    TEST_ASSERT_TRUE(data.failure == ReadFailure::NoReply);
    TEST_ASSERT_EQUAL_INT(0, data.volume);
    TEST_ASSERT_EQUAL_UINT8(0, data.reads_counter);
}

void test_read_reports_a_corrupted_frame_rather_than_a_reading(void)
{
    RawCapture capture;
    if (!armRadioWithCapture(capture))
    {
        TEST_IGNORE_MESSAGE("raw_frames.lst not found; skipping radio replay");
    }
    // Damage the middle of the capture: enough to break the CRC, not enough to
    // stop the frame decoding, which is exactly what a marginal link produces.
    std::vector<uint8_t> corrupted = capture.oversampled;
    for (size_t i = corrupted.size() / 2; i < corrupted.size() / 2 + 24; i++)
    {
        corrupted[i] = (uint8_t)~corrupted[i];
    }
    nativeCC1101ArmReply(corrupted);

    const tmeter_data data = readFixtureMeter();

    TEST_ASSERT_TRUE(data.failure == ReadFailure::CrcFailed);
    TEST_ASSERT_EQUAL_INT(0, data.volume);
}

void test_read_blames_near_field_saturation_when_the_signal_is_too_strong(void)
{
    // A CRC failure at better than -50 dBm is the front-end clipping, not a weak
    // link, and the two need opposite remedies.
    RawCapture capture;
    if (!armRadioWithCapture(capture))
    {
        TEST_IGNORE_MESSAGE("raw_frames.lst not found; skipping radio replay");
    }
    std::vector<uint8_t> corrupted = capture.oversampled;
    for (size_t i = corrupted.size() / 2; i < corrupted.size() / 2 + 24; i++)
    {
        corrupted[i] = (uint8_t)~corrupted[i];
    }
    nativeCC1101ArmReply(corrupted);
    nativeCC1101().rssiRaw = 120; // -14 dBm

    const tmeter_data data = readFixtureMeter();

    TEST_ASSERT_TRUE(data.failure == ReadFailure::CrcFailed);
    TEST_ASSERT_EQUAL_INT(-14, data.rssi_dbm);
}

void test_read_gives_up_when_the_reply_stops_part_way_through(void)
{
    // The ACK window is satisfied but the data frame never completes, which is
    // what a meter dropping back to sleep mid-reply looks like.
    RawCapture capture;
    if (!armRadioWithCapture(capture))
    {
        TEST_IGNORE_MESSAGE("raw_frames.lst not found; skipping radio replay");
    }
    std::vector<uint8_t> truncated(capture.oversampled.begin(), capture.oversampled.begin() + 160);
    nativeCC1101ArmReply(truncated);

    const tmeter_data data = readFixtureMeter();

    TEST_ASSERT_TRUE(data.failure == ReadFailure::NoReply);
}

void test_read_times_out_when_the_frame_start_never_arrives(void)
{
    // Observed on a sleeping meter: enough RF for the sync stage to trigger, then
    // nothing. The receiver must abandon the window at the frame-start wait
    // rather than sit draining an empty FIFO for the rest of the timeout.
    RawCapture capture;
    if (!armRadioWithCapture(capture))
    {
        TEST_IGNORE_MESSAGE("raw_frames.lst not found; skipping radio replay");
    }
    // Sized to satisfy the ACK window and the data frame's sync stage, and to run
    // out immediately after: 64 (ACK sync) + 112 (ACK frame) + 64 (data sync).
    std::vector<uint8_t> finite(capture.oversampled.begin(), capture.oversampled.begin() + 240);
    nativeCC1101ArmReply(finite);
    nativeCC1101().rxRewindOnFlush = false;

    const tmeter_data data = readFixtureMeter();

    TEST_ASSERT_TRUE(data.failure == ReadFailure::NoReply);
    TEST_ASSERT_EQUAL_INT(0, data.volume);
}

void test_get_meter_data_resolves_the_configured_meter_code(void)
{
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(kReadFrequency));

    const tmeter_data data = get_meter_data();

#ifdef METER_CODE
    // A configured code is parsed and handed to the radio, so the outcome is
    // whatever the (unarmed) radio reports rather than "never tried".
    TEST_ASSERT_TRUE(data.failure == ReadFailure::NoReply);
#else
    // No METER_CODE is the position a user is in before configuring one: report
    // that nothing was tried, not a radio symptom that points at the aerial.
    TEST_ASSERT_TRUE(data.failure == ReadFailure::NotAttempted);
#endif
}

// ---------------------------------------------------------------------------
// Every capture on record
// ---------------------------------------------------------------------------

void test_every_recorded_capture_replays_through_the_radio(void)
{
    // Three meters, two reads each, with different meter types, clock fields and
    // history tails. Replaying all of them exercises the parser's optional-field
    // branches on real data rather than on hand-written buffers.
    std::ifstream in = openRawCaptures();
    if (!in.good())
    {
        TEST_IGNORE_MESSAGE("raw_frames.lst not found; skipping radio replay");
    }

    int replayed = 0;
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        RawCapture capture;
        if (!parseCaptureLine(line, capture))
        {
            continue;
        }

        nativeCC1101Install();
        TEST_ASSERT_TRUE(cc1101_init(kReadFrequency));
        nativeCC1101ArmReply(capture.oversampled);

        const tmeter_data data = readFixtureMeter();

        char message[96];
        snprintf(message, sizeof(message), "capture %s did not decode", capture.name.c_str());
        TEST_ASSERT_TRUE_MESSAGE(data.failure == ReadFailure::None, message);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(capture.volume, (uint32_t)data.volume, message);
        // Every capture carries the meter's own clock and type string.
        TEST_ASSERT_TRUE_MESSAGE(data.meter_type[0] != '\0', message);
        replayed++;
    }
    TEST_ASSERT_GREATER_THAN_INT(0, replayed);
}

// ---------------------------------------------------------------------------
// Entering receive
// ---------------------------------------------------------------------------

void test_receive_recovers_a_radio_that_ignores_the_first_rx_strobe(void)
{
    // A CC1101 left in RXFIFO_OVERFLOW ignores SRX. The driver flushes and
    // re-strobes once, which must be enough to get it listening again.
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(kReadFrequency));
    nativeCC1101().rxEntryFailures = 1;

    cc1101_rec_mode();

    TEST_ASSERT_EQUAL_UINT8(0x0D, nativeCC1101().marcstate);
}

void test_receive_gives_up_on_a_radio_that_never_enters_rx(void)
{
    // Without a bounded wait this spun forever feeding the watchdog, so the
    // firmware hung with no reboot. It must return and let the caller fail.
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(kReadFrequency));
    nativeCC1101().rxEntryFailures = 0xFFFF;

    cc1101_rec_mode();

    TEST_ASSERT_EQUAL_UINT8(0x11, nativeCC1101().marcstate);
}
