#include "FingerprintManager.h"
#include "global.h"

#include <Adafruit_Fingerprint.h>
#include "SerialRedirect.h" // must stay the last include

bool FingerprintManager::connect(bool quick)
{

  // initialize input pins
  pinMode(touchRingPin, INPUT_PULLDOWN);

  Serial.println("\n\nSearching for fingerprint sensor..");

  // open the sensor serial port on the configured pins
  startSerial(!quick);
  delay(50);
  if (finger.verifyPassword())
  {
    Serial.println("Found fingerprint sensor!");
  }
  else if (quick)
  {
    connected = false;
    return connected;
  }
  else
  {
    delay(5000); // wait a bit longer for sensor to start before 2nd try (usually after a OTA-Update the esp32 is faster with startup than the fingerprint sensor)
    if (finger.verifyPassword())
    {
      Serial.println("Found fingerprint sensor!");
    }
    else
    {
      Serial.println("Did not find fingerprint sensor :(");
      connected = false;
      return connected;
    }
  }
  finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_BLUE, 0); // sensor connected signal

  Serial.println(F("Reading sensor parameters"));
  finger.getParameters();
  Serial.print(F("Status: 0x"));
  Serial.println(finger.status_reg, HEX);
  Serial.print(F("Sys ID: 0x"));
  Serial.println(finger.system_id, HEX);
  Serial.print(F("Capacity: "));
  Serial.println(finger.capacity);
  Serial.print(F("Security level: "));
  Serial.println(finger.security_level);
  Serial.print(F("Device address: "));
  Serial.println(finger.device_addr, HEX);
  Serial.print(F("Packet len: "));
  Serial.println(finger.packet_len);
  Serial.print(F("Baud rate: "));
  Serial.println(finger.baud_rate);

  finger.getTemplateCount();
  Serial.print("Sensor contains ");
  Serial.print(finger.templateCount);
  Serial.println(" templates");

  loadFingerListFromPrefs();

  // templates on the sensor we don't have a name for (e.g. enrolled with another firmware) are shown without name
  {
    bool occupied[256];
    uint16_t slots = finger.capacity > 0 && finger.capacity <= 256 ? finger.capacity : 200;
    if (readIndexTable(occupied, slots))
    {
      for (uint16_t i = 1; i < slots && i <= 200; i++)
      {
        if (occupied[i] && fingerList[i].compareTo("@empty") == 0)
          fingerList[i] = "";
      }
    }
  }

  connected = true;
  return connected;

  // updateTouchState(false);
}

bool FingerprintManager::ping()
{
  return finger.verifyPassword();
}

void FingerprintManager::setPins(int8_t rx, int8_t tx, int8_t touchRing)
{
  sensorRxPin = rx;
  sensorTxPin = tx;
  touchRingPin = touchRing;
}

// (re)opens the serial port to the sensor. The Adafruit library's begin() would always use the default pins of Serial2,
// so the port is opened here, the library only uses the stream.
void FingerprintManager::startSerial(bool waitForSensorBoot)
{
  mySerial.end();
  delay(waitForSensorBoot ? 1000 : 100); // the sensor needs a moment to boot after power-up
  mySerial.begin(57600, SERIAL_8N1, sensorRxPin, sensorTxPin);
}

void FingerprintManager::restartSerial()
{
  startSerial(false);
  delay(50);
}

void FingerprintManager::markDisconnected()
{
  connected = false;
  lastTouchState = false;
}

void FingerprintManager::updateTouchState(bool touched)
{
  if ((touched != lastTouchState) || (ignoreTouchRing != lastIgnoreTouchRing))
  {
    // check if sensor or ring is touched
    if (touched)
    {
      // turn touch indicator on:
      finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, scanColor, 0);
    }
    else
    {
      // turn touch indicator off:
      setLedRingReady();
    }
  }
  lastTouchState = touched;
  lastIgnoreTouchRing = ignoreTouchRing;
}

