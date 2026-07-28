/**
 * @file test_esphome_publisher.cpp
 * @brief Host tests for ESPHomeDataPublisher
 *
 * The whole file is wrapped in `#ifdef USE_ESPHOME`, so the ordinary host build
 * compiles it to nothing and it had no coverage at all. This suite builds it
 * the way the shipped external component does and points it at recording
 * sensor stubs, so every value Home Assistant would receive is assertable.
 */

#include <unity.h>

#include <string>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include "esphome_data_publisher.h"
#include "utils.h"

using esphome::binary_sensor::BinarySensor;
using esphome::sensor::Sensor;
using esphome::text_sensor::TextSensor;

namespace
{
    /**
     * @brief One sensor object per entity the publisher can drive.
     *
     * Several of the publisher's sensor pointers are static and use a
     * "first non-null registration wins" rule, so the objects have to outlive
     * any single publisher instance. They are declared once here and cleared
     * between tests rather than recreated.
     */
    struct Sensors
    {
        Sensor volume, battery, counter, rssi, rssiPercent, lqi, lqiPercent, frequency;
        Sensor totalAttempts, successfulReads, failedReads, uptime;
        Sensor frequencyOffset, tunedFrequency, frequencyEstimate;

        TextSensor timeStart, timeEnd, status, error, radioState, timestamp, history;
        TextSensor version, meterSerial, meterYear, meterClock, meterModel;
        TextSensor readingSchedule, readingTimeUtc;

        BinarySensor activeReading, radioConnected;

        void clear()
        {
            Sensor *numeric[] = {&volume, &battery, &counter, &rssi, &rssiPercent, &lqi,
                                 &lqiPercent, &frequency, &totalAttempts, &successfulReads,
                                 &failedReads, &uptime, &frequencyOffset, &tunedFrequency,
                                 &frequencyEstimate};
            for (Sensor *s : numeric)
            {
                s->clear();
            }

            TextSensor *text[] = {&timeStart, &timeEnd, &status, &error, &radioState,
                                  &timestamp, &history, &version, &meterSerial, &meterYear,
                                  &meterClock, &meterModel, &readingSchedule, &readingTimeUtc};
            for (TextSensor *t : text)
            {
                t->clear();
            }

            activeReading.clear();
            radioConnected.clear();
        }
    };

    Sensors g_sensors;
    ESPHomeDataPublisher *g_publisher = nullptr;

    /// Wire every sensor up to a fresh publisher.
    ESPHomeDataPublisher &publisher()
    {
        return *g_publisher;
    }

    void wireAll(ESPHomeDataPublisher &p)
    {
        p.set_volume_sensor(&g_sensors.volume);
        p.set_battery_sensor(&g_sensors.battery);
        p.set_counter_sensor(&g_sensors.counter);
        p.set_rssi_sensor(&g_sensors.rssi);
        p.set_rssi_percentage_sensor(&g_sensors.rssiPercent);
        p.set_lqi_sensor(&g_sensors.lqi);
        p.set_lqi_percentage_sensor(&g_sensors.lqiPercent);
        p.set_time_start_sensor(&g_sensors.timeStart);
        p.set_time_end_sensor(&g_sensors.timeEnd);
        p.set_frequency_sensor(&g_sensors.frequency);

        p.set_total_attempts_sensor(&g_sensors.totalAttempts);
        p.set_successful_reads_sensor(&g_sensors.successfulReads);
        p.set_failed_reads_sensor(&g_sensors.failedReads);
        p.set_uptime_sensor(&g_sensors.uptime);
        p.set_frequency_offset_sensor(&g_sensors.frequencyOffset);
        p.set_tuned_frequency_sensor(&g_sensors.tunedFrequency);
        p.set_frequency_estimate_sensor(&g_sensors.frequencyEstimate);

        p.set_status_sensor(&g_sensors.status);
        p.set_error_sensor(&g_sensors.error);
        p.set_radio_state_sensor(&g_sensors.radioState);
        p.set_timestamp_sensor(&g_sensors.timestamp);
        p.set_history_sensor(&g_sensors.history);
        p.set_version_sensor(&g_sensors.version);
        p.set_meter_serial_sensor(&g_sensors.meterSerial);
        p.set_meter_year_sensor(&g_sensors.meterYear);
        p.set_meter_clock_sensor(&g_sensors.meterClock);
        p.set_meter_model_sensor(&g_sensors.meterModel);
        p.set_reading_schedule_sensor(&g_sensors.readingSchedule);
        p.set_reading_time_utc_sensor(&g_sensors.readingTimeUtc);

        p.set_active_reading_sensor(&g_sensors.activeReading);
        p.set_radio_connected_sensor(&g_sensors.radioConnected);
    }

