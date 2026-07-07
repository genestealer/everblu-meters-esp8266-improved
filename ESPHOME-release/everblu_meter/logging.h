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

// TS_PRINTLN / TS_PRINTF are the timestamped Serial helpers used by MQTT-mode
// code. Some files are shared with the ESPHome build (e.g. frequency_manager.cpp,
// schedule_manager.cpp), so provide ESPHome-mode equivalents that route through
// the ESPHome logger. Without these the shared files fail to compile in ESPHome
// mode. ESPHome supplies its own timestamp/level prefix and trailing newline.
#define TS_PRINTLN(msg) ESP_LOGI("everblu_meter", "%s", msg)
#define TS_PRINTF(fmt, ...) ESP_LOGI("everblu_meter", fmt, ##__VA_ARGS__)

// Component TAG should be defined as: static const char *const TAG = "everblu_meter";
// This ensures all logs appear under the "everblu_meter" component in ESPHome logs

#else
// ============================================================================
// MQTT STANDALONE MODE: Use Serial with ESPHome-like formatting
// ============================================================================
// Mimics ESPHome log format for consistency
// Format: [Level][tag] message
//
// ANSI COLOUR SUPPORT (standalone serial / WiFi monitor only):
// ------------------------------------------------------------
// ESPHome already colourises its own logger output, so these colours apply
// only to the standalone Serial / WiFiSerial monitor (rendered by the VS Code
// integrated terminal, PlatformIO monitor, telnet, etc.).
//
// Colours are keyed off the log level (LOG_* macros) and off the leading
// subsystem tag for free-form echo_debug() lines (e.g. "[METER]", "[FREQ]").
//
// Disable at build time with -D EVERBLU_LOG_COLOR=0 (for example when piping
// logs to a file or to the fixture-extraction script).
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <Arduino.h> // for millis()

#ifndef EVERBLU_LOG_COLOR
#define EVERBLU_LOG_COLOR 1
#endif

#if EVERBLU_LOG_COLOR
#define EVB_ANSI_RESET "\033[0m"
#define EVB_ANSI_RED "\033[1;31m"
#define EVB_ANSI_YELLOW "\033[33m"
#define EVB_ANSI_GREEN "\033[32m"
#define EVB_ANSI_CYAN "\033[36m"
#define EVB_ANSI_BLUE "\033[34m"
#define EVB_ANSI_MAGENTA "\033[35m"
#define EVB_ANSI_GRAY "\033[90m"
#else
#define EVB_ANSI_RESET ""
#define EVB_ANSI_RED ""
#define EVB_ANSI_YELLOW ""
#define EVB_ANSI_GREEN ""
#define EVB_ANSI_CYAN ""
#define EVB_ANSI_BLUE ""
#define EVB_ANSI_MAGENTA ""
#define EVB_ANSI_GRAY ""
#endif

// Returns the ANSI colour for a log line based on its leading "[TAG]" token.
// Returns an empty string (no colour) when the tag is unknown or absent, which
// keeps machine-parsed lines such as hex dumps ("[000-015]: ...") clean.
inline const char *everblu_log_color_for_prefix(const char *msg)
{
	if (!msg)
		return "";
	while (*msg == ' ')
		++msg;
	if (*msg != '[')
		return "";

	const char *tag = msg + 1;
	struct EvbTagColor
	{
		const char *name;
		size_t len;
		const char *color;
	};
	static const EvbTagColor kMap[] = {
		{"ERROR", 5, EVB_ANSI_RED},
		{"WARNING", 7, EVB_ANSI_YELLOW},
		{"WARN", 4, EVB_ANSI_YELLOW},
		{"DEBUG", 5, EVB_ANSI_GRAY},
		{"METER", 5, EVB_ANSI_CYAN},
		{"CC1101", 6, EVB_ANSI_GRAY},
		{"FREQ", 4, EVB_ANSI_MAGENTA},
		{"MQTT", 4, EVB_ANSI_BLUE},
		{"TIME", 4, EVB_ANSI_GREEN},
		{"OTA", 3, EVB_ANSI_GREEN},
		{"STATUS", 6, EVB_ANSI_GREEN},
		{"WIFI", 4, EVB_ANSI_BLUE},
	};
	for (const EvbTagColor &e : kMap)
	{
		if (strncmp(tag, e.name, e.len) == 0 && tag[e.len] == ']')
			return e.color;
	}
	return "";
}

// Returns a formatted UTC timestamp string "[HH:MM:SS]" for serial log lines.
// Uses a static buffer (safe on single-threaded ESP8266/ESP32).
// Before NTP sync (time() < 2020-01-01), shows actual boot uptime via millis().
// Buffer is 20 bytes: "[boot+4294967s]" is 15 chars; "[HH:MM:SS]" is 10 chars.
inline const char *everblu_log_timestamp()
{
	static char buf[20]; // "[boot+XXXXXXXs]" (15) or "[HH:MM:SS]" (10) + null
	time_t now = time(nullptr);
	if (now < 1577836800L) // before 2020-01-01 → not yet NTP-synced; show boot uptime
	{
		snprintf(buf, sizeof(buf), "[boot+%lus]", millis() / 1000UL);
	}
	else
	{
		struct tm *t = gmtime(&now);
		snprintf(buf, sizeof(buf), "[%02d:%02d:%02d]", t->tm_hour, t->tm_min, t->tm_sec);
	}
	return buf;
}

// Timestamped Serial output helpers for tagged log lines (MQTT mode only).
// Use TS_PRINTLN / TS_PRINTF in place of direct Serial.println / Serial.printf
// for lines that carry a [TAG] prefix, so the output matches ESPHome log style.
#define TS_PRINTLN(msg) do { Serial.print(everblu_log_timestamp()); Serial.println(msg); } while (0)
#define TS_PRINTF(fmt, ...) do { Serial.print(everblu_log_timestamp()); Serial.printf(fmt, ##__VA_ARGS__); } while (0)

#define LOG_D(tag, format, ...) Serial.printf("%s" EVB_ANSI_GRAY "[D][%s]" EVB_ANSI_RESET " " format "\n", everblu_log_timestamp(), tag, ##__VA_ARGS__)
#define LOG_I(tag, format, ...) Serial.printf("%s" EVB_ANSI_GREEN "[I]" EVB_ANSI_RESET "[%s] " format "\n", everblu_log_timestamp(), tag, ##__VA_ARGS__)
#define LOG_W(tag, format, ...) Serial.printf("%s" EVB_ANSI_YELLOW "[W][%s] " format EVB_ANSI_RESET "\n", everblu_log_timestamp(), tag, ##__VA_ARGS__)
#define LOG_E(tag, format, ...) Serial.printf("%s" EVB_ANSI_RED "[E][%s] " format EVB_ANSI_RESET "\n", everblu_log_timestamp(), tag, ##__VA_ARGS__)
#define LOG_V(tag, format, ...) Serial.printf("%s" EVB_ANSI_GRAY "[V][%s] " format EVB_ANSI_RESET "\n", everblu_log_timestamp(), tag, ##__VA_ARGS__)
#endif
