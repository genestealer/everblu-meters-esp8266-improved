/**
 * @file ntp_time_provider.cpp
 * @brief Implementation of NTP time provider
 */

#include "ntp_time_provider.h"
#include <Arduino.h>

NTPTimeProvider::NTPTimeProvider()
    : m_synced(false), m_ntpServer(nullptr), m_lastSyncAttempt(0)
{
}

void NTPTimeProvider::begin(const char *ntpServer)
{
    m_ntpServer = ntpServer;
    Serial.printf("[NTPTimeProvider] Configuring NTP server: %s\n", ntpServer);

    configTzTime("UTC0", ntpServer);

    // Wait for initial sync
    requestSync();
}

void NTPTimeProvider::requestSync()
{
    Serial.println("[NTPTimeProvider] Requesting time synchronization...");

    m_lastSyncAttempt = millis();
    unsigned long waitStart = millis();

    while (millis() - waitStart < SYNC_TIMEOUT_MS)
    {
        time_t probe = time(nullptr);
        if (probe >= MIN_VALID_EPOCH)
        {
            m_synced = true;
            Serial.printf("[NTPTimeProvider] Sync successful after %lu ms\n",
                          (unsigned long)(millis() - waitStart));
            Serial.println("[NTPTimeProvider] Automatic scheduling is now ACTIVE");
            return;
        }
        delay(200);
    }

    m_synced = false;
    Serial.printf("[NTPTimeProvider] Sync failed after %lu ms\n", SYNC_TIMEOUT_MS);
    Serial.println("[NTPTimeProvider] Automatic scheduling is PAUSED (manual requests still available)");
}

bool NTPTimeProvider::isTimeSynced() const
{
    if (!m_synced)
        return false;

    // Verify time is still valid
    time_t now = time(nullptr);
    return now >= MIN_VALID_EPOCH;
}

time_t NTPTimeProvider::getCurrentTime() const
{
    return time(nullptr);
}
