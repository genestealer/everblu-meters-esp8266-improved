/**
 * @file cc1101.h
 * @brief CC1101 radio driver for Everblu Cyble water/gas meter communication
 *
 * This header defines the interface for communicating with Everblu Cyble
 * water and gas meters using the CC1101 sub-GHz radio transceiver and the RADIAN protocol.
 * Supports adaptive frequency tracking and historical data extraction.
 */

#ifndef __CC1101_H__
#define __CC1101_H__

#include <stdint.h>
#include <stdbool.h>
#ifdef USE_ESPHOME
// Forward declaration for ESPHome SPI device
namespace esphome { namespace spi { class SPIDevice; } }

/**
 * @brief Configure CC1101 to use ESPHome SPI device
 *
 * Must be called before cc1101_init() when compiling with ESPHome.
 * Provides the SPI device and CS pin for CC1101 communication.
 *
 * @param device Pointer to ESPHome SPIDevice instance
 * @param cs_pin GPIO pin number for chip select
 */
void cc1101_set_spi_device(esphome::spi::SPIDevice<> *device, int cs_pin);
#endif


/**
 * @struct tmeter_data
 * @brief Meter data structure containing current readings and metadata
 *
 * Contains all data extracted from an Everblu Cyble water/gas meter reading,
 * including current consumption, historical data, signal quality metrics,
 * and battery information.
 */
struct tmeter_data
{
  int volume;             // Current consumption reading in liters (water) or cubic meters (gas)
  int reads_counter;      // Number of times meter has been read (wraps around 255→1)
  int battery_left;       // Estimated battery life remaining in months
  int time_start;         // Reading window start time (24-hour format, e.g., 8 = 8am)
  int time_end;           // Reading window end time (24-hour format, e.g., 18 = 6pm)
  int rssi;               // Radio Signal Strength Indicator (raw value)
  int rssi_dbm;           // RSSI converted to dBm
  int lqi;                // Link Quality Indicator (0-255, higher is better)
  int8_t freqest;         // Frequency offset estimate from CC1101 for adaptive tracking
  uint32_t history[13];   // Monthly historical readings (13 months), index 0 = oldest, 12 = most recent
  bool history_available; // True if historical data was successfully extracted
};

/**
 * @brief Set the CC1101 radio frequency in MHz
 *
 * Configures the CC1101 transceiver to operate at the specified frequency.
 * Used for fine-tuning frequency to match meter transmissions or for
 * adaptive frequency tracking.
 *
 * @param mhz Frequency in MHz (typically around 433.82 MHz for Cyble meters)
 */
void setMHZ(float mhz);

/**
 * @brief Initialize the CC1101 radio transceiver
 *
 * Performs complete initialization of the CC1101 radio including:
 * - SPI communication setup
 * - Register configuration for RADIAN protocol
 * - Frequency calibration
 * - Power amplifier configuration
 *
 * @param freq Initial operating frequency in MHz
 * @return true if initialization succeeded, false on failure
 */
bool cc1101_init(float freq);

/**
 * @brief Put CC1101 radio into receive (RX) mode
 *
 * Configures the radio to listen for incoming meter transmissions.
 * Must be called after initialization or frequency changes to enable reception.
 */
void cc1101_rec_mode(void);

/**
 * @brief Read data from Everblu Cyble water/gas meter
 *
 * Performs a complete read cycle:
 * 1. Transmits RADIAN protocol request frame to meter
 * 2. Waits for meter response
 * 3. Decodes received data including current reading and history
 * 4. Validates CRC and data integrity
 * 5. Extracts signal quality metrics (RSSI, LQI, frequency offset)
 *
 * This is a blocking operation that may take several seconds to complete.
 *
 * @return tmeter_data structure containing all extracted meter data
 */
struct tmeter_data get_meter_data(void);

#endif // __CC1101_H__