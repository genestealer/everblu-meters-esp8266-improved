/**
 * @file native_fakes.h
 * @brief Host-side test doubles shared by every PlatformIO `native` suite
 *
 * This file lives in the test root, not inside a suite folder, because
 * PlatformIO compiles the whole of `build_src_filter` into every host test
 * binary. Suites that never mention MeterReader still have to resolve the
 * symbols its translation units reference, so the link seams below are shared.
 *
 * The production code reaches the outside world through four seams:
 *   - the IConfigProvider / ITimeProvider / IDataPublisher interfaces, and
 *   - the free functions declared in core/cc1101.h, StorageAbstraction, and the
 *     WiFi serial mirror, which are replaced at link time by native_fakes.cpp.
 *
 * Nothing here is compiled into the firmware.
 */

#ifndef EVERBLU_TEST_FAKES_H
#define EVERBLU_TEST_FAKES_H

#include <string>
#include <vector>

#include "adapters/config_provider.h"
#include "adapters/data_publisher.h"
#include "adapters/time_provider.h"
#include "core/cc1101.h"

// ---------------------------------------------------------------------------
// Radio fake (link seam over core/cc1101.h)
// ---------------------------------------------------------------------------

/**
 * @brief Scriptable stand-in for the CC1101 driver.
 *
 * Tests push outcomes onto @ref responses; each call to get_meter_data_for_meter()
 * consumes the next one, and the last entry repeats once the script runs out.
 * Every call is recorded so tests can assert on attempt counts and on the
 * frequency the radio was tuned to at the time.
 */
struct FakeRadio
{
    struct Call
    {
        uint8_t year;
        uint32_t serial;
        float frequency; // Frequency the radio was last initialised at
    };

    // Scripted outcomes, consumed in order
    std::vector<tmeter_data> responses;
    std::vector<Call> calls;

    // Frequency-selective mode: when carrierFrequency is non-zero the scripted
    // responses are ignored and the meter answers only while the radio is tuned
    // within carrierWidthMHz of the carrier, with a FREQEST proportional to how
    // far off centre it is. This models the response window that a frequency
    // scan is trying to find.
    float carrierFrequency = 0.0f;
    float carrierWidthMHz = 0.010f;

    // When positive, ask FrequencyManager to cancel the running scan once this
    // many reads have been served. A scan clears the cancel flag on entry, so
    // cancellation can only be tested from inside the sweep.
    int cancelScanAfterCalls = 0;

    // cc1101_init() behaviour and history
    bool initSucceeds = true;
    std::vector<float> initFrequencies;
    int recModeCalls = 0;

    void reset();

    /// Build a successful reading (non-zero volume and reads_counter).
    static tmeter_data success(int volume = 12345, int8_t freqest = 0);

    /// Build a failed reading of the given kind (zeroed volume and counter).
    static tmeter_data failure(ReadFailure reason);

    float lastInitFrequency() const
    {
        return initFrequencies.empty() ? 0.0f : initFrequencies.back();
    }
};

/// The single fake radio instance the cc1101 stubs in fakes.cpp operate on.
FakeRadio &fakeRadio();

// ---------------------------------------------------------------------------
// Storage fake (link seam over services/storage_abstraction.h)
// ---------------------------------------------------------------------------

/**
 * @brief In-memory replacement for the EEPROM/Preferences backend.
 *
 * Keeps values across a simulated reboot unless clear() is called, so
 * persistence round-trips can be tested.
 */
struct FakeStorage
{
    struct Entry
    {
        std::string key;
        float value;
        uint16_t magic;
    };

    std::vector<Entry> entries;
    int beginCalls = 0;
    int saveCalls = 0;

    void clear();
    const Entry *find(const char *key) const;
};

FakeStorage &fakeStorage();

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

/// Mutable IConfigProvider whose values tests set directly as public fields.
class FakeConfig : public IConfigProvider
{
public:
    uint8_t meterYear = 21;
    uint32_t meterSerial = 123456;
    bool meterIsGas = false;
    int gasVolumeDivisor = 100;

    float frequency = 433.82f;
    bool autoScan = false;
    bool autoScanOnFailure = false;

    std::string schedule = "Monday-Friday";
    int readHourUTC = 10;
    int readMinuteUTC = 0;
    int timezoneOffsetMinutes = 0;
    bool autoAlign = false;
    bool autoAlignMidpoint = false;

    int maxRetries = 3;
    unsigned long retryCooldownMs = 60000;

    uint8_t getMeterYear() const override { return meterYear; }
    uint32_t getMeterSerial() const override { return meterSerial; }
    bool isMeterGas() const override { return meterIsGas; }
    int getGasVolumeDivisor() const override { return gasVolumeDivisor; }

    float getFrequency() const override { return frequency; }
    bool isAutoScanEnabled() const override { return autoScan; }
    bool isAutoScanOnFailureEnabled() const override { return autoScanOnFailure; }

    const char *getReadingSchedule() const override { return schedule.c_str(); }
    int getReadHourUTC() const override { return readHourUTC; }
    int getReadMinuteUTC() const override { return readMinuteUTC; }
    int getTimezoneOffsetMinutes() const override { return timezoneOffsetMinutes; }
    bool isAutoAlignReadingTime() const override { return autoAlign; }
    bool useAutoAlignMidpoint() const override { return autoAlignMidpoint; }

    int getMaxRetries() const override { return maxRetries; }
    unsigned long getRetryCooldownMs() const override { return retryCooldownMs; }

    const char *getWiFiSSID() const override { return "ssid"; }
    const char *getWiFiPassword() const override { return "password"; }
    const char *getMqttServer() const override { return "127.0.0.1"; }
    const char *getMqttUsername() const override { return "user"; }
    const char *getMqttPassword() const override { return "pass"; }
    const char *getMqttClientId() const override { return "everblu-test"; }
    const char *getNtpServer() const override { return "pool.ntp.org"; }
};

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

/// ITimeProvider with a UTC timestamp the test sets explicitly.
class FakeTime : public ITimeProvider
{
public:
    bool synced = true;
    time_t nowUtc = 1735689600; // 2025-01-01 00:00:00 UTC
    int syncRequests = 0;

    bool isTimeSynced() const override { return synced; }
    time_t getCurrentTime() const override { return nowUtc; }
    void requestSync() override { syncRequests++; }

    /// Set the clock to a specific UTC wall-clock instant.
    void setUtc(int year, int month, int day, int hour, int minute, int second);
};

// ---------------------------------------------------------------------------
// Publisher
// ---------------------------------------------------------------------------

/// IDataPublisher that records every call so tests can assert on the sequence.
class RecordingPublisher : public IDataPublisher
{
public:
    struct Reading
    {
        tmeter_data data;
        std::string timestamp;
    };

    bool ready = true;

    std::vector<Reading> readings;
    std::vector<std::string> statuses;
    std::vector<std::string> radioStates;
    std::vector<std::string> errors;
    std::vector<bool> activeReadingFlags;
    std::vector<float> frequencyOffsets;
    std::vector<float> tunedFrequencies;

    struct Stats
    {
        unsigned long attempts;
        unsigned long successes;
        unsigned long failures;
    };
    std::vector<Stats> statistics;

    int historyPublishes = 0;
    int settingsPublishes = 0;
    int discoveryPublishes = 0;

    void reset();

    /// Number of recorded entries equal to @p value.
    static int countOf(const std::vector<std::string> &v, const char *value);

    bool sawStatus(const char *value) const { return countOf(statuses, value) > 0; }
    bool sawError(const char *value) const { return countOf(errors, value) > 0; }

    const std::string &lastStatus() const { return last(statuses); }
    const std::string &lastRadioState() const { return last(radioStates); }
    const std::string &lastError() const { return last(errors); }

    void publishMeterReading(const tmeter_data &data, const char *timestamp) override;
    void publishHistory(const uint32_t *history, bool historyAvailable) override;
    void publishWiFiDetails(const char *ip, int rssi, int signalPercent,
                            const char *mac, const char *ssid, const char *bssid) override;
    void publishMeterSettings(int meterYear, unsigned long meterSerial,
                              const char *schedule, const char *readingTime,
                              float frequency) override;
    void publishStatusMessage(const char *message) override;
    void publishRadioState(const char *state) override;
    void publishActiveReading(bool active) override;
    void publishError(const char *error) override;
    void publishStatistics(unsigned long totalAttempts, unsigned long successfulReads,
                           unsigned long failedReads) override;
    void publishFrequencyOffset(float offsetMHz) override;
    void publishTunedFrequency(float frequencyMHz) override;
    void publishFrequencyEstimate(int8_t freqestValue) override;
    void publishUptime(unsigned long uptimeSeconds, const char *uptimeISO) override;
    void publishFirmwareVersion(const char *version) override;
    void publishDiscovery() override;
    bool isReady() const override { return ready; }

private:
    static const std::string &last(const std::vector<std::string> &v);
};

// ---------------------------------------------------------------------------
// Shared setup
// ---------------------------------------------------------------------------

/**
 * @brief Return every fake and every piece of static production state to a
 *        known baseline.
 *
 * FrequencyManager and MeterReader both hold static state that would otherwise
 * leak between test cases, so this is called from setUp().
 */
void resetAllFakes();

#endif // EVERBLU_TEST_FAKES_H
