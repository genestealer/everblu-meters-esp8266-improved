/**
 * @file native_fakes.cpp
 * @brief Definitions for the host-side test doubles declared in native_fakes.h
 *
 * This file also supplies link-time replacements for three sets of symbols the
 * production code expects the platform to provide:
 *   - the CC1101 driver free functions (core/cc1101.h),
 *   - StorageAbstraction (services/storage_abstraction.h), and
 *   - the WiFi serial mirror (core/wifi_serial.h), which is reduced to plain
 *     stdout so that logging still works without a network stack.
 */

#include "native_fakes.h"

#include <cmath>
#include <cstring>
#include <ctime>

#include "services/frequency_manager.h"
#include "services/storage_abstraction.h"

// Take the real Serial object rather than the WiFiSerial remap this header
// installs, since this translation unit is what defines WiFiSerial.
#define WIFI_SERIAL_NO_REMAP
#include "core/wifi_serial.h"

// ---------------------------------------------------------------------------
// FakeRadio and the cc1101 link seam
// ---------------------------------------------------------------------------

FakeRadio &fakeRadio()
{
    static FakeRadio instance;
    return instance;
}

void FakeRadio::reset()
{
    responses.clear();
    calls.clear();
    initFrequencies.clear();
    initSucceeds = true;
    recModeCalls = 0;
    onInit = nullptr;
    carrierFrequency = 0.0f;
    carrierWidthMHz = 0.010f;
    cancelScanAfterCalls = 0;
}

tmeter_data FakeRadio::success(int volume, int8_t freqest)
{
    tmeter_data data{};
    data.volume = volume;
    data.reads_counter = 42;
    data.battery_left = 120;
    data.time_start = 8;
    data.time_end = 18;
    data.rssi_dbm = -70;
    data.lqi = 20;
    data.freqest = freqest;
    data.history_available = false;
    data.failure = ReadFailure::None;
    return data;
}

tmeter_data FakeRadio::failure(ReadFailure reason)
{
    tmeter_data data{};
    data.failure = reason;
    return data;
}

// The CC1101 link seam is skipped by [env:native_cc1101], which compiles the real
// src/core/cc1101.cpp against a simulated SPI bus instead. Everything else in
// this file (FakeRadio itself, storage, WiFi serial) is still needed there.
#ifndef EVERBLU_NATIVE_REAL_CC1101

void setMHZ(float mhz)
{
    fakeRadio().initFrequencies.push_back(mhz);
}

bool cc1101_init(float freq)
{
    FakeRadio &radio = fakeRadio();
    if (radio.onInit)
    {
        radio.onInit(freq);
    }
    radio.initFrequencies.push_back(freq);
    return radio.initSucceeds;
}

void cc1101_rec_mode(void)
{
    fakeRadio().recModeCalls++;
}

uint32_t cc1101_get_gdo2_timeout_count(void)
{
    return 0;
}

struct tmeter_data get_meter_data_for_meter(uint8_t meter_year, uint32_t meter_serial)
{
    FakeRadio &radio = fakeRadio();
    const float tuned = radio.lastInitFrequency();
    radio.calls.push_back({meter_year, meter_serial, tuned});

    if (radio.cancelScanAfterCalls > 0 &&
        (int)radio.calls.size() >= radio.cancelScanAfterCalls)
    {
        FrequencyManager::requestScanCancel();
    }

    if (radio.carrierFrequency > 0.0f)
    {
        const float errorMHz = tuned - radio.carrierFrequency;
        if (std::fabs(errorMHz) > radio.carrierWidthMHz)
        {
            return FakeRadio::failure(ReadFailure::NoReply);
        }
        // FREQEST reports how far the receiver is from the true carrier, with
        // the sign the CC1101 uses: positive when tuned below the carrier.
        const float lsb = -errorMHz / 0.001587f;
        int quantised = (int)lroundf(lsb);
        if (quantised > 127) quantised = 127;
        if (quantised < -128) quantised = -128;
        tmeter_data data = FakeRadio::success(12345, (int8_t)quantised);
        data.rssi_dbm = -60 - (int)(std::fabs(errorMHz) * 1000.0f);
        return data;
    }

    if (radio.responses.empty())
    {
        return FakeRadio::failure(ReadFailure::NoReply);
    }

    // The final scripted response repeats, so a test only has to describe the
    // outcomes it cares about rather than pad the script to the retry count.
    const size_t index = radio.calls.size() - 1;
    if (index >= radio.responses.size())
    {
        return radio.responses.back();
    }
    return radio.responses[index];
}

struct tmeter_data get_meter_data(void)
{
    return get_meter_data_for_meter(0, 0);
}

#endif // EVERBLU_NATIVE_REAL_CC1101

// ---------------------------------------------------------------------------
// FakeStorage and the StorageAbstraction link seam
// ---------------------------------------------------------------------------

FakeStorage &fakeStorage()
{
    static FakeStorage instance;
    return instance;
}

void FakeStorage::clear()
{
    entries.clear();
    beginCalls = 0;
    saveCalls = 0;
}

const FakeStorage::Entry *FakeStorage::find(const char *key) const
{
    for (const Entry &entry : entries)
    {
        if (entry.key == key)
        {
            return &entry;
        }
    }
    return nullptr;
}

bool StorageAbstraction::begin()
{
    fakeStorage().beginCalls++;
    return true;
}

bool StorageAbstraction::saveFloat(const char *key, float value, uint16_t magic)
{
    FakeStorage &storage = fakeStorage();
    storage.saveCalls++;
    for (FakeStorage::Entry &entry : storage.entries)
    {
        if (entry.key == key)
        {
            entry.value = value;
            entry.magic = magic;
            return true;
        }
    }
    storage.entries.push_back({key, value, magic});
    return true;
}

float StorageAbstraction::loadFloat(const char *key, float defaultValue, uint16_t magic,
                                    float minValue, float maxValue)
{
    const FakeStorage::Entry *entry = fakeStorage().find(key);
    if (!entry || entry->magic != magic)
    {
        return defaultValue;
    }
    if (std::isnan(entry->value) || entry->value < minValue || entry->value > maxValue)
    {
        return defaultValue;
    }
    return entry->value;
}

bool StorageAbstraction::hasKey(const char *key)
{
    return fakeStorage().find(key) != nullptr;
}

bool StorageAbstraction::clearKey(const char *key)
{
    FakeStorage &storage = fakeStorage();
    for (size_t i = 0; i < storage.entries.size(); i++)
    {
        if (storage.entries[i].key == key)
        {
            storage.entries.erase(storage.entries.begin() + (long)i);
            return true;
        }
    }
    return false;
}

bool StorageAbstraction::clearAll()
{
    fakeStorage().entries.clear();
    return true;
}

// ---------------------------------------------------------------------------
// WiFi serial mirror: reduced to the local stdout stream
// ---------------------------------------------------------------------------

// Only needed when the monitor is compiled in; otherwise wifi_serial.h aliases
// WiFiSerial straight to the native Serial shim and there is nothing to fake.
#if WIFI_SERIAL_MONITOR_ENABLED

WifiSerialStream WiFiSerial(::Serial);

size_t WifiSerialStream::write(uint8_t c) { return _usb.write(c); }
size_t WifiSerialStream::write(const uint8_t *buffer, size_t size) { return _usb.write(buffer, size); }
void WifiSerialStream::flush() { _usb.flush(); }
int WifiSerialStream::available() { return 0; }
int WifiSerialStream::read() { return -1; }
int WifiSerialStream::peek() { return -1; }
void WifiSerialStream::beginServer() {}
void WifiSerialStream::loop() {}

size_t WifiSerialStream::printf(const char *format, ...)
{
    char buf[512];
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (n <= 0)
    {
        return 0;
    }
    const size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
    return write((const uint8_t *)buf, len);
}

void wifiSerialBegin() {}
void wifiSerialLoop() {}
void wifiSerialPrint(const char *str) { WiFiSerial.print(str); }
void wifiSerialPrintln(const char *str) { WiFiSerial.println(str); }

void wifiSerialPrintf(const char *format, ...)
{
    char buf[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    WiFiSerial.print(buf);
}

#endif // WIFI_SERIAL_MONITOR_ENABLED

// ---------------------------------------------------------------------------
// FakeTime
// ---------------------------------------------------------------------------

void FakeTime::setUtc(int year, int month, int day, int hour, int minute, int second)
{
    struct tm parts = {};
    parts.tm_year = year - 1900;
    parts.tm_mon = month - 1;
    parts.tm_mday = day;
    parts.tm_hour = hour;
    parts.tm_min = minute;
    parts.tm_sec = second;

    // timegm() is not portable, so convert by hand: the meter code only ever
    // reads the result back through gmtime(), so a pure UTC epoch is enough.
    static const int cumulativeDays[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    long days = 0;
    for (int y = 1970; y < year; y++)
    {
        const bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        days += leap ? 366 : 365;
    }
    days += cumulativeDays[parts.tm_mon];
    const bool leapThisYear = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if (leapThisYear && month > 2)
    {
        days += 1;
    }
    days += day - 1;

    nowUtc = (time_t)days * 86400 + hour * 3600 + minute * 60 + second;
}

// ---------------------------------------------------------------------------
// RecordingPublisher
// ---------------------------------------------------------------------------

void RecordingPublisher::reset()
{
    ready = true;
    readings.clear();
    statuses.clear();
    radioStates.clear();
    errors.clear();
    activeReadingFlags.clear();
    frequencyOffsets.clear();
    tunedFrequencies.clear();
    statistics.clear();
    historyPublishes = 0;
    settingsPublishes = 0;
    discoveryPublishes = 0;
}

int RecordingPublisher::countOf(const std::vector<std::string> &v, const char *value)
{
    int count = 0;
    for (const std::string &entry : v)
    {
        if (entry == value)
        {
            count++;
        }
    }
    return count;
}

const std::string &RecordingPublisher::last(const std::vector<std::string> &v)
{
    static const std::string empty;
    return v.empty() ? empty : v.back();
}

void RecordingPublisher::publishMeterReading(const tmeter_data &data, const char *timestamp)
{
    readings.push_back({data, timestamp ? timestamp : ""});
}

void RecordingPublisher::publishHistory(const uint32_t *, bool) { historyPublishes++; }

void RecordingPublisher::publishWiFiDetails(const char *, int, int, const char *, const char *, const char *) {}

void RecordingPublisher::publishMeterSettings(int, unsigned long, const char *, const char *, float)
{
    settingsPublishes++;
}

void RecordingPublisher::publishStatusMessage(const char *message)
{
    statuses.push_back(message ? message : "");
}

void RecordingPublisher::publishRadioState(const char *state)
{
    radioStates.push_back(state ? state : "");
}

void RecordingPublisher::publishActiveReading(bool active)
{
    activeReadingFlags.push_back(active);
}

void RecordingPublisher::publishError(const char *error)
{
    errors.push_back(error ? error : "");
}

void RecordingPublisher::publishStatistics(unsigned long totalAttempts, unsigned long successfulReads,
                                           unsigned long failedReads)
{
    statistics.push_back({totalAttempts, successfulReads, failedReads});
}

void RecordingPublisher::publishFrequencyOffset(float offsetMHz)
{
    frequencyOffsets.push_back(offsetMHz);
}

void RecordingPublisher::publishTunedFrequency(float frequencyMHz)
{
    tunedFrequencies.push_back(frequencyMHz);
}

void RecordingPublisher::publishFrequencyEstimate(int8_t) {}
void RecordingPublisher::publishUptime(unsigned long, const char *) {}
void RecordingPublisher::publishFirmwareVersion(const char *) {}
void RecordingPublisher::publishDiscovery() { discoveryPublishes++; }

// ---------------------------------------------------------------------------
// Shared reset
// ---------------------------------------------------------------------------

void resetAllFakes()
{
    // A case may leave a non-blocking deep scan part-way through. Step it to
    // completion while its callbacks are still valid, so neither the scan state
    // nor a dangling callback leaks into the next test.
    if (FrequencyManager::isScanInProgress())
    {
        FrequencyManager::requestScanCancel();
        FrequencyManager::loopScan();
    }

    // Start at a plausible uptime rather than zero: several timers in the
    // firmware use "0" as their "never happened" sentinel, so a test running at
    // millis() == 0 would exercise a state the device never boots into.
    nativeClockSet(10000);
    fakeRadio().reset();
    fakeStorage().clear();

    // FrequencyManager keeps its calibration in static members that outlive a
    // single test, so put it back to a clean, uncalibrated state.
    FrequencyManager::setAdaptiveThreshold(10);
    FrequencyManager::resetAdaptiveTracking();
    FrequencyManager::setAutoScanEnabled(false);
    FrequencyManager::setOffset(0.0f);
    FrequencyManager::setRadioInitCallback(nullptr);
    FrequencyManager::setMeterReadCallback(nullptr);
}