    tmeter_data sampleReading()
    {
        tmeter_data data{};
        data.volume = 123456;
        data.reads_counter = 77;
        data.battery_left = 118;
        data.time_start = 8;
        data.time_end = 18;
        data.rssi = -140;
        data.rssi_dbm = -70;
        data.lqi = 25;
        data.freqest = 12;
        data.history_available = false;
        data.failure = ReadFailure::None;
        return data;
    }
}

void esphomePublisherSetUp()
{
    g_sensors.clear();
    delete g_publisher;
    g_publisher = new ESPHomeDataPublisher();
    wireAll(*g_publisher);
}

void esphomePublisherTearDown()
{
}

// ---------------------------------------------------------------------------
// Meter reading
// ---------------------------------------------------------------------------

void test_pub_reading_populates_every_sensor(void)
{
    publisher().publishMeterReading(sampleReading(), "2026-04-27T09:59:49Z");

    TEST_ASSERT_EQUAL_FLOAT(123456.0f, g_sensors.volume.last());
    TEST_ASSERT_EQUAL_FLOAT(118.0f, g_sensors.battery.last());
    TEST_ASSERT_EQUAL_FLOAT(77.0f, g_sensors.counter.last());
    TEST_ASSERT_EQUAL_FLOAT(-70.0f, g_sensors.rssi.last());
    TEST_ASSERT_EQUAL_FLOAT(25.0f, g_sensors.lqi.last());
    TEST_ASSERT_EQUAL_STRING("08:00", g_sensors.timeStart.last());
    TEST_ASSERT_EQUAL_STRING("18:00", g_sensors.timeEnd.last());
    TEST_ASSERT_EQUAL_STRING("2026-04-27T09:59:49Z", g_sensors.timestamp.last());
}

void test_pub_reading_publishes_each_sensor_once(void)
{
    publisher().publishMeterReading(sampleReading(), "2026-04-27T09:59:49Z");

    TEST_ASSERT_EQUAL_INT(1, g_sensors.volume.count());
    TEST_ASSERT_EQUAL_INT(1, g_sensors.counter.count());
    TEST_ASSERT_EQUAL_INT(1, g_sensors.timestamp.count());
}

void test_pub_reading_formats_wake_window_with_leading_zeros(void)
{
    tmeter_data data = sampleReading();
    data.time_start = 0;
    data.time_end = 9;
    publisher().publishMeterReading(data, "t");

    TEST_ASSERT_EQUAL_STRING("00:00", g_sensors.timeStart.last());
    TEST_ASSERT_EQUAL_STRING("09:00", g_sensors.timeEnd.last());
}

void test_pub_reading_omits_empty_meter_clock_and_model(void)
{
    // An undecoded clock or identifier must leave the entity untouched rather
    // than overwrite a good value with an empty string.
    publisher().publishMeterReading(sampleReading(), "t");

    TEST_ASSERT_FALSE(g_sensors.meterClock.published());
    TEST_ASSERT_FALSE(g_sensors.meterModel.published());
}

void test_pub_reading_publishes_meter_clock_and_model_when_present(void)
{
    tmeter_data data = sampleReading();
    snprintf(data.meter_time, sizeof(data.meter_time), "2026-04-27 09:59:49");
    snprintf(data.meter_type, sizeof(data.meter_type), "133290AL02");
    publisher().publishMeterReading(data, "t");

    TEST_ASSERT_EQUAL_STRING("2026-04-27 09:59:49", g_sensors.meterClock.last());
    TEST_ASSERT_EQUAL_STRING("133290AL02", g_sensors.meterModel.last());
}

void test_pub_reading_publishes_frequency_estimate_in_khz(void)
{
    tmeter_data data = sampleReading();
    data.freqest = 12;
    publisher().publishMeterReading(data, "t");

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12 * 1.587f, g_sensors.frequencyEstimate.last());
}

void test_pub_reading_without_sensors_is_safe(void)
{
    // A YAML config that declares no sensors at all must not crash the device.
    ESPHomeDataPublisher bare;
    bare.publishMeterReading(sampleReading(), "t");
    bare.publishStatusMessage("Ready");
    bare.publishError("None");
    bare.publishActiveReading(true);
    bare.publishStatistics(1, 2, 3);
    bare.publishUptime(60, "PT1M");
    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Signal quality conversions
// ---------------------------------------------------------------------------

void test_pub_lqi_percentage_matches_the_shared_helper(void)
{
    // The publisher keeps its own copy of this conversion; both builds must
    // report the same number for the same link quality.
    static const int samples[] = {0, 1, 20, 25, 63, 64, 100, 127};
    for (int lqi : samples)
    {
        tmeter_data data = sampleReading();
        data.lqi = lqi;
        g_sensors.lqiPercent.clear();
        publisher().publishMeterReading(data, "t");

        TEST_ASSERT_EQUAL_FLOAT((float)calculateLQIToPercentage(lqi),
                                g_sensors.lqiPercent.last());
    }
}

void test_pub_lqi_percentage_masks_the_crc_ok_bit(void)
{
    tmeter_data data = sampleReading();
    data.lqi = 0x80 | 25; // CRC_OK set alongside an error count of 25
    publisher().publishMeterReading(data, "t");

    TEST_ASSERT_EQUAL_FLOAT((float)calculateLQIToPercentage(25), g_sensors.lqiPercent.last());
}

void test_pub_rssi_percentage_matches_the_shared_helper(void)
{
    static const int samples[] = {-130, -120, -100, -80, -70, -60, -50, -40, -20};
    for (int dbm : samples)
    {
        tmeter_data data = sampleReading();
        data.rssi_dbm = dbm;
        g_sensors.rssiPercent.clear();
        publisher().publishMeterReading(data, "t");

        TEST_ASSERT_EQUAL_FLOAT((float)calculateMeterdBmToPercentage(dbm),
                                g_sensors.rssiPercent.last());
    }
}

void test_pub_rssi_percentage_is_clamped_to_0_100(void)
{
    static const int samples[] = {-200, -130, -70, -10, 0};
    for (int dbm : samples)
    {
        tmeter_data data = sampleReading();
        data.rssi_dbm = dbm;
        g_sensors.rssiPercent.clear();
        publisher().publishMeterReading(data, "t");

        const float percent = g_sensors.rssiPercent.last();
        TEST_ASSERT_TRUE(percent >= 0.0f && percent <= 100.0f);
    }
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

void test_pub_history_publishes_json_payload(void)
{
    // Valid months are packed from index 0; the first zero ends the series.
    uint32_t history[13] = {0};
    history[0] = 100;
    history[1] = 150;
    history[2] = 220;

    tmeter_data data = sampleReading();
    data.volume = 260;
    publisher().publishMeterReading(data, "t"); // caches the current volume
    publisher().publishHistory(history, true);

    TEST_ASSERT_EQUAL_STRING(
        "{\"history\":[100,150,220],\"monthly_usage\":[50,70],"
        "\"current_month_usage\":40,\"months_available\":3}",
        g_sensors.history.last());
}

void test_pub_history_reports_unavailable_when_not_decoded(void)
{
    uint32_t history[13] = {0};
    publisher().publishHistory(history, false);

    TEST_ASSERT_EQUAL_STRING("unavailable", g_sensors.history.last());
}

void test_pub_history_reports_unavailable_for_a_null_array(void)
{
    publisher().publishHistory(nullptr, true);

    TEST_ASSERT_EQUAL_STRING("unavailable", g_sensors.history.last());
}

void test_pub_history_without_a_sensor_is_safe(void)
{
    uint32_t history[13] = {0};
    history[0] = 100;

    ESPHomeDataPublisher bare;
    bare.publishHistory(history, true);
    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Status, radio state and errors
// ---------------------------------------------------------------------------

void test_pub_radio_state_drives_the_connected_binary_sensor(void)
{
    publisher().publishRadioState("Idle");
    TEST_ASSERT_EQUAL_STRING("Idle", g_sensors.radioState.last());
    TEST_ASSERT_TRUE(g_sensors.radioConnected.last());

    publisher().publishRadioState("unavailable");
    TEST_ASSERT_EQUAL_STRING("unavailable", g_sensors.radioState.last());
    TEST_ASSERT_FALSE(g_sensors.radioConnected.last());

    publisher().publishRadioState("Reading");
    TEST_ASSERT_TRUE(g_sensors.radioConnected.last());
}

void test_pub_radio_state_null_is_ignored(void)
{
    // Guards against dereferencing a null state while deriving connectivity.
    publisher().publishRadioState(nullptr);

    TEST_ASSERT_FALSE(g_sensors.radioState.published());
    TEST_ASSERT_FALSE(g_sensors.radioConnected.published());
}

void test_pub_status_and_error_are_published(void)
{
    publisher().publishStatusMessage("Reading successful");
    publisher().publishError("None");

    TEST_ASSERT_EQUAL_STRING("Reading successful", g_sensors.status.last());
    TEST_ASSERT_EQUAL_STRING("None", g_sensors.error.last());
}

void test_pub_status_and_error_ignore_null(void)
{
    publisher().publishStatusMessage(nullptr);
    publisher().publishError(nullptr);

    TEST_ASSERT_FALSE(g_sensors.status.published());
    TEST_ASSERT_FALSE(g_sensors.error.published());
}

void test_pub_active_reading_is_published(void)
{
    publisher().publishActiveReading(true);
    publisher().publishActiveReading(false);

    TEST_ASSERT_EQUAL_INT(2, g_sensors.activeReading.count());
    TEST_ASSERT_TRUE(g_sensors.activeReading.states[0]);
    TEST_ASSERT_FALSE(g_sensors.activeReading.states[1]);
}

// ---------------------------------------------------------------------------
// Statistics, frequency and settings
// ---------------------------------------------------------------------------

void test_pub_statistics_are_published(void)
{
    publisher().publishStatistics(10, 7, 3);

    TEST_ASSERT_EQUAL_FLOAT(10.0f, g_sensors.totalAttempts.last());
    TEST_ASSERT_EQUAL_FLOAT(7.0f, g_sensors.successfulReads.last());
    TEST_ASSERT_EQUAL_FLOAT(3.0f, g_sensors.failedReads.last());
}

void test_pub_frequency_offset_is_published_in_khz(void)
{
    publisher().publishFrequencyOffset(0.0175f);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 17.5f, g_sensors.frequencyOffset.last());
}

void test_pub_tuned_frequency_is_published_in_mhz(void)
{
    publisher().publishTunedFrequency(433.8375f);

    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 433.8375f, g_sensors.tunedFrequency.last());
}

void test_pub_negative_frequency_estimate_is_signed(void)
{
    publisher().publishFrequencyEstimate(-40);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, -40 * 1.587f, g_sensors.frequencyEstimate.last());
}

void test_pub_meter_settings_are_formatted_for_display(void)
{
    publisher().publishMeterSettings(7, 501090, "Monday-Friday", "10:00", 433.82f);

    TEST_ASSERT_EQUAL_STRING("501090", g_sensors.meterSerial.last());
    TEST_ASSERT_EQUAL_STRING("07", g_sensors.meterYear.last()); // two digits, zero padded
    TEST_ASSERT_EQUAL_STRING("Monday-Friday", g_sensors.readingSchedule.last());
    TEST_ASSERT_EQUAL_STRING("10:00", g_sensors.readingTimeUtc.last());
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 433.82f, g_sensors.frequency.last());
}

void test_pub_meter_settings_ignore_null_strings(void)
{
    publisher().publishMeterSettings(21, 123456, nullptr, nullptr, 433.82f);

    TEST_ASSERT_FALSE(g_sensors.readingSchedule.published());
    TEST_ASSERT_FALSE(g_sensors.readingTimeUtc.published());
    TEST_ASSERT_EQUAL_STRING("123456", g_sensors.meterSerial.last());
}

void test_pub_firmware_version_ignores_null(void)
{
    publisher().publishFirmwareVersion("3.2.0");
    TEST_ASSERT_EQUAL_STRING("3.2.0", g_sensors.version.last());

    publisher().publishFirmwareVersion(nullptr);
    TEST_ASSERT_EQUAL_INT(1, g_sensors.version.count());
}

