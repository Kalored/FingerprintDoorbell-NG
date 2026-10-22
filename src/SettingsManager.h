#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <Preferences.h>
#include "global.h"

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