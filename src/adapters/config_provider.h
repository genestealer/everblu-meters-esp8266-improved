/**
 * @file config_provider.h
 * @brief Abstract interface for configuration access
 *
 * Provides a platform-agnostic way to access configuration parameters.
 * This allows the core meter reading logic to work with different configuration
 * sources (compile-time defines, runtime YAML, web interface, etc.)
 *
 * Implementations:
 * - DefineConfigProvider: Uses compile-time #defines from private.h (standalone mode)
 * - ESPHomeConfigProvider: Uses ESPHome YAML configuration (ESPHome mode)
 */

#ifndef CONFIG_PROVIDER_H
#define CONFIG_PROVIDER_H

#include <stdint.h>

/**
 * @class IConfigProvider
 * @brief Abstract interface for accessing configuration parameters
 *
 * All configuration access should go through this interface to enable
 * different configuration backends (defines, YAML, web UI, etc.)
 */
class IConfigProvider
{
public:
    virtual ~IConfigProvider() = default;

    // Meter identification
    virtual uint8_t getMeterYear() const = 0;
    virtual uint32_t getMeterSerial() const = 0;
    virtual bool isMeterGas() const = 0;
    virtual int getGasVolumeDivisor() const = 0;

    // RF configuration
    virtual float getFrequency() const = 0;
    virtual bool isAutoScanEnabled() const = 0;

    // Scheduling configuration
    virtual const char *getReadingSchedule() const = 0;
    virtual int getReadHourUTC() const = 0;
    virtual int getReadMinuteUTC() const = 0;
    virtual int getTimezoneOffsetMinutes() const = 0;
    virtual bool isAutoAlignReadingTime() const = 0;
    virtual bool useAutoAlignMidpoint() const = 0;

    // Retry configuration
    virtual int getMaxRetries() const = 0;
    virtual unsigned long getRetryCooldownMs() const = 0;

    // Network configuration (for standalone mode)
    virtual const char *getWiFiSSID() const = 0;
    virtual const char *getWiFiPassword() const = 0;
    virtual const char *getMqttServer() const = 0;
    virtual const char *getMqttUsername() const = 0;
    virtual const char *getMqttPassword() const = 0;
    virtual const char *getMqttClientId() const = 0;
    virtual const char *getNtpServer() const = 0;
};

#endif // CONFIG_PROVIDER_H
