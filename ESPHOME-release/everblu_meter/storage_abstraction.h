/**
 * @file storage_abstraction.h
 * @brief Platform-independent persistent storage abstraction layer
 *
 * Provides a unified interface for storing and retrieving configuration data
 * across different ESP platforms (ESP8266 EEPROM, ESP32 Preferences).
 *
 * This abstraction allows the same code to work on both ESP8266 and ESP32
 * without platform-specific #ifdef blocks scattered throughout the application.
 */

#ifndef STORAGE_ABSTRACTION_H
#define STORAGE_ABSTRACTION_H

#include <Arduino.h>

/**
 * @class StorageAbstraction
 * @brief Unified storage interface for ESP8266/ESP32 platforms
 *
 * Encapsulates platform-specific storage mechanisms (EEPROM on ESP8266,
 * Preferences on ESP32) behind a common API. Handles initialization,
 * data validation, and platform differences transparently.
 */
class StorageAbstraction
{
public:
    /**
     * @brief Initialize the storage system
     *
     * Prepares the underlying storage mechanism (EEPROM or Preferences).
     * Must be called once during setup before any read/write operations.
     *
     * @return true if initialization succeeded, false on error
     */
    static bool begin();

    /**
     * @brief Save a float value to persistent storage
     *
     * Stores a float value with an optional magic number for validation.
     * The magic number helps detect corrupted or uninitialized storage.
     *
     * @param key Storage key/identifier (e.g., "freq_offset")
     * @param value Float value to store
     * @param magic Optional magic number for validation (default: 0xABCD)
     * @return true if save succeeded, false on error
     */
    static bool saveFloat(const char *key, float value, uint16_t magic = 0xABCD);

    /**
     * @brief Load a float value from persistent storage
     *
     * Retrieves a previously saved float value. Validates magic number
     * to ensure data integrity. Returns default value if data is missing
     * or invalid.
     *
     * @param key Storage key/identifier (e.g., "freq_offset")
     * @param defaultValue Value to return if key not found or invalid
     * @param magic Expected magic number for validation (default: 0xABCD)
     * @param minValue Minimum acceptable value (for sanity checking)
     * @param maxValue Maximum acceptable value (for sanity checking)
     * @return Retrieved value if valid, or defaultValue if invalid/missing
     */
    static float loadFloat(const char *key, float defaultValue = 0.0, uint16_t magic = 0xABCD,
                           float minValue = -999999.0, float maxValue = 999999.0);

    /**
     * @brief Check if a key exists in storage
     *
     * Tests whether a given key has been previously written to storage.
     * Does not validate the data, only checks for presence.
     *
     * @param key Storage key to check
     * @return true if key exists, false otherwise
     */
    static bool hasKey(const char *key);

    /**
     * @brief Clear a specific key from storage
     *
     * Removes a key and its associated value from persistent storage.
     *
     * @param key Storage key to remove
     * @return true if removal succeeded, false on error
     */
    static bool clearKey(const char *key);

    /**
     * @brief Clear all storage (factory reset)
     *
     * Erases all stored data. Use with caution as this cannot be undone.
     *
     * @return true if clear succeeded, false on error
     */
    static bool clearAll();

private:
    // Private constructor - this is a static-only utility class
    StorageAbstraction() = delete;

    // Storage addresses for ESP8266 EEPROM
    static constexpr uint16_t EEPROM_SIZE = 64;
    static constexpr uint16_t FREQ_OFFSET_ADDR = 0;
};

#endif // STORAGE_ABSTRACTION_H
