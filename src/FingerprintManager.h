#ifndef FINGERPRINTMANAGER_H
#define FINGERPRINTMANAGER_H

#include <Adafruit_Fingerprint.h>
#include <Preferences.h>
#include "global.h"

#define mySerial Serial2

#define FINGERPRINT_WRITENOTEPAD 0x18 // Write Notepad on sensor
#define FINGERPRINT_READNOTEPAD 0x19  // Read Notepad from sensor

/*
  By using the touch ring as an additional input to the image sensor the sensitivity is much higher for door bell ring events. Unfortunately
  we cannot differ between touches on the ring by fingers or rain drops, so rain on the ring will cause false alarms.
*/
const int touchRingPin = 5; // touch/wakeup pin connected to fingerprint sensor

enum class ScanResult
{
  noFinger,
  matchFound,
  noMatchFound,
  error
};
enum class EnrollResult
{
  ok,
  error
};

struct Match
{
  ScanResult scanResult = ScanResult::noFinger;
  uint16_t matchId = 0;
  String matchName = "unknown";
  uint16_t matchConfidence = 0;
  uint8_t returnCode = 0;
};

struct NewFinger
{
  EnrollResult enrollResult = EnrollResult::error;
  uint8_t returnCode = 0;
};

class FingerprintManager
{
private:
  Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
  bool lastTouchState = false;
  String fingerList[201];
  int fingerCountOnSensor = 0;
  bool ignoreTouchRing = false; // set to true when the sensor is usually exposed to rain to avoid false ring events. Can also be set conditional by a rain sensor over MQTT
  bool lastIgnoreTouchRing = false;
  bool ledRingEnabled = true;     // can be switched off by MQTT, feedback while scanning/enrolling is still shown
  bool ledRingSuppressed = false; // idle LED off because of night mode or radar (nobody near the door)
  bool lockedOut = false;         // too many failed attempts, LED ring shows red

  void updateTouchState(bool touched);
  bool isRingTouched();
  void loadFingerListFromPrefs();
  void disconnect();
  uint8_t writeNotepad(uint8_t pageNumber, const char *text, uint8_t length);
  uint8_t readNotepad(uint8_t pageNumber, char *text, uint8_t length);

  // raw packet functions, the Adafruit library only supports packets with up to 64 bytes payload (data packets have up to 256)
  void flushInput();
  void writeRawPacket(uint8_t type, const uint8_t *payload, uint16_t length);
  int readRawPacket(uint8_t *payload, uint16_t maxLength, uint16_t &length, uint32_t timeoutMs);

  uint8_t touchRingActiveColor = 2;
  uint8_t touchRingActiveSequence = 1;
  uint8_t scanColor = 1;
  uint8_t matchColor = 2;

public:
  bool connected;
  bool connect();
  Match scanFingerprint();
  NewFinger enrollFinger(int id, String name);
  void deleteFinger(int id);
  void renameFinger(int id, String newName);
  String getFingerListAsHtmlOptionList();
  void setIgnoreTouchRing(bool state);
  bool getIgnoreTouchRing();
  void setLedRingEnabled(bool state);
  bool getLedRingEnabled();
  void setLedRingSuppressed(bool state);
  void setLockout(bool state);
  void showAccessDenied();
  bool hasFinger(int id);
  String getFingerName(int id);
  int getFingerCount();
  uint16_t getCapacity();
  bool isFingerOnSensor();
  void setLedRingError();
  void setLedRingWifiConfig();
  void setLedRingReady();
  String getPairingCode();
  bool setPairingCode(String pairingCode);

  void configTouchRingActive(uint8_t color, uint8_t sequence, uint8_t scanColor, uint8_t matchColor);
  bool deleteAll();

  // template transfer for backup/restore
  bool readIndexTable(bool *occupied, uint16_t slots);
  int readTemplate(uint16_t id, uint8_t *buffer, size_t bufferSize); // returns template size or -1
  bool writeTemplate(uint16_t id, const uint8_t *data, size_t length);
  void setFingerName(int id, const String &name); // stores the name for an existing template (prefs + list)
};

#endif