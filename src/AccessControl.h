#ifndef ACCESSCONTROL_H
#define ACCESSCONTROL_H

#include <Arduino.h>

// allowed weekdays and time window for one finger. Default: always allowed.
struct FingerSchedule
{
  uint8_t days = 0x7F;  // bit 0 = Monday ... bit 6 = Sunday
  uint16_t from = 0;    // minutes since midnight
  uint16_t to = 1440;   // minutes since midnight (exclusive), from > to = window over midnight
  bool isRestricted() const { return !(days == 0x7F && from == 0 && to == 1440); }
};

class AccessControl
{
public:
  // brute force protection
  void configure(uint8_t maxFailures, uint16_t windowSeconds, uint16_t lockoutSeconds);
  bool recordFailure(); // returns true if this failure started a lockout
  bool isLockedOut();
  bool pollLockoutEnded(); // returns true once after a lockout has expired
  uint8_t getRecentFailures();
  unsigned long getLockoutRemainingSeconds();

  // time windows per finger
  void loadSchedules();
  FingerSchedule getSchedule(int id);
  void setSchedule(int id, const FingerSchedule &schedule);
  bool isAccessAllowed(int id, String &reason);
  static String scheduleToString(const FingerSchedule &schedule);

  // current local time, false if the time is not synchronized yet
  static bool getLocalMinutes(int &weekdayMondayFirst, int &minutes);

private:
  static const int MaxTrackedFailures = 30;
  unsigned long failureTimes[MaxTrackedFailures];
  uint8_t failureCount = 0;
  uint8_t maxFailures = 0; // 0 = protection disabled
  unsigned long windowMs = 120000;
  unsigned long lockoutMs = 300000;
  bool lockoutActive = false;
  bool lockoutEnded = false;
  unsigned long lockoutStart = 0;

  FingerSchedule schedules[201];
  void dropOldFailures();
};

#endif
