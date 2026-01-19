/**
 * @file esphome_config_provider.cpp
 * @brief Implementation of ESPHome configuration provider
 */

#include "esphome_config_provider.h"
#include <string.h>

ESPHomeConfigProvider::ESPHomeConfigProvider()
{
    // Initialize with default values
    // These will be overridden during component setup
}

void ESPHomeConfigProvider::setReadingSchedule(const char *schedule)
{
    if (schedule)
    {
        strncpy(reading_schedule_, schedule, sizeof(reading_schedule_) - 1);
        reading_schedule_[sizeof(reading_schedule_) - 1] = '\0';
    }
}
