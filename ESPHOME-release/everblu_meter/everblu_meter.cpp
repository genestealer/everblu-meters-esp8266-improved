/**
 * @file everblu_meter.cpp
 * @brief Implementation of ESPHome component for EverBlu Cyble Enhanced meters
 */

#include "everblu_meter.h"
#ifndef __INTELLISENSE__
#include "esphome/core/log.h"
#endif

// Include CC1101 header for SPI device setup
#include "cc1101.h"

#ifdef USE_ESP8266
#include <ESP8266WiFi.h>
#elif defined(USE_ESP32)
#include <WiFi.h>
#endif

namespace esphome
{
    namespace everblu_meter
    {

        static const char *const TAG = "everblu_meter";

        void EverbluMeterTriggerButton::press_action()
        {
            if (parent_ == nullptr)
            {
                ESP_LOGW(TAG, "Trigger button pressed but parent not set");
                return;
            }

            if (is_frequency_scan_)
            {
                parent_->request_frequency_scan();
            }
            else if (is_reset_frequency_)
            {
                parent_->request_reset_frequency();
            }
            else
            {
                parent_->request_manual_read();
            }
        }

        void EverbluMeterComponent::setup()
        {
            ESP_LOGCONFIG(TAG, "Setting up EverBlu Meter...");

            // Initialize ESPHome SPI device before any SPI transactions
            this->spi_setup();

            // Reset initialization state on every setup (after reboot/OTA)
            meter_initialized_ = false;
            wifi_ready_at_ = 0;

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
            data_publisher_->set_frequency_offset_sensor(frequency_offset_sensor_);
            data_publisher_->set_tuned_frequency_sensor(tuned_frequency_sensor_);
            data_publisher_->set_status_sensor(status_sensor_);
            data_publisher_->set_error_sensor(error_sensor_);
            data_publisher_->set_radio_state_sensor(radio_state_sensor_);
            data_publisher_->set_timestamp_sensor(timestamp_sensor_);
            data_publisher_->set_history_sensor(history_sensor_);
            data_publisher_->set_meter_serial_sensor(meter_serial_sensor_);
            data_publisher_->set_meter_year_sensor(meter_year_sensor_);
            data_publisher_->set_reading_schedule_sensor(reading_schedule_sensor_);
            data_publisher_->set_reading_time_utc_sensor(reading_time_utc_sensor_);
            data_publisher_->set_active_reading_sensor(active_reading_sensor_);
            data_publisher_->set_radio_connected_sensor(radio_connected_sensor_);

            // Quick diagnostic: report how many sensors were linked
            int numeric = 0;
            numeric += (volume_sensor_ != nullptr);
            numeric += (battery_sensor_ != nullptr);
            numeric += (counter_sensor_ != nullptr);
            numeric += (rssi_sensor_ != nullptr);
            numeric += (rssi_percentage_sensor_ != nullptr);
            numeric += (lqi_sensor_ != nullptr);
            numeric += (lqi_percentage_sensor_ != nullptr);
            numeric += (time_start_sensor_ != nullptr);
            numeric += (time_end_sensor_ != nullptr);
            numeric += (total_attempts_sensor_ != nullptr);
            numeric += (successful_reads_sensor_ != nullptr);
            numeric += (failed_reads_sensor_ != nullptr);
            numeric += (frequency_offset_sensor_ != nullptr);

            int texts = 0;
            texts += (status_sensor_ != nullptr);
            texts += (error_sensor_ != nullptr);
            texts += (radio_state_sensor_ != nullptr);
            texts += (timestamp_sensor_ != nullptr);
            texts += (history_sensor_ != nullptr);
            texts += (meter_serial_sensor_ != nullptr);
            texts += (meter_year_sensor_ != nullptr);
            texts += (reading_schedule_sensor_ != nullptr);
            texts += (reading_time_utc_sensor_ != nullptr);

            int binaries = 0;
            binaries += (active_reading_sensor_ != nullptr);
            binaries += (radio_connected_sensor_ != nullptr);

            ESP_LOGD(TAG, "Linked sensors -> numeric: %d, text: %d, binary: %d", numeric, texts, binaries);

            // Initialize CC1101 SPI device before creating meter reader
            auto *spi_device = static_cast<spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST,
                                                          spi::CLOCK_POLARITY_LOW,
                                                          spi::CLOCK_PHASE_LEADING,
                                                          spi::DATA_RATE_1MHZ> *>(this);
            cc1101_set_spi_device(static_cast<void *>(spi_device), -1);
            ESP_LOGD(TAG, "CC1101 SPI device configured");

            // Configure GDO0 pin for CC1101
            if (gdo0_pin_ >= 0)
            {
                cc1101_set_gdo0_pin(gdo0_pin_);
                ESP_LOGD(TAG, "CC1101 GDO0 pin configured: %d", gdo0_pin_);
            }
            else
            {
                ESP_LOGE(TAG, "GDO0 pin not configured for CC1101!");
            }

            // Create meter reader with all adapters (but don't initialize yet)
            meter_reader_ = new MeterReader(config_provider_, time_provider_, data_publisher_);

            ESP_LOGCONFIG(TAG, "EverBlu Meter setup complete (meter initialization deferred until WiFi connected)");
        }

