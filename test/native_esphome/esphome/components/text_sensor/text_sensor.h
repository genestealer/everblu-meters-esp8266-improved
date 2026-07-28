/**
 * @file text_sensor.h
 * @brief Minimal recording stand-in for esphome::text_sensor::TextSensor
 *
 * Host-build only, used by the PlatformIO `native_esphome` environment.
 */

#ifndef EVERBLU_NATIVE_ESPHOME_TEXT_SENSOR_H
#define EVERBLU_NATIVE_ESPHOME_TEXT_SENSOR_H

#include <string>
#include <vector>

namespace esphome
{
    namespace text_sensor
    {
        class TextSensor
        {
        public:
            std::vector<std::string> states;

            void publish_state(const std::string &state) { states.push_back(state); }

            bool published() const { return !states.empty(); }
            const char *last() const { return states.empty() ? "" : states.back().c_str(); }
            int count() const { return (int)states.size(); }
            void clear() { states.clear(); }
        };
    }
}

#endif // EVERBLU_NATIVE_ESPHOME_TEXT_SENSOR_H
