/**
 * @file everblu_meter.cpp
 * @brief Implementation of ESPHome component for EverBlu Cyble Enhanced meters
 */

#include "everblu_meter.h"
#include "esphome/core/log.h"

namespace esphome
{
    namespace everblu_meter
    {

        static const char *const TAG = "everblu_meter";

        void EverbluMeterComponent::setup()
        {
            ESP_LOGCONFIG(TAG, "Setting up EverBlu Meter...");

            // Create config provider and configure it
            config_provider_ = new ESPHomeConfigProvider();
            config_provider_->setMeterYear(meter_year_);
            config_provider_->setMeterSerial(meter_serial_);
            config_provider_->setMeterType(is_gas_);
            config_provider_->setGasVolumeDivisor(gas_volume_divisor_);
            config_provider_->setFrequency(frequency_);
            config_provider_->setAutoScanEnabled(auto_scan_);
            config_provider_->setReadingSchedule(reading_schedule_.c_str());
            config_provider_->setReadHourUTC(read_hour_);
            config_provider_->setReadMinuteUTC(read_minute_);
            config_provider_->setTimezoneOffsetMinutes(timezone_offset_);
            config_provider_->setAutoAlignReadingTime(auto_align_time_);
            config_provider_->setUseAutoAlignMidpoint(auto_align_midpoint_);
            config_provider_->setMaxRetries(max_retries_);
            config_provider_->setRetryCooldownMs(retry_cooldown_ms_);

            // Create time provider
            if (time_component_ != nullptr)
            {
                time_provider_ = new ESPHomeTimeProvider(time_component_);
            }
            else
            {
                ESP_LOGW(TAG, "No time component configured, some features may not work correctly");
                time_provider_ = new ESPHomeTimeProvider(nullptr);
            }

            // Create data publisher and link all sensors
            data_publisher_ = new ESPHomeDataPublisher();
            data_publisher_->set_volume_sensor(volume_sensor_);
            data_publisher_->set_battery_sensor(battery_sensor_);
            data_publisher_->set_counter_sensor(counter_sensor_);
            data_publisher_->set_rssi_sensor(rssi_sensor_);
            data_publisher_->set_rssi_percentage_sensor(rssi_percentage_sensor_);
            data_publisher_->set_lqi_sensor(lqi_sensor_);
            data_publisher_->set_lqi_percentage_sensor(lqi_percentage_sensor_);
            data_publisher_->set_time_start_sensor(time_start_sensor_);
            data_publisher_->set_time_end_sensor(time_end_sensor_);
            data_publisher_->set_total_attempts_sensor(total_attempts_sensor_);
            data_publisher_->set_successful_reads_sensor(successful_reads_sensor_);
            data_publisher_->set_failed_reads_sensor(failed_reads_sensor_);
            data_publisher_->set_status_sensor(status_sensor_);
            data_publisher_->set_error_sensor(error_sensor_);
            data_publisher_->set_radio_state_sensor(radio_state_sensor_);
            data_publisher_->set_timestamp_sensor(timestamp_sensor_);
            data_publisher_->set_active_reading_sensor(active_reading_sensor_);

            // Create meter reader with all adapters
            meter_reader_ = new MeterReader(config_provider_, time_provider_, data_publisher_);

            // Initialize the meter reader
            meter_reader_->begin();

            ESP_LOGCONFIG(TAG, "EverBlu Meter setup complete");
        }

        void EverbluMeterComponent::loop()
        {
            // Let the meter reader handle its periodic tasks
            if (meter_reader_ != nullptr)
            {
                meter_reader_->loop();
            }
        }

        void EverbluMeterComponent::update()
        {
            // This is called according to the update_interval
            // The meter reader handles its own scheduling via loop()
            // This method is here to satisfy ESPHome's PollingComponent interface
            // but the actual work happens in loop()
        }

        void EverbluMeterComponent::dump_config()
        {
            ESP_LOGCONFIG(TAG, "EverBlu Meter:");
            ESP_LOGCONFIG(TAG, "  Meter Year: %u", meter_year_);
            ESP_LOGCONFIG(TAG, "  Meter Serial: %u", meter_serial_);
            ESP_LOGCONFIG(TAG, "  Meter Type: %s", is_gas_ ? "Gas" : "Water");
            if (is_gas_)
            {
                ESP_LOGCONFIG(TAG, "  Gas Volume Divisor: %d", gas_volume_divisor_);
            }
            ESP_LOGCONFIG(TAG, "  Frequency: %.2f MHz", frequency_);
            ESP_LOGCONFIG(TAG, "  Auto Scan: %s", auto_scan_ ? "Enabled" : "Disabled");
            ESP_LOGCONFIG(TAG, "  Reading Schedule: %s", reading_schedule_.c_str());
            ESP_LOGCONFIG(TAG, "  Read Time: %02d:%02d", read_hour_, read_minute_);
            ESP_LOGCONFIG(TAG, "  Timezone Offset: %d", timezone_offset_);
            ESP_LOGCONFIG(TAG, "  Auto Align Time: %s", auto_align_time_ ? "Enabled" : "Disabled");
            ESP_LOGCONFIG(TAG, "  Auto Align Midpoint: %s", auto_align_midpoint_ ? "Enabled" : "Disabled");
            ESP_LOGCONFIG(TAG, "  Max Retries: %d", max_retries_);
            ESP_LOGCONFIG(TAG, "  Retry Cooldown: %lu ms", retry_cooldown_ms_);

            ESP_LOGCONFIG(TAG, "  Sensors:");
            LOG_SENSOR("    ", "Volume", volume_sensor_);
            LOG_SENSOR("    ", "Battery", battery_sensor_);
            LOG_SENSOR("    ", "Counter", counter_sensor_);
            LOG_SENSOR("    ", "RSSI", rssi_sensor_);
            LOG_SENSOR("    ", "RSSI Percentage", rssi_percentage_sensor_);
            LOG_SENSOR("    ", "LQI", lqi_sensor_);
            LOG_SENSOR("    ", "LQI Percentage", lqi_percentage_sensor_);
            LOG_SENSOR("    ", "Time Start", time_start_sensor_);
            LOG_SENSOR("    ", "Time End", time_end_sensor_);
            LOG_SENSOR("    ", "Total Attempts", total_attempts_sensor_);
            LOG_SENSOR("    ", "Successful Reads", successful_reads_sensor_);
            LOG_SENSOR("    ", "Failed Reads", failed_reads_sensor_);
            LOG_TEXT_SENSOR("    ", "Status", status_sensor_);
            LOG_TEXT_SENSOR("    ", "Error", error_sensor_);
            LOG_TEXT_SENSOR("    ", "Radio State", radio_state_sensor_);
            LOG_TEXT_SENSOR("    ", "Timestamp", timestamp_sensor_);
            LOG_BINARY_SENSOR("    ", "Active Reading", active_reading_sensor_);
        }

    } // namespace everblu_meter
} // namespace esphome
