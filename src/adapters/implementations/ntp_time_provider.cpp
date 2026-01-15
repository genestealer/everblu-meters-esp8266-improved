/**
 * @file ntp_time_provider.cpp
 * @brief Implementation of NTP time provider
 */

#include "ntp_time_provider.h"
#include "../../core/logging.h"
#include <Arduino.h>

NTPTimeProvider::NTPTimeProvider()
    : m_synced(false), m_ntpServer(nullptr), m_lastSyncAttempt(0)
{
}

void NTPTimeProvider::begin(const char *ntpServer)
{
    m_ntpServer = ntpServer;
    LOG_I("everblu_meter", "Configuring NTP server: %s", ntpServer);

    configTzTime("UTC0", ntpServer);

    // Wait for initial sync
    requestSync();
}

void NTPTimeProvider::requestSync()
{
    LOG_I("everblu_meter", "Requesting time synchronization...");

    m_lastSyncAttempt = millis();
    unsigned long waitStart = millis();

    while (millis() - waitStart < SYNC_TIMEOUT_MS)
    {
        time_t probe = time(nullptr);
        if (probe >= MIN_VALID_EPOCH)
        {
            m_synced = true;
            LOG_I("everblu_meter", "Sync successful after %lu ms",
                  (unsigned long)(millis() - waitStart));
            LOG_I("everblu_meter", "Automatic scheduling is now ACTIVE");
            return;
        }
        delay(200);
    }

    m_synced = false;
    LOG_W("everblu_meter", "Sync failed after %lu ms", SYNC_TIMEOUT_MS);
    LOG_W("everblu_meter", "Automatic scheduling is PAUSED (manual requests still available)");
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