void test_pub_uptime_is_published(void)
{
    publisher().publishUptime(3600, "PT1H");

    TEST_ASSERT_EQUAL_FLOAT(3600.0f, g_sensors.uptime.last());
}

void test_pub_is_always_ready(void)
{
    // ESPHome sensors have no connection state of their own, unlike MQTT.
    TEST_ASSERT_TRUE(publisher().isReady());

    ESPHomeDataPublisher bare;
    TEST_ASSERT_TRUE(bare.isReady());
}

void test_pub_wifi_details_and_discovery_are_no_ops(void)
{
    // ESPHome reports WiFi state and performs discovery itself.
    publisher().publishWiFiDetails("192.168.1.2", -60, 80, "aa:bb", "ssid", "cc:dd");
    publisher().publishDiscovery();
    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Shared, device-level sensors
// ---------------------------------------------------------------------------

void test_pub_shared_sensors_keep_their_first_registration(void)
{
    // Calibration and radio entities describe the one shared CC1101, so a
    // second meter instance in the same YAML must not steal them.
    Sensor secondOffset;
    TextSensor secondRadioState;
    BinarySensor secondConnected;

    ESPHomeDataPublisher second;
    second.set_frequency_offset_sensor(&secondOffset);
    second.set_radio_state_sensor(&secondRadioState);
    second.set_radio_connected_sensor(&secondConnected);

    second.publishFrequencyOffset(0.010f);
    second.publishRadioState("Idle");

    TEST_ASSERT_FALSE(secondOffset.published());
    TEST_ASSERT_FALSE(secondRadioState.published());
    TEST_ASSERT_FALSE(secondConnected.published());

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, g_sensors.frequencyOffset.last());
    TEST_ASSERT_EQUAL_STRING("Idle", g_sensors.radioState.last());
}

void test_pub_per_meter_sensors_are_not_shared(void)
{
    // Volume and status are per-meter, so a second instance drives its own.
    Sensor secondVolume;
    TextSensor secondStatus;

    ESPHomeDataPublisher second;
    second.set_volume_sensor(&secondVolume);
    second.set_status_sensor(&secondStatus);

    second.publishMeterReading(sampleReading(), "t");
    second.publishStatusMessage("Ready");

    TEST_ASSERT_TRUE(secondVolume.published());
    TEST_ASSERT_EQUAL_STRING("Ready", secondStatus.last());
    TEST_ASSERT_FALSE(g_sensors.volume.published());
    TEST_ASSERT_FALSE(g_sensors.status.published());
}
