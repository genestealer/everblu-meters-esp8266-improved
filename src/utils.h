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
 void show_in_hex(const uint8_t* buffer, size_t len);
 
 /**
  * @brief Display buffer contents as hexadecimal array notation
  * @param buffer Pointer to data buffer
  * @param len Length of buffer in bytes
  */
 void show_in_hex_array(const uint8_t* buffer, size_t len);
 
 /**
  * @brief Display buffer contents in hexadecimal format (single line)
  * @param buffer Pointer to data buffer
  * @param len Length of buffer in bytes
  */
 void show_in_hex_one_line(const uint8_t* buffer, size_t len);
 
 /**
  * @brief Display buffer contents in hexadecimal format for GET requests
  * @param buffer Pointer to data buffer
  * @param len Length of buffer in bytes
  */
 void show_in_hex_one_line_GET(const uint8_t* buffer, size_t len);
 
 /**
  * @brief Display buffer contents in binary format
  * @param buffer Pointer to data buffer
  * @param len Length of buffer in bytes
  */
 void show_in_bin(const uint8_t* buffer, size_t len);
 
 /**
  * @brief Conditional debug output with printf-style formatting
  * @param l_flag Boolean flag controlling whether to print (true = print)
  * @param fmt Format string (printf-style)
  * @param ... Variable arguments for format string
  */
 void echo_debug(T_BOOL l_flag, const char *fmt, ...);
 
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
 