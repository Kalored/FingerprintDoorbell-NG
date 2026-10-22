#ifndef BACKUPMANAGER_H
#define BACKUPMANAGER_H

#include <Arduino.h>
#include "FingerprintManager.h"
#include "AccessControl.h"

/*
  Backup file format (little endian):
    "FPDB" | version (1 byte) | entry count (2 bytes)
    per entry: id (2) | name length (1) | name | schedule days (1) | from (2) | to (2) | template length (2) | template
  The templates are the raw sensor data ("UpChar"), so they can be restored to the same or another R503 sensor.
*/
class BackupManager
{
public:
  enum class State : uint8_t
  {
    idle,
    running,
    done,
    error
  };

  static constexpr const char *BackupFile = "/backup.fpdb";
  static constexpr const char *RestoreFile = "/restore.fpdb";

  // both run in the loop task, because only this task may talk to the sensor
  bool createBackup(FingerprintManager &fingerManager, AccessControl &accessControl);
  bool restoreBackup(FingerprintManager &fingerManager, AccessControl &accessControl, bool clearFirst);

  String getStatusJson();
  State getState() { return state; }
  int getProgress() { return progress; }
  void setRunning(const String &message); // called when a job is queued

private:
  volatile State state = State::idle;
  volatile int progress = 0;
  volatile int total = 0;
  String message;
  void finish(State newState, const String &newMessage);
};

#endif
