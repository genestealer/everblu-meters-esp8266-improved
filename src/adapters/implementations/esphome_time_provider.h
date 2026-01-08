/**
 * @file esphome_time_provider.h
 * @brief Time provider using ESPHome time component
 *
 * Wraps ESPHome's time component (SNTP, GPS, RTC, etc.) for use
 * with the meter reading scheduler.
 */

#ifndef ESPHOME_TIME_PROVIDER_H
#define ESPHOME_TIME_PROVIDER_H

#include "../time_provider.h"

#ifdef USE_ESPHOME
namespace esphome
{
    namespace time
    {
        class RealTimeClock;
    }
}
#endif

/**
 * @class ESPHomeTimeProvider
 * @brief Time provider using ESPHome time component
 *
 * Wraps ESPHome's time component to provide time synchronization
 * status and current time for scheduling.
 *
 * ESPHome supports multiple time sources:
 * - SNTP (default)
 * - GPS
 * - DS1307 RTC
 * - PCF8563 RTC
 * - etc.
 */
class ESPHomeTimeProvider : public ITimeProvider
{
public:
#ifdef USE_ESPHOME
    /**
     * @brief Constructor with ESPHome time component
     * @param time_component Pointer to ESPHome RealTimeClock component
     */
    explicit ESPHomeTimeProvider(esphome::time::RealTimeClock *time_component);
#else
    ESPHomeTimeProvider();
#endif

    ~ESPHomeTimeProvider() override = default;

    // ITimeProvider interface implementation
    bool isTimeSynced() const override;
    time_t getCurrentTime() const override;
    void requestSync() override;

private:
#ifdef USE_ESPHOME
    esphome::time::RealTimeClock *time_component_{nullptr};
#endif
};

#endif // ESPHOME_TIME_PROVIDER_H