Match FingerprintManager::scanFingerprint()
{

  Match match;
  match.scanResult = ScanResult::error;

  if (!connected)
  {
    return match;
  }

  // finger detection by capacitive touchRing state (increased sensitivy but error prone due to rain)
  bool ringTouched = false;
  if (!ignoreTouchRing)
  {
    if (isRingTouched())
      ringTouched = true;
    if (ringTouched || lastTouchState)
    {
      updateTouchState(true);
      // Serial.println("touched");
    }
    else
    {
      updateTouchState(false);
      match.scanResult = ScanResult::noFinger;
      return match;
    }
  }

  bool doAnotherScan = true;
  int scanPass = 0;
  while (doAnotherScan)
  {
    doAnotherScan = false;
    scanPass++;

    ///////////////////////////////////////////////////////////
    // STEP 1: Get Image from Sensor
    ///////////////////////////////////////////////////////////
    bool doImaging = true;
    int imagingPass = 0;
    while (doImaging)
    {
      doImaging = false;
      imagingPass++;
      // Serial.println(String("Get Image try ") + imagingPass);
      match.returnCode = finger.getImage();
      switch (match.returnCode)
      {
      case FINGERPRINT_OK:
        // Important: do net set touch state to true yet! Reason:
        // - if touchRing is NOT ignored, updateTouchState(true) was already called a few lines up, ring is already flashing red
        // - if touchRing IS ignored, wait for next step because image still can be "too messy" (=raindrop on sensor), and we don't want to flash red in this case
        // updateTouchState(true);
        // Serial.println("Image taken");
        break;
      case FINGERPRINT_NOFINGER:
      case FINGERPRINT_PACKETRECIEVEERR: // occurs from time to time, handle it like a "nofinger detected but touched" situation
        if (ringTouched)
        {
          // no finger on sensor but ring was touched -> ring event
          // Serial.println("ring touched");
          updateTouchState(true);
          if (imagingPass < 15) // up to x image passes in a row are taken after touch ring was touched until noFinger will raise a noMatchFound event
          {
            doImaging = true; // scan another image
            // delay(50);
            break;
          }
          else
          {
            // Serial.println("15 times no image after touching ring");
            match.scanResult = ScanResult::noMatchFound;
            return match;
          }
        }
        else
        {
          if (ignoreTouchRing && scanPass > 1)
          {
            // the scan(s) in last iteration(s) have not found any match, now the finger was released (=no finger) -> return "no match" as result
            match.scanResult = ScanResult::noMatchFound;
          }
          else
          {
            match.scanResult = ScanResult::noFinger;
            updateTouchState(false);
          }
          return match;
        }
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error");
        updateTouchState(true);
        return match;
      default:
        Serial.println("Unknown error");
        return match;
      }
    }

    ///////////////////////////////////////////////////////////
    // STEP 2: Convert Image to feature map
    ///////////////////////////////////////////////////////////
    match.returnCode = finger.image2Tz();
    switch (match.returnCode)
    {
    case FINGERPRINT_OK:
      // Serial.println("Image converted");
      updateTouchState(true);
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      return match;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      return match;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      return match;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return match;
    default:
      Serial.println("Unknown error");
      return match;
    }

    ///////////////////////////////////////////////////////////
    // STEP 3: Search DB for matching features
    ///////////////////////////////////////////////////////////
    match.returnCode = finger.fingerSearch();
    if (match.returnCode == FINGERPRINT_OK)
    {
      // found a match!
      finger.LEDcontrol(FINGERPRINT_LED_ON, 0, matchColor);

      match.scanResult = ScanResult::matchFound;
      match.matchId = finger.fingerID;
      match.matchConfidence = finger.confidence;
      match.matchName = fingerList[finger.fingerID];
    }
    else if (match.returnCode == FINGERPRINT_PACKETRECIEVEERR)
    {
      Serial.println("Communication error");
    }
    else if (match.returnCode == FINGERPRINT_NOTFOUND)
    {
      Serial.println(String("Did not find a match. (Scan #") + scanPass + String(" of 5)"));
      match.scanResult = ScanResult::noMatchFound;
      if (scanPass < 5) // max 5 Scans until no match found is given back as result
        doAnotherScan = true;
    }
    else
    {
      Serial.println("Unknown error");
    }

  } // while

  return match;
}

