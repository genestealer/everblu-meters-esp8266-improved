/**
 * @file esphome_time_provider.cpp
 * @brief Implementation of ESPHome time provider
 */

#include "esphome_time_provider.h"

#if __has_include("esphome/components/time/real_time_clock.h")
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/log.h"
static const char *TAG = "esphome_time_provider";
#define EVERBLU_HAS_ESPHOME_TIME 1
#endif

// Always provide the RealTimeClock* constructor so the linker finds it
ESPHomeTimeProvider::ESPHomeTimeProvider(
#ifdef EVERBLU_HAS_ESPHOME_TIME
    esphome::time::RealTimeClock *time_component
#else
    void *time_component
#endif
    )
#ifdef EVERBLU_HAS_ESPHOME_TIME
    : time_component_(time_component)
#endif
{
#ifdef EVERBLU_HAS_ESPHOME_TIME
    if (!time_component_)
    {
        ESP_LOGE(TAG, "Time component pointer is null!");
    }
#else
    (void)time_component;
#endif
}

#ifdef EVERBLU_HAS_ESPHOME_TIME
bool ESPHomeTimeProvider::isTimeSynced() const
{
    if (!time_component_)
    {
        return false;
    }

    // Check if the time is valid (not at epoch)
    auto now = time_component_->now();
    return now.is_valid() && now.timestamp > 0;
}

time_t ESPHomeTimeProvider::getCurrentTime() const
{
    if (!time_component_)
    {
        return 0;
    }

    auto now = time_component_->now();
    return now.timestamp;
}

void ESPHomeTimeProvider::requestSync()
{
    // ESPHome handles time synchronization automatically
    // The time component will sync periodically based on its configuration
    // No manual sync request needed

    if (time_component_)
    {
        // Force a sync by calling the time component's update method
        // This is optional - ESPHome will sync automatically
        // time_component_->call_setup(); // Don't call this - it's for initialization only
    }
}

#else

// Stub implementation when ESPHome time headers are not available
bool ESPHomeTimeProvider::isTimeSynced() const { return false; }
time_t ESPHomeTimeProvider::getCurrentTime() const { return 0; }
void ESPHomeTimeProvider::requestSync() {}

#endif
