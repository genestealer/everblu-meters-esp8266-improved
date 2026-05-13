/**
 * @file esphome_config_provider.h
 * @brief Configuration provider using ESPHome YAML config
 *
 * Provides configuration from ESPHome YAML rather than compile-time defines.
 * Values are set during component initialization from YAML config blocks.
 */

#ifndef ESPHOME_CONFIG_PROVIDER_H
#define ESPHOME_CONFIG_PROVIDER_H

#include "config_provider.h"

/**
 * @class ESPHomeConfigProvider
 * @brief Configuration provider for ESPHome mode
 *
 * Reads configuration from ESPHome YAML config instead of #defines.
 * All values are set during component setup() via setters.
 */
class ESPHomeConfigProvider : public IConfigProvider
{
public:
    ESPHomeConfigProvider();
    ~ESPHomeConfigProvider() override = default;

    // Configuration setters (called during component setup)
    void setMeterYear(uint8_t year) { meter_year_ = year; }
    void setMeterSerial(uint32_t serial) { meter_serial_ = serial; }
    void setMeterType(bool isGas) { is_gas_ = isGas; }
    void setGasVolumeDivisor(int divisor) { gas_volume_divisor_ = divisor; }
    void setFrequency(float freq) { frequency_ = freq; }
    void setAutoScanEnabled(bool enabled) { auto_scan_enabled_ = enabled; }
    void setReadingSchedule(const char *schedule);
    void setReadHourUTC(int hour) { read_hour_utc_ = hour; }
    void setReadMinuteUTC(int minute) { read_minute_utc_ = minute; }
    void setTimezoneOffsetMinutes(int offset) { timezone_offset_minutes_ = offset; }
    void setAutoAlignReadingTime(bool enabled) { auto_align_enabled_ = enabled; }
    void setUseAutoAlignMidpoint(bool enabled) { auto_align_midpoint_ = enabled; }
    void setMaxRetries(int retries) { max_retries_ = retries; }
    void setRetryCooldownMs(unsigned long ms) { retry_cooldown_ms_ = ms; }

    // IConfigProvider interface implementation
    uint8_t getMeterYear() const override { return meter_year_; }
    uint32_t getMeterSerial() const override { return meter_serial_; }
    bool isMeterGas() const override { return is_gas_; }
    int getGasVolumeDivisor() const override { return gas_volume_divisor_; }
    float getFrequency() const override { return frequency_; }
    bool isAutoScanEnabled() const override { return auto_scan_enabled_; }
    const char *getReadingSchedule() const override { return reading_schedule_; }
    int getReadHourUTC() const override { return read_hour_utc_; }
    int getReadMinuteUTC() const override { return read_minute_utc_; }
    int getTimezoneOffsetMinutes() const override { return timezone_offset_minutes_; }
    bool isAutoAlignReadingTime() const override { return auto_align_enabled_; }
    bool useAutoAlignMidpoint() const override { return auto_align_midpoint_; }
    int getMaxRetries() const override { return max_retries_; }
    unsigned long getRetryCooldownMs() const override { return retry_cooldown_ms_; }

    // Network configuration - Not applicable in ESPHome
    // ESPHome handles WiFi/MQTT/network connectivity through its core
    const char *getWiFiSSID() const override { return ""; }
    const char *getWiFiPassword() const override { return ""; }
    const char *getMqttServer() const override { return ""; }
    const char *getMqttUsername() const override { return ""; }
    const char *getMqttPassword() const override { return ""; }
    const char *getMqttClientId() const override { return ""; }
    const char *getNtpServer() const override { return ""; }

private:
    // Meter identification
    uint8_t meter_year_{0};
    uint32_t meter_serial_{0};
    bool is_gas_{false};
    int gas_volume_divisor_{100};

    // RF configuration
    float frequency_{433.82f};
    bool auto_scan_enabled_{true};

    // Scheduling configuration
    char reading_schedule_[32]{"Monday-Friday"};
    int read_hour_utc_{10};
    int read_minute_utc_{0};
    int timezone_offset_minutes_{0};
    bool auto_align_enabled_{true};
    bool auto_align_midpoint_{true};

    // Retry configuration
    int max_retries_{10};
    unsigned long retry_cooldown_ms_{3600000}; // 1 hour
};

#endif // ESPHOME_CONFIG_PROVIDER_H