// Preferences
void FingerprintManager::loadFingerListFromPrefs()
{
  Preferences preferences;
  preferences.begin("fingerList", true);
  int counter = 0;
  for (int i = 1; i <= 200; i++)
  {
    String key = String(i);
    if (preferences.isKey(key.c_str()))
    {
      fingerList[i] = preferences.getString(key.c_str(), String("@empty"));
      counter++;
    }
    else
      fingerList[i] = String("@empty");
  }
  Serial.println(String(counter) + " fingers loaded from preferences.");
  if (counter != finger.templateCount)
    notifyClients(String("Warning: Fingerprint count mismatch! ") + finger.templateCount + " fingerprints stored on sensor, but we are aware of " + counter + " fingerprints.");
  preferences.end();
}

// Add/Enroll fingerprint
NewFinger FingerprintManager::enrollFinger(int id, String name)
{
  const int maxTimeout = 20; // sec
  NewFinger newFinger;
  newFinger.enrollResult = EnrollResult::error;

  lastTouchState = true; // after enrollment, scan mode kicks in again. Force update of the ring light back to normal on first iteration of scan mode.

  notifyClients(String("Enrollment for id #") + id + " started. We need to scan your finger 5 times until enrollment is completed.");

  // Repeat n times to get better resulting templates (as stated in R503 documentation up to 6 combined image samples possible, but I got an communication error when trying more than 5 samples, so dont go >5)
  for (int nTimes = 1; nTimes <= 5; nTimes++)
  {
    int timeout = 0;
    notifyClients(String("Take #" + String(nTimes)) + " (place your finger on the sensor until led ring stops flashing, then remove it).");
    onEnrollProgress(String("step ") + nTimes + "/5");

    if (nTimes != 1) // not on first run
    {
      // wait until the finger was lifted from the sensor
      newFinger.returnCode = 0xFF;
      while (newFinger.returnCode != FINGERPRINT_NOFINGER)
      {
        newFinger.returnCode = finger.getImage();
        if (newFinger.returnCode != FINGERPRINT_NOFINGER)
        {
          delay(100);
          timeout++;
          if (timeout > maxTimeout * 10)
          {
            Serial.println("finger not lifted, timeout!");
            notifyClients("Enrollment timeout: finger was not lifted from the sensor.");
            return newFinger;
          }
        }
      }
      timeout = 0;
    }

    Serial.print("Taking image sample ");
    Serial.print(nTimes);
    Serial.print(": ");
    finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_PURPLE, 0);
    newFinger.returnCode = 0xFF;
    while (newFinger.returnCode != FINGERPRINT_OK)
    {
      newFinger.returnCode = finger.getImage();
      switch (newFinger.returnCode)
      {
      case FINGERPRINT_OK:
        Serial.print("taken, ");
        break;
      case FINGERPRINT_NOFINGER:
        delay(100);
        timeout++;
        if (timeout > maxTimeout*10)
        {
          Serial.println("no finger, timeout!");
          notifyClients("Enrollment timeout: no finger was placed on the sensor.");
          return newFinger;
        }
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.print("Communication error, ");
        break;
      case FINGERPRINT_IMAGEFAIL:
        Serial.print("Imaging error, ");
        break;
      default:
        Serial.print("Unknown error, ");
        break;
      }
    }

    // OK success!

    newFinger.returnCode = finger.image2Tz(nTimes);
    switch (newFinger.returnCode)
    {
    case FINGERPRINT_OK:
      Serial.print("converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.print("too messy");
      return newFinger;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.print("Communication error");
      return newFinger;
    case FINGERPRINT_FEATUREFAIL:
      Serial.print("Could not find fingerprint features");
      return newFinger;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.print("Could not find fingerprint features");
      return newFinger;
    default:
      Serial.print("Unknown error");
      return newFinger;
    }
    finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_PURPLE);
  }

  // OK converted!
  Serial.println();
  Serial.print("Creating model for #");
  Serial.println(id);

  newFinger.returnCode = finger.createModel();
  if (newFinger.returnCode == FINGERPRINT_OK)
  {
    Serial.println("Prints matched!");
  }
  else if (newFinger.returnCode == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error");
    return newFinger;
  }
  else if (newFinger.returnCode == FINGERPRINT_ENROLLMISMATCH)
  {
    Serial.println("Fingerprints did not match");
    return newFinger;
  }
  else
  {
    Serial.println("Unknown error");
    return newFinger;
  }

  Serial.print("ID ");
  Serial.println(id);
  newFinger.returnCode = finger.storeModel(id);
  if (newFinger.returnCode == FINGERPRINT_OK)
  {
    Serial.println("Stored!");
    newFinger.enrollResult = EnrollResult::ok;
    // save to prefs
    fingerList[id] = name;
    Preferences preferences;
    preferences.begin("fingerList", false);
    preferences.putString(String(id).c_str(), name);
    preferences.end();
  }
  else if (newFinger.returnCode == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error");
    return newFinger;
  }
  else if (newFinger.returnCode == FINGERPRINT_BADLOCATION)
  {
    Serial.println("Could not store in that location");
    return newFinger;
  }
  else if (newFinger.returnCode == FINGERPRINT_FLASHERR)
  {
    Serial.println("Error writing to flash");
    return newFinger;
  }
  else
  {
    Serial.println("Unknown error");
    return newFinger;
  }

  // finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, FINGERPRINT_LED_RED);

  return newFinger;
}

void FingerprintManager::deleteFinger(int id)
{

  if ((id > 0) && (id <= 200))
  {
    int8_t result = finger.deleteModel(id);
    if (result != FINGERPRINT_OK)
    {
      notifyClients(String("Delete of finger template #") + id + " from sensor failed with code " + result);
      return;
    }
    else
    {
      fingerList[id] = "@empty";
      Preferences preferences;
      preferences.begin("fingerList", false);
      preferences.remove(String(id).c_str());
      preferences.end();
      Serial.println(String("Finger template #") + id + " deleted from sensor and prefs.");
    }
  }
}

void FingerprintManager::renameFinger(int id, String newName)
{
  if ((id > 0) && (id <= 200))
  {
    Preferences preferences;
    preferences.begin("fingerList", false);
    preferences.putString(String(id).c_str(), newName);
    preferences.end();
    Serial.println(String("Finger template #") + id + " renamed from " + fingerList[id] + " to " + newName);
    fingerList[id] = newName;
  }
}

String FingerprintManager::getFingerListAsHtmlOptionList()
{
  String htmlOptions = "";
  int counter = 0;
  for (int i = 1; i <= 200; i++)
  {
    if (fingerList[i].compareTo("@empty") != 0)
    {
      String option;
      if (counter == 0)
        option = "<option value=\"" + String(i) + "\" selected>" + String(i) + " - " + htmlEscape(fingerList[i]) + "</option>";
      else
        option = "<option value=\"" + String(i) + "\">" + String(i) + " - " + htmlEscape(fingerList[i]) + "</option>";
      htmlOptions += option;
      counter++;
    }
  }
  return htmlOptions;
}

void FingerprintManager::setIgnoreTouchRing(bool state)
{
  if (ignoreTouchRing != state)
  {
    ignoreTouchRing = state;
    if (state == true)
      notifyClients("IgnoreTouchRing is now 'on'");
    else
      notifyClients("IgnoreTouchRing is now 'off'");
  }
}

bool FingerprintManager::getIgnoreTouchRing()
{
  return ignoreTouchRing;
}

void FingerprintManager::setLedRingEnabled(bool state)
{
  if (ledRingEnabled != state)
  {
    ledRingEnabled = state;
    if (state == true)
      notifyClients("LED ring is now 'on'");
    else
      notifyClients("LED ring is now 'off'");
    if (connected && !lastTouchState)
      setLedRingReady();
  }
}

bool FingerprintManager::getLedRingEnabled()
{
  return ledRingEnabled;
}

void FingerprintManager::setLedRingSuppressed(bool state)
{
  if (ledRingSuppressed != state)
  {
    ledRingSuppressed = state;
    if (connected && !lastTouchState)
      setLedRingReady();
  }
}

void FingerprintManager::setLockout(bool state)
{
  if (lockedOut != state)
  {
    lockedOut = state;
    if (connected && !lastTouchState)
      setLedRingReady();
  }
}

void FingerprintManager::showAccessDenied()
{
  finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 15, FINGERPRINT_LED_RED, 3);
}

bool FingerprintManager::hasFinger(int id)
{
  return (id > 0) && (id <= 200) && (fingerList[id].compareTo("@empty") != 0);
}

String FingerprintManager::getFingerName(int id)
{
  if (!hasFinger(id))
    return "";
  return fingerList[id];
}

int FingerprintManager::getFingerCount()
{
  int count = 0;
  for (int i = 1; i <= 200; i++)
  {
    if (hasFinger(i))
      count++;
  }
  return count;
}

uint16_t FingerprintManager::getCapacity()
{
  return finger.capacity;
}

bool FingerprintManager::isRingTouched()
{
  if (digitalRead(touchRingPin) == LOW) // LOW = touched. Caution: touchSignal on this pin occour only once (at beginning of touching the ring, not every iteration if you keep your finger on the ring)
    return true;
  else
    return false;
}

bool FingerprintManager::isFingerOnSensor()
{
  // get an image
  uint8_t returnCode = finger.getImage();
  if (returnCode == FINGERPRINT_OK)
  {
    // try to find fingerprint features in image, because image taken does not already means finger on sensor, could also be a raindrop
    returnCode = finger.image2Tz();
    if (returnCode == FINGERPRINT_OK)
      return true;
  }
  return false;
}

void FingerprintManager::setLedRingError()
{
  finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_RED);
}

void FingerprintManager::setLedRingWifiConfig()
{
  finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 250, FINGERPRINT_LED_RED);
}

void FingerprintManager::setLedRingReady()
{
  if (lockedOut)
    finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_RED);
  else if (!ledRingEnabled || ledRingSuppressed)
    finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, touchRingActiveColor);
  else if (!ignoreTouchRing)
    finger.LEDcontrol(touchRingActiveSequence, 250, touchRingActiveColor);
  else
    finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_BLUE); // just an indicator for me to see if touch ring is active or not
}

bool FingerprintManager::getRingPinTouched()
{
  return isRingTouched();
}

void FingerprintManager::getRingState(uint8_t &color, uint8_t &sequence, String &reason)
{
  color = 0;
  sequence = FINGERPRINT_LED_OFF;
  if (!connected)
    reason = "sensor not connected";
  else if (lastTouchState)
  {
    color = scanColor;
    sequence = FINGERPRINT_LED_FLASHING;
    reason = "ring touched, scanning";
  }
  else if (lockedOut)
  {
    color = FINGERPRINT_LED_RED;
    sequence = FINGERPRINT_LED_BREATHING;
    reason = "lockout after too many failed attempts";
  }
  else if (!ledRingEnabled)
    reason = "LED ring switched off";
  else if (ledRingSuppressed)
    reason = "idle light off (night mode or nobody near the door)";
  else if (!ignoreTouchRing)
  {
    color = touchRingActiveColor;
    sequence = touchRingActiveSequence;
    reason = "idle, waiting for a touch";
  }
  else
  {
    color = FINGERPRINT_LED_BLUE;
    sequence = FINGERPRINT_LED_ON;
    reason = "touch ring is ignored, scanning without ring signal";
  }
}

bool FingerprintManager::deleteAll()
{
  if (finger.emptyDatabase() == FINGERPRINT_OK)
  {
    bool rc;
    Preferences preferences;
    rc = preferences.begin("fingerList", false);
    if (rc)
      rc = preferences.clear();
    preferences.end();

    for (int i = 1; i <= 200; i++)
    {
      fingerList[i] = String("@empty");
    };

    return rc;
  }
  else
    return false;
}

uint8_t FingerprintManager::writeNotepad(uint8_t pageNumber, const char *text, uint8_t length)
{
  uint8_t data[34];

  if (length > 32)
    length = 32;

  data[0] = FINGERPRINT_WRITENOTEPAD;
  data[1] = pageNumber;
  for (int i = 0; i < length; i++)
    data[i + 2] = text[i];

  Adafruit_Fingerprint_Packet packet(FINGERPRINT_COMMANDPACKET, sizeof(data), data);
  finger.writeStructuredPacket(packet);
  if (finger.getStructuredPacket(&packet) != FINGERPRINT_OK)
    return FINGERPRINT_PACKETRECIEVEERR;
  if (packet.type != FINGERPRINT_ACKPACKET)
    return FINGERPRINT_PACKETRECIEVEERR;
  return packet.data[0];
}

uint8_t FingerprintManager::readNotepad(uint8_t pageNumber, char *text, uint8_t length)
{
  uint8_t data[2];

  data[0] = FINGERPRINT_READNOTEPAD;
  data[1] = pageNumber;

  Adafruit_Fingerprint_Packet packet(FINGERPRINT_COMMANDPACKET, sizeof(data), data);
  finger.writeStructuredPacket(packet);
  if (finger.getStructuredPacket(&packet) != FINGERPRINT_OK)
    return FINGERPRINT_PACKETRECIEVEERR;
  if (packet.type != FINGERPRINT_ACKPACKET)
    return FINGERPRINT_PACKETRECIEVEERR;

  if (packet.data[0] == FINGERPRINT_OK)
  {
    // read data payload
    for (uint8_t i = 0; i < length; i++)
    {
      text[i] = packet.data[i + 1];
    }
  }

  return packet.data[0];
}

String FingerprintManager::getPairingCode()
{
  char buffer[33];
  buffer[32] = 0; // null termination needed for convertion to string at the end
  if (readNotepad(0, (char *)buffer, 32) == FINGERPRINT_OK)
    return String((char *)buffer);
  else
    return "";
}

bool FingerprintManager::setPairingCode(String pairingCode)
{
  if (writeNotepad(0, pairingCode.c_str(), 32) == FINGERPRINT_OK)
    return true;
  else
    return false;
}

void FingerprintManager::flushInput()
{
  while (mySerial.available())
    mySerial.read();
}

void FingerprintManager::writeRawPacket(uint8_t type, const uint8_t *payload, uint16_t length)
{
  uint16_t wireLength = length + 2; // payload + checksum
  uint8_t header[9] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, type, (uint8_t)(wireLength >> 8), (uint8_t)(wireLength & 0xFF)};
  uint16_t sum = type + (wireLength >> 8) + (wireLength & 0xFF);
  for (uint16_t i = 0; i < length; i++)
    sum += payload[i];
  mySerial.write(header, sizeof(header));
  mySerial.write(payload, length);
  mySerial.write((uint8_t)(sum >> 8));
  mySerial.write((uint8_t)(sum & 0xFF));
}

// reads one packet, returns the packet type or -1 on timeout/checksum error. 'length' is the payload length without checksum.
int FingerprintManager::readRawPacket(uint8_t *payload, uint16_t maxLength, uint16_t &length, uint32_t timeoutMs)
{
  uint8_t header[9];
  uint16_t idx = 0;
  uint16_t wireLength = 0;
  uint16_t sum = 0;
  uint8_t type = 0;
  uint8_t checksum[2];
  unsigned long start = millis();
  length = 0;

  while (true)
  {
    if (!mySerial.available())
    {
      if (millis() - start > timeoutMs)
        return -1;
      delay(1);
      continue;
    }
    uint8_t b = mySerial.read();
    if (idx < 9)
    {
      if ((idx == 0 && b != 0xEF) || (idx == 1 && b != 0x01))
      {
        idx = 0; // resync on start code
        continue;
      }
      header[idx++] = b;
      if (idx == 9)
      {
        type = header[6];
        wireLength = ((uint16_t)header[7] << 8) | header[8];
        if (wireLength < 2 || wireLength - 2 > maxLength)
          return -1;
        sum = type + header[7] + header[8];
      }
    }
    else if (idx < 9 + wireLength - 2)
    {
      payload[idx - 9] = b;
      sum += b;
      idx++;
    }
    else
    {
      checksum[idx - (9 + wireLength - 2)] = b;
      idx++;
      if (idx == 9 + wireLength)
      {
        if ((((uint16_t)checksum[0] << 8) | checksum[1]) != sum)
          return -1;
        length = wireLength - 2;
        return type;
      }
    }
  }
}

