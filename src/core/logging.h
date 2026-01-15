/**
 * @file logging.h
 * @brief Cross-platform logging macros for MQTT and ESPHome modes
 *
 * ESPHOME EXTERNAL COMPONENT LOGGING GUIDE:
 * ========================================
 *
 * For ESPHome external components, logging MUST use ESP_LOG* macros:
 *
 * 1. Include header:
 *    #include "esphome/core/log.h"
 *
 * 2. Define component TAG (in namespace):
 *    static const char *const TAG = "everblu_meter";
 *
 * 3. Use ESP_LOG* macros:
 *    ESP_LOGD(TAG, "Debug: value=%d", val);   // [D][everblu_meter:123]: Debug: value=42
 *    ESP_LOGI(TAG, "Info: %s", msg);          // [I][everblu_meter:124]: Info: OK
 *    ESP_LOGW(TAG, "Warning: %s", warn);      // [W][everblu_meter:125]: Warning: ...
 *    ESP_LOGE(TAG, "Error: %s", err);         // [E][everblu_meter:126]: Error: ...
 *    ESP_LOGV(TAG, "Verbose trace");          // [V][everblu_meter:127]: Verbose trace
 *
 * 4. YAML configuration:
 *    logger:
 *      level: DEBUG              # Global: NONE, ERROR, WARN, INFO, DEBUG, VERBOSE
 *      logs:
 *        everblu_meter: VERBOSE  # Per-component override
 *
 * 5. DO NOT use Serial.print/printf - it bypasses ESPHome logger and won't
 *    appear in WiFi logs or ESPHome web interface.
 *
 * 6. For hex buffers, use format_hex_pretty():
 *    ESP_LOGD(TAG, "Data: %s", format_hex_pretty(buf, len).c_str());
 *
 * DUAL-MODE SUPPORT (MQTT + ESPHome):
 * ===================================
 * This file provides LOG_* wrapper macros for code shared between:
 * - ESPHome external component (USE_ESPHOME defined)
 * - MQTT standalone mode (USE_ESPHOME not defined)
 *
 * Usage: LOG_I("everblu_meter", "Message: %d", value);
 * - ESPHome: Expands to ESP_LOGI → WiFi + USB logs
 * - MQTT: Expands to Serial.printf → USB logs only
 */

#pragma once

#if defined(USE_ESPHOME) || (__has_include("esphome/core/log.h"))
// ============================================================================
// ESPHOME MODE (or esphome headers available): Use ESPHome logger
// This makes logs visible over WiFi/API and USB
// ============================================================================
#include "esphome/core/log.h"

// Wrapper macros for dual-mode compatibility
// These expand to ESP_LOG* which integrates with ESPHome's logger component
#define LOG_D(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
#define LOG_I(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#define LOG_W(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
#define LOG_E(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#define LOG_V(tag, format, ...) ESP_LOGV(tag, format, ##__VA_ARGS__)

// Component TAG should be defined as: static const char *const TAG = "everblu_meter";
// This ensures all logs appear under the "everblu_meter" component in ESPHome logs

#else
// ============================================================================
// MQTT STANDALONE MODE: Use Serial with ESPHome-like formatting
// ============================================================================
// Mimics ESPHome log format for consistency
// Format: [Level][tag] message
#define LOG_D(tag, format, ...) Serial.printf("[D][%s] " format "\n", tag, ##__VA_ARGS__)
#define LOG_I(tag, format, ...) Serial.printf("[I][%s] " format "\n", tag, ##__VA_ARGS__)
#define LOG_W(tag, format, ...) Serial.printf("[W][%s] " format "\n", tag, ##__VA_ARGS__)
#define LOG_E(tag, format, ...) Serial.printf("[E][%s] " format "\n", tag, ##__VA_ARGS__)
#define LOG_V(tag, format, ...) Serial.printf("[V][%s] " format "\n", tag, ##__VA_ARGS__)
#endif
