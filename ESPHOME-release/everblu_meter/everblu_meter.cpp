/**
 * @file everblu_meter.cpp
 * @brief Implementation of ESPHome component for EverBlu Cyble Enhanced meters
 */

#include "everblu_meter.h"
#ifndef __INTELLISENSE__
#include "esphome/core/log.h"
#endif

#include "version.h"

// Include CC1101 header for SPI device setup
#include "cc1101.h"
namespace esphome {
namespace everblu_meter {

static const char *const TAG = "everblu_meter";

void EverbluMeterTriggerButton::press_action() {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "Trigger button pressed but parent not set");
    return;
  }

  if (this->is_frequency_scan_) {
    this->parent_->request_frequency_scan();
  } else if (this->is_reset_frequency_) {
    this->parent_->request_reset_frequency();
  } else {
    this->parent_->request_manual_read();
  }
}

void EverbluMeterComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up");

  // Initialize ESPHome SPI device before any SPI transactions
  this->spi_setup();

  // Reset initialization state on every setup (after reboot/OTA)
  this->meter_initialized_ = false;
  this->wifi_ready_at_ = 0;

  // Create config provider and configure it
  this->config_provider_ = new ESPHomeConfigProvider();
  this->config_provider_->setMeterYear(this->meter_year_);
  this->config_provider_->setMeterSerial(this->meter_serial_);
  this->config_provider_->setMeterType(this->is_gas_);
  this->config_provider_->setGasVolumeDivisor(this->gas_volume_divisor_);
  this->config_provider_->setFrequency(this->frequency_);
  this->config_provider_->setAutoScanEnabled(this->auto_scan_);
  this->config_provider_->setReadingSchedule(this->reading_schedule_.c_str());
  this->config_provider_->setReadHourUTC(this->read_hour_);
  this->config_provider_->setReadMinuteUTC(this->read_minute_);
  this->config_provider_->setTimezoneOffsetMinutes(this->timezone_offset_);
  this->config_provider_->setAutoAlignReadingTime(this->auto_align_time_);
  this->config_provider_->setUseAutoAlignMidpoint(this->auto_align_midpoint_);
  this->config_provider_->setMaxRetries(this->max_retries_);
  this->config_provider_->setRetryCooldownMs(this->retry_cooldown_ms_);

  // Create time provider
  if (this->time_component_ != nullptr) {
    this->time_provider_ = new ESPHomeTimeProvider(this->time_component_);
  } else {
    ESP_LOGW(TAG, "No time component configured; some features unavailable");
    this->time_provider_ = new ESPHomeTimeProvider(nullptr);
  }

  // Create data publisher and link all sensors
  this->data_publisher_ = new ESPHomeDataPublisher();
  this->data_publisher_->set_volume_sensor(this->volume_sensor_);
  this->data_publisher_->set_battery_sensor(this->battery_sensor_);
  this->data_publisher_->set_counter_sensor(this->counter_sensor_);
  this->data_publisher_->set_rssi_sensor(this->rssi_sensor_);
  this->data_publisher_->set_rssi_percentage_sensor(this->rssi_percentage_sensor_);
  this->data_publisher_->set_lqi_sensor(this->lqi_sensor_);
  this->data_publisher_->set_lqi_percentage_sensor(this->lqi_percentage_sensor_);
  this->data_publisher_->set_time_start_sensor(this->time_start_sensor_);
  this->data_publisher_->set_time_end_sensor(this->time_end_sensor_);
  this->data_publisher_->set_total_attempts_sensor(this->total_attempts_sensor_);
  this->data_publisher_->set_successful_reads_sensor(this->successful_reads_sensor_);
  this->data_publisher_->set_failed_reads_sensor(this->failed_reads_sensor_);
  this->data_publisher_->set_frequency_offset_sensor(this->frequency_offset_sensor_);
  this->data_publisher_->set_tuned_frequency_sensor(this->tuned_frequency_sensor_);
  this->data_publisher_->set_frequency_estimate_sensor(this->frequency_estimate_sensor_);
  this->data_publisher_->set_status_sensor(this->status_sensor_);
  this->data_publisher_->set_error_sensor(this->error_sensor_);
  this->data_publisher_->set_radio_state_sensor(this->radio_state_sensor_);
  this->data_publisher_->set_timestamp_sensor(this->timestamp_sensor_);
  this->data_publisher_->set_history_sensor(this->history_sensor_);
  this->data_publisher_->set_version_sensor(this->version_sensor_);
  this->data_publisher_->set_meter_serial_sensor(this->meter_serial_sensor_);
  this->data_publisher_->set_meter_year_sensor(this->meter_year_sensor_);
  this->data_publisher_->set_reading_schedule_sensor(this->reading_schedule_sensor_);
  this->data_publisher_->set_reading_time_utc_sensor(this->reading_time_utc_sensor_);
  this->data_publisher_->set_active_reading_sensor(this->active_reading_sensor_);
  this->data_publisher_->set_radio_connected_sensor(this->radio_connected_sensor_);

  // Quick diagnostic: report how many sensors were linked
  int numeric = 0;
  numeric += (this->volume_sensor_ != nullptr);
  numeric += (this->battery_sensor_ != nullptr);
  numeric += (this->counter_sensor_ != nullptr);
  numeric += (this->rssi_sensor_ != nullptr);
  numeric += (this->rssi_percentage_sensor_ != nullptr);
  numeric += (this->lqi_sensor_ != nullptr);
  numeric += (this->lqi_percentage_sensor_ != nullptr);
  numeric += (this->time_start_sensor_ != nullptr);
  numeric += (this->time_end_sensor_ != nullptr);
  numeric += (this->total_attempts_sensor_ != nullptr);
  numeric += (this->successful_reads_sensor_ != nullptr);
  numeric += (this->failed_reads_sensor_ != nullptr);
  numeric += (this->frequency_offset_sensor_ != nullptr);

  int texts = 0;
  texts += (this->status_sensor_ != nullptr);
  texts += (this->error_sensor_ != nullptr);
  texts += (this->radio_state_sensor_ != nullptr);
  texts += (this->timestamp_sensor_ != nullptr);
  texts += (this->history_sensor_ != nullptr);
  texts += (this->version_sensor_ != nullptr);
  texts += (this->meter_serial_sensor_ != nullptr);
  texts += (this->meter_year_sensor_ != nullptr);
  texts += (this->reading_schedule_sensor_ != nullptr);
  texts += (this->reading_time_utc_sensor_ != nullptr);

  int binaries = 0;
  binaries += (this->active_reading_sensor_ != nullptr);
  binaries += (this->radio_connected_sensor_ != nullptr);

  ESP_LOGD(TAG, "Linked sensors -> numeric: %d, text: %d, binary: %d", numeric, texts, binaries);

  // Initialize CC1101 context before creating meter reader
  this->apply_radio_context();

  // Create meter reader with all adapters (but don't initialize yet)
  this->meter_reader_ = new MeterReader(this->config_provider_, this->time_provider_, this->data_publisher_);

  ESP_LOGCONFIG(TAG, "Setup complete; meter init deferred until connected");
}

