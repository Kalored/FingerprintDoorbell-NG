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
  enum class RestoreMode : uint8_t
  {
    overwrite, // slot numbers from the file, existing fingerprints in those slots are overwritten
    clearFirst, // delete everything on the sensor first, then slot numbers from the file
    addToFree   // keep everything on the sensor, every fingerprint of the file goes to the next free slot
  };

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
  // singleId = 0: all fingerprints, otherwise only the fingerprint in this slot
  bool createBackup(FingerprintManager &fingerManager, AccessControl &accessControl, uint16_t singleId = 0);
  bool restoreBackup(FingerprintManager &fingerManager, AccessControl &accessControl, RestoreMode mode);
  uint16_t getExportedId() { return exportedId; } // slot of the last single export, 0 = full backup

  String getStatusJson();
  State getState() { return state; }
  int getProgress() { return progress; }
  void setRunning(const String &message); // called when a job is queued

private:
  volatile State state = State::idle;
  volatile int progress = 0;
  volatile int total = 0;
  String message;
  uint16_t exportedId = 0;
  void finish(State newState, const String &newMessage);
};

#endif
