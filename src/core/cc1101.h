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
/**
 * @brief Configure CC1101 to use ESPHome SPI device
 *
 * Must be called before cc1101_init() when compiling with ESPHome.
 * Provides the SPI device used for CC1101 communication.
 *
 * @param device Pointer to ESPHome SPIDevice instance (as void* for generic handling)
 */
void cc1101_set_spi_device(void *device);

/**
 * @brief Set GDO0 pin for ESPHome mode
 *
 * Must be called before cc1101_init() when compiling with ESPHome.
 *
 * @param gdo0_pin GPIO pin number for GDO0 interrupt signal
 */
void cc1101_set_gdo0_pin(int gdo0_pin);

/**
 * @brief Set GDO2 pin for ESPHome mode (optional)
 *
 * When configured, GDO2 is used as a hardware FIFO threshold signal whose
 * meaning is switched dynamically per phase via IOCFG2:
 *   - TX phase (IOCFG2 = 0x02): asserts HIGH when the TX FIFO is at/above the
 *     threshold, replacing SPI-based TXBYTES polling in the TX feeding loop and
 *     preventing TXFIFO_UNDERFLOW under scheduler load.
 *   - RX phase (IOCFG2 = 0x01): asserts HIGH when the RX FIFO reaches the
 *     threshold OR at end-of-packet, letting the RX drain loop skip unnecessary
 *     RXBYTES SPI reads while still draining the FIFO promptly.
 * If not called (or called with -1), the driver falls back to SPI polling
 * (CC1101_status_FIFO_FreeByte + delay on TX, blind RXBYTES polling on RX).
 *
 * Wiring: connect CC1101 GDO2 to any free GPIO that is not used by the SPI bus
 * or GDO0. Leaving GDO2 unwired is fully supported via the SPI-polling fallback.
 *
 * @param gdo2_pin GPIO pin number connected to CC1101 GDO2, or -1 to disable
 */
void cc1101_set_gdo2_pin(int gdo2_pin);

/**
 * @brief Set the front-end RX input attenuation level (ESPHome mode).
 *
 * Must be called before cc1101_init() when compiling with ESPHome.
 * Limits the CC1101 LNA gain via the AGCCTRL2 MAX_LNA_GAIN field to prevent
 * front-end saturation when the device is permanently mounted close to the meter.
 *
 * @param db Attenuation in dB. Accepted values: 0 (default), 6, 12, 18.
 *           Values are rounded down to the nearest supported step.
 *           Actual hardware reduction is approximate (6.1 / 11.5 / 17.1 dB).
 */
void cc1101_set_rx_attenuation(int db);
#endif

/**
 * @brief Number of GDO2 FIFO-threshold faults observed (stuck-HIGH timeouts + failed self-test).
 *
 * Lifetime (monotonic) diagnostic counter incremented from two sources:
 *   - the boot-time GDO2 wiring self-test in cc1101_init() when GDO2 does not toggle
 *     LOW->HIGH across a known empty->filled TX FIFO transition; and
 *   - the runtime TX interrogation-frame gate, when GDO2 never indicates the FIFO has
 *     drained below threshold within the safety window.
 * A non-zero, growing value strongly indicates a miswired / wrong-GPIO / disconnected
 * GDO2 rather than an RF or meter problem.
 *
 * @return Cumulative count since boot. Always 0 when GDO2 is not configured.
 */
uint32_t cc1101_get_gdo2_timeout_count(void);

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
  int lqi;                // Link Quality Indicator (0-127, lower is better; CRC_OK bit masked)
  int8_t freqest;         // Frequency offset estimate from CC1101 for adaptive tracking
  uint32_t history[13];   // Monthly historical readings (13 months), index 0 = oldest, 12 = most recent
  bool history_available; // True if historical data was successfully extracted
  char meter_time[20];    // Meter real-time clock "YYYY-MM-DD HH:MM:SS" (empty if not decoded)
  char meter_type[12];    // Meter type/identifier ASCII string, e.g. "133290AL02" (empty if not decoded)
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

/**
 * @brief Read data from a specific meter identity
 *
 * Same as get_meter_data(), but uses explicit meter identification instead of
 * compile-time defines. This is required for ESPHome multi-instance support.
 *
 * @param meter_year Last two digits of meter production year
 * @param meter_serial Meter serial number
 * @return tmeter_data structure containing all extracted meter data
 */
struct tmeter_data get_meter_data_for_meter(uint8_t meter_year, uint32_t meter_serial);

#endif // __CC1101_H__
