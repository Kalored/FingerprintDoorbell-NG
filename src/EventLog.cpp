#include "EventLog.h"
#include <SPIFFS.h>
#include <time.h>
#include "SerialRedirect.h" // must stay the last include

static const char *EventLogFile = "/events.bin";
static const uint8_t EventLogVersion = 1;

void EventLog::begin()
{
  next = 0;
  count = 0;
  File file = SPIFFS.open(EventLogFile, "r");
  if (!file)
    return;
  uint8_t header[3];
  if (file.read(header, sizeof(header)) == sizeof(header) && header[0] == EventLogVersion && header[1] < MaxEvents && header[2] <= MaxEvents)
  {
    if (file.read((uint8_t *)records, sizeof(records)) == sizeof(records))
    {
      next = header[1];
      count = header[2];
    }
  }
  file.close();
}

void EventLog::save()
{
  File file = SPIFFS.open(EventLogFile, "w");
  if (!file)
  {
    Serial.println("Could not write event log");
    return;
  }
  uint8_t header[3] = {EventLogVersion, next, count};
  file.write(header, sizeof(header));
  file.write((const uint8_t *)records, sizeof(records));
  file.close();
}

void EventLog::add(EventType type, uint16_t fingerId, uint16_t confidence, const String &text)
{
  EventRecord &record = records[next];
  memset(&record, 0, sizeof(record));
  time_t now = time(nullptr);
  record.timestamp = (now > 1600000000) ? (uint32_t)now : 0; // before NTP sync the time is around 1970
  record.uptime = millis() / 1000;
  record.type = (uint8_t)type;
  record.fingerId = fingerId;
  record.confidence = confidence;
  strncpy(record.text, text.c_str(), sizeof(record.text) - 1);

  next = (next + 1) % MaxEvents;
  if (count < MaxEvents)
    count++;
  save();
}

void EventLog::clear()
{
  next = 0;
  count = 0;
  save();
}

const char *EventLog::getTypeName(uint8_t type)
{
  switch ((EventType)type)
  {
  case EventType::boot: return "Boot";
  case EventType::ring: return "Ring";
  case EventType::match: return "Match";
  case EventType::failedAttempt: return "Failed attempt";
  case EventType::accessDenied: return "Access denied";
  case EventType::lockout: return "Lockout";
  case EventType::lockoutEnded: return "Lockout ended";
  case EventType::doorOpened: return "Door opened";
  case EventType::enrolled: return "Enrolled";
  case EventType::deleted: return "Deleted";
  case EventType::backup: return "Backup";
  case EventType::restore: return "Restore";
  case EventType::ringSuppressed: return "Ring suppressed";
  case EventType::sensorLost: return "Sensor lost";
  case EventType::sensorRestored: return "Sensor restored";
  default: return "Unknown";
  }
}

String EventLog::formatRecord(const EventRecord &record, bool html)
{
  String timeText;
  if (record.timestamp != 0)
  {
    time_t t = record.timestamp;
    struct tm timeinfo;
    localtime_r(&t, &timeinfo);
    char buffer[24];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    timeText = buffer;
  }
  else
    timeText = String("uptime ") + record.uptime + "s";

  String finger = record.fingerId ? String("#") + record.fingerId : String("");
  String details = String(record.text);
  if (record.confidence)
    details += String(details.isEmpty() ? "" : ", ") + "confidence " + record.confidence;

  if (!html)
    return timeText + " " + getTypeName(record.type) + (finger.isEmpty() ? "" : " " + finger) + (details.isEmpty() ? "" : " " + details);

  return "<tr><td>" + timeText + "</td><td>" + getTypeName(record.type) + "</td><td>" + finger + "</td><td>" + htmlEscape(details) + "</td></tr>";
}

String EventLog::getEventsAsHtmlTable()
{
  if (count == 0)
    return "<tr><td colspan=\"4\">No events yet</td></tr>";
  String html;
  // newest first
  for (int i = 0; i < count; i++)
  {
    int idx = (next - 1 - i + MaxEvents) % MaxEvents;
    html += formatRecord(records[idx], true);
  }
  return html;
}

String EventLog::getLastEventAsText()
{
  if (count == 0)
    return "";
  return formatRecord(records[(next - 1 + MaxEvents) % MaxEvents], false);
}
