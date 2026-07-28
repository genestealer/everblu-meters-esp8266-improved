/**
 * @file binary_sensor.h
 * @brief Minimal recording stand-in for esphome::binary_sensor::BinarySensor
 *
 * Host-build only, used by the PlatformIO `native_esphome` environment.
 */

#ifndef EVERBLU_NATIVE_ESPHOME_BINARY_SENSOR_H
#define EVERBLU_NATIVE_ESPHOME_BINARY_SENSOR_H

#include <vector>

namespace esphome
{
    namespace binary_sensor
    {
        class BinarySensor
        {
        public:
            std::vector<bool> states;

            void publish_state(bool state) { states.push_back(state); }

            bool published() const { return !states.empty(); }
            bool last() const { return states.empty() ? false : states.back(); }
            int count() const { return (int)states.size(); }
            void clear() { states.clear(); }
        };
    }
}

#endif // EVERBLU_NATIVE_ESPHOME_BINARY_SENSOR_H