        void EverbluMeterComponent::republish_initial_states()
        {
            if (!meter_reader_ || !meter_initialized_)
            {
                ESP_LOGW(TAG, "Cannot republish states: meter_initialized=%d, meter_reader=%p", meter_initialized_, meter_reader_);
                return;
            }

            ESP_LOGD(TAG, "Republishing initial states for Home Assistant...");

            // Publish static configuration values that are known at boot
            // These never change and should always be available
            if (data_publisher_)
            {
                // Publish meter configuration (serial, year, schedule, reading time)
                char reading_time_buf[6];
                snprintf(reading_time_buf, sizeof(reading_time_buf), "%02d:%02d", read_hour_, read_minute_);
                data_publisher_->publishMeterSettings(
                    meter_year_,
                    meter_serial_,
                    reading_schedule_.c_str(),
                    reading_time_buf,
                    frequency_);

                // Publish initial status states
                // Preserve radio state after init failure - don't overwrite "unavailable" with "Idle"
                if (meter_reader_->isRadioConnected())
                {
                    ESP_LOGD(TAG, "Publishing: radio state=Idle");
                    data_publisher_->publishRadioState("Idle");
                }
                else
                {
                    ESP_LOGD(TAG, "Skipping radio state publish - radio init failed, preserving 'unavailable' state");
                }

                ESP_LOGD(TAG, "Publishing: status=Ready");
                data_publisher_->publishStatusMessage("Ready");

                ESP_LOGD(TAG, "Publishing: error=None");
                data_publisher_->publishError("None");

                ESP_LOGD(TAG, "Publishing: active_reading=false");
                data_publisher_->publishActiveReading(false);

                ESP_LOGD(TAG, "Republish complete - meter readings will be available after first successful read");
            }
            else
            {
                ESP_LOGW(TAG, "Cannot republish: data_publisher is null");
            }
        }

        void EverbluMeterComponent::loop()
        {
            // Let the meter reader handle its periodic tasks
            if (meter_reader_ != nullptr)
            {
#ifdef USE_API
                // Initialize meter reader when Home Assistant connects (ensures safe boot sequence)
                // This is better than WiFi-only check because API connection is more stable
                if (esphome::api::global_api_server != nullptr)
                {
                    bool is_ha_connected = esphome::api::global_api_server->is_connected(true);

                    // Initialize meter reader if not already done
                    if (!meter_initialized_ && is_ha_connected)
                    {
                        ESP_LOGI(TAG, "Home Assistant connected, initializing meter reader...");
                        meter_reader_->begin();

                        // Set adaptive frequency tracking threshold
                        FrequencyManager::setAdaptiveThreshold(adaptive_threshold_);

                        meter_initialized_ = true;
                        ESP_LOGI(TAG, "Meter reader initialized successfully");
                    }

                    // Republish initial states when HA connects (if already initialized)
                    // (initial publishes may happen before HA is ready to receive)
                    // Use is_connected(true) to check for state subscription (HA actively monitoring)
                    if (meter_initialized_ && is_ha_connected && !last_api_client_count_)
                    {
                        ESP_LOGI(TAG, "Home Assistant connected, republishing initial states...");
                        republish_initial_states();
                        if (meter_reader_ != nullptr)
                        {
                            meter_reader_->setHAConnected(true);
                        }
                        last_api_client_count_ = true;
                    }
                    else if (!is_ha_connected)
                    {
                        if (meter_reader_ != nullptr)
                        {
                            meter_reader_->setHAConnected(false);
                        }
                        last_api_client_count_ = false;
                    }
                }
#endif

                // Optionally kick off a first read once time is synced so users see data without waiting
                // Controlled by initial_read_on_boot_ (default: disabled to avoid boot-time blocking when meter is absent)
                if (initial_read_on_boot_ && !initial_read_triggered_ && time_provider_ != nullptr && time_provider_->isTimeSynced())
                {
                    initial_read_triggered_ = true;
                    meter_reader_->triggerReading(false);
                }

                meter_reader_->loop();
            }
        }

