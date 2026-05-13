/**
 * @file storage_abstraction.cpp
 * @brief Implementation of platform-independent storage abstraction
 */

#include "storage_abstraction.h"
#include "../core/logging.h"

// Fallback to ESPHome preferences
#if defined(USE_ESPHOME) || (__has_include("esphome/core/preferences.h"))
#include "esphome/core/preferences.h"
#define EVERBLU_USE_ESPHOME_PREFS 1
#elif defined(ESP8266)
#include <EEPROM.h>
#elif defined(ESP32)
#include <Preferences.h>
static Preferences preferences;
#endif

bool StorageAbstraction::begin()
{
#ifdef EVERBLU_USE_ESPHOME_PREFS
    // ESPHome preferences are auto-initialized
    return true;
#elif defined(ESP8266)
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
#ifdef EVERBLU_USE_ESPHOME_PREFS
    // ESPHome preferences API - use a struct to ensure proper storage
    if (esphome::global_preferences == nullptr)
    {
        LOG_E("everblu_meter", "Cannot save %s: global_preferences is null!", key);
        return false;
    }

    // Create a unique hash for this key
    uint32_t hash = esphome::fnv1_hash(key);
    LOG_D("everblu_meter", "Saving %s (hash: 0x%08X) = %.6f", key, hash, value);

    // Use struct wrapper to ensure proper storage format
    struct FloatStorage
    {
        uint16_t magic_number;
        float data;
    };

    FloatStorage storage;
    storage.magic_number = magic;
    storage.data = value;

    // Use ESPHome's standard flash-based preferences storage
    esphome::ESPPreferenceObject pref = esphome::global_preferences->make_preference<FloatStorage>(hash);
    bool success = pref.save(&storage);

    if (success)
    {
        LOG_I("everblu_meter", "Saved %s = %.6f to ESPHome preferences", key, value);

        // Force sync to ensure data is written to flash
        esphome::global_preferences->sync();
        // Longer delay to ensure flash write completes on ESP8266
        delay(100);

        // Verify the save by reading back the value
        FloatStorage readback;
        readback.magic_number = 0;
        readback.data = 0.0f;

        if (pref.load(&readback))
        {
            if (readback.magic_number == magic && readback.data == value)
            {
                LOG_I("everblu_meter", "✓ Verification PASSED: Read back %.6f with magic 0x%04X", readback.data, readback.magic_number);
            }
            else
            {
                LOG_E("everblu_meter", "✗ Verification FAILED: Wrote %.6f (magic 0x%04X) but read back %.6f (magic 0x%04X)",
                      value, magic, readback.data, readback.magic_number);
                return false;
            }
        }
        else
        {
            LOG_E("everblu_meter", "✗ Verification FAILED: Could not read back value for %s", key);
            return false;
        }
    }
    else
    {
        LOG_E("everblu_meter", "Failed to save %s to ESPHome preferences", key);
    }

    return success;

#elif defined(ESP8266)
    // ESP8266: Use EEPROM with magic number validation
    // Note: This assumes the first float key is at FREQ_OFFSET_ADDR
    // For multiple keys, you'd need a more sophisticated address mapping
    EEPROM.put(FREQ_OFFSET_ADDR, magic);
    EEPROM.put(FREQ_OFFSET_ADDR + 2, value);
    bool success = EEPROM.commit();

    if (success)
    {
        LOG_I("everblu_meter", "Saved %s = %.6f to EEPROM", key, value);
    }
    else
    {
        LOG_E("everblu_meter", "Failed to save %s to EEPROM", key);
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
        LOG_I("everblu_meter", "Saved %s = %.6f to Preferences", key, value);
    }
    else
    {
        LOG_E("everblu_meter", "Failed to save %s to Preferences", key);
    }

    return success;

#else
    LOG_E("everblu_meter", "Storage not supported on this platform");
    return false;
#endif
}

