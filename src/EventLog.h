#ifndef EVENTLOG_H
#define EVENTLOG_H

#include <Arduino.h>
#include "global.h"

enum class EventType : uint8_t
{
  boot = 0,
  ring,
  match,
  failedAttempt,
  accessDenied,
  lockout,
  lockoutEnded,
  doorOpened,
  enrolled,
  deleted,
  backup,
  restore,
  ringSuppressed
};

// one persistent event, fixed size so the log can be stored as a simple ring buffer file
struct EventRecord
{
  uint32_t timestamp; // unix time, 0 = time was unknown
  uint32_t uptime;    // seconds since boot
  uint8_t type;
  uint8_t reserved;
  uint16_t fingerId;
  uint16_t confidence;
  char text[42];
};

class EventLog
{
public:
  static const int MaxEvents = 30;

  void begin(); // load the events from SPIFFS (SPIFFS must already be mounted)
  void add(EventType type, uint16_t fingerId = 0, uint16_t confidence = 0, const String &text = "");
  void clear();
  String getEventsAsHtmlTable();
  String getLastEventAsText();
  static const char *getTypeName(uint8_t type);

private:
  EventRecord records[MaxEvents];
  uint8_t next = 0;  // index of the next record to write
  uint8_t count = 0; // number of valid records
  void save();
  String formatRecord(const EventRecord &record, bool html);
};

#endif
