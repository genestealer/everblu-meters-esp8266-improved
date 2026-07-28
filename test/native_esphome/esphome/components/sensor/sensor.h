/**
 * @file sensor.h
 * @brief Minimal recording stand-in for esphome::sensor::Sensor
 *
 * Host-build only, used by the PlatformIO `native_esphome` environment. Every
 * published state is kept so tests can assert on what Home Assistant would
 * have received.
 */

#ifndef EVERBLU_NATIVE_ESPHOME_SENSOR_H
#define EVERBLU_NATIVE_ESPHOME_SENSOR_H

#include <vector>

namespace esphome
{
    namespace sensor
    {
        class Sensor
        {
        public:
            std::vector<float> states;

            void publish_state(float state) { states.push_back(state); }

            bool published() const { return !states.empty(); }
            float last() const { return states.empty() ? 0.0f : states.back(); }
            int count() const { return (int)states.size(); }
            void clear() { states.clear(); }
        };
    }
}

#endif // EVERBLU_NATIVE_ESPHOME_SENSOR_H
