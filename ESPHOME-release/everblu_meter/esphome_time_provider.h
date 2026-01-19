/**
 * @file esphome_time_provider.h
 * @brief Time provider using ESPHome time component
 *
 * Wraps ESPHome's time component (SNTP, GPS, RTC, etc.) for use
 * with the meter reading scheduler.
 */

#ifndef ESPHOME_TIME_PROVIDER_H
#define ESPHOME_TIME_PROVIDER_H

#include "time_provider.h"

#if defined(USE_ESPHOME) || __has_include("esphome/components/time/real_time_clock.h")
namespace esphome
{
    namespace time
    {
        class RealTimeClock;
    }
}
#define EVERBLU_HAS_ESPHOME_TIME 1
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
    /**
     * @brief Constructor with ESPHome time component (pointer may be null)
     */
#ifdef EVERBLU_HAS_ESPHOME_TIME
    explicit ESPHomeTimeProvider(esphome::time::RealTimeClock *time_component);
#else
    explicit ESPHomeTimeProvider(void *time_component = nullptr);
#endif

    ~ESPHomeTimeProvider() override = default;

    // ITimeProvider interface implementation
    bool isTimeSynced() const override;
    time_t getCurrentTime() const override;
    void requestSync() override;

private:
#ifdef EVERBLU_HAS_ESPHOME_TIME
    esphome::time::RealTimeClock *time_component_{nullptr};
#endif
};

#endif // ESPHOME_TIME_PROVIDER_H
