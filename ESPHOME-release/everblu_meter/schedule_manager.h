/**
 * @file schedule_manager.h
 * @brief Daily reading schedule management
 *
 * Handles scheduled meter readings with support for different reading patterns
 * (weekdays, weekdays + Saturday, daily). Manages time zone conversions between
 * UTC and local time, and auto-alignment of reading times to meter wake windows.
 *
 * This module is designed to be reusable across different projects (Arduino, ESPHome, etc.)
 * and is independent of MQTT or WiFi dependencies.
 */

#ifndef SCHEDULE_MANAGER_H
#define SCHEDULE_MANAGER_H

#include <Arduino.h>
#include <time.h>

/**
 * @class ScheduleManager
 * @brief Manages daily meter reading schedules with timezone support
 *
 * Provides reading schedule validation, timezone conversion, and automatic
 * alignment to meter wake windows. Supports three reading patterns:
 * - "Monday-Friday" (weekdays only)
 * - "Monday-Saturday" (weekdays plus Saturday)
 * - "Monday-Sunday" (daily)
 */
class ScheduleManager
{
public:
    /**
     * @brief Initialize schedule manager
     *
     * @param schedule Reading schedule string ("Monday-Friday", "Monday-Saturday", "Monday-Sunday")
     * @param readHourUtc Initial reading hour in UTC (0-23)
     * @param readMinuteUtc Initial reading minute in UTC (0-59)
     * @param timezoneOffsetMinutes Timezone offset from UTC in minutes (positive for east, negative for west)
     */
    static void begin(const char *schedule, int readHourUtc, int readMinuteUtc, int timezoneOffsetMinutes);

    /**
     * @brief Validate a reading schedule string
     *
     * @param schedule Schedule string to validate
     * @return true if schedule is valid ("Monday-Friday", "Monday-Saturday", or "Monday-Sunday")
     */
    static bool isValidSchedule(const char *schedule);

    /**
     * @brief Set and validate the reading schedule
     *
     * Falls back to "Monday-Friday" if invalid schedule is provided.
     *
     * @param schedule Reading schedule string
     */
    static void setSchedule(const char *schedule);

    /**
     * @brief Get current reading schedule
     *
     * @return Reading schedule string
     */
    static const char *getSchedule();

    /**
     * @brief Check if today is a scheduled reading day
     *
     * @param ptm Pointer to tm structure with current date/time (typically from gmtime())
     * @return true if today falls within the reading schedule
     */
    static bool isReadingDay(struct tm *ptm);

    /**
     * @brief Update reading time from local (UTC+offset) time
     *
     * Automatically calculates and stores equivalent UTC time.
     *
     * @param hourLocal Hour in local time (0-23)
     * @param minuteLocal Minute in local time (0-59)
     */
    static void setReadingTimeFromLocal(int hourLocal, int minuteLocal);

    /**
     * @brief Update reading time from UTC
     *
     * Automatically calculates and stores equivalent local time.
     *
     * @param hourUtc Hour in UTC (0-23)
     * @param minuteUtc Minute in UTC (0-59)
     */
    static void setReadingTimeFromUtc(int hourUtc, int minuteUtc);

    /**
     * @brief Get reading hour in UTC
     * @return Reading hour (0-23)
     */
    static int getReadingHourUtc();

    /**
     * @brief Get reading minute in UTC
     * @return Reading minute (0-59)
     */
    static int getReadingMinuteUtc();

    /**
     * @brief Get reading hour in local time (UTC+offset)
     * @return Reading hour (0-23)
     */
    static int getReadingHourLocal();

    /**
     * @brief Get reading minute in local time (UTC+offset)
     * @return Reading minute (0-59)
     */
    static int getReadingMinuteLocal();

    /**
     * @brief Auto-align reading time to meter's wake window
     *
     * Adjusts the configured reading time to fall within the meter's active
     * window (time_start to time_end). Optionally uses the midpoint of the
     * window instead of the start time.
     *
     * @param meterTimeStartHour Start of meter's active window (local time, 0-23)
     * @param meterTimeEndHour End of meter's active window (local time, 0-23)
     * @param useMidpoint If true, align to window midpoint; if false, align to start
     * @return true if alignment was performed, false if window was invalid
     */
    static bool autoAlignToMeterWindow(int meterTimeStartHour, int meterTimeEndHour, bool useMidpoint = false);

    /**
     * @brief Get timezone offset in minutes
     * @return Offset from UTC in minutes
     */
    static int getTimezoneOffsetMinutes();

    /**
     * @brief Set timezone offset
     * @param offsetMinutes Offset from UTC in minutes (positive for east, negative for west)
     */
    static void setTimezoneOffset(int offsetMinutes);

private:
    static const char *s_schedule;
    static int s_readHourUtc;
    static int s_readMinuteUtc;
    static int s_readHourLocal;
    static int s_readMinuteLocal;
    static int s_timezoneOffsetMinutes;

    // Helper functions
    static void recalculateLocalFromUtc();
    static void recalculateUtcFromLocal();

    // Private constructor - static-only class
    ScheduleManager() = delete;
};

#endif // SCHEDULE_MANAGER_H
