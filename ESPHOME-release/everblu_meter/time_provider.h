/**
 * @file time_provider.h
 * @brief Abstract interface for time synchronization
 *
 * Provides a platform-agnostic way to access synchronized time and check
 * time synchronization status. This allows the scheduling system to work
 * with different time sources (NTP, ESPHome time component, manual time, etc.)
 *
 * Implementations:
 * - NTPTimeProvider: Uses Arduino NTP (standalone mode)
 * - ESPHomeTimeProvider: Uses ESPHome time component (ESPHome mode)
 */

#ifndef TIME_PROVIDER_H
#define TIME_PROVIDER_H

#include <time.h>

/**
 * @class ITimeProvider
 * @brief Abstract interface for time access and synchronization status
 *
 * Allows the core scheduling logic to work with different time sources
 * without depending on specific implementations.
 */
class ITimeProvider
{
public:
    virtual ~ITimeProvider() = default;

    /**
     * @brief Check if time is currently synchronized
     * @return true if time is synchronized and reliable
     */
    virtual bool isTimeSynced() const = 0;

    /**
     * @brief Get current UTC time
     * @return Current time as Unix timestamp (seconds since epoch)
     */
    virtual time_t getCurrentTime() const = 0;

    /**
     * @brief Request time synchronization
     *
     * For NTP: initiates NTP sync
     * For ESPHome: may be a no-op if handled by component
     */
    virtual void requestSync() = 0;

    /**
     * @brief Get time in local timezone (UTC + offset)
     * @param offsetMinutes Timezone offset in minutes
     * @return Local time as Unix timestamp
     */
    virtual time_t getLocalTime(int offsetMinutes) const
    {
        return getCurrentTime() + (time_t)(offsetMinutes * 60);
    }

    /**
     * @brief Check if time is valid (not at epoch start)
     * @return true if time appears valid
     */
    virtual bool isTimeValid() const
    {
        // 2021-01-01 00:00:00 UTC
        const time_t MIN_VALID_EPOCH = 1609459200;
        return getCurrentTime() >= MIN_VALID_EPOCH;
    }
};

#endif // TIME_PROVIDER_H