// reads the bitmap of used template slots ("ReadIndexTable", 32 bytes per 256 slots)
bool FingerprintManager::readIndexTable(bool *occupied, uint16_t slots)
{
  for (uint16_t i = 0; i < slots; i++)
    occupied[i] = false;

  for (uint8_t page = 0; page * 256 < slots; page++)
  {
    uint8_t command[2] = {0x1F, page};
    uint8_t response[40];
    uint16_t length;
    flushInput();
    writeRawPacket(FINGERPRINT_COMMANDPACKET, command, sizeof(command));
    if (readRawPacket(response, sizeof(response), length, 1000) != FINGERPRINT_ACKPACKET || length < 33 || response[0] != FINGERPRINT_OK)
      return false;
    for (uint16_t byteIdx = 0; byteIdx < 32; byteIdx++)
    {
      for (uint8_t bit = 0; bit < 8; bit++)
      {
        uint16_t slot = page * 256 + byteIdx * 8 + bit;
        if (slot < slots && (response[1 + byteIdx] & (1 << bit)))
          occupied[slot] = true;
      }
    }
  }
  return true;
}

// loads a template from the sensor flash into char buffer 1 and transfers it to the ESP ("UpChar")
int FingerprintManager::readTemplate(uint16_t id, uint8_t *buffer, size_t bufferSize)
{
  flushInput();
  if (finger.loadModel(id) != FINGERPRINT_OK)
    return -1;

  uint8_t command[2] = {FINGERPRINT_UPLOAD, 0x01};
  uint8_t packet[300];
  uint16_t length;
  flushInput();
  writeRawPacket(FINGERPRINT_COMMANDPACKET, command, sizeof(command));
  if (readRawPacket(packet, sizeof(packet), length, 1000) != FINGERPRINT_ACKPACKET || length < 1 || packet[0] != FINGERPRINT_OK)
    return -1;

  size_t total = 0;
  while (true)
  {
    int type = readRawPacket(packet, sizeof(packet), length, 1000);
    if (type != FINGERPRINT_DATAPACKET && type != FINGERPRINT_ENDDATAPACKET)
      return -1;
    if (total + length > bufferSize)
      return -1;
    memcpy(buffer + total, packet, length);
    total += length;
    if (type == FINGERPRINT_ENDDATAPACKET)
      return total;
  }
}

// transfers a template to char buffer 1 ("DownChar") and stores it in the given slot
bool FingerprintManager::writeTemplate(uint16_t id, const uint8_t *data, size_t length)
{
  uint8_t command[2] = {0x09, 0x01};
  uint8_t response[16];
  uint16_t responseLength;
  flushInput();
  writeRawPacket(FINGERPRINT_COMMANDPACKET, command, sizeof(command));
  if (readRawPacket(response, sizeof(response), responseLength, 1000) != FINGERPRINT_ACKPACKET || responseLength < 1 || response[0] != FINGERPRINT_OK)
    return false;

  uint16_t chunk = finger.packet_len;
  if (chunk == 0 || chunk > 256)
    chunk = 128;
  size_t offset = 0;
  while (offset < length)
  {
    uint16_t n = (length - offset > chunk) ? chunk : (length - offset);
    bool last = (offset + n >= length);
    writeRawPacket(last ? FINGERPRINT_ENDDATAPACKET : FINGERPRINT_DATAPACKET, data + offset, n);
    offset += n;
  }
  mySerial.flush(); // wait until everything is sent
  delay(50);
  flushInput(); // the Adafruit functions don't discard unexpected bytes, so make sure the store answer is the next packet

  return finger.storeModel(id) == FINGERPRINT_OK;
}

void FingerprintManager::setFingerName(int id, const String &name)
{
  if ((id > 0) && (id <= 200))
  {
    fingerList[id] = name;
    Preferences preferences;
    preferences.begin("fingerList", false);
    preferences.putString(String(id).c_str(), name);
    preferences.end();
  }
}

void FingerprintManager::configTouchRingActive(uint8_t color, uint8_t sequence, uint8_t scanColor, uint8_t matchColor)
{
  this->touchRingActiveColor = color;
  this->touchRingActiveSequence = sequence;
  this->scanColor = scanColor;
  this->matchColor = matchColor;
}