float StorageAbstraction::loadFloat(const char *key, float defaultValue, uint16_t magic,
                                    float minValue, float maxValue)
{
#ifdef EVERBLU_USE_ESPHOME_PREFS
    // ESPHome preferences API - use same struct as save
    if (esphome::global_preferences == nullptr)
    {
        LOG_E("everblu_meter", "Cannot load %s: global_preferences is null!", key);
        return defaultValue;
    }

    uint32_t hash = esphome::fnv1_hash(key);
    LOG_D("everblu_meter", "Loading %s (hash: 0x%08X)", key, hash);

    // Use struct wrapper matching the save format
    struct FloatStorage
    {
        uint16_t magic_number;
        float data;
    };

    // Use ESPHome's standard flash-based preferences storage
    esphome::ESPPreferenceObject pref = esphome::global_preferences->make_preference<FloatStorage>(hash);

    FloatStorage storage;
    storage.magic_number = 0;
    storage.data = defaultValue;

    if (!pref.load(&storage))
    {
        LOG_I("everblu_meter", "No valid data for %s in ESPHome preferences (returning default: %.6f)", key, defaultValue);
        return defaultValue;
    }

    // Validate magic number
    if (storage.magic_number != magic)
    {
        LOG_W("everblu_meter", "Invalid magic number for %s: expected 0x%04X, got 0x%04X (returning default)",
              key, magic, storage.magic_number);
        return defaultValue;
    }

    float value = storage.data;

    // Sanity check: ensure value is within acceptable range
    if (value < minValue || value > maxValue)
    {
        LOG_W("everblu_meter", "Invalid %s value %.6f in preferences (out of range [%.2f, %.2f])",
              key, value, minValue, maxValue);
        return defaultValue;
    }

    LOG_I("everblu_meter", "Loaded %s = %.6f from ESPHome preferences", key, value);
    return value;

#elif defined(ESP8266)
    // ESP8266: Read from EEPROM with magic number validation
    uint16_t storedMagic = 0;
    EEPROM.get(FREQ_OFFSET_ADDR, storedMagic);

    if (storedMagic != magic)
    {
        LOG_I("everblu_meter", "No valid data for %s in EEPROM (magic mismatch)", key);
        return defaultValue;
    }

    float value = 0.0;
    EEPROM.get(FREQ_OFFSET_ADDR + 2, value);

    // Sanity check: ensure value is within acceptable range
    if (value < minValue || value > maxValue)
    {
        LOG_W("everblu_meter", "Invalid %s value %.6f in EEPROM (out of range [%.2f, %.2f])",
              key, value, minValue, maxValue);
        return defaultValue;
    }

    LOG_I("everblu_meter", "Loaded %s = %.6f from EEPROM", key, value);
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
        LOG_I("everblu_meter", "No valid data for %s in Preferences (magic mismatch)", key);
        preferences.end();
        return defaultValue;
    }

    // Read the value
    if (!preferences.isKey(key))
    {
        LOG_I("everblu_meter", "Key %s not found in Preferences", key);
        preferences.end();
        return defaultValue;
    }

    float value = preferences.getFloat(key, defaultValue);
    preferences.end();

    // Sanity check
    if (value < minValue || value > maxValue)
    {
        LOG_W("everblu_meter", "Invalid %s value %.6f in Preferences (out of range [%.2f, %.2f])",
              key, value, minValue, maxValue);
        return defaultValue;
    }

    LOG_I("everblu_meter", "Loaded %s = %.6f from Preferences", key, value);
    return value;

#else
    LOG_E("everblu_meter", "Storage not supported on this platform");
    return defaultValue;
#endif
}

bool StorageAbstraction::hasKey(const char *key)
{
#ifdef EVERBLU_USE_ESPHOME_PREFS
    uint32_t hash = esphome::fnv1_hash(key);
    esphome::ESPPreferenceObject pref = esphome::global_preferences->make_preference<float>(hash);
    float tmp = 0.0f;
    return pref.load(&tmp);

#elif defined(ESP8266)
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
#ifdef EVERBLU_USE_ESPHOME_PREFS
    uint32_t hash = esphome::fnv1_hash(key);
    esphome::ESPPreferenceObject pref = esphome::global_preferences->make_preference<float>(hash);
    float zero = 0.0f;
    return pref.save(&zero);

#elif defined(ESP8266)
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
#ifdef EVERBLU_USE_ESPHOME_PREFS
    // Not supported via ESPHome API in bulk; caller can clear individual keys
    return false;

#elif defined(ESP8266)
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
