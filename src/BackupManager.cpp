#include "BackupManager.h"
#include <SPIFFS.h>

static const uint8_t BackupVersion = 1;
static const size_t MaxTemplateSize = 2560; // R503 templates are smaller, this leaves some headroom

static void writeU16(File &file, uint16_t value)
{
  uint8_t data[2] = {(uint8_t)(value & 0xFF), (uint8_t)(value >> 8)};
  file.write(data, 2);
}

static bool readU16(File &file, uint16_t &value)
{
  uint8_t data[2];
  if (file.read(data, 2) != 2)
    return false;
  value = data[0] | ((uint16_t)data[1] << 8);
  return true;
}

void BackupManager::setRunning(const String &newMessage)
{
  message = newMessage;
  progress = 0;
  total = 0;
  state = State::running;
}

void BackupManager::finish(State newState, const String &newMessage)
{
  message = newMessage;
  state = newState;
  notifyClients(newMessage);
}

String BackupManager::getStatusJson()
{
  const char *stateText = "idle";
  switch (state)
  {
  case State::running: stateText = "running"; break;
  case State::done: stateText = "done"; break;
  case State::error: stateText = "error"; break;
  default: break;
  }
  String escaped = message;
  escaped.replace("\\", "\\\\");
  escaped.replace("\"", "\\\"");
  return String("{\"state\":\"") + stateText + "\",\"progress\":" + progress + ",\"total\":" + total + ",\"message\":\"" + escaped + "\"}";
}

bool BackupManager::createBackup(FingerprintManager &fingerManager, AccessControl &accessControl)
{
  setRunning("Creating backup...");

  uint16_t slots = fingerManager.getCapacity();
  if (slots == 0 || slots > 256)
    slots = 200;
  bool occupied[256];
  if (!fingerManager.readIndexTable(occupied, slots))
  {
    finish(State::error, "Backup failed: could not read the template index from the sensor.");
    return false;
  }

  uint16_t count = 0;
  for (uint16_t id = 1; id < slots && id <= 200; id++)
  {
    if (occupied[id])
      count++;
  }
  total = count;

  File file = SPIFFS.open(BackupFile, "w");
  if (!file)
  {
    finish(State::error, "Backup failed: could not create file.");
    return false;
  }

  uint8_t *buffer = (uint8_t *)malloc(MaxTemplateSize);
  if (!buffer)
  {
    file.close();
    finish(State::error, "Backup failed: out of memory.");
    return false;
  }

  file.write((const uint8_t *)"FPDB", 4);
  file.write(BackupVersion);
  writeU16(file, count);

  bool ok = true;
  for (uint16_t id = 1; id < slots && id <= 200 && ok; id++)
  {
    if (!occupied[id])
      continue;

    int length = fingerManager.readTemplate(id, buffer, MaxTemplateSize);
    if (length <= 0)
      length = fingerManager.readTemplate(id, buffer, MaxTemplateSize); // one retry, serial errors happen from time to time
    if (length <= 0)
    {
      message = String("Backup failed: could not read template #") + id + " from the sensor.";
      ok = false;
      break;
    }

    String name = fingerManager.getFingerName(id);
    if (name.length() > 255)
      name = name.substring(0, 255);
    FingerSchedule schedule = accessControl.getSchedule(id);

    writeU16(file, id);
    file.write((uint8_t)name.length());
    file.write((const uint8_t *)name.c_str(), name.length());
    file.write(schedule.days);
    writeU16(file, schedule.from);
    writeU16(file, schedule.to);
    writeU16(file, (uint16_t)length);
    if (file.write(buffer, length) != (size_t)length)
    {
      message = "Backup failed: file system full?";
      ok = false;
      break;
    }
    progress = progress + 1;
  }

  free(buffer);
  file.close();

  if (!ok)
  {
    SPIFFS.remove(BackupFile);
    finish(State::error, message);
    return false;
  }
  finish(State::done, String("Backup created with ") + count + " fingerprints.");
  return true;
}

bool BackupManager::restoreBackup(FingerprintManager &fingerManager, AccessControl &accessControl, bool clearFirst)
{
  setRunning("Restoring backup...");

  File file = SPIFFS.open(RestoreFile, "r");
  if (!file)
  {
    finish(State::error, "Restore failed: no uploaded backup file found.");
    return false;
  }

  char magic[4];
  uint8_t version = 0;
  uint16_t count = 0;
  if (file.read((uint8_t *)magic, 4) != 4 || memcmp(magic, "FPDB", 4) != 0 || file.read(&version, 1) != 1 || version != BackupVersion || !readU16(file, count))
  {
    file.close();
    SPIFFS.remove(RestoreFile);
    finish(State::error, "Restore failed: this is not a valid FingerprintDoorbell backup file.");
    return false;
  }
  total = count;

  uint8_t *buffer = (uint8_t *)malloc(MaxTemplateSize);
  if (!buffer)
  {
    file.close();
    finish(State::error, "Restore failed: out of memory.");
    return false;
  }

  if (clearFirst)
  {
    fingerManager.deleteAll();
    for (int id = 1; id <= 200; id++)
    {
      if (accessControl.getSchedule(id).isRestricted())
        accessControl.setSchedule(id, FingerSchedule());
    }
  }

  bool ok = true;
  int restored = 0;
  for (uint16_t i = 0; i < count && ok; i++)
  {
    uint16_t id, from, to, length;
    uint8_t nameLength, days;
    char name[256];
    if (!readU16(file, id) || file.read(&nameLength, 1) != 1 || file.read((uint8_t *)name, nameLength) != nameLength ||
        file.read(&days, 1) != 1 || !readU16(file, from) || !readU16(file, to) || !readU16(file, length) ||
        id < 1 || id > 200 || length == 0 || length > MaxTemplateSize || file.read(buffer, length) != length)
    {
      message = String("Restore failed: backup file is damaged (entry ") + (i + 1) + ").";
      ok = false;
      break;
    }
    name[nameLength] = 0;

    bool written = fingerManager.writeTemplate(id, buffer, length);
    if (!written)
      written = fingerManager.writeTemplate(id, buffer, length); // one retry
    if (!written)
    {
      message = String("Restore failed: could not write template #") + id + " to the sensor.";
      ok = false;
      break;
    }
    fingerManager.setFingerName(id, String(name));
    FingerSchedule schedule;
    schedule.days = days;
    schedule.from = from;
    schedule.to = to;
    accessControl.setSchedule(id, schedule);
    restored++;
    progress = progress + 1;
  }

  free(buffer);
  file.close();
  SPIFFS.remove(RestoreFile);

  if (!ok)
  {
    finish(State::error, message + String(" ") + restored + " of " + count + " fingerprints were restored.");
    return false;
  }
  finish(State::done, String("Restore completed: ") + restored + " fingerprints restored.");
  return true;
}