        void EverbluMeterComponent::request_manual_read()
        {
            if (meter_reader_ == nullptr)
            {
                ESP_LOGW(TAG, "Manual read ignored: meter reader not ready");
                return;
            }

            ESP_LOGI(TAG, "Manual read requested via button");
            meter_reader_->triggerReading(false);
        }

        void EverbluMeterComponent::request_frequency_scan()
        {
            if (meter_reader_ == nullptr)
            {
                ESP_LOGW(TAG, "Frequency scan ignored: meter reader not ready");
                return;
            }

            ESP_LOGI(TAG, "Frequency scan requested via button");
            meter_reader_->performFrequencyScan(false);
        }

        void EverbluMeterComponent::request_reset_frequency()
        {
            if (meter_reader_ == nullptr)
            {
                ESP_LOGW(TAG, "Reset frequency ignored: meter reader not ready");
                return;
            }

            ESP_LOGI(TAG, "Reset frequency offset requested via button");
            meter_reader_->resetFrequencyOffset();
            ESP_LOGI(TAG, "Frequency offset reset to 0.000 kHz");
        }

        void EverbluMeterComponent::update()
        {
            // The meter reader handles its own scheduling via loop().
            // This method is kept for potential future use with update_interval.
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
            ESP_LOGCONFIG(TAG, "  Initial Read On Boot: %s", initial_read_on_boot_ ? "Enabled" : "Disabled");

            ESP_LOGCONFIG(TAG, "  Sensors:");
            LOG_SENSOR("    ", "Volume", volume_sensor_);
            LOG_SENSOR("    ", "Battery", battery_sensor_);
            LOG_SENSOR("    ", "Counter", counter_sensor_);
            LOG_SENSOR("    ", "RSSI", rssi_sensor_);
            LOG_SENSOR("    ", "RSSI Percentage", rssi_percentage_sensor_);
            LOG_SENSOR("    ", "LQI", lqi_sensor_);
            LOG_SENSOR("    ", "LQI Percentage", lqi_percentage_sensor_);
            LOG_TEXT_SENSOR("    ", "Time Start", time_start_sensor_);
            LOG_TEXT_SENSOR("    ", "Time End", time_end_sensor_);
            LOG_SENSOR("    ", "Total Attempts", total_attempts_sensor_);
            LOG_SENSOR("    ", "Successful Reads", successful_reads_sensor_);
            LOG_SENSOR("    ", "Failed Reads", failed_reads_sensor_);
            LOG_SENSOR("    ", "Frequency Offset", frequency_offset_sensor_);
            LOG_TEXT_SENSOR("    ", "Status", status_sensor_);
            LOG_TEXT_SENSOR("    ", "Error", error_sensor_);
            LOG_TEXT_SENSOR("    ", "Radio State", radio_state_sensor_);
            LOG_TEXT_SENSOR("    ", "Timestamp", timestamp_sensor_);
            LOG_TEXT_SENSOR("    ", "History", history_sensor_);
            LOG_BINARY_SENSOR("    ", "Active Reading", active_reading_sensor_);
            LOG_BINARY_SENSOR("    ", "Radio Connected", radio_connected_sensor_);
        }

    } // namespace everblu_meter
} // namespace esphome
