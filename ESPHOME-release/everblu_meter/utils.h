/**
 * @file utils.h
 * @brief Utility functions for RADIAN protocol and debugging
 *
 * IMPORTANT LICENSING NOTICE:
 * The RADIAN protocol implementation (radian_trx SW) shall not be distributed
 * nor used for commercial products. It is exposed only to demonstrate CC1101
 * capability to read water meter indexes. There is no warranty on radian_trx SW.
 */

#include <Arduino.h>

/**
 * @brief Display buffer contents in hexadecimal format (multi-line)
 * @param buffer Pointer to data buffer
 * @param len Length of buffer in bytes
 */
void show_in_hex(const uint8_t *buffer, size_t len);

/**
 * @brief Display buffer contents as hexadecimal array notation
 * @param buffer Pointer to data buffer
 * @param len Length of buffer in bytes
 */
void show_in_hex_array(const uint8_t *buffer, size_t len);

/**
 * @brief Display buffer contents in hexadecimal format (single line)
 * @param buffer Pointer to data buffer
 * @param len Length of buffer in bytes
 */
void show_in_hex_one_line(const uint8_t *buffer, size_t len);

/**
 * @brief Display buffer contents in hexadecimal format for GET requests
 * @param buffer Pointer to data buffer
 * @param len Length of buffer in bytes
 */
void show_in_hex_one_line_GET(const uint8_t *buffer, size_t len);

/**
 * @brief Display buffer contents in binary format
 * @param buffer Pointer to data buffer
 * @param len Length of buffer in bytes
 */
void show_in_bin(const uint8_t *buffer, size_t len);

/**
 * @brief Conditional debug output with printf-style formatting
 * @param l_flag Boolean flag controlling whether to print (true = print)
 * @param fmt Format string (printf-style)
 * @param ... Variable arguments for format string
 */
void echo_debug(bool l_flag, const char *fmt, ...);

/**
 * @brief Print current timestamp for debugging
 */
void print_time(void);

/**
 * @brief Initialize CRC lookup table for Kermit CRC-16
 *
 * Must be called before using crc_kermit(). Initializes internal
 * lookup table for efficient CRC calculation.
 *
 * @note This function is implemented as static inline in utils.cpp
 * and is automatically called when needed. No need to call explicitly.
 */
// Note: init_crc_tab() is now static inline in utils.cpp

/**
 * @brief Calculate Kermit CRC-16 checksum
 *
 * Calculates CRC-16/KERMIT checksum used in RADIAN protocol for
 * data integrity verification.
 *
 * @param input_ptr Pointer to input data buffer
 * @param num_bytes Number of bytes to include in CRC calculation
 * @return 16-bit CRC checksum value
 */
uint16_t crc_kermit(const unsigned char *input_ptr, size_t num_bytes);

/**
 * @brief Encode buffer using RADIAN 1:3 encoding scheme
 *
 * Encodes input buffer using proprietary 1:3 encoding required by
 * RADIAN protocol. Output buffer must be at least 3x input buffer size.
 *
 * @param inputBuffer Pointer to input data buffer
 * @param inputBufferLen Length of input buffer in bytes
 * @param outputBuffer Pointer to output buffer (must be >= inputBufferLen * 3)
 * @return Length of encoded output in bytes
 */
int encode2serial_1_3(uint8_t *inputBuffer, int inputBufferLen, uint8_t *outputBuffer);

/**
 * @brief Create RADIAN protocol master request frame
 *
 * Constructs a complete RADIAN protocol request frame to query a water meter.
 * Frame includes meter identification (year + serial) and proper encoding/CRC.
 *
 * @param outputBuffer Pointer to output buffer for request frame (must be sufficiently large)
 * @param year Last 2 digits of meter manufacturing year (e.g., 15 for 2015)
 * @param serial Meter serial number (32-bit value)
 * @return Length of generated request frame in bytes
 */
int Make_Radian_Master_req(uint8_t *outputBuffer, uint8_t year, uint32_t serial);

/**
 * @brief Convert 433 MHz meter RSSI to percentage
 *
 * Converts CC1101 RSSI measurement (in dBm) to 0-100% scale.
 * Uses wider range (-120 to -40 dBm) appropriate for sub-GHz band.
 *
 * @param rssi_dbm Meter RSSI in dBm (typically -120 to -40)
 * @return Signal strength as percentage (0-100)
 */
int calculateMeterdBmToPercentage(int rssi_dbm);

/**
 * @brief Convert LQI to percentage
 *
 * Converts CC1101 Link Quality Indicator (0-255) to 0-100% scale.
 * LQI represents overall link quality including interference effects.
 *
 * @param lqi Link Quality Indicator (0-255, higher is better)
 * @return Link quality as percentage (0-100)
 */
int calculateLQIToPercentage(int lqi);

/**
 * @brief Validate reading schedule string
 *
 * @param schedule Schedule string to validate
 * @return true if schedule is one of: "Monday-Friday", "Monday-Saturday", "Monday-Sunday"
 */
bool isValidReadingSchedule(const char *schedule);

/**
 * @brief Print meter data summary to serial console
 *
 * Prints a formatted MQTT-style summary of meter data including:
 * - Volume (L for water, m³ for gas)
 * - Battery life, counter, RSSI, LQI with percentages
 * - Time window information
 *
 * @param meter_data Meter data structure
 * @param isMeterGas true if gas meter, false if water meter
 * @param volumeDivisor Gas volume divisor (ignored for water meters)
 */
void printMeterDataSummary(const struct tmeter_data *meter_data, bool isMeterGas, int volumeDivisor);