void EverbluMeterComponent::republish_initial_states() {
  if (!this->meter_reader_ || !this->meter_initialized_) {
    ESP_LOGW(TAG, "Cannot republish states: meter_initialized=%d, meter_reader=%p", this->meter_initialized_,
             this->meter_reader_);
    return;
  }

  ESP_LOGD(TAG, "Republishing initial states");

  // Publish static configuration values that are known at boot
  // These never change and should always be available
  if (this->data_publisher_) {
    // Publish meter configuration (serial, year, schedule, reading time)
    char reading_time_buf[6];
    snprintf(reading_time_buf, sizeof(reading_time_buf), "%02d:%02d", this->read_hour_, this->read_minute_);
    this->data_publisher_->publishMeterSettings(this->meter_year_, this->meter_serial_, this->reading_schedule_.c_str(),
                                                reading_time_buf, this->frequency_);

    // Publish initial status states
    // Preserve radio state after init failure - don't overwrite "unavailable" with "Idle"
    if (this->meter_reader_->isRadioConnected()) {
      ESP_LOGD(TAG, "Publishing: radio state=Idle");
      this->data_publisher_->publishRadioState("Idle");
    } else {
      ESP_LOGD(TAG, "Skipping radio state publish - radio init failed, preserving 'unavailable' state");
    }

    if (this->meter_reader_->isRadioConnected()) {
      ESP_LOGD(TAG, "Publishing: status=Ready");
      this->data_publisher_->publishStatusMessage("Ready");

      ESP_LOGD(TAG, "Publishing: error=None");
      this->data_publisher_->publishError("None");
    } else {
      ESP_LOGD(TAG, "Skipping Ready/None publish - preserving radio init error state");
    }

    ESP_LOGD(TAG, "Publishing: firmware version=%s", EVERBLU_FW_VERSION);
    this->data_publisher_->publishFirmwareVersion(EVERBLU_FW_VERSION);

    ESP_LOGD(TAG, "Publishing: active_reading=false");
    this->data_publisher_->publishActiveReading(false);

    ESP_LOGD(TAG, "Republish complete - meter readings will be available after first successful read");
  } else {
    ESP_LOGW(TAG, "Cannot republish: data_publisher is null");
  }
}

void EverbluMeterComponent::loop() {
  // Let the meter reader handle its periodic tasks
  if (this->meter_reader_ != nullptr) {
    // Ensure this instance's SPI and GDO0 settings are active before any radio operations.
    this->apply_radio_context();

#ifdef USE_API
    // Initialize meter reader when Home Assistant connects (ensures safe boot sequence)
    // This is better than WiFi-only check because API connection is more stable
    if (esphome::api::global_api_server != nullptr) {
      // Use is_connected_with_state_subscription() to check for state subscription (HA is actively monitoring)
      bool is_ha_connected = esphome::api::global_api_server->is_connected_with_state_subscription();

      // Initialize meter reader if not already done
      if (!this->meter_initialized_ && is_ha_connected) {
        ESP_LOGI(TAG, "Home Assistant connected, initializing meter reader");
        this->apply_radio_context();
        this->meter_reader_->begin();

        // Set adaptive frequency tracking threshold
        FrequencyManager::setAdaptiveThreshold(this->adaptive_threshold_);

        this->meter_initialized_ = true;
        ESP_LOGI(TAG, "Meter reader initialized successfully");
      }

      // Republish initial states when HA connects (if already initialized)
      // (initial publishes may happen before HA is ready to receive)
      if (this->meter_initialized_ && is_ha_connected && !this->last_api_client_count_) {
        ESP_LOGI(TAG, "Home Assistant connected, republishing initial states");
        this->republish_initial_states();
        if (this->meter_reader_ != nullptr) {
          this->meter_reader_->setHAConnected(true);
        }
        this->last_api_client_count_ = true;
      } else if (!is_ha_connected) {
        if (this->meter_reader_ != nullptr) {
          this->meter_reader_->setHAConnected(false);
        }
        this->last_api_client_count_ = false;
      }
    }
#endif

    // Optionally kick off a first read once time is synced so users see data without waiting
    // Controlled by initial_read_on_boot_ (default: disabled to avoid boot-time blocking when meter is absent)
    if (this->initial_read_on_boot_ && !this->initial_read_triggered_ && this->time_provider_ != nullptr &&
        this->time_provider_->isTimeSynced()) {
      this->initial_read_triggered_ = true;
      this->meter_reader_->triggerReading(false);
    }

    this->meter_reader_->loop();
  }
}

void EverbluMeterComponent::request_manual_read() {
  if (this->meter_reader_ == nullptr) {
    ESP_LOGW(TAG, "Manual read ignored: meter reader not ready");
    return;
  }

  ESP_LOGI(TAG, "Manual read requested via button");
  this->apply_radio_context();
  this->meter_reader_->triggerReading(false);
}

void EverbluMeterComponent::request_frequency_scan() {
  if (this->meter_reader_ == nullptr) {
    ESP_LOGW(TAG, "Frequency scan ignored: meter reader not ready");
    return;
  }

  ESP_LOGI(TAG, "Frequency scan requested via button");
  this->apply_radio_context();
  this->meter_reader_->performFrequencyScan(false);
}

void EverbluMeterComponent::request_reset_frequency() {
  if (this->meter_reader_ == nullptr) {
    ESP_LOGW(TAG, "Reset frequency ignored: meter reader not ready");
    return;
  }

  ESP_LOGI(TAG, "Reset frequency offset requested via button");
  this->apply_radio_context();
  this->meter_reader_->resetFrequencyOffset();
  ESP_LOGI(TAG, "Frequency offset reset to 0.000 kHz");
}

void EverbluMeterComponent::apply_radio_context() {
  auto *spi_device = static_cast<spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                                spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_1MHZ> *>(this);
  cc1101_set_spi_device(static_cast<void *>(spi_device));

  if (this->gdo0_pin_ != nullptr) {
    cc1101_set_gdo0_pin(this->gdo0_pin_->get_pin());
  } else {
    // Log error only once to avoid flooding the log
    if (!this->gdo0_error_logged_) {
      ESP_LOGE(TAG, "GDO0 pin not configured for CC1101!");
      this->gdo0_error_logged_ = true;
    }
  }

  // GDO2 is optional: when wired and configured, it drives hardware-assisted
  // TX FIFO threshold detection instead of SPI TXBYTES polling.
  if (this->gdo2_pin_ != nullptr) {
    cc1101_set_gdo2_pin(this->gdo2_pin_->get_pin());
  } else {
    cc1101_set_gdo2_pin(-1);  // Ensure fallback mode on re-entry
  }
}

void EverbluMeterComponent::update() {
  // The meter reader handles its own scheduling via loop().
  // This method is kept for potential future use with update_interval.
}

void EverbluMeterComponent::dump_config() {
  ESP_LOGCONFIG(TAG,
                "EverBlu Meter:\n"
                "  Meter Code: %s (year=%u, serial=%lu)\n"
                "  Meter Type: %s\n"
                "  Component Version: %s\n"
                "  Frequency: %.2f MHz\n"
                "  Auto Scan: %s\n"
                "  Reading Schedule: %s\n"
                "  Read Time: %02d:%02d\n"
                "  Timezone Offset: %d\n"
                "  Auto Align Time: %s\n"
                "  Auto Align Midpoint: %s\n"
                "  Max Retries: %d\n"
                "  Retry Cooldown: %lu ms\n"
                "  Initial Read On Boot: %s\n"
                "  GDO0 Pin: %s\n"
                "  GDO2 Pin: %s",
                this->meter_code_.c_str(), this->meter_year_, (unsigned long) this->meter_serial_,
                this->is_gas_ ? "Gas" : "Water", EVERBLU_FW_VERSION, this->frequency_,
                this->auto_scan_ ? "Enabled" : "Disabled", this->reading_schedule_.c_str(), this->read_hour_,
                this->read_minute_, this->timezone_offset_, this->auto_align_time_ ? "Enabled" : "Disabled",
                this->auto_align_midpoint_ ? "Enabled" : "Disabled", this->max_retries_, this->retry_cooldown_ms_,
                this->initial_read_on_boot_ ? "Enabled" : "Disabled",
                this->gdo0_pin_ != nullptr ? "configured" : "NOT configured (error)",
                this->gdo2_pin_ != nullptr ? "configured (HW FIFO threshold, TX+RX dynamic)"
                                           : "disabled (legacy SPI polling fallback)");
  if (this->is_gas_)
    ESP_LOGCONFIG(TAG, "  Gas Volume Divisor: %d", this->gas_volume_divisor_);

  ESP_LOGCONFIG(TAG, "  Sensors:");
  LOG_SENSOR("    ", "Volume", this->volume_sensor_);
  LOG_SENSOR("    ", "Battery", this->battery_sensor_);
  LOG_SENSOR("    ", "Counter", this->counter_sensor_);
  LOG_SENSOR("    ", "RSSI", this->rssi_sensor_);
  LOG_SENSOR("    ", "RSSI Percentage", this->rssi_percentage_sensor_);
  LOG_SENSOR("    ", "LQI", this->lqi_sensor_);
  LOG_SENSOR("    ", "LQI Percentage", this->lqi_percentage_sensor_);
  LOG_TEXT_SENSOR("    ", "Time Start", this->time_start_sensor_);
  LOG_TEXT_SENSOR("    ", "Time End", this->time_end_sensor_);
  LOG_SENSOR("    ", "Total Attempts", this->total_attempts_sensor_);
  LOG_SENSOR("    ", "Successful Reads", this->successful_reads_sensor_);
  LOG_SENSOR("    ", "Failed Reads", this->failed_reads_sensor_);
  LOG_SENSOR("    ", "Frequency Offset", this->frequency_offset_sensor_);
  LOG_SENSOR("    ", "Frequency Estimate", this->frequency_estimate_sensor_);
  LOG_TEXT_SENSOR("    ", "Status", this->status_sensor_);
  LOG_TEXT_SENSOR("    ", "Error", this->error_sensor_);
  LOG_TEXT_SENSOR("    ", "Radio State", this->radio_state_sensor_);
  LOG_TEXT_SENSOR("    ", "Timestamp", this->timestamp_sensor_);
  LOG_TEXT_SENSOR("    ", "History", this->history_sensor_);
  LOG_BINARY_SENSOR("    ", "Active Reading", this->active_reading_sensor_);
  LOG_BINARY_SENSOR("    ", "Radio Connected", this->radio_connected_sensor_);
}

}  // namespace everblu_meter
}  // namespace esphome
