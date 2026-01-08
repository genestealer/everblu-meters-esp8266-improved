/**
 * @file schedule_manager.cpp
 * @brief Implementation of schedule management
 */

#include "schedule_manager.h"

// Static member initialization
const char *ScheduleManager::s_schedule = "Monday-Friday";
int ScheduleManager::s_readHourUtc = 10;
int ScheduleManager::s_readMinuteUtc = 0;
int ScheduleManager::s_readHourLocal = 10;
int ScheduleManager::s_readMinuteLocal = 0;
int ScheduleManager::s_timezoneOffsetMinutes = 0;

void ScheduleManager::begin(const char *schedule, int readHourUtc, int readMinuteUtc, int timezoneOffsetMinutes)
{
    s_timezoneOffsetMinutes = timezoneOffsetMinutes;
    setSchedule(schedule);
    setReadingTimeFromUtc(readHourUtc, readMinuteUtc);

    Serial.printf("[SCHEDULE] Initialized: schedule=%s, read_time=%02d:%02d UTC (offset=%d min)\n",
                  s_schedule, s_readHourUtc, s_readMinuteUtc, s_timezoneOffsetMinutes);
}

bool ScheduleManager::isValidSchedule(const char *schedule)
{
    return (strcmp(schedule, "Monday-Friday") == 0 ||
            strcmp(schedule, "Monday-Saturday") == 0 ||
            strcmp(schedule, "Monday-Sunday") == 0);
}

void ScheduleManager::setSchedule(const char *schedule)
{
    if (isValidSchedule(schedule))
    {
        s_schedule = schedule;
        Serial.printf("[SCHEDULE] Reading schedule set to: %s\n", s_schedule);
    }
    else
    {
        Serial.printf("[SCHEDULE] [WARN] Invalid schedule '%s' - falling back to 'Monday-Friday'\n", schedule);
        s_schedule = "Monday-Friday";
    }
}

const char *ScheduleManager::getSchedule()
{
    return s_schedule;
}

bool ScheduleManager::isReadingDay(struct tm *ptm)
{
    if (!ptm)
        return false;

    // ptm->tm_wday: 0=Sunday, 1=Monday, ..., 6=Saturday
    int dayOfWeek = ptm->tm_wday;

    if (strcmp(s_schedule, "Monday-Friday") == 0)
    {
        return dayOfWeek >= 1 && dayOfWeek <= 5; // Monday-Friday
    }
    else if (strcmp(s_schedule, "Monday-Saturday") == 0)
    {
        return dayOfWeek >= 1 && dayOfWeek <= 6; // Monday-Saturday
    }
    else if (strcmp(s_schedule, "Monday-Sunday") == 0)
    {
        return dayOfWeek != 0; // All days except Sunday
    }

    return false;
}

void ScheduleManager::setReadingTimeFromLocal(int hourLocal, int minuteLocal)
{
    s_readHourLocal = constrain(hourLocal, 0, 23);
    s_readMinuteLocal = constrain(minuteLocal, 0, 59);

    recalculateUtcFromLocal();
}

void ScheduleManager::setReadingTimeFromUtc(int hourUtc, int minuteUtc)
{
    s_readHourUtc = constrain(hourUtc, 0, 23);
    s_readMinuteUtc = constrain(minuteUtc, 0, 59);

    recalculateLocalFromUtc();
}

int ScheduleManager::getReadingHourUtc()
{
    return s_readHourUtc;
}

int ScheduleManager::getReadingMinuteUtc()
{
    return s_readMinuteUtc;
}

int ScheduleManager::getReadingHourLocal()
{
    return s_readHourLocal;
}

int ScheduleManager::getReadingMinuteLocal()
{
    return s_readMinuteLocal;
}

bool ScheduleManager::autoAlignToMeterWindow(int meterTimeStartHour, int meterTimeEndHour, bool useMidpoint)
{
    int timeStart = constrain(meterTimeStartHour, 0, 23);
    int timeEnd = constrain(meterTimeEndHour, 0, 23);
    int window = (timeEnd - timeStart + 24) % 24;

    if (window == 0)
    {
        Serial.println("[SCHEDULE] [WARN] Cannot auto-align: meter window is invalid (0 hours)");
        return false;
    }

    int alignedHourLocal;
    if (useMidpoint)
    {
        alignedHourLocal = (timeStart + (window / 2)) % 24;
    }
    else
    {
        alignedHourLocal = timeStart;
    }

    setReadingTimeFromLocal(alignedHourLocal, s_readMinuteLocal);

    Serial.printf("[SCHEDULE] Auto-aligned reading time to %02d:%02d local-offset (%02d:%02d UTC) "
                  "(meter window %02d-%02d local)\n",
                  s_readHourLocal, s_readMinuteLocal, s_readHourUtc, s_readMinuteUtc, timeStart, timeEnd);

    return true;
}

int ScheduleManager::getTimezoneOffsetMinutes()
{
    return s_timezoneOffsetMinutes;
}

void ScheduleManager::setTimezoneOffset(int offsetMinutes)
{
    s_timezoneOffsetMinutes = offsetMinutes;
    recalculateLocalFromUtc();
    Serial.printf("[SCHEDULE] Timezone offset set to %d minutes\n", s_timezoneOffsetMinutes);
}

void ScheduleManager::recalculateLocalFromUtc()
{
    int totalUtcMin = s_readHourUtc * 60 + s_readMinuteUtc;
    int localMin = (totalUtcMin + s_timezoneOffsetMinutes) % (24 * 60);

    if (localMin < 0)
    {
        localMin += 24 * 60;
    }

    s_readHourLocal = localMin / 60;
    s_readMinuteLocal = localMin % 60;
}

void ScheduleManager::recalculateUtcFromLocal()
{
    int totalLocalMin = s_readHourLocal * 60 + s_readMinuteLocal;
    int utcMin = (totalLocalMin - s_timezoneOffsetMinutes) % (24 * 60);

    if (utcMin < 0)
    {
        utcMin += 24 * 60;
    }

    s_readHourUtc = utcMin / 60;
    s_readMinuteUtc = utcMin % 60;
}
