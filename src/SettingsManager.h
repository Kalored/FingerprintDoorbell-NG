#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <Preferences.h>
#include "global.h"
#include "GpioConfig.h"

struct WifiSettings
{
  String ssid = "";
  String password = "";
  String hostname = "";
};

struct AppSettings
{
  String webPassword = "";
  String mqttServer = "";
  String mqttUsername = "";
  String mqttPassword = "";
  String mqttRootTopic = "fingerprintDoorbell";
  bool haDiscovery = true;
  uint16_t mqttPort = 1883;
  String mqttClientId = ""; // empty = use hostname
  uint16_t mqttKeepAlive = 15; // seconds
  bool mqttRetain = false;     // publish event topics (ring, match...) with retain flag
  bool mqttPublishLog = true;  // publish log messages to topicLastLog
  String payloadOn = "on";     // used for ring and switch states, commands also accept on/true/1
  String payloadOff = "off";
  // sub topics below the root topic
  String topicLwt = "LWT";
  String topicRing = "ring";
  String topicMatchId = "matchId";
  String topicMatchName = "matchName";
  String topicMatchConfidence = "matchConfidence";
  String topicLastLog = "lastLogMessage";
  String topicIgnoreTouchRing = "ignoreTouchRing";
  String topicLedRing = "ledRing";
  String topicOpenDoor = "openDoor";
  bool mqttEnroll = false; // allow to start an enrollment over MQTT / Home Assistant
  // brute force and ring spam protection
  uint8_t failMax = 8;       // failed attempts within failWindow until lockout, 0 = disabled
  uint16_t failWindow = 120; // seconds
  uint16_t lockoutTime = 300; // seconds
  uint16_t ringCooldown = 0; // seconds between two ring events, 0 = disabled
  // night mode: idle LED ring off between nightStart and nightEnd (minutes since midnight)
  bool nightEnabled = false;
  uint16_t nightStart = 22 * 60;
  uint16_t nightEnd = 6 * 60;
  // radar sensor (e.g. HLK-LD2410) connected with its OUT pin
  bool radarEnabled = false;
  int8_t radarPin = 32;
  bool radarActiveHigh = true;
  bool radarLedWake = false; // idle LED ring only on while somebody is detected
  bool ignoreTouchRing = false; // scan without the touch ring signal (e.g. wake-up wire not connected), kept after a restart
  String language = "en";    // web interface language: "en" or "de" (translated in the browser by i18n.js)
  PinSet pins;               // GPIOs of the sensor UART, touch ring, doorbell and door opener (applied after a restart)
  String ntpServer = "pool.ntp.org";
  String timezone = "CET-1CEST,M3.5.0,M10.5.0/3"; // POSIX TZ string, this is what is applied
  String timezoneName = "Europe/Berlin";              // name selected in the web UI, empty = custom POSIX string
  String sensorPin = "00000000";
  String sensorPairingCode = "";
  int8_t touchRingActiveColor = 2;
  int8_t touchRingActiveSequence = 1;
  int8_t scanColor = 1;
  int8_t matchColor = 3;
  bool sensorPairingValid = false;
};

class SettingsManager
{
private:
  WifiSettings wifiSettings;
  AppSettings appSettings;

  void saveWifiSettings();
  void saveAppSettings();

public:
  bool loadWifiSettings();
  bool loadAppSettings();

  const WifiSettings &getWifiSettings();
  void saveWifiSettings(WifiSettings newSettings);

  const AppSettings &getAppSettings();
  void saveAppSettings(AppSettings newSettings);

  bool isWifiConfigured();

  bool deleteAppSettings();
  bool deleteWifiSettings();

  String generateNewPairingCode();
};

#endif