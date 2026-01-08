/**
 * @file define_config_provider.h
 * @brief Configuration provider using compile-time defines from private.h
 *
 * This is the implementation for standalone MQTT mode that reads
 * configuration from preprocessor defines in private.h
 */

#ifndef DEFINE_CONFIG_PROVIDER_H
#define DEFINE_CONFIG_PROVIDER_H

#include "../config_provider.h"
#include "private.h"

/**
 * @class DefineConfigProvider
 * @brief Configuration provider using #defines from private.h
 *
 * Provides configuration values from compile-time defines.
 * Used in standalone MQTT mode for backward compatibility.
 */
class DefineConfigProvider : public IConfigProvider
{
public:
        DefineConfigProvider() = default;
        ~DefineConfigProvider() override = default;

        // Meter identification
        uint8_t getMeterYear() const override { return METER_YEAR; }
        uint32_t getMeterSerial() const override { return METER_SERIAL; }
        bool isMeterGas() const override
        {
#ifdef METER_IS_GAS
                return METER_IS_GAS != 0;
#else
                return false;
#endif
        }

        int getGasVolumeDivisor() const override
        {
#ifdef GAS_VOLUME_DIVISOR
                return GAS_VOLUME_DIVISOR;
#else
                return 100;
#endif
        }

        // RF configuration
        float getFrequency() const override
        {
#ifdef FREQUENCY
                return FREQUENCY;
#else
                return 433.82f;
#endif
        }

        bool isAutoScanEnabled() const override
        {
#ifdef AUTO_SCAN_ENABLED
                return AUTO_SCAN_ENABLED != 0;
#else
                return true;
#endif
        }

        // Scheduling configuration
        const char *getReadingSchedule() const override
        {
#ifdef DEFAULT_READING_SCHEDULE
                return DEFAULT_READING_SCHEDULE;
#else
                return "Monday-Friday";
#endif
        }

        int getReadHourUTC() const override
        {
#ifdef DEFAULT_READING_HOUR_UTC
                return DEFAULT_READING_HOUR_UTC;
#else
                return 10;
#endif
        }

        int getReadMinuteUTC() const override
        {
#ifdef DEFAULT_READING_MINUTE_UTC
                return DEFAULT_READING_MINUTE_UTC;
#else
                return 0;
#endif
        }

        int getTimezoneOffsetMinutes() const override
        {
#ifdef TIMEZONE_OFFSET_MINUTES
                return TIMEZONE_OFFSET_MINUTES;
#else
                return 0;
#endif
        }

        bool isAutoAlignReadingTime() const override
        {
#ifdef AUTO_ALIGN_READING_TIME
                return AUTO_ALIGN_READING_TIME != 0;
#else
                return true;
#endif
        }

        bool useAutoAlignMidpoint() const override
        {
#ifdef AUTO_ALIGN_USE_MIDPOINT
                return AUTO_ALIGN_USE_MIDPOINT != 0;
#else
                return true;
#endif
        }

        // Retry configuration
        int getMaxRetries() const override
        {
#ifdef MAX_RETRIES
                return MAX_RETRIES;
#else
                return 10;
#endif
        }

        unsigned long getRetryCooldownMs() const override
        {
                return 3600000; // 1 hour
        }

        // Network configuration (for standalone mode)
        const char *getWiFiSSID() const override { return SECRET_WIFI_SSID; }
        const char *getWiFiPassword() const override { return SECRET_WIFI_PASSWORD; }
        const char *getMqttServer() const override { return SECRET_MQTT_SERVER; }
        const char *getMqttUsername() const override { return SECRET_MQTT_USERNAME; }
        const char *getMqttPassword() const override { return SECRET_MQTT_PASSWORD; }
        const char *getMqttClientId() const override { return SECRET_MQTT_CLIENT_ID; }
        const char *getNtpServer() const override { return SECRET_NTP_SERVER; }
};

#endif // DEFINE_CONFIG_PROVIDER_H
