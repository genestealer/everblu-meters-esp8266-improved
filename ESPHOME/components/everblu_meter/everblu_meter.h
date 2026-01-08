/**
 * @file everblu_meter.h
 * @brief ESPHome component for EverBlu Cyble Enhanced meters
 *
 * Main component that integrates the meter reading logic with ESPHome.
 * Uses the adapter pattern to connect ESPHome sensors with the core
 * meter reading functionality.
 */

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/time/real_time_clock.h"

// Include core meter reading components
// Note: These paths assume the component structure includes the src/ files
#include "../../src/services/meter_reader.h"
#include "../../src/adapters/implementations/esphome_config_provider.h"
#include "../../src/adapters/implementations/esphome_time_provider.h"
#include "../../src/adapters/implementations/esphome_data_publisher.h"

namespace esphome
{
    namespace everblu_meter
    {

        class EverbluMeterComponent : public PollingComponent
        {
        public:
            EverbluMeterComponent() = default;

            // Component lifecycle
            void setup() override;
            void loop() override;
            void update() override;
            void dump_config() override;
            float get_setup_priority() const override { return setup_priority::DATA; }

            // Configuration setters (called from Python code generation)
            void set_meter_year(uint8_t year) { meter_year_ = year; }
            void set_meter_serial(uint32_t serial) { meter_serial_ = serial; }
            void set_meter_type(bool is_gas) { is_gas_ = is_gas; }
            void set_gas_volume_divisor(int divisor) { gas_volume_divisor_ = divisor; }
            void set_frequency(float freq) { frequency_ = freq; }
            void set_auto_scan(bool enabled) { auto_scan_ = enabled; }
            void set_reading_schedule(const std::string &schedule) { reading_schedule_ = schedule; }
            void set_read_hour(int hour) { read_hour_ = hour; }
            void set_read_minute(int minute) { read_minute_ = minute; }
            void set_timezone_offset(int offset) { timezone_offset_ = offset; }
            void set_auto_align_time(bool enabled) { auto_align_time_ = enabled; }
            void set_auto_align_midpoint(bool enabled) { auto_align_midpoint_ = enabled; }
            void set_max_retries(int retries) { max_retries_ = retries; }
            void set_retry_cooldown(unsigned long ms) { retry_cooldown_ms_ = ms; }
            void set_time_component(time::RealTimeClock *time) { time_component_ = time; }

            // Sensor setters
            void set_volume_sensor(sensor::Sensor *sensor) { volume_sensor_ = sensor; }
            void set_battery_sensor(sensor::Sensor *sensor) { battery_sensor_ = sensor; }
            void set_counter_sensor(sensor::Sensor *sensor) { counter_sensor_ = sensor; }
            void set_rssi_sensor(sensor::Sensor *sensor) { rssi_sensor_ = sensor; }
            void set_rssi_percentage_sensor(sensor::Sensor *sensor) { rssi_percentage_sensor_ = sensor; }
            void set_lqi_sensor(sensor::Sensor *sensor) { lqi_sensor_ = sensor; }
            void set_lqi_percentage_sensor(sensor::Sensor *sensor) { lqi_percentage_sensor_ = sensor; }
            void set_time_start_sensor(sensor::Sensor *sensor) { time_start_sensor_ = sensor; }
            void set_time_end_sensor(sensor::Sensor *sensor) { time_end_sensor_ = sensor; }
            void set_total_attempts_sensor(sensor::Sensor *sensor) { total_attempts_sensor_ = sensor; }
            void set_successful_reads_sensor(sensor::Sensor *sensor) { successful_reads_sensor_ = sensor; }
            void set_failed_reads_sensor(sensor::Sensor *sensor) { failed_reads_sensor_ = sensor; }

            void set_status_sensor(text_sensor::TextSensor *sensor) { status_sensor_ = sensor; }
            void set_error_sensor(text_sensor::TextSensor *sensor) { error_sensor_ = sensor; }
            void set_radio_state_sensor(text_sensor::TextSensor *sensor) { radio_state_sensor_ = sensor; }
            void set_timestamp_sensor(text_sensor::TextSensor *sensor) { timestamp_sensor_ = sensor; }

            void set_active_reading_sensor(binary_sensor::BinarySensor *sensor) { active_reading_sensor_ = sensor; }

        protected:
            // Configuration
            uint8_t meter_year_{0};
            uint32_t meter_serial_{0};
            bool is_gas_{false};
            int gas_volume_divisor_{100};
            float frequency_{433.82f};
            bool auto_scan_{true};
            std::string reading_schedule_{"Monday-Friday"};
            int read_hour_{10};
            int read_minute_{0};
            int timezone_offset_{0};
            bool auto_align_time_{true};
            bool auto_align_midpoint_{true};
            int max_retries_{10};
            unsigned long retry_cooldown_ms_{3600000};

            // ESPHome components
            time::RealTimeClock *time_component_{nullptr};

            // Sensors
            sensor::Sensor *volume_sensor_{nullptr};
            sensor::Sensor *battery_sensor_{nullptr};
            sensor::Sensor *counter_sensor_{nullptr};
            sensor::Sensor *rssi_sensor_{nullptr};
            sensor::Sensor *rssi_percentage_sensor_{nullptr};
            sensor::Sensor *lqi_sensor_{nullptr};
            sensor::Sensor *lqi_percentage_sensor_{nullptr};
            sensor::Sensor *time_start_sensor_{nullptr};
            sensor::Sensor *time_end_sensor_{nullptr};
            sensor::Sensor *total_attempts_sensor_{nullptr};
            sensor::Sensor *successful_reads_sensor_{nullptr};
            sensor::Sensor *failed_reads_sensor_{nullptr};

            text_sensor::TextSensor *status_sensor_{nullptr};
            text_sensor::TextSensor *error_sensor_{nullptr};
            text_sensor::TextSensor *radio_state_sensor_{nullptr};
            text_sensor::TextSensor *timestamp_sensor_{nullptr};

            binary_sensor::BinarySensor *active_reading_sensor_{nullptr};

            // Core meter reading components (adapters + orchestrator)
            ESPHomeConfigProvider *config_provider_{nullptr};
            ESPHomeTimeProvider *time_provider_{nullptr};
            ESPHomeDataPublisher *data_publisher_{nullptr};
            MeterReader *meter_reader_{nullptr};
        };

    } // namespace everblu_meter
} // namespace esphome
