/**
 * @file ntp_time_provider.h
 * @brief Time provider using Arduino NTP implementation
 *
 * This is the implementation for standalone mode that uses
 * the ESP8266/ESP32 NTP client for time synchronization.
 */

#ifndef NTP_TIME_PROVIDER_H
#define NTP_TIME_PROVIDER_H

#include "time_provider.h"

/**
 * @class NTPTimeProvider
 * @brief Time provider using Arduino NTP client
 *
 * Manages NTP synchronization and provides time access
 * for standalone MQTT mode.
 */
class NTPTimeProvider : public ITimeProvider
{
public:
    NTPTimeProvider();
    ~NTPTimeProvider() override = default;

    /**
     * @brief Initialize NTP client with server
     * @param ntpServer NTP server hostname or IP
     */
    void begin(const char *ntpServer);

    // ITimeProvider interface
    bool isTimeSynced() const override;
    time_t getCurrentTime() const override;
    void requestSync() override;

private:
    bool m_synced;
    const char *m_ntpServer;
    unsigned long m_lastSyncAttempt;

    static const time_t MIN_VALID_EPOCH = 1609459200; // 2021-01-01
    static const unsigned long SYNC_TIMEOUT_MS = 10000;
};

#endif // NTP_TIME_PROVIDER_H
