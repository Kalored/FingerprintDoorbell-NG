#include "AccessControl.h"
#include <Preferences.h>
#include <time.h>

void AccessControl::configure(uint8_t maxFailures, uint16_t windowSeconds, uint16_t lockoutSeconds)
{
  this->maxFailures = (maxFailures > MaxTrackedFailures) ? MaxTrackedFailures : maxFailures;
  windowMs = (unsigned long)windowSeconds * 1000UL;
  lockoutMs = (unsigned long)lockoutSeconds * 1000UL;
  failureCount = 0;
}

void AccessControl::dropOldFailures()
{
  unsigned long now = millis();
  uint8_t kept = 0;
  for (uint8_t i = 0; i < failureCount; i++)
  {
    if (now - failureTimes[i] < windowMs)
      failureTimes[kept++] = failureTimes[i];
  }
  failureCount = kept;
}

bool AccessControl::recordFailure()
{
  if (maxFailures == 0 || isLockedOut())
    return false;
  dropOldFailures();
  if (failureCount < MaxTrackedFailures)
    failureTimes[failureCount++] = millis();
  if (failureCount >= maxFailures)
  {
    lockoutActive = true;
    lockoutStart = millis();
    failureCount = 0;
    return true;
  }
  return false;
}

bool AccessControl::isLockedOut()
{
  if (lockoutActive && millis() - lockoutStart >= lockoutMs)
  {
    lockoutActive = false;
    lockoutEnded = true;
  }
  return lockoutActive;
}

bool AccessControl::pollLockoutEnded()
{
  isLockedOut(); // update state
  if (lockoutEnded)
  {
    lockoutEnded = false;
    return true;
  }
  return false;
}

uint8_t AccessControl::getRecentFailures()
{
  dropOldFailures();
  return failureCount;
}

unsigned long AccessControl::getLockoutRemainingSeconds()
{
  if (!isLockedOut())
    return 0;
  return (lockoutMs - (millis() - lockoutStart)) / 1000UL;
}

void AccessControl::loadSchedules()
{
  Preferences preferences;
  bool opened = preferences.begin("fingerAccess", true);
  for (int i = 0; i <= 200; i++)
  {
    schedules[i] = FingerSchedule();
    String key = String(i);
    if (opened && preferences.isKey(key.c_str()))
    {
      uint8_t data[5];
      if (preferences.getBytes(key.c_str(), data, sizeof(data)) == sizeof(data))
      {
        schedules[i].days = data[0];
        schedules[i].from = ((uint16_t)data[1] << 8) | data[2];
        schedules[i].to = ((uint16_t)data[3] << 8) | data[4];
      }
    }
  }
  if (opened)
    preferences.end();
}

FingerSchedule AccessControl::getSchedule(int id)
{
  if (id < 0 || id > 200)
    return FingerSchedule();
  return schedules[id];
}

void AccessControl::setSchedule(int id, const FingerSchedule &schedule)
{
  if (id < 1 || id > 200)
    return;
  schedules[id] = schedule;
  Preferences preferences;
  preferences.begin("fingerAccess", false);
  if (schedule.isRestricted())
  {
    uint8_t data[5] = {schedule.days, (uint8_t)(schedule.from >> 8), (uint8_t)(schedule.from & 0xFF), (uint8_t)(schedule.to >> 8), (uint8_t)(schedule.to & 0xFF)};
    preferences.putBytes(String(id).c_str(), data, sizeof(data));
  }
  else
    preferences.remove(String(id).c_str());
  preferences.end();
}

bool AccessControl::getLocalMinutes(int &weekdayMondayFirst, int &minutes)
{
  time_t now = time(nullptr);
  if (now < 1600000000) // not synchronized by NTP yet
    return false;
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  weekdayMondayFirst = (timeinfo.tm_wday + 6) % 7; // tm_wday: 0 = Sunday
  minutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  return true;
}

bool AccessControl::isAccessAllowed(int id, String &reason)
{
  FingerSchedule schedule = getSchedule(id);
  if (!schedule.isRestricted())
    return true;
  if (schedule.days == 0)
  {
    reason = "finger is disabled";
    return false;
  }

  int weekday, minutes;
  if (!getLocalMinutes(weekday, minutes))
  {
    reason = "time not synchronized, restricted fingers are not allowed";
    return false;
  }

  bool inWindow;
  int dayToCheck = weekday;
  if (schedule.from <= schedule.to)
    inWindow = (minutes >= schedule.from && minutes < schedule.to);
  else
  {
    // window over midnight: the part after midnight belongs to the day the window started
    inWindow = (minutes >= schedule.from || minutes < schedule.to);
    if (minutes < schedule.to)
      dayToCheck = (weekday + 6) % 7;
  }

  if (!(schedule.days & (1 << dayToCheck)))
  {
    reason = "not allowed on this weekday";
    return false;
  }
  if (!inWindow)
  {
    reason = "outside of allowed time window";
    return false;
  }
  return true;
}

String AccessControl::scheduleToString(const FingerSchedule &schedule)
{
  if (!schedule.isRestricted())
    return "";
  if (schedule.days == 0)
    return "disabled";

  static const char *dayNames[7] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
  String text;
  if (schedule.days != 0x7F)
  {
    for (int i = 0; i < 7; i++)
    {
      if (schedule.days & (1 << i))
        text += String(text.isEmpty() ? "" : ",") + dayNames[i];
    }
  }
  if (!(schedule.from == 0 && schedule.to == 1440))
  {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02u:%02u-%02u:%02u", schedule.from / 60, schedule.from % 60, (schedule.to / 60) % 25, schedule.to % 60);
    text += String(text.isEmpty() ? "" : " ") + buffer;
  }
  return text;
}
