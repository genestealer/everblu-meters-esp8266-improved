/**
 * @file storage_abstraction.cpp
 * @brief Implementation of platform-independent storage abstraction
 */

#include "storage_abstraction.h"

#if defined(ESP8266)
#include <EEPROM.h>
#elif defined(ESP32)
#include <Preferences.h>
static Preferences preferences;
#endif

bool StorageAbstraction::begin()
{
#if defined(ESP8266)
    EEPROM.begin(EEPROM_SIZE);
    return true;
#elif defined(ESP32)
    // Preferences don't need initialization, they're opened per-operation
    return true;
#else
    return false;
#endif
}

bool StorageAbstraction::saveFloat(const char *key, float value, uint16_t magic)
{
#if defined(ESP8266)
    // ESP8266: Use EEPROM with magic number validation
    // Note: This assumes the first float key is at FREQ_OFFSET_ADDR
    // For multiple keys, you'd need a more sophisticated address mapping
    EEPROM.put(FREQ_OFFSET_ADDR, magic);
    EEPROM.put(FREQ_OFFSET_ADDR + 2, value);
    bool success = EEPROM.commit();

    if (success)
    {
        Serial.printf("[STORAGE] Saved %s = %.6f to EEPROM\n", key, value);
    }
    else
    {
        Serial.printf("[STORAGE] [ERROR] Failed to save %s to EEPROM\n", key);
    }

    return success;

#elif defined(ESP32)
    // ESP32: Use Preferences (namespace-based key-value store)
    preferences.begin("everblu", false); // false = read-write mode

    // Save magic number as separate key
    char magicKey[32];
    snprintf(magicKey, sizeof(magicKey), "%s_magic", key);
    preferences.putUShort(magicKey, magic);

    // Save the actual value
    size_t written = preferences.putFloat(key, value);
    preferences.end();

    bool success = (written > 0);
    if (success)
    {
        Serial.printf("[STORAGE] Saved %s = %.6f to Preferences\n", key, value);
    }
    else
    {
        Serial.printf("[STORAGE] [ERROR] Failed to save %s to Preferences\n", key);
    }

    return success;

#else
    Serial.printf("[STORAGE] [ERROR] Storage not supported on this platform\n");
    return false;
#endif
}

float StorageAbstraction::loadFloat(const char *key, float defaultValue, uint16_t magic,
                                    float minValue, float maxValue)
{
#if defined(ESP8266)
    // ESP8266: Read from EEPROM with magic number validation
    uint16_t storedMagic = 0;
    EEPROM.get(FREQ_OFFSET_ADDR, storedMagic);

    if (storedMagic != magic)
    {
        Serial.printf("[STORAGE] No valid data for %s in EEPROM (magic mismatch)\n", key);
        return defaultValue;
    }

    float value = 0.0;
    EEPROM.get(FREQ_OFFSET_ADDR + 2, value);

    // Sanity check: ensure value is within acceptable range
    if (value < minValue || value > maxValue)
    {
        Serial.printf("[STORAGE] Invalid %s value %.6f in EEPROM (out of range [%.2f, %.2f])\n",
                      key, value, minValue, maxValue);
        return defaultValue;
    }

    Serial.printf("[STORAGE] Loaded %s = %.6f from EEPROM\n", key, value);
    return value;

#elif defined(ESP32)
    // ESP32: Read from Preferences with magic number validation
    preferences.begin("everblu", true); // true = read-only mode

    // Check magic number
    char magicKey[32];
    snprintf(magicKey, sizeof(magicKey), "%s_magic", key);
    uint16_t storedMagic = preferences.getUShort(magicKey, 0);

    if (storedMagic != magic)
    {
        Serial.printf("[STORAGE] No valid data for %s in Preferences (magic mismatch)\n", key);
        preferences.end();
        return defaultValue;
    }

    // Read the value
    if (!preferences.isKey(key))
    {
        Serial.printf("[STORAGE] Key %s not found in Preferences\n", key);
        preferences.end();
        return defaultValue;
    }

    float value = preferences.getFloat(key, defaultValue);
    preferences.end();

    // Sanity check
    if (value < minValue || value > maxValue)
    {
        Serial.printf("[STORAGE] Invalid %s value %.6f in Preferences (out of range [%.2f, %.2f])\n",
                      key, value, minValue, maxValue);
        return defaultValue;
    }

    Serial.printf("[STORAGE] Loaded %s = %.6f from Preferences\n", key, value);
    return value;

#else
    Serial.printf("[STORAGE] [ERROR] Storage not supported on this platform\n");
    return defaultValue;
#endif
}

bool StorageAbstraction::hasKey(const char *key)
{
#if defined(ESP8266)
    // For ESP8266, check if magic number is valid
    uint16_t storedMagic = 0;
    EEPROM.get(FREQ_OFFSET_ADDR, storedMagic);
    return (storedMagic == 0xABCD); // Using default magic

#elif defined(ESP32)
    preferences.begin("everblu", true); // Read-only
    bool exists = preferences.isKey(key);
    preferences.end();
    return exists;

#else
    return false;
#endif
}

bool StorageAbstraction::clearKey(const char *key)
{
#if defined(ESP8266)
    // Clear by setting magic to 0
    EEPROM.put(FREQ_OFFSET_ADDR, (uint16_t)0);
    return EEPROM.commit();

#elif defined(ESP32)
    preferences.begin("everblu", false); // Read-write
    bool success = preferences.remove(key);

    // Also remove magic key
    char magicKey[32];
    snprintf(magicKey, sizeof(magicKey), "%s_magic", key);
    preferences.remove(magicKey);

    preferences.end();
    return success;

#else
    return false;
#endif
}

bool StorageAbstraction::clearAll()
{
#if defined(ESP8266)
    // Clear entire EEPROM
    for (int i = 0; i < EEPROM_SIZE; i++)
    {
        EEPROM.write(i, 0);
    }
    return EEPROM.commit();

#elif defined(ESP32)
    preferences.begin("everblu", false); // Read-write
    bool success = preferences.clear();
    preferences.end();
    return success;

#else
    return false;
#endif
}
