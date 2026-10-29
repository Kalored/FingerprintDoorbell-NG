/***************************************************
  Main of FingerprintDoorbell
 ****************************************************/

#include <WiFi.h>
#include <DNSServer.h>
#include <time.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncHTTPUpdateServer.h>
#include <SPIFFS.h>
#include <PubSubClient.h>
#include <lwip/apps/sntp.h>
#include "FingerprintManager.h"
#include "SettingsManager.h"
#include "EventLog.h"
#include "AccessControl.h"
#include "BackupManager.h"
#include "global.h"
#include "SerialRedirect.h" // must stay the last include

enum class Mode
{
  scan,
  enroll,
  wificonfig,
  maintenance,
  backup,
  restore
};

const char *VersionInfo = "0.13.0";

// ===================================================================================================================
// Caution: below are not the credentials for connecting to your home network, they are for the Access Point mode!!!
// ===================================================================================================================
const char *WifiConfigSsid = "FingerprintDoorbell-Config"; // SSID used for WiFi when in Access Point mode for configuration
const char *WifiConfigPassword = "12345678";               // password used for WiFi when in Access Point mode for configuration. Min. 8 chars needed!
IPAddress WifiConfigIp(192, 168, 4, 1);                    // IP of access point in wifi config mode

const char *Selected = "selected"; // password used for WiFi when in Access Point mode for configuration. Min. 8 chars needed!
const char *Checked = "checked";   // password used for WiFi when in Access Point mode for configuration. Min. 8 chars needed!

// GPIOs, configurable in the web UI (Settings, GPIO pins), applied at boot from the stored settings
int doorbellOutputPin = 19;   // pin connected to the doorbell (when using hardware connection instead of mqtt to ring the bell)
int dooropenerOutputPin = 23; // pin connected to the door opener (when using hardware connection instead of mqtt to open the door)
PinSet activePins;            // pins in use since the last boot, differences to the settings need a restart
String gpioMessage = "";      // result of the last attempt to save GPIO settings, shown on the settings page

const int statusLed = LED_BUILTIN;

#ifdef CUSTOM_GPIOS
const int customOutput1 = 18; // not used internally, but can be set over MQTT
const int customOutput2 = 26; // not used internally, but can be set over MQTT
const int customInput1 = 21;  // not used internally, but changes are published over MQTT
const int customInput2 = 22;  // not used internally, but changes are published over MQTT
bool customInput1Value = false;
bool customInput2Value = false;
#endif

const int logMessagesCount = 50;      // kept in RAM, shown on the log page
const int logPanelMessagesCount = 5;  // shown in the log panel on top of every page
String logMessages[logMessagesCount]; // log messages, 0=most recent log message
bool shouldReboot = false;
unsigned long wifiReconnectPreviousMillis = 0;
unsigned long wifiConnectAttempts = 0;
bool wifiEverConnected = false;
volatile uint8_t lastWifiDisconnectReason = 0; // set by the WiFi event handler
volatile unsigned long wifiDisconnectEvents = 0;
unsigned long mqttReconnectPreviousMillis = 0;

String enrollId;
String enrollName;
Mode currentMode = Mode::scan;

FingerprintManager fingerManager;
SettingsManager settingsManager;
EventLog eventLog;
AccessControl accessControl;
BackupManager backupManager;
bool needMaintenanceMode = false;
BackupManager::RestoreMode restoreMode = BackupManager::RestoreMode::overwrite;
volatile uint16_t backupSingleId = 0; // 0 = all fingerprints, set by the web server together with backupRequested
volatile bool backupRequested = false;  // set by the web server, executed by the loop task
volatile bool restoreRequested = false;
volatile bool clearEventsRequested = false;
volatile bool applySettingsRequested = false; // settings saved in the web UI, applied by the loop task without reboot
volatile bool applyMqttRequested = false;
String oldLwtTopic; // LWT topic before the MQTT settings were changed, "Offline" is published there before reconnecting

// ring spam protection, night mode, radar
unsigned long lastRingMillis = 0;
bool ringEverHappened = false;
bool nightModeActive = false;
bool presenceDetected = false;
bool lastPublishedPresence = false;
unsigned long presenceChangeMillis = 0;
unsigned long lastInfoPublishMillis = 0;
unsigned long lastNightCheckMillis = 0;
unsigned long accessDeniedMillis = 0; // accessDenied topic is reset after 3s

const byte DNS_PORT = 53;
DNSServer dnsServer;
ESPAsyncHTTPUpdateServer updateServer;
AsyncWebServer webServer(80);       // AsyncWebServer  on port 80
AsyncEventSource events("/events"); // event source (Server-Sent events)


const char *webServerUser = "admin"; // User name for authentication
#define MQTT_LWT_RETAIN true
#define MQTT_LWT_QOS 2
#define MQTT_LWT_PAYLOAD_ONLINE "Online"
#define MQTT_LWT_PAYLOAD_OFFLINE "Offline"

#define HA_DISCOVERY_PREFIX "homeassistant"

WiFiClient espClient;
PubSubClient mqttClient(espClient);
String mqttHostname;
u16_t mqttPort = 1883;
long lastMsg = 0;
char msg[50];
int value = 0;
bool mqttConfigValid = true;

// connection statistics for diagnosis (shown on the log page)
bool mqttWasConnected = false;
bool wifiWasConnected = false;
unsigned long mqttConnectCount = 0;
unsigned long mqttLostCount = 0;
unsigned long wifiLostCount = 0;

// fingerprint sensor supervision: the sensor is checked while running and reconnected automatically
const unsigned long SENSOR_CHECK_INTERVAL = 10000ul; // ping while connected
const unsigned long SENSOR_RETRY_INTERVAL = 15000ul; // reconnect attempts while missing
const unsigned long SENSOR_RETRY_WINDOW = 180000ul; // automatic reconnect attempts stop after 3 minutes, then only manual reconnect
const uint8_t SENSOR_MAX_FAILS = 3;                  // failed pings in a row until the sensor counts as lost
unsigned long lastSensorCheckMillis = 0;
unsigned long sensorStateSinceMillis = 0;
unsigned long sensorLostCount = 0;
uint8_t sensorPingFails = 0;
bool sensorAutoRetryActive = false;    // automatic reconnecting within the retry window
bool sensorReconnecting = false;       // manual or automatic reconnect in progress (yellow)
volatile int8_t ignoreRingRequest = -1; // from the web UI: 0/1 = set "ignore touch ring", -1 = nothing
volatile int8_t ledRingRequest = -1;    // from the web UI: 0/1 = set LED ring on/off
volatile bool sensorReconnectRequested = false; // set by the web UI, handled in the loop task

String htmlEscape(const String &text);

// traffic light: green = sensor ok, yellow = problems / reconnecting, red = not connected
String sensorLightState()
{
  if (sensorReconnecting)
    return "yellow";
  if (!fingerManager.connected)
    return "red";
  if (sensorPingFails > 0)
    return "yellow";
  return "green";
}

String sensorStateText()
{
  unsigned long sec = (millis() - sensorStateSinceMillis) / 1000;
  String since = sec < 120 ? String(sec) + " s" : (sec < 7200 ? String(sec / 60) + " min" : String(sec / 3600) + " h");
  if (sensorReconnecting)
    return String("reconnecting...");
  if (fingerManager.connected)
    return String("connected (since ") + since + ", " + sensorLostCount + " losses)";
  if (!sensorAutoRetryActive)
    return String("NOT CONNECTED since ") + since + " (automatic retries stopped, use Reconnect sensor on the Log page)";
  return String("NOT CONNECTED (since ") + since + ", " + sensorLostCount + " losses, retrying automatically)";
}

// traffic light for the navbar (next to the device name), updates itself every 3s
String sensorLightHtml()
{
  String st = sensorLightState();
  auto dot = [&](const char *id, const char *color, bool on)
  {
    return String("<span id=\"") + id + "\" style=\"display:inline-block;width:10px;height:10px;border-radius:5px;margin:0 2px;background:" + color + ";opacity:" + (on ? "1" : "0.18") + "\"></span>";
  };
  String html = "<span id=\"sensorLight\" title=\"" + htmlEscape(sensorStateText()) + "\" style=\"display:inline-flex;align-items:center;vertical-align:middle;margin-left:12px;padding:4px 6px;border-radius:9px;background:#111\">";
  html += dot("slR", "#e53935", st == "red");
  html += dot("slY", "#fdd835", st == "yellow");
  html += dot("slG", "#43a047", st == "green");
  html += "</span><script>(function(){function u(s,t){var o={slR:'red',slY:'yellow',slG:'green'};for(var k in o){var e=document.getElementById(k);if(e)e.style.opacity=(s===o[k])?'1':'0.18';}var l=document.getElementById('sensorLight');if(l)l.title=t||'Fingerprint sensor: unknown';}"
          "function p(){fetch('/sensor/status').then(function(r){return r.json();}).then(function(j){u(j.state,'Fingerprint sensor: '+j.text);}).catch(function(){u('','Fingerprint sensor: no answer from device');});}"
          "setInterval(p,3000);})();</script>";
  return html;
}

Match lastMatch;

// fixed sub topics (below the root topic) of the newer features
#define TOPIC_LOCKOUT "lockout"
#define TOPIC_PRESENCE "presence"
#define TOPIC_NIGHTMODE "nightMode"
#define TOPIC_ACCESS_DENIED "accessDenied"
#define TOPIC_LAST_EVENT "lastEvent"
#define TOPIC_ENROLL "enroll"
#define TOPIC_ENROLL_STATE "enrollState"
#define TOPIC_INFO "info"

// GPIOs that can be used for the radar sensor (not used by the sensor UART, touch ring, outputs or flash)
const int8_t radarPinCandidates[] = {4, 13, 14, 25, 26, 27, 32, 33, 34, 35, 36, 39};

String getFingerListHtml();
String mqttTopic(const String &subTopic);
String getWifiReasonString(uint8_t reason);
String minutesToTimeString(uint16_t minutes);
uint16_t timeStringToMinutes(const String &text, uint16_t defaultValue);
bool isRadarPinAllowed(int pin);
size_t getExtraUsedPins(const AppSettings &settings, int8_t *out, size_t maxCount);
String getGpioOptionsHtml(PinRole role);
String getGpioNoteHtml();
String getRadarPinOptionsHtml();
void updateLedSuppression();
void setupRadarPin();
void applyTimeSettings();
void initMqtt();

void addLogMessage(const String &message)
{
  // shift all messages in array by 1, oldest message will die
  for (int i = logMessagesCount - 1; i > 0; i--)
    logMessages[i] = logMessages[i - 1];
  logMessages[0] = message;
}

String htmlEscape(const String &text);

String getLogMessagesAsHtml(int count = logPanelMessagesCount)
{
  String html = "";
  for (int i = count - 1; i >= 0; i--)
  {
    if (logMessages[i] != "")
      html = html + htmlEscape(logMessages[i]) + "<br>";
  }
  return html;
}

String getTimestampString()
{
  // getLocalTime() would block for 5s while the time is not synchronized, so check the time directly
  time_t now = time(nullptr);
  if (now < 1600000000)
    return "no time";
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);
  String datetime = String(buffer);
  return datetime;
}

String getUptimeString()
{
  unsigned long milli = millis();
  unsigned long secs=milli/1000, mins=secs/60;
  unsigned int hours=mins/60, days=hours/24;
  milli-=secs*1000;
  secs-=mins*60;
  mins-=hours*60;
  hours-=days*24;
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "Uptime %u days %2.2u:%2.2lu:%2.2lu", days, hours, mins, secs);
  String uptime = String(buffer);
  return uptime;
}

// Escapes user defined strings before they are inserted into HTML (attribute values or text)
String htmlEscape(const String &text)
{
  String escaped;
  escaped.reserve(text.length() + 16);
  for (unsigned int i = 0; i < text.length(); i++)
  {
    char c = text.charAt(i);
    switch (c)
    {
    case '&': escaped += "&amp;"; break;
    case '<': escaped += "&lt;"; break;
    case '>': escaped += "&gt;"; break;
    case '"': escaped += "&quot;"; break;
    case '\'': escaped += "&#39;"; break;
    case '%': escaped += "&#37;"; break; // '%' is the placeholder char of the template processor
    default: escaped += c;
    }
  }
  return escaped;
}

// Escapes strings before they are inserted into JSON payloads
String jsonEscape(const String &text)
{
  String escaped;
  escaped.reserve(text.length() + 8);
  for (unsigned int i = 0; i < text.length(); i++)
  {
    char c = text.charAt(i);
    if (c == '"' || c == '\\')
    {
      escaped += '\\';
      escaped += c;
    }
    else if ((uint8_t)c >= 0x20)
      escaped += c;
  }
  return escaped;
}

// like jsonEscape(), but keeps line breaks (for the serial monitor)
String jsonEscapeMultiline(const String &text)
{
  String escaped;
  escaped.reserve(text.length() + 32);
  for (unsigned int i = 0; i < text.length(); i++)
  {
    char c = text.charAt(i);
    if (c == '"' || c == '\\')
    {
      escaped += '\\';
      escaped += c;
    }
    else if (c == '\n')
      escaped += "\\n";
    else if (c == '\t')
      escaped += ' ';
    else if ((uint8_t)c >= 0x20)
      escaped += c;
  }
  return escaped;
}

String splitIpAndPort(String mqttServerConfigString, u16_t &mqttPort)
{
  String mqttHostname;
  s8_t positionOfColon = mqttServerConfigString.indexOf(":");

  if (positionOfColon >= 0)
  {
    mqttHostname = mqttServerConfigString.substring(0, positionOfColon);
    mqttPort = mqttServerConfigString.substring(positionOfColon + 1).toInt();
  }
  else
  {
    mqttHostname = mqttServerConfigString;
    mqttPort = settingsManager.getAppSettings().mqttPort;
  }
  return mqttHostname;
}

// full topic for a sub topic below the configured root topic
String mqttTopic(const String &subTopic)
{
  const String &root = settingsManager.getAppSettings().mqttRootTopic;
  if (root.isEmpty())
    return subTopic;
  return root + "/" + subTopic;
}

// commands accept the configured payloads as well as the common on/off variants
bool isOnPayload(const String &payload)
{
  return payload.equalsIgnoreCase(settingsManager.getAppSettings().payloadOn) || payload.equalsIgnoreCase("on") || payload.equalsIgnoreCase("true") || payload == "1";
}

bool isOffPayload(const String &payload)
{
  return payload.equalsIgnoreCase(settingsManager.getAppSettings().payloadOff) || payload.equalsIgnoreCase("off") || payload.equalsIgnoreCase("false") || payload == "0";
}

const char *onOffPayload(bool state)
{
  return state ? settingsManager.getAppSettings().payloadOn.c_str() : settingsManager.getAppSettings().payloadOff.c_str();
}

// cleans up a topic entered in the web UI: no wildcards, no leading/trailing slashes, default if empty
String sanitizeTopic(String topic, const char *defaultTopic, bool allowEmpty = false)
{
  topic.trim();
  topic.replace("#", "");
  topic.replace("+", "");
  while (topic.startsWith("/"))
    topic.remove(0, 1);
  while (topic.endsWith("/"))
    topic.remove(topic.length() - 1);
  if (topic.isEmpty() && !allowEmpty)
    return String(defaultTopic);
  return topic;
}

// Common time zones (name as shown in the web UI -> POSIX TZ string understood by the ESP32)
struct TimezoneEntry
{
  const char *name;
  const char *posix;
};

const TimezoneEntry timezones[] = {
    {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Vienna", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Zurich", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Brussels", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Luxembourg", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Copenhagen", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Oslo", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Stockholm", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Prague", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Budapest", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Dublin", "IST-1GMT0,M10.5.0,M3.5.0/1"},
    {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Istanbul", "<+03>-3"},
    {"Europe/Moscow", "MSK-3"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"Asia/Dubai", "<+04>-4"},
    {"Asia/Kolkata", "IST-5:30"},
    {"Asia/Shanghai", "CST-8"},
    {"Asia/Tokyo", "JST-9"},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"UTC", "UTC0"},
};
const char *DefaultTimezone = "CET-1CEST,M3.5.0,M10.5.0/3";

const char *findTimezonePosix(const String &name)
{
  for (const TimezoneEntry &tz : timezones)
  {
    if (name.equalsIgnoreCase(tz.name))
      return tz.posix;
  }
  return nullptr;
}

// rough check of a POSIX TZ string ("CET-1CEST,...", "<+03>-3"). An invalid string is silently treated as UTC by the
// C library, which shows up as a time offset of 1-2 hours, so better fall back to a sane default.
bool isValidPosixTimezone(const String &tz)
{
  unsigned int i = 0;
  if (tz.startsWith("<"))
  {
    int end = tz.indexOf('>');
    if (end < 2)
      return false;
    i = end + 1;
  }
  else
  {
    while (i < tz.length() && isalpha(tz.charAt(i)))
      i++;
    if (i < 3)
      return false;
  }
  if (i < tz.length() && (tz.charAt(i) == '+' || tz.charAt(i) == '-'))
    i++;
  return i < tz.length() && isdigit(tz.charAt(i));
}

// accepts a name from the table (e.g. "Europe/Berlin") or a POSIX TZ string, returns a valid POSIX TZ string
String resolveTimezone(String tz)
{
  tz.trim();
  const char *posix = findTimezonePosix(tz);
  if (posix)
    return String(posix);
  if (isValidPosixTimezone(tz))
    return tz;
  return String(DefaultTimezone);
}

String getTimezoneOptionsAsHtml()
{
  const AppSettings &settings = settingsManager.getAppSettings();
  String selectedName = settings.timezoneName;
  if (selectedName.isEmpty())
  {
    // no name stored (older settings): select the first entry with the same POSIX string, if any
    for (const TimezoneEntry &tz : timezones)
    {
      if (settings.timezone == tz.posix)
      {
        selectedName = tz.name;
        break;
      }
    }
  }
  String html;
  bool found = false;
  for (const TimezoneEntry &tz : timezones)
  {
    bool selected = selectedName.equalsIgnoreCase(tz.name);
    found = found || selected;
    html += String("<option value=\"") + tz.name + "\"" + (selected ? " selected" : "") + ">" + tz.name + "</option>";
  }
  html += String("<option value=\"custom\"") + (found ? "" : " selected") + ">Custom (POSIX string)</option>";
  return html;
}

/* wait for maintenance mode or timeout 5s */
bool waitForMaintenanceMode()
{
  needMaintenanceMode = true;
  unsigned long startMillis = millis();
  while (currentMode != Mode::maintenance)
  {
    if ((millis() - startMillis) >= 5000ul)
    {
      needMaintenanceMode = false;
      return false;
    }
    delay(50);
  }
  needMaintenanceMode = false;
  return true;
}

String getMqttStateString(int state)
{
  switch (state)
  {
  case MQTT_CONNECTION_TIMEOUT: return "connection timeout (no answer to keepalive ping)";
  case MQTT_CONNECTION_LOST: return "connection lost (TCP connection closed)";
  case MQTT_CONNECT_FAILED: return "connect failed (broker not reachable)";
  case MQTT_DISCONNECTED: return "disconnected";
  case MQTT_CONNECTED: return "connected";
  case MQTT_CONNECT_BAD_PROTOCOL: return "bad protocol";
  case MQTT_CONNECT_BAD_CLIENT_ID: return "bad client id";
  case MQTT_CONNECT_UNAVAILABLE: return "server unavailable";
  case MQTT_CONNECT_BAD_CREDENTIALS: return "bad credentials";
  case MQTT_CONNECT_UNAUTHORIZED: return "unauthorized";
  default: return String("unknown (") + state + ")";
  }
}

// Replaces placeholder in HTML pages
String processor(const String &var)
{
  if (var == "LOGMESSAGES")
  {
    return getLogMessagesAsHtml();
  }
  else if (var == "FINGERLIST")
  {
    return getFingerListHtml();
  }
  else if (var == "HOSTNAME")
  {
    return htmlEscape(settingsManager.getWifiSettings().hostname);
  }
  else if (var == "VERSIONINFO")
  {
    return VersionInfo;
  }
  else if (var == "LANG")
  {
    return settingsManager.getAppSettings().language == "de" ? "de" : "en";
  }
  else if (var == "IP_ADDRESS")
  {
    return WiFi.localIP().toString();
  }
  else if (var == "MAC_ADDRESS")
  {
    return WiFi.macAddress();
  }
  else if (var == "SSID")
  {
    return htmlEscape(WiFi.SSID());
  }
  else if (var == "BSSID")
  {
    return WiFi.BSSIDstr();
  }
  else if (var == "WIFI_CHANNEL")
  {
    return String(WiFi.channel());
  }
  else if (var == "RSSI")
  {
    return String(WiFi.RSSI()) + " dBm";
  }
  else if (var == "GATEWAY")
  {
    return WiFi.gatewayIP().toString();
  }
  else if (var == "DNS_SERVER")
  {
    return WiFi.dnsIP().toString();
  }
  else if (var == "MQTT_SERVER_IP")
  {
    // no DNS lookup here, it would block the web server: use the address of the current connection
    if (mqttClient.connected())
      return espClient.remoteIP().toString();
    return "not connected";
  }
  else if (var == "CHIP")
  {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    return String(ESP.getChipModel()) + String(" rev:") + String(chip_info.revision);
  }
  else if (var == "CPU")
  {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    return String(ESP.getCpuFreqMHz()) + String(" Mhz ") + String(chip_info.cores) + String(" Core(s)");
  }
  else if (var == "CHIP_TEMP")
  {
    return String(round(temperatureRead() * 10) / 10) + String(" °C");
  }
  else if (var == "FLASH_SIZE")
  {
    return String(ESP.getFlashChipSize());
  }
  else if (var == "FREE_HEAP")
  {
    return String(ESP.getFreeHeap());
  }
  else if (var == "RESET_REASON")
  {
    switch (esp_reset_reason())
    {
      case ESP_RST_UNKNOWN:
          return String("Unknown");
      case ESP_RST_POWERON:
          return String("Power on");
      case ESP_RST_EXT:
          return String("External");
      case ESP_RST_SW:
          return String("Software");
      case ESP_RST_PANIC:
          return String("Panic");
      case ESP_RST_INT_WDT:
          return String("Interrupt Watchdog");
      case ESP_RST_TASK_WDT:
          return String("Task Watchdog");
      case ESP_RST_WDT:
          return String("Watchdog");
      case ESP_RST_DEEPSLEEP:
          return String("Deepsleep");
      case ESP_RST_BROWNOUT:
          return String("Brownout");
      case ESP_RST_SDIO:
          return String("SDIO");
    }
  }
  else if (var == "MQTT_STATE")
  {
    return getMqttStateString(mqttClient.state());
  }
  else if (var == "MQTT_STATS")
  {
    return String(mqttConnectCount) + " connects, " + mqttLostCount + " lost";
  }
  else if (var == "SENSOR_STATE")
  {
    return sensorStateText();
  }
  else if (var == "SENSOR_ALERT")
  {
    String st = sensorLightState();
    return st == "green" ? "success" : (st == "yellow" ? "warning" : "danger");
  }
  else if (var == "SENSOR_LIGHT")
  {
    return sensorLightHtml();
  }
  else if (var == "WIFI_LOST")
  {
    return String(wifiLostCount);
  }
  else if (var == "WIFI_REASON")
  {
    return getWifiReasonString(lastWifiDisconnectReason) + " (" + wifiDisconnectEvents + " disconnect events)";
  }
  else if (var == "UPTIME")
  {
    return getUptimeString();
  }
  else if (var == "WIFI_SSID")
  {
    return htmlEscape(settingsManager.getWifiSettings().ssid);
  }
  else if (var == "WIFI_PASSWORD")
  {
    if (settingsManager.getWifiSettings().password.isEmpty())
      return "";
    else
      return "********"; // for security reasons the wifi password will not left the device once configured
  }
  else if (var == "WEB_PASSWORD")
  {
    if (settingsManager.getAppSettings().webPassword.isEmpty())
      return "";
    else
      return "********"; // for security reasons the web password will not left the device once configured
  }
  else if (var == "MQTT_SERVER")
  {
    return htmlEscape(settingsManager.getAppSettings().mqttServer);
  }
  else if (var == "MQTT_USERNAME")
  {
    return htmlEscape(settingsManager.getAppSettings().mqttUsername);
  }
  else if (var == "MQTT_PASSWORD")
  {
    if (settingsManager.getAppSettings().mqttPassword.isEmpty())
      return "";
    else
      return "********"; // for security reasons the mqtt password will not left the device once configured
  }
  else if (var == "MQTT_ROOTTOPIC")
  {
    return htmlEscape(settingsManager.getAppSettings().mqttRootTopic);
  }
  else if (var == "MQTT_PORT")
  {
    return String(settingsManager.getAppSettings().mqttPort);
  }
  else if (var == "MQTT_CLIENTID")
  {
    return htmlEscape(settingsManager.getAppSettings().mqttClientId);
  }
  else if (var == "MQTT_KEEPALIVE")
  {
    return String(settingsManager.getAppSettings().mqttKeepAlive);
  }
  else if (var == "MQTT_RETAIN")
  {
    return settingsManager.getAppSettings().mqttRetain ? Checked : "";
  }
  else if (var == "MQTT_PUBLISHLOG")
  {
    return settingsManager.getAppSettings().mqttPublishLog ? Checked : "";
  }
  else if (var == "PAYLOAD_ON")
  {
    return htmlEscape(settingsManager.getAppSettings().payloadOn);
  }
  else if (var == "PAYLOAD_OFF")
  {
    return htmlEscape(settingsManager.getAppSettings().payloadOff);
  }
  else if (var == "T_LWT")
  {
    return htmlEscape(settingsManager.getAppSettings().topicLwt);
  }
  else if (var == "T_RING")
  {
    return htmlEscape(settingsManager.getAppSettings().topicRing);
  }
  else if (var == "T_MATCHID")
  {
    return htmlEscape(settingsManager.getAppSettings().topicMatchId);
  }
  else if (var == "T_MATCHNAME")
  {
    return htmlEscape(settingsManager.getAppSettings().topicMatchName);
  }
  else if (var == "T_MATCHCONF")
  {
    return htmlEscape(settingsManager.getAppSettings().topicMatchConfidence);
  }
  else if (var == "T_LASTLOG")
  {
    return htmlEscape(settingsManager.getAppSettings().topicLastLog);
  }
  else if (var == "T_IGNORERING")
  {
    return htmlEscape(settingsManager.getAppSettings().topicIgnoreTouchRing);
  }
  else if (var == "T_LEDRING")
  {
    return htmlEscape(settingsManager.getAppSettings().topicLedRing);
  }
  else if (var == "T_OPENDOOR")
  {
    return htmlEscape(settingsManager.getAppSettings().topicOpenDoor);
  }
  else if (var == "LOGMESSAGES_FULL")
  {
    return getLogMessagesAsHtml(logMessagesCount);
  }
  else if (var == "EVENTLIST")
  {
    return eventLog.getEventsAsHtmlTable();
  }
  else if (var == "LOCKOUT_STATUS")
  {
    if (accessControl.isLockedOut())
      return String("<span class=\"label label-danger\">Lockout active, ") + accessControl.getLockoutRemainingSeconds() + "s left</span>";
    return String("<span class=\"label label-success\">No lockout</span> ") + accessControl.getRecentFailures() + " recent failed attempts";
  }
  else if (var == "FINGER_COUNT")
  {
    return String(fingerManager.getFingerCount());
  }
  else if (var == "MQTT_ENROLL")
  {
    return settingsManager.getAppSettings().mqttEnroll ? Checked : "";
  }
  else if (var == "FAIL_MAX")
  {
    return String(settingsManager.getAppSettings().failMax);
  }
  else if (var == "FAIL_WINDOW")
  {
    return String(settingsManager.getAppSettings().failWindow);
  }
  else if (var == "LOCKOUT_TIME")
  {
    return String(settingsManager.getAppSettings().lockoutTime);
  }
  else if (var == "RING_COOLDOWN")
  {
    return String(settingsManager.getAppSettings().ringCooldown);
  }
  else if (var == "NIGHT_ENABLED")
  {
    return settingsManager.getAppSettings().nightEnabled ? Checked : "";
  }
  else if (var == "NIGHT_START")
  {
    return minutesToTimeString(settingsManager.getAppSettings().nightStart);
  }
  else if (var == "NIGHT_END")
  {
    return minutesToTimeString(settingsManager.getAppSettings().nightEnd);
  }
  else if (var == "NIGHT_STATUS")
  {
    if (!settingsManager.getAppSettings().nightEnabled)
      return "disabled";
    return nightModeActive ? "active now" : "not active now";
  }
  else if (var == "RADAR_ENABLED")
  {
    return settingsManager.getAppSettings().radarEnabled ? Checked : "";
  }
  else if (var == "RADAR_PIN_OPTIONS")
  {
    return getRadarPinOptionsHtml();
  }
  else if (var == "GPIO_RX_OPTIONS")
  {
    return getGpioOptionsHtml(PinRole::sensorRx);
  }
  else if (var == "GPIO_TX_OPTIONS")
  {
    return getGpioOptionsHtml(PinRole::sensorTx);
  }
  else if (var == "GPIO_TOUCH_OPTIONS")
  {
    return getGpioOptionsHtml(PinRole::touchRing);
  }
  else if (var == "GPIO_BELL_OPTIONS")
  {
    return getGpioOptionsHtml(PinRole::doorbell);
  }
  else if (var == "GPIO_OPENER_OPTIONS")
  {
    return getGpioOptionsHtml(PinRole::doorOpener);
  }
  else if (var == "GPIO_NOTE")
  {
    return getGpioNoteHtml();
  }
  else if (var == "RADAR_HIGH_SELECTED")
  {
    return settingsManager.getAppSettings().radarActiveHigh ? Selected : "";
  }
  else if (var == "RADAR_LOW_SELECTED")
  {
    return settingsManager.getAppSettings().radarActiveHigh ? "" : Selected;
  }
  else if (var == "RADAR_LED_WAKE")
  {
    return settingsManager.getAppSettings().radarLedWake ? Checked : "";
  }
  else if (var == "RADAR_STATUS")
  {
    if (!settingsManager.getAppSettings().radarEnabled)
      return "disabled";
    return presenceDetected ? "somebody detected" : "nobody detected";
  }
  else if (var == "HA_DISCOVERY")
  {
    return settingsManager.getAppSettings().haDiscovery ? Checked : "";
  }
  else if (var == "NTP_SERVER")
  {
    return htmlEscape(settingsManager.getAppSettings().ntpServer);
  }
  else if (var == "TIMEZONE_OPTIONS")
  {
    return getTimezoneOptionsAsHtml();
  }
  else if (var == "LOCAL_TIME")
  {
    return getTimestampString();
  }
  else if (var == "TIMEZONE")
  {
    return htmlEscape(settingsManager.getAppSettings().timezone);
  }
  else if (var == "TOUCH_RING_ACTIVE_COLOR_1")
  {
    return (settingsManager.getAppSettings().touchRingActiveColor == 1) ? Selected : "";
  }
  else if (var == "TOUCH_RING_ACTIVE_COLOR_2")
  {
    return (settingsManager.getAppSettings().touchRingActiveColor == 2) ? Selected : "";
  }
  else if (var == "TOUCH_RING_ACTIVE_COLOR_3")
  {
    return (settingsManager.getAppSettings().touchRingActiveColor == 3) ? Selected : "";
  }
  else if (var == "TOUCH_RING_ACTIVE_COLOR_4")
  {
    return (settingsManager.getAppSettings().touchRingActiveColor == 4) ? Selected : "";
  }
  else if (var == "TOUCH_RING_ACTIVE_COLOR_5")
  {
    return (settingsManager.getAppSettings().touchRingActiveColor == 5) ? Selected : "";
  }
  else if (var == "TOUCH_RING_ACTIVE_COLOR_6")
  {
    return (settingsManager.getAppSettings().touchRingActiveColor == 6) ? Selected : "";
  }
  else if (var == "TOUCH_RING_ACTIVE_COLOR_7")
  {
    return (settingsManager.getAppSettings().touchRingActiveColor == 7) ? Selected : "";
  }
  else if (var == "TOUCH_RING_ACTIVE_SEQUENCE_4")
  {
    return (settingsManager.getAppSettings().touchRingActiveSequence == 4) ? Checked : "";
  }
  else if (var == "TOUCH_RING_ACTIVE_SEQUENCE_3")
  {
    return (settingsManager.getAppSettings().touchRingActiveSequence == 3) ? Checked : "";
  }
  else if (var == "TOUCH_RING_ACTIVE_SEQUENCE_1")
  {
    return (settingsManager.getAppSettings().touchRingActiveSequence == 1) ? Checked : "";
  }
  else if (var == "TOUCH_RING_ACTIVE_SEQUENCE_2")
  {
    return (settingsManager.getAppSettings().touchRingActiveSequence == 2) ? Checked : "";
  }
  else if (var == "SCAN_COLOR_1")
  {
    return (settingsManager.getAppSettings().scanColor == 1) ? Selected : "";
  }
  else if (var == "SCAN_COLOR_2")
  {
    return (settingsManager.getAppSettings().scanColor == 2) ? Selected : "";
  }
  else if (var == "SCAN_COLOR_3")
  {
    return (settingsManager.getAppSettings().scanColor == 3) ? Selected : "";
  }
  else if (var == "SCAN_COLOR_4")
  {
    return (settingsManager.getAppSettings().scanColor == 4) ? Selected : "";
  }
  else if (var == "SCAN_COLOR_5")
  {
    return (settingsManager.getAppSettings().scanColor == 5) ? Selected : "";
  }
  else if (var == "SCAN_COLOR_6")
  {
    return (settingsManager.getAppSettings().scanColor == 6) ? Selected : "";
  }
  else if (var == "SCAN_COLOR_7")
  {
    return (settingsManager.getAppSettings().scanColor == 7) ? Selected : "";
  }
  else if (var == "MATCH_COLOR_1")
  {
    return (settingsManager.getAppSettings().matchColor == 1) ? Selected : "";
  }
  else if (var == "MATCH_COLOR_2")
  {
    return (settingsManager.getAppSettings().matchColor == 2) ? Selected : "";
  }
  else if (var == "MATCH_COLOR_3")
  {
    return (settingsManager.getAppSettings().matchColor == 3) ? Selected : "";
  }
  else if (var == "MATCH_COLOR_4")
  {
    return (settingsManager.getAppSettings().matchColor == 4) ? Selected : "";
  }
  else if (var == "MATCH_COLOR_5")
  {
    return (settingsManager.getAppSettings().matchColor == 5) ? Selected : "";
  }
  else if (var == "MATCH_COLOR_6")
  {
    return (settingsManager.getAppSettings().matchColor == 6) ? Selected : "";
  }
  else if (var == "MATCH_COLOR_7")
  {
    return (settingsManager.getAppSettings().matchColor == 7) ? Selected : "";
  }

  return String();
}

// send LastMessage to websocket clients
void notifyClients(String message)
{
  String messageWithTimestamp = "[" + getTimestampString() + "]: " + message;
  Serial.println(messageWithTimestamp);
  addLogMessage(messageWithTimestamp);
  events.send(getLogMessagesAsHtml().c_str(), "message", millis(), 1000);
  events.send(htmlEscape(messageWithTimestamp).c_str(), "logline", millis(), 1000); // used by the log page

  if (settingsManager.getAppSettings().mqttPublishLog)
    mqttClient.publish(mqttTopic(settingsManager.getAppSettings().topicLastLog).c_str(), message.c_str());
}

void updateClientsFingerlist(String fingerlist)
{
  Serial.println("New fingerlist was sent to clients");
  events.send(fingerlist.c_str(), "fingerlist", millis(), 1000);
}

bool doPairing()
{
  String newPairingCode = settingsManager.generateNewPairingCode();

  if (fingerManager.setPairingCode(newPairingCode))
  {
    AppSettings settings = settingsManager.getAppSettings();
    settings.sensorPairingCode = newPairingCode;
    settings.sensorPairingValid = true;
    settingsManager.saveAppSettings(settings);
    notifyClients("Pairing successful.");
    return true;
  }
  else
  {
    notifyClients("Pairing failed.");
    return false;
  }
}

bool checkPairingValid()
{
  AppSettings settings = settingsManager.getAppSettings();

  if (!settings.sensorPairingValid)
  {
    if (settings.sensorPairingCode.isEmpty())
    {
      // first boot, do pairing automatically so the user does not have to do this manually
      return doPairing();
    }
    else
    {
      Serial.println("Pairing has been invalidated previously.");
      return false;
    }
  }

  String actualSensorPairingCode = fingerManager.getPairingCode();
  // Serial.println("Awaited pairing code: " + settings.sensorPairingCode);
  // Serial.println("Actual pairing code: " + actualSensorPairingCode);

  if (actualSensorPairingCode.equals(settings.sensorPairingCode))
    return true;
  else
  {
    if (!actualSensorPairingCode.isEmpty())
    {
      // An empty code means there was a communication problem. So we don't have a valid code, but maybe next read will succeed and we get one again.
      // But here we just got an non-empty pairing code that was different to the awaited one. So don't expect that will change in future until repairing was done.
      // -> invalidate pairing for security reasons
      AppSettings settings = settingsManager.getAppSettings();
      settings.sensorPairingValid = false;
      settingsManager.saveAppSettings(settings);
    }
    return false;
  }
}

String getWifiReasonString(uint8_t reason)
{
  switch (reason)
  {
  case 0: return "none";
  case WIFI_REASON_AUTH_EXPIRE: return "2 auth expired";
  case WIFI_REASON_AUTH_LEAVE: return "3 auth leave";
  case WIFI_REASON_ASSOC_EXPIRE: return "4 assoc expired";
  case WIFI_REASON_ASSOC_TOOMANY: return "5 too many clients on AP";
  case WIFI_REASON_NOT_AUTHED: return "6 not authenticated";
  case WIFI_REASON_ASSOC_LEAVE: return "8 assoc leave";
  case WIFI_REASON_MIC_FAILURE: return "14 MIC failure";
  case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "15 4-way handshake timeout (wrong password or WPA3/PMF setting?)";
  case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "16 group key update timeout";
  case WIFI_REASON_IE_IN_4WAY_DIFFERS: return "17 IE in 4-way handshake differs";
  case WIFI_REASON_PAIRWISE_CIPHER_INVALID: return "19 pairwise cipher invalid";
  case WIFI_REASON_AKMP_INVALID: return "20 AKMP invalid (WPA3 only?)";
  case WIFI_REASON_INVALID_RSN_IE_CAP: return "22 invalid RSN capabilities (PMF required?)";
  case WIFI_REASON_BEACON_TIMEOUT: return "200 beacon timeout (signal lost)";
  case WIFI_REASON_NO_AP_FOUND: return "201 no AP found";
  case WIFI_REASON_AUTH_FAIL: return "202 authentication failed";
  case WIFI_REASON_ASSOC_FAIL: return "203 association failed";
  case WIFI_REASON_HANDSHAKE_TIMEOUT: return "204 handshake timeout";
  case WIFI_REASON_CONNECTION_FAIL: return "205 connection failed";
  default: return String(reason);
  }
}

// scans for the configured SSID and connects to the strongest access point (helps with mesh/repeaters).
// fullReset restarts the WiFi stack: with this framework version the connection sometimes hangs until then.
void connectWifi(bool fullReset)
{
  const WifiSettings &wifiSettings = settingsManager.getWifiSettings();
  if (fullReset)
  {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(200);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setHostname(wifiSettings.hostname.c_str());
  }
  else
    WiFi.disconnect();

  int found = WiFi.scanNetworks(false, false, false, 120); // blocks ~2s
  int best = -1;
  int apCount = 0;
  for (int i = 0; i < found; i++)
  {
    if (WiFi.SSID(i) == wifiSettings.ssid)
    {
      apCount++;
      if (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))
        best = i;
    }
  }

  if (best >= 0)
  {
    uint8_t bssid[6];
    memcpy(bssid, WiFi.BSSID(best), 6);
    int32_t channel = WiFi.channel(best);
    int32_t rssi = WiFi.RSSI(best);
    char bssidText[18];
    snprintf(bssidText, sizeof(bssidText), "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    WiFi.scanDelete();
    Serial.printf("Connecting to AP %s (channel %d, %d dBm, %d AP(s) with this SSID)\n", bssidText, channel, rssi, apCount);
    if (wifiConnectAttempts > 0)
      notifyClients(String("WiFi retry: connecting to AP ") + bssidText + " (channel " + channel + ", " + rssi + " dBm, " + apCount + " AP(s) found)" + (fullReset ? ", WiFi restarted" : ""));
    WiFi.begin(wifiSettings.ssid.c_str(), wifiSettings.password.c_str(), channel, bssid);
  }
  else
  {
    WiFi.scanDelete();
    if (wifiConnectAttempts > 0)
      notifyClients("WiFi retry: network '" + wifiSettings.ssid + "' not found in scan (" + found + " networks visible).");
    WiFi.begin(wifiSettings.ssid.c_str(), wifiSettings.password.c_str());
  }
  wifiReconnectPreviousMillis = millis();
}

// starts the WiFi connection in the background. The boot does not wait for it anymore: before, the device rebooted
// after 30s without connection, which could end in a reboot loop of several minutes with a weak signal.
void initWifi()
{
  static bool eventHandlerRegistered = false;
  if (!eventHandlerRegistered)
  {
    // remember why the connection failed / was lost, the reason codes tell much more than the status
    WiFi.onEvent([](system_event_id_t event, system_event_info_t info)
                 {
                   lastWifiDisconnectReason = info.disconnected.reason;
                   wifiDisconnectEvents++;
                 },
                 SYSTEM_EVENT_STA_DISCONNECTED);
    eventHandlerRegistered = true;
  }

  WifiSettings wifiSettings = settingsManager.getWifiSettings();
  WiFi.persistent(false); // credentials are stored in our own settings, don't write them to flash on every boot
  WiFi.mode(WIFI_STA);
  // disable WiFi modem sleep: it adds 100-300ms latency to every packet (slow web UI, unstable MQTT).
  // The doorbell is powered permanently, so the higher power consumption doesn't matter.
  WiFi.setSleep(false);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(wifiSettings.hostname.c_str()); // define hostname
  WiFi.setAutoReconnect(true);
  connectWifi(false);
  Serial.println("WiFi connection started in background");
}

void initWiFiAccessPointForConfiguration()
{
  WiFi.softAPConfig(WifiConfigIp, WifiConfigIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(WifiConfigSsid, WifiConfigPassword);

  // if DNSServer is started with "*" for domain name, it will reply with
  // provided IP to all DNS request
  dnsServer.start(DNS_PORT, "*", WifiConfigIp);

  Serial.print("AP IP address: ");
  Serial.println(WifiConfigIp);
}

// NTP server and time zone, called at startup and when the settings are saved
void applyTimeSettings()
{
  // Init time by NTP Client
  // SNTP only stores the pointer to the server name, so it must not point into a temporary copy of the settings
  // (otherwise random memory content is resolved as hostname every few seconds)
  static String ntpServer;
  if (sntp_enabled())
    sntp_stop(); // stop before the string is changed, SNTP still holds the pointer to the old one
  ntpServer = settingsManager.getAppSettings().ntpServer;
  String timezone = resolveTimezone(settingsManager.getAppSettings().timezone);
  Serial.printf("Setting Timezone to %s\n", timezone.c_str());
  if (!ntpServer.isEmpty())
    configTzTime(timezone.c_str(), ntpServer.c_str()); // sets NTP server and time zone (setenv copies the TZ string)
  else
  {
    setenv("TZ", timezone.c_str(), 1);
    tzset();
  }
  if (timezone != settingsManager.getAppSettings().timezone)
    notifyClients("Invalid timezone '" + settingsManager.getAppSettings().timezone + "', using '" + timezone + "' instead. Please check your settings.");
}

// checks the web password, requests authentication if needed
bool isAuthorized(AsyncWebServerRequest *request)
{
  const String &password = settingsManager.getAppSettings().webPassword;
  if (password.isEmpty() || request->authenticate(webServerUser, password.c_str()))
    return true;
  request->requestAuthentication();
  return false;
}

void startWebserver()
{

  // Initialize SPIFFS
  // Never return here without starting the web server: the OTA update page doesn't need SPIFFS, it is the only
  // way to repair a broken file system without USB.
  unsigned long spiffsStart = millis();
  bool spiffsOk = SPIFFS.begin(false);
  if (!spiffsOk)
  {
    Serial.println("SPIFFS mount failed, formatting...");
    spiffsOk = SPIFFS.begin(true); // formats the partition, this can take a while
    if (spiffsOk)
      notifyClients(String("File system was damaged and has been formatted (") + (millis() - spiffsStart) / 1000 + "s). Please upload spiffs.bin again on the update page.");
    else
      notifyClients("File system could not be mounted. Please upload spiffs.bin again on the update page (/update).");
  }
  if (spiffsOk)
    eventLog.begin();

  applyTimeSettings();

  // webserver for normal operating or wifi config?
  if (currentMode == Mode::wificonfig)
  {
    // =================
    // WiFi config mode
    // =================

    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                 { request->send(SPIFFS, "/wificonfig.html", String(), false, processor); });

    webServer.on("/save", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
                 {
      if(request->hasArg("hostname"))
      {
        Serial.println("Save wifi config");
        WifiSettings settings = settingsManager.getWifiSettings();
        settings.hostname = request->arg("hostname");
        settings.ssid = request->arg("ssid");
        if (request->arg("password").equals("********")) // password is replaced by wildcards when given to the browser, so if the user didn't changed it, don't save it
          settings.password = settingsManager.getWifiSettings().password; // use the old, already saved, one
        else
          settings.password = request->arg("password");
        settingsManager.saveWifiSettings(settings);
        shouldReboot = true;
      }
      request->redirect("/"); });

    webServer.onNotFound([](AsyncWebServerRequest *request)
                         {
      AsyncResponseStream *response = request->beginResponseStream("text/html");
      response->printf("<!DOCTYPE html><html><head><title>FingerprintDoorbell</title><meta http-equiv=\"refresh\" content=\"0; url=http://%s\" /></head><body>", WiFi.softAPIP().toString().c_str());
      response->printf("<p>Please configure your WiFi settings <a href='http://%s'>here</a> to connect FingerprintDoorbell to your home network.</p>", WiFi.softAPIP().toString().c_str());
      response->print("</body></html>");
      request->send(response); });
  }
  else
  {
    // =======================
    // normal operating mode
    // =======================
    events.onConnect([](AsyncEventSourceClient *client)
                     {
      if(client->lastId()){
        Serial.printf("Client reconnected! Last message ID it got was: %u\n", client->lastId());
      }
      //send event with message "ready", id current millis
      // and set reconnect delay to 1 second
      client->send(getLogMessagesAsHtml().c_str(),"message",millis(),1000); });
    webServer.addHandler(&events);

    // Route for root / web page
    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
      if (!settingsManager.getAppSettings().webPassword.isEmpty())
      {
        if(!request->authenticate(webServerUser, settingsManager.getAppSettings().webPassword.c_str()))
          return request->requestAuthentication();
      }
      if (!SPIFFS.exists("/index.html"))
      {
        request->send(200, "text/html", "<html><body><h3>FingerprintDoorbell</h3><p>The web pages are missing, the file system has to be updated.</p><p><a href=\"/update\">Upload spiffs.bin on the update page</a></p></body></html>");
        return;
      }
      request->send(SPIFFS, "/index.html", String(), false, processor); });

    webServer.on("/enroll", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
      if (!settingsManager.getAppSettings().webPassword.isEmpty())
      {
        if(!request->authenticate(webServerUser, settingsManager.getAppSettings().webPassword.c_str()))
          return request->requestAuthentication();
      }
      if(request->hasArg("startEnrollment"))
      {
        enrollId = request->arg("newFingerprintId");
        enrollName = request->arg("newFingerprintName");
        currentMode = Mode::enroll;
      }
      request->redirect("/"); });

    webServer.on("/editFingerprints", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
      if (!settingsManager.getAppSettings().webPassword.isEmpty())
      {
        if(!request->authenticate(webServerUser, settingsManager.getAppSettings().webPassword.c_str()))
          return request->requestAuthentication();
      }
      if(request->hasArg("selectedFingerprint"))
      {
        if(request->hasArg("btnDelete"))
        {
          int id = request->arg("selectedFingerprint").toInt();
          waitForMaintenanceMode();
          fingerManager.deleteFinger(id);
          currentMode = Mode::scan;
        }
        else if (request->hasArg("btnRename"))
        {
          int id = request->arg("selectedFingerprint").toInt();
          String newName = request->arg("renameNewName");
          fingerManager.renameFinger(id, newName);
        }
      }
      request->redirect("/"); });

    webServer.on("/settings", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
                 {
      if (!settingsManager.getAppSettings().webPassword.isEmpty())
      {
        if(!request->authenticate(webServerUser, settingsManager.getAppSettings().webPassword.c_str()))
          return request->requestAuthentication();
      }

      if(request->hasArg("btnSaveSettings"))
      {
        Serial.println("Save settings");
        AppSettings settings = settingsManager.getAppSettings();
        if (request->arg("web_password").equals("********")) // password is replaced by wildcards when given to the browser, so if the user didn't changed it, don't save it
          settings.webPassword = settingsManager.getAppSettings().webPassword; // use the old, already saved, one
        else
          settings.webPassword = request->arg("web_password");
        settings.ntpServer = request->arg("ntpServer");
        String timezoneSelection = request->arg("timezone_select");
        if (timezoneSelection.isEmpty() || timezoneSelection == "custom")
        {
          settings.timezoneName = "";
          settings.timezone = resolveTimezone(request->arg("timezone"));
          if (findTimezonePosix(request->arg("timezone"))) // user typed a known name like "Europe/Berlin"
            settings.timezoneName = request->arg("timezone");
        }
        else
        {
          settings.timezoneName = timezoneSelection;
          settings.timezone = resolveTimezone(timezoneSelection);
        }
        settings.failMax = constrain(request->arg("fail_max").toInt(), 0, 30);
        settings.failWindow = constrain(request->arg("fail_window").toInt(), 10, 3600);
        settings.lockoutTime = constrain(request->arg("lockout_time").toInt(), 10, 43200);
        settings.ringCooldown = constrain(request->arg("ring_cooldown").toInt(), 0, 600);
        settings.nightEnabled = request->hasArg("night_enabled");
        settings.nightStart = timeStringToMinutes(request->arg("night_start"), 22 * 60) % 1440;
        settings.nightEnd = timeStringToMinutes(request->arg("night_end"), 6 * 60) % 1440;
        settings.radarEnabled = request->hasArg("radar_enabled");
        if (isRadarPinAllowed(request->arg("radar_pin").toInt()))
          settings.radarPin = request->arg("radar_pin").toInt();
        if (request->hasArg("gpio_rx")) // GPIO block, validated as a whole, pins are used after a restart
        {
          PinSet newPins = settings.pins;
          newPins.sensorRx = request->arg("gpio_rx").toInt();
          newPins.sensorTx = request->arg("gpio_tx").toInt();
          newPins.touchRing = request->arg("gpio_touch").toInt();
          newPins.doorbell = request->arg("gpio_bell").toInt();
          newPins.doorOpener = request->arg("gpio_opener").toInt();
          int8_t extraUsed[6];
          size_t extraCount = getExtraUsedPins(settings, extraUsed, 6);
          String error;
          if (GpioConfig::validate(newPins, extraUsed, extraCount, error))
          {
            settings.pins = newPins;
            gpioMessage = "";
          }
          else
          {
            gpioMessage = "GPIO settings not saved: " + error;
            notifyClients(gpioMessage);
          }
        }
        settings.radarActiveHigh = request->arg("radar_level") != "low";
        settings.radarLedWake = request->hasArg("radar_led_wake");
        settings.touchRingActiveColor = request->arg("touchRingActiveColor").toInt();
        settings.touchRingActiveSequence = request->arg("touchRingActiveSequence").toInt();
        settings.scanColor = request->arg("scanColor").toInt();
        settings.matchColor = request->arg("matchColor").toInt();
        settingsManager.saveAppSettings(settings);
        applySettingsRequested = true; // applied by the loop task, no reboot needed
        request->redirect("/settings");
      } else {
        request->send(SPIFFS, "/settings.html", String(), false, processor);
      } });

    webServer.on("/mqtt", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
                 {
      if (!settingsManager.getAppSettings().webPassword.isEmpty())
      {
        if(!request->authenticate(webServerUser, settingsManager.getAppSettings().webPassword.c_str()))
          return request->requestAuthentication();
      }

      if(request->hasArg("btnSaveMqtt"))
      {
        Serial.println("Save MQTT settings");
        AppSettings settings = settingsManager.getAppSettings();
        settings.mqttServer = request->arg("mqtt_server");
        settings.mqttServer.trim();
        settings.mqttPort = request->arg("mqtt_port").toInt();
        if (settings.mqttPort == 0)
          settings.mqttPort = 1883;
        int colon = settings.mqttServer.indexOf(':');
        if (colon >= 0) // old "server:port" notation -> move port to its own field
        {
          long port = settings.mqttServer.substring(colon + 1).toInt();
          if (port > 0 && port <= 65535)
            settings.mqttPort = port;
          settings.mqttServer = settings.mqttServer.substring(0, colon);
        }
        settings.mqttUsername = request->arg("mqtt_username");
        if (request->arg("mqtt_password").equals("********")) // password is replaced by wildcards when given to the browser, so if the user didn't changed it, don't save it
          settings.mqttPassword = settingsManager.getAppSettings().mqttPassword; // use the old, already saved, one
        else
          settings.mqttPassword = request->arg("mqtt_password");
        settings.mqttRootTopic = sanitizeTopic(request->arg("mqtt_rootTopic"), "fingerprintDoorbell", true);
        settings.mqttClientId = request->arg("mqtt_clientId");
        settings.mqttClientId.trim();
        settings.mqttKeepAlive = constrain(request->arg("mqtt_keepAlive").toInt(), 5, 300);
        settings.mqttRetain = request->hasArg("mqtt_retain");
        settings.mqttPublishLog = request->hasArg("mqtt_publishLog");
        settings.payloadOn = request->arg("payload_on");
        settings.payloadOn.trim();
        if (settings.payloadOn.isEmpty())
          settings.payloadOn = "on";
        settings.payloadOff = request->arg("payload_off");
        settings.payloadOff.trim();
        if (settings.payloadOff.isEmpty())
          settings.payloadOff = "off";
        settings.topicLwt = sanitizeTopic(request->arg("t_lwt"), "LWT");
        settings.topicRing = sanitizeTopic(request->arg("t_ring"), "ring");
        settings.topicMatchId = sanitizeTopic(request->arg("t_matchId"), "matchId");
        settings.topicMatchName = sanitizeTopic(request->arg("t_matchName"), "matchName");
        settings.topicMatchConfidence = sanitizeTopic(request->arg("t_matchConf"), "matchConfidence");
        settings.topicLastLog = sanitizeTopic(request->arg("t_lastLog"), "lastLogMessage");
        settings.topicIgnoreTouchRing = sanitizeTopic(request->arg("t_ignoreRing"), "ignoreTouchRing");
        settings.topicLedRing = sanitizeTopic(request->arg("t_ledRing"), "ledRing");
        settings.topicOpenDoor = sanitizeTopic(request->arg("t_openDoor"), "openDoor");
        settings.haDiscovery = request->hasArg("ha_discovery");
        settings.mqttEnroll = request->hasArg("mqtt_enroll");
        oldLwtTopic = mqttTopic(settingsManager.getAppSettings().topicLwt);
        settingsManager.saveAppSettings(settings);
        applyMqttRequested = true; // reconnect is done by the loop task, no reboot needed
        request->redirect("/mqtt");
      } else {
        request->send(SPIFFS, "/mqtt.html", String(), false, processor);
      } });

    webServer.on("/log", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
      if (!isAuthorized(request))
        return;
      request->send(SPIFFS, "/log.html", String(), false, processor); });

    webServer.on("/log/clear", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
                 {
      if (!isAuthorized(request))
        return;
      if (request->hasArg("btnClearEvents"))
        clearEventsRequested = true;
      request->redirect("/log"); });

    // access schedule of one finger
    webServer.on("/access", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
                 {
      if (!isAuthorized(request))
        return;
      int id = request->arg("accessFingerId").toInt();
      if (id >= 1 && id <= 200 && request->hasArg("btnSaveAccess"))
      {
        FingerSchedule schedule;
        if (request->arg("accessMode") == "restricted")
        {
          schedule.days = 0;
          for (int day = 0; day < 7; day++)
          {
            if (request->hasArg((String("day") + day).c_str()))
              schedule.days |= (1 << day);
          }
          schedule.from = timeStringToMinutes(request->arg("accessFrom"), 0);
          schedule.to = timeStringToMinutes(request->arg("accessTo"), 1440);
          if (schedule.to == 0)
            schedule.to = 1440; // "00:00" as end means end of the day
        }
        else if (request->arg("accessMode") == "disabled")
          schedule.days = 0;
        accessControl.setSchedule(id, schedule);
        String text = AccessControl::scheduleToString(schedule);
        notifyClients(String("Access times for #") + id + ": " + (text.isEmpty() ? String("always") : text));
        updateClientsFingerlist(getFingerListHtml());
      }
      request->redirect("/"); });

    // backup and restore of the fingerprint templates
    webServer.on("/backup/create", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
                 {
      if (!isAuthorized(request))
        return;
      if (backupManager.getState() == BackupManager::State::running || currentMode != Mode::scan)
      {
        request->send(409, "application/json", "{\"state\":\"error\",\"message\":\"Device is busy, try again later.\"}");
        return;
      }
      long singleId = request->hasArg("id") ? request->arg("id").toInt() : 0;
      if (singleId < 0 || singleId > 200)
      {
        request->send(400, "application/json", "{\"state\":\"error\",\"message\":\"Invalid slot number.\"}");
        return;
      }
      backupSingleId = (uint16_t)singleId;
      backupManager.setRunning("Waiting for the sensor...");
      backupRequested = true;
      request->send(200, "application/json", backupManager.getStatusJson()); });

    webServer.on("/backup/status", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
      if (!isAuthorized(request))
        return;
      request->send(200, "application/json", backupManager.getStatusJson()); });

    webServer.on("/backup/download", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
      if (!isAuthorized(request))
        return;
      if (!SPIFFS.exists(BackupManager::BackupFile))
      {
        request->send(404, "text/plain", "No backup available, please create one first.");
        return;
      }
      String fileName = backupManager.getExportedId() > 0
                            ? "finger-" + String(backupManager.getExportedId()) + "-" + settingsManager.getWifiSettings().hostname
                            : "fingerprints-" + settingsManager.getWifiSettings().hostname;
      time_t now = time(nullptr);
      if (now > 1600000000)
      {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char buffer[16];
        strftime(buffer, sizeof(buffer), "-%Y%m%d", &timeinfo);
        fileName += buffer;
      }
      fileName += ".fpdb";
      // Stream the file through a callback: the SPIFFS file response of the library adds its own
      // Content-Disposition header, a second one makes Chrome reject the download
      // (ERR_RESPONSE_HEADERS_MULTIPLE_CONTENT_DISPOSITION).
      File backupFile = SPIFFS.open(BackupManager::BackupFile, "r");
      if (!backupFile)
      {
        request->send(500, "text/plain", "Backup file could not be opened.");
        return;
      }
      AsyncWebServerResponse *response = request->beginResponse(
          "application/octet-stream", backupFile.size(),
          [backupFile](uint8_t *buffer, size_t maxLen, size_t index) mutable -> size_t
          {
            if (backupFile.position() != index)
              backupFile.seek(index);
            return backupFile.read(buffer, maxLen);
          });
      response->addHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
      request->send(response); });

    webServer.on(
        "/backup/restore", HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
          if (!isAuthorized(request))
            return;
          if (backupManager.getState() == BackupManager::State::running || currentMode != Mode::scan)
          {
            request->send(409, "application/json", "{\"state\":\"error\",\"message\":\"Device is busy, try again later.\"}");
            return;
          }
          File file = SPIFFS.open(BackupManager::RestoreFile, "r");
          size_t size = file ? file.size() : 0;
          if (file)
            file.close();
          if (size < 7)
          {
            request->send(400, "application/json", "{\"state\":\"error\",\"message\":\"Upload failed or file is empty.\"}");
            return;
          }
          if (request->hasArg("clearFirst"))
            restoreMode = BackupManager::RestoreMode::clearFirst;
          else if (request->hasArg("addToFree"))
            restoreMode = BackupManager::RestoreMode::addToFree;
          else
            restoreMode = BackupManager::RestoreMode::overwrite;
          backupManager.setRunning("Waiting for the sensor...");
          restoreRequested = true;
          request->send(200, "application/json", backupManager.getStatusJson());
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
        {
          // upload handler: store the file in SPIFFS, the restore itself is done by the loop task
          const String &password = settingsManager.getAppSettings().webPassword;
          if (!password.isEmpty() && !request->authenticate(webServerUser, password.c_str()))
            return;
          if (index == 0)
          {
            if (request->_tempFile)
              request->_tempFile.close();
            request->_tempFile = SPIFFS.open(BackupManager::RestoreFile, "w");
          }
          if (!request->_tempFile)
            return;
          if (index + len > 600000) // 200 templates are about 350kB
          {
            request->_tempFile.close();
            SPIFFS.remove(BackupManager::RestoreFile);
            return;
          }
          request->_tempFile.write(data, len);
          if (final)
            request->_tempFile.close();
        });

    // the system page was merged into the log page, keep old bookmarks working
    webServer.on("/system", HTTP_GET, [](AsyncWebServerRequest *request)
                 { request->redirect("/log"); });

    webServer.on("/pairing", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
      if (!settingsManager.getAppSettings().webPassword.isEmpty())
      {
        if(!request->authenticate(webServerUser, settingsManager.getAppSettings().webPassword.c_str()))
          return request->requestAuthentication();
      }
      if(request->hasArg("btnDoPairing"))
      {
        Serial.println("Do (re)pairing");
        doPairing();
        request->redirect("/");  
      } else {
        request->send(SPIFFS, "/settings.html", String(), false, processor);
      } });

    webServer.on("/factoryReset", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
      if (!settingsManager.getAppSettings().webPassword.isEmpty())
      {
        if(!request->authenticate(webServerUser, settingsManager.getAppSettings().webPassword.c_str()))
          return request->requestAuthentication();
      }
      if(request->hasArg("btnFactoryReset"))
      {
        notifyClients("Factory reset initiated...");
        
        if (!fingerManager.deleteAll())
          notifyClients("Finger database could not be deleted.");
        
        if (!settingsManager.deleteAppSettings())
          notifyClients("App settings could not be deleted.");

        if (!settingsManager.deleteWifiSettings())
          notifyClients("Wifi settings could not be deleted.");
        
        request->redirect("/");  
        shouldReboot = true;
      } else {
        request->send(SPIFFS, "/settings.html", String(), false, processor);
      } });

    webServer.on("/deleteAllFingerprints", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
      if (!settingsManager.getAppSettings().webPassword.isEmpty())
      {
        if(!request->authenticate(webServerUser, settingsManager.getAppSettings().webPassword.c_str()))
          return request->requestAuthentication();
      }
      if(request->hasArg("btnDeleteAllFingerprints"))
      {
        notifyClients("Deleting all fingerprints...");
        
        if (!fingerManager.deleteAll())
          notifyClients("Finger database could not be deleted.");
        
        request->redirect("/");  
        
      }
      else
      {
        if (!settingsManager.getAppSettings().webPassword.isEmpty())
        {
          if(!request->authenticate(webServerUser, settingsManager.getAppSettings().webPassword.c_str()))
            return request->requestAuthentication();
        }
        request->send(SPIFFS, "/settings.html", String(), false, processor);
      } });

    webServer.onNotFound([](AsyncWebServerRequest *request)
                         { request->send(404); });

  } // end normal operating mode

  // fingerprint sensor status (traffic light) and manual reconnect
  webServer.on("/sensor/status", HTTP_GET, [](AsyncWebServerRequest *request)
               {
    if (currentMode != Mode::wificonfig && !isAuthorized(request))
      return;
    uint8_t ringColor, ringSequence;
    String ringReason;
    fingerManager.getRingState(ringColor, ringSequence, ringReason);
    String json = "{\"state\":\"" + sensorLightState() + "\",\"text\":\"" + jsonEscape(sensorStateText()) + "\"";
    json += String(",\"pin\":") + (fingerManager.getRingPinTouched() ? "true" : "false");
    json += String(",\"ignore\":") + (fingerManager.getIgnoreTouchRing() ? "true" : "false");
    json += String(",\"led\":") + (fingerManager.getLedRingEnabled() ? "true" : "false");
    json += String(",\"color\":") + ringColor + ",\"seq\":" + ringSequence + ",\"reason\":\"" + jsonEscape(ringReason) + "\"}";
    request->send(200, "application/json", json); });

  // serial monitor: text that was written to the serial port, polled by the Log page
  webServer.on("/serial/log", HTTP_GET, [](AsyncWebServerRequest *request)
               {
    if (currentMode != Mode::wificonfig && !isAuthorized(request))
      return;
    uint32_t from = request->hasArg("from") ? strtoul(request->arg("from").c_str(), nullptr, 10) : 0;
    uint32_t next = 0;
    bool more = false;
    String text = SerialLog.readFrom(from, next, 3000, more);
    request->send(200, "application/json", "{\"next\":" + String(next) + ",\"more\":" + (more ? "true" : "false") + ",\"text\":\"" + jsonEscapeMultiline(text) + "\"}"); });

  webServer.on("/serial/clear", HTTP_POST, [](AsyncWebServerRequest *request)
               {
    if (currentMode != Mode::wificonfig && !isAuthorized(request))
      return;
    SerialLog.clear();
    request->send(200, "application/json", "{\"ok\":true}"); });

  // switches for the touch ring diagnosis, applied by the loop task
  webServer.on("/sensor/ring", HTTP_POST, [](AsyncWebServerRequest *request)
               {
    if (currentMode != Mode::wificonfig && !isAuthorized(request))
      return;
    if (request->hasArg("ignore"))
      ignoreRingRequest = request->arg("ignore") == "1" ? 1 : 0;
    if (request->hasArg("led"))
      ledRingRequest = request->arg("led") == "1" ? 1 : 0;
    request->send(200, "application/json", "{\"ok\":true}"); });

  webServer.on("/sensor/reconnect", HTTP_POST, [](AsyncWebServerRequest *request)
               {
    if (currentMode != Mode::wificonfig && !isAuthorized(request))
      return;
    sensorReconnectRequested = true;
    request->send(200, "application/json", "{\"ok\":true}"); });

  // common url callbacks
  webServer.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request)
               {
    if (currentMode != Mode::wificonfig && !settingsManager.getAppSettings().webPassword.isEmpty())
    {
      if(!request->authenticate(webServerUser, settingsManager.getAppSettings().webPassword.c_str()))
        return request->requestAuthentication();
    }
    request->redirect("/");
    shouldReboot = true; });

  // the stylesheet is stored gzip compressed (19kB instead of 121kB) and may be cached by the browser,
  // so it is only transferred once instead of on every page load
  webServer.on("/bootstrap.min.css", HTTP_GET, [](AsyncWebServerRequest *request)
               {
    // fall back to the uncompressed file, e.g. if only the firmware was updated but not the file system
    // (a missing stylesheet makes the browser wait and shows the pages without layout)
    bool gzip = SPIFFS.exists("/bootstrap.min.css.gz");
    if (!gzip && !SPIFFS.exists("/bootstrap.min.css"))
    {
      request->send(404, "text/plain", "bootstrap.min.css missing, please update the file system (spiffs.bin)");
      return;
    }
    AsyncWebServerResponse *response = request->beginResponse(SPIFFS, gzip ? "/bootstrap.min.css.gz" : "/bootstrap.min.css", "text/css");
    if (gzip)
      response->addHeader("Content-Encoding", "gzip");
    response->addHeader("Cache-Control", "public, max-age=2592000");
    request->send(response); });

  // look of the web interface, cached by the browser (the pages load it with ?v=<version>)
  webServer.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
               {
    if (!SPIFFS.exists("/style.css"))
    {
      request->send(200, "text/css", ""); // old file system: pages keep the plain Bootstrap look
      return;
    }
    AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/style.css", "text/css; charset=utf-8");
    response->addHeader("Cache-Control", "public, max-age=2592000");
    request->send(response); });

  // translations of the web interface, cached by the browser (the pages load it with ?v=<version>)
  webServer.on("/i18n.js", HTTP_GET, [](AsyncWebServerRequest *request)
               {
    if (!SPIFFS.exists("/i18n.js"))
    {
      request->send(200, "application/javascript", "window.t=function(s){return s;};"); // old file system: pages stay English
      return;
    }
    AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/i18n.js", "application/javascript; charset=utf-8");
    response->addHeader("Cache-Control", "public, max-age=2592000");
    request->send(response); });

  // language switch in the navbar, saved on the device so it applies to every browser
  webServer.on("/language", HTTP_POST, [](AsyncWebServerRequest *request)
               {
    if (currentMode != Mode::wificonfig && !isAuthorized(request))
      return;
    AppSettings settings = settingsManager.getAppSettings();
    settings.language = request->arg("lang") == "de" ? "de" : "en";
    settingsManager.saveAppSettings(settings);
    request->send(200, "application/json", "{\"ok\":true}"); });

  // OTA update handler, must be registered only once (a second unauthenticated registration would win over the authenticated one)
  if (currentMode == Mode::wificonfig || settingsManager.getAppSettings().webPassword.isEmpty())
    updateServer.setup(&webServer, "/update");
  else
    updateServer.setup(&webServer, "/update", webServerUser, settingsManager.getAppSettings().webPassword.c_str());

  // Start server
  webServer.begin();

  notifyClients("System booted successfully!");
}

// finger list for the web UI, including the access schedule (data attributes are used by the forms on the index page)
String getFingerListHtml()
{
  String html;
  bool first = true;
  for (int i = 1; i <= 200; i++)
  {
    if (!fingerManager.hasFinger(i))
      continue;
    FingerSchedule schedule = accessControl.getSchedule(i);
    String name = fingerManager.getFingerName(i);
    String label = String(i) + " - " + name;
    String scheduleText = AccessControl::scheduleToString(schedule);
    if (!scheduleText.isEmpty())
      label += " [" + scheduleText + "]";
    html += String("<option value=\"") + i + "\"" + (first ? " selected" : "") +
            " data-name=\"" + htmlEscape(name) + "\" data-days=\"" + schedule.days + "\" data-from=\"" + schedule.from + "\" data-to=\"" + schedule.to + "\">" +
            htmlEscape(label) + "</option>";
    first = false;
  }
  return html;
}

String minutesToTimeString(uint16_t minutes)
{
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02u:%02u", (minutes / 60) % 24, minutes % 60);
  return String(buffer);
}

// "HH:MM" -> minutes since midnight, "24:00" is allowed as end of day
uint16_t timeStringToMinutes(const String &text, uint16_t defaultValue)
{
  int colon = text.indexOf(':');
  if (colon < 1)
    return defaultValue;
  int hours = text.substring(0, colon).toInt();
  int minutes = text.substring(colon + 1).toInt();
  if (hours < 0 || hours > 24 || minutes < 0 || minutes > 59 || (hours == 24 && minutes != 0))
    return defaultValue;
  return hours * 60 + minutes;
}

bool isRadarPinAllowed(int pin)
{
  const PinSet &used = settingsManager.getAppSettings().pins;
  for (int8_t candidate : radarPinCandidates)
  {
    if (candidate == used.sensorRx || candidate == used.sensorTx || candidate == used.touchRing || candidate == used.doorbell || candidate == used.doorOpener)
      continue;
#ifdef CUSTOM_GPIOS
    if (candidate == customOutput2)
      continue;
#endif
    if (candidate == pin)
      return true;
  }
  return false;
}

// pins of the fixed custom GPIOs and the radar sensor that must not be used twice
size_t getExtraUsedPins(const AppSettings &settings, int8_t *out, size_t maxCount)
{
  size_t n = 0;
  if (settings.radarEnabled && n < maxCount)
    out[n++] = settings.radarPin;
#ifdef CUSTOM_GPIOS
  if (n + 4 <= maxCount)
  {
    out[n++] = customOutput1;
    out[n++] = customOutput2;
    out[n++] = customInput1;
    out[n++] = customInput2;
  }
#endif
  return n;
}

// <option> list with only the pins that work for this function
String getGpioOptionsHtml(PinRole role)
{
  const AppSettings &settings = settingsManager.getAppSettings();
  String html;
  for (int pin = 0; pin <= 39; pin++)
  {
    if (!GpioConfig::isAllowed(role, pin))
      continue;
    String note;
    if (role == PinRole::sensorRx && pin >= 34)
      note = " (input only)";
    else if (pin == 16 || pin == 17)
      note = " (not on WROVER modules)";
    html += String("<option value=\"") + pin + "\"" + (settings.pins.get(role) == pin ? " selected" : "") + ">GPIO " + pin + note + "</option>";
  }
  return html;
}

String getGpioNoteHtml()
{
  String html;
  if (!gpioMessage.isEmpty())
    html += "<div class=\"alert alert-danger\">" + htmlEscape(gpioMessage) + "</div>";
  if (settingsManager.getAppSettings().pins != activePins)
    html += "<div class=\"alert alert-warning\">The new pins are saved and are used after a restart. <a href=\"/reboot\">Restart now</a></div>";
  return html;
}

String getRadarPinOptionsHtml()
{
  String html;
  for (int8_t candidate : radarPinCandidates)
  {
    if (!isRadarPinAllowed(candidate))
      continue;
    html += String("<option value=\"") + candidate + "\"" + (settingsManager.getAppSettings().radarPin == candidate ? " selected" : "") + ">GPIO " + candidate +
            (candidate >= 34 ? " (input only)" : "") + "</option>";
  }
  return html;
}

void publishOnOff(const char *subTopic, bool state, bool retain = true)
{
  mqttClient.publish(mqttTopic(subTopic).c_str(), onOffPayload(state), retain);
}

void publishLastEvent()
{
  mqttClient.publish(mqttTopic(TOPIC_LAST_EVENT).c_str(), eventLog.getLastEventAsText().c_str(), true);
}

// adds an event to the persistent log and publishes it as "last event"
void logEvent(EventType type, uint16_t fingerId = 0, uint16_t confidence = 0, const String &text = "")
{
  eventLog.add(type, fingerId, confidence, text);
  publishLastEvent();
}

void onEnrollProgress(const String &state)
{
  mqttClient.publish(mqttTopic(TOPIC_ENROLL_STATE).c_str(), state.c_str(), true);
}

// diagnostic values for Home Assistant / ioBroker, published every 60s
void publishInfo()
{
  String t = mqttTopic(TOPIC_INFO) + "/";
  mqttClient.publish((t + "rssi").c_str(), String(WiFi.RSSI()).c_str(), true);
  mqttClient.publish((t + "uptime").c_str(), String(millis() / 1000).c_str(), true);
  mqttClient.publish((t + "ip").c_str(), WiFi.localIP().toString().c_str(), true);
  mqttClient.publish((t + "freeHeap").c_str(), String(ESP.getFreeHeap()).c_str(), true);
  mqttClient.publish((t + "fingerCount").c_str(), String(fingerManager.getFingerCount()).c_str(), true);
  mqttClient.publish((t + "failedAttempts").c_str(), String(accessControl.getRecentFailures()).c_str(), true);
  mqttClient.publish((t + "wifiLost").c_str(), String(wifiLostCount).c_str(), true);
  mqttClient.publish((t + "mqttLost").c_str(), String(mqttLostCount).c_str(), true);
  mqttClient.publish((t + "version").c_str(), VersionInfo, true);
  mqttClient.publish((t + "sensor").c_str(), fingerManager.connected ? "online" : "offline", true);
  mqttClient.publish((t + "sensorLost").c_str(), String(sensorLostCount).c_str(), true);
  lastInfoPublishMillis = millis();
}

// states of the newer features, published on (re)connect
void publishFeatureStates()
{
  publishOnOff(TOPIC_LOCKOUT, accessControl.isLockedOut());
  publishOnOff(TOPIC_NIGHTMODE, nightModeActive);
  if (settingsManager.getAppSettings().radarEnabled)
    publishOnOff(TOPIC_PRESENCE, presenceDetected);
  mqttClient.publish(mqttTopic(TOPIC_ACCESS_DENIED).c_str(), "", true);
  mqttClient.publish(mqttTopic(TOPIC_ENROLL_STATE).c_str(), "idle", true);
  publishLastEvent();
  publishInfo();
}

// LED ring idle state depends on night mode and radar presence
void updateLedSuppression()
{
  const AppSettings &settings = settingsManager.getAppSettings();
  bool suppressed = nightModeActive || (settings.radarEnabled && settings.radarLedWake && !presenceDetected);
  fingerManager.setLedRingSuppressed(suppressed);
}

void updateNightMode()
{
  const AppSettings &settings = settingsManager.getAppSettings();
  bool active = false;
  int weekday, minutes;
  if (settings.nightEnabled && AccessControl::getLocalMinutes(weekday, minutes))
  {
    if (settings.nightStart <= settings.nightEnd)
      active = (minutes >= settings.nightStart && minutes < settings.nightEnd);
    else
      active = (minutes >= settings.nightStart || minutes < settings.nightEnd); // over midnight
  }
  if (active != nightModeActive)
  {
    nightModeActive = active;
    notifyClients(active ? "Night mode started, LED ring is off." : "Night mode ended.");
    publishOnOff(TOPIC_NIGHTMODE, active);
    updateLedSuppression();
  }
}

void updateRadar()
{
  const AppSettings &settings = settingsManager.getAppSettings();
  if (!settings.radarEnabled || !isRadarPinAllowed(settings.radarPin))
    return;
  bool level = digitalRead(settings.radarPin) == HIGH;
  bool present = settings.radarActiveHigh ? level : !level;
  if (present != presenceDetected)
  {
    // debounce: the new state has to be stable for 300ms
    if (presenceChangeMillis == 0)
      presenceChangeMillis = millis();
    else if (millis() - presenceChangeMillis >= 300)
    {
      presenceDetected = present;
      presenceChangeMillis = 0;
      Serial.println(present ? "Presence detected" : "Presence cleared");
      publishOnOff(TOPIC_PRESENCE, present);
      updateLedSuppression();
    }
  }
  else
    presenceChangeMillis = 0;
}

// starts an enrollment requested over MQTT, payload "5", "5:Name" or {"id":5,"name":"Name"}
void handleMqttEnroll(const String &payload)
{
  if (!settingsManager.getAppSettings().mqttEnroll)
  {
    notifyClients("Enrollment over MQTT is disabled (see MQTT settings).");
    return;
  }
  if (currentMode != Mode::scan)
  {
    onEnrollProgress("busy");
    return;
  }

  String text = payload;
  text.trim();
  String id, name;
  if (text.startsWith("{"))
  {
    int idPos = text.indexOf("\"id\"");
    if (idPos >= 0)
    {
      int colon = text.indexOf(':', idPos);
      if (colon > 0)
        id = String(text.substring(colon + 1).toInt());
    }
    int namePos = text.indexOf("\"name\"");
    if (namePos >= 0)
    {
      int start = text.indexOf('"', text.indexOf(':', namePos) + 1);
      int end = text.indexOf('"', start + 1);
      if (start > 0 && end > start)
        name = text.substring(start + 1, end);
    }
  }
  else
  {
    int separator = -1;
    for (unsigned int i = 0; i < text.length(); i++)
    {
      char c = text.charAt(i);
      if (c == ':' || c == ';' || c == ',')
      {
        separator = i;
        break;
      }
    }
    id = separator >= 0 ? text.substring(0, separator) : text;
    name = separator >= 0 ? text.substring(separator + 1) : "";
    id.trim();
    name.trim();
  }

  int idValue = id.toInt();
  if (idValue < 1 || idValue > 200)
  {
    notifyClients("Enrollment over MQTT: invalid id in '" + payload + "', expected e.g. '5:Name'.");
    onEnrollProgress("failed: invalid id");
    return;
  }
  enrollId = String(idValue);
  enrollName = name;
  currentMode = Mode::enroll;
}

// publish the current state of all switchable options (retained, so the state is also known after a restart of the home automation)
void publishSwitchStates()
{
  const AppSettings &settings = settingsManager.getAppSettings();
  mqttClient.publish(mqttTopic(settings.topicIgnoreTouchRing + "/state").c_str(), onOffPayload(fingerManager.getIgnoreTouchRing()), true);
  mqttClient.publish(mqttTopic(settings.topicLedRing + "/state").c_str(), onOffPayload(fingerManager.getLedRingEnabled()), true);
}

// publish (or remove) one Home Assistant discovery config
void publishHaDiscoveryEntity(const String &component, const String &objectId, const String &nodeId, const String &specificConfig, const String &deviceConfig, bool enabled)
{
  String topic = String(HA_DISCOVERY_PREFIX) + "/" + component + "/" + nodeId + "/" + objectId + "/config";
  if (!enabled)
  {
    mqttClient.publish(topic.c_str(), "", true); // an empty retained message removes the entity from Home Assistant
    return;
  }

  String payload = "{" + specificConfig +
                   ",\"unique_id\":\"" + nodeId + "_" + objectId + "\"" +
                   ",\"availability_topic\":\"" + jsonEscape(mqttTopic(settingsManager.getAppSettings().topicLwt)) + "\"" +
                   ",\"payload_available\":\"" + MQTT_LWT_PAYLOAD_ONLINE + "\"" +
                   ",\"payload_not_available\":\"" + MQTT_LWT_PAYLOAD_OFFLINE + "\"" +
                   "," + deviceConfig + "}";
  if (!mqttClient.publish(topic.c_str(), payload.c_str(), true))
    Serial.println("Failed to publish HA discovery config for " + objectId);
}

// JSON snippet "key":"<full topic>"
String jsonTopic(const char *key, const String &subTopic)
{
  return String("\"") + key + "\":\"" + jsonEscape(mqttTopic(subTopic)) + "\"";
}

// Home Assistant MQTT discovery, see https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery
void publishHaDiscovery()
{
  const AppSettings &settings = settingsManager.getAppSettings();
  bool enabled = settings.haDiscovery;
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  String nodeId = "fpdoorbell_" + mac;
  String onOff = ",\"payload_on\":\"" + jsonEscape(settings.payloadOn) + "\",\"payload_off\":\"" + jsonEscape(settings.payloadOff) + "\"";

  String device = String("\"device\":{\"identifiers\":[\"") + nodeId + "\"]" +
                  ",\"name\":\"" + jsonEscape(settingsManager.getWifiSettings().hostname) + "\"" +
                  ",\"manufacturer\":\"FingerprintDoorbell\"" +
                  ",\"model\":\"ESP32 + R503\"" +
                  ",\"sw_version\":\"" + VersionInfo + "\"" +
                  ",\"configuration_url\":\"http://" + WiFi.localIP().toString() + "\"}";

  publishHaDiscoveryEntity("binary_sensor", "ring", nodeId,
                           "\"name\":\"Ring\",\"icon\":\"mdi:doorbell\"," + jsonTopic("state_topic", settings.topicRing) + onOff,
                           device, enabled);
  publishHaDiscoveryEntity("sensor", "match_id", nodeId,
                           "\"name\":\"Match ID\",\"icon\":\"mdi:fingerprint\"," + jsonTopic("state_topic", settings.topicMatchId),
                           device, enabled);
  publishHaDiscoveryEntity("sensor", "match_name", nodeId,
                           "\"name\":\"Match name\",\"icon\":\"mdi:account\"," + jsonTopic("state_topic", settings.topicMatchName),
                           device, enabled);
  publishHaDiscoveryEntity("sensor", "match_confidence", nodeId,
                           "\"name\":\"Match confidence\",\"icon\":\"mdi:percent\"," + jsonTopic("state_topic", settings.topicMatchConfidence),
                           device, enabled);
  publishHaDiscoveryEntity("sensor", "last_log_message", nodeId,
                           "\"name\":\"Last log message\",\"icon\":\"mdi:text\",\"entity_category\":\"diagnostic\"," + jsonTopic("state_topic", settings.topicLastLog),
                           device, enabled && settings.mqttPublishLog);
  publishHaDiscoveryEntity("switch", "ignore_touch_ring", nodeId,
                           "\"name\":\"Ignore touch ring\",\"icon\":\"mdi:weather-pouring\",\"entity_category\":\"config\"," + jsonTopic("command_topic", settings.topicIgnoreTouchRing) + "," + jsonTopic("state_topic", settings.topicIgnoreTouchRing + "/state") + onOff + ",\"retain\":true",
                           device, enabled);
  publishHaDiscoveryEntity("switch", "led_ring", nodeId,
                           "\"name\":\"LED ring\",\"icon\":\"mdi:led-on\",\"entity_category\":\"config\"," + jsonTopic("command_topic", settings.topicLedRing) + "," + jsonTopic("state_topic", settings.topicLedRing + "/state") + onOff + ",\"retain\":true",
                           device, enabled);
  publishHaDiscoveryEntity("button", "open_door", nodeId,
                           "\"name\":\"Open door\",\"icon\":\"mdi:door-open\"," + jsonTopic("command_topic", settings.topicOpenDoor) + ",\"payload_press\":\"" + jsonEscape(settings.payloadOn) + "\"",
                           device, enabled);

  // newer features
  String diag = ",\"entity_category\":\"diagnostic\"";
  publishHaDiscoveryEntity("binary_sensor", "lockout", nodeId,
                           "\"name\":\"Lockout\",\"icon\":\"mdi:shield-lock\",\"device_class\":\"problem\"," + jsonTopic("state_topic", TOPIC_LOCKOUT) + onOff,
                           device, enabled);
  publishHaDiscoveryEntity("binary_sensor", "presence", nodeId,
                           "\"name\":\"Presence\",\"device_class\":\"occupancy\"," + jsonTopic("state_topic", TOPIC_PRESENCE) + onOff,
                           device, enabled && settings.radarEnabled);
  publishHaDiscoveryEntity("binary_sensor", "night_mode", nodeId,
                           "\"name\":\"Night mode\",\"icon\":\"mdi:weather-night\"," + jsonTopic("state_topic", TOPIC_NIGHTMODE) + onOff,
                           device, enabled);
  publishHaDiscoveryEntity("sensor", "access_denied", nodeId,
                           "\"name\":\"Access denied\",\"icon\":\"mdi:account-cancel\"," + jsonTopic("state_topic", TOPIC_ACCESS_DENIED),
                           device, enabled);
  publishHaDiscoveryEntity("sensor", "last_event", nodeId,
                           "\"name\":\"Last event\",\"icon\":\"mdi:history\"," + jsonTopic("state_topic", TOPIC_LAST_EVENT),
                           device, enabled);
  publishHaDiscoveryEntity("text", "enroll", nodeId,
                           "\"name\":\"Enroll finger (id:name)\",\"icon\":\"mdi:fingerprint\",\"entity_category\":\"config\",\"max\":64," + jsonTopic("command_topic", TOPIC_ENROLL),
                           device, enabled && settings.mqttEnroll);
  publishHaDiscoveryEntity("sensor", "enroll_state", nodeId,
                           "\"name\":\"Enroll state\",\"icon\":\"mdi:fingerprint\"," + jsonTopic("state_topic", TOPIC_ENROLL_STATE),
                           device, enabled && settings.mqttEnroll);
  String info = String(TOPIC_INFO) + "/";
  publishHaDiscoveryEntity("sensor", "rssi", nodeId,
                           "\"name\":\"WiFi signal\",\"device_class\":\"signal_strength\",\"unit_of_measurement\":\"dBm\",\"state_class\":\"measurement\"" + diag + "," + jsonTopic("state_topic", info + "rssi"),
                           device, enabled);
  publishHaDiscoveryEntity("sensor", "uptime", nodeId,
                           "\"name\":\"Uptime\",\"device_class\":\"duration\",\"unit_of_measurement\":\"s\",\"state_class\":\"total_increasing\"" + diag + "," + jsonTopic("state_topic", info + "uptime"),
                           device, enabled);
  publishHaDiscoveryEntity("sensor", "ip", nodeId,
                           "\"name\":\"IP address\",\"icon\":\"mdi:ip-network\"" + diag + "," + jsonTopic("state_topic", info + "ip"),
                           device, enabled);
  publishHaDiscoveryEntity("sensor", "free_heap", nodeId,
                           "\"name\":\"Free memory\",\"icon\":\"mdi:memory\",\"unit_of_measurement\":\"B\",\"state_class\":\"measurement\",\"enabled_by_default\":false" + diag + "," + jsonTopic("state_topic", info + "freeHeap"),
                           device, enabled);
  publishHaDiscoveryEntity("sensor", "finger_count", nodeId,
                           "\"name\":\"Stored fingerprints\",\"icon\":\"mdi:fingerprint\",\"state_class\":\"measurement\"" + diag + "," + jsonTopic("state_topic", info + "fingerCount"),
                           device, enabled);
  publishHaDiscoveryEntity("sensor", "failed_attempts", nodeId,
                           "\"name\":\"Failed attempts\",\"icon\":\"mdi:fingerprint-off\",\"state_class\":\"measurement\"" + diag + "," + jsonTopic("state_topic", info + "failedAttempts"),
                           device, enabled);
  publishHaDiscoveryEntity("binary_sensor", "sensor_connected", nodeId,
                           "\"name\":\"Fingerprint sensor\",\"device_class\":\"connectivity\",\"payload_on\":\"online\",\"payload_off\":\"offline\"" + diag + "," + jsonTopic("state_topic", info + "sensor"),
                           device, enabled);
  publishHaDiscoveryEntity("sensor", "sensor_lost", nodeId,
                           "\"name\":\"Fingerprint sensor losses\",\"icon\":\"mdi:fingerprint-off\",\"enabled_by_default\":false" + diag + "," + jsonTopic("state_topic", info + "sensorLost"),
                           device, enabled);
  publishHaDiscoveryEntity("sensor", "wifi_lost", nodeId,
                           "\"name\":\"WiFi connection losses\",\"icon\":\"mdi:wifi-off\",\"enabled_by_default\":false" + diag + "," + jsonTopic("state_topic", info + "wifiLost"),
                           device, enabled);
  publishHaDiscoveryEntity("sensor", "mqtt_lost", nodeId,
                           "\"name\":\"MQTT connection losses\",\"icon\":\"mdi:lan-disconnect\",\"enabled_by_default\":false" + diag + "," + jsonTopic("state_topic", info + "mqttLost"),
                           device, enabled);
}

void mqttCallback(char *topic, byte *message, unsigned int length)
{
  // copy the topic, because PubSubClient reuses the buffer behind 'topic' when publishing inside this callback
  String topicStr = String(topic);
  Serial.print("Message arrived on topic: ");
  Serial.print(topicStr);
  Serial.print(". Message: ");
  String messageTemp;

  for (int i = 0; i < length; i++)
  {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  // Check incomming message for interesting topics
  if (topicStr == mqttTopic(settingsManager.getAppSettings().topicIgnoreTouchRing))
  {
    if (isOnPayload(messageTemp))
    {
      fingerManager.setIgnoreTouchRing(true);
    }
    else if (isOffPayload(messageTemp))
    {
      fingerManager.setIgnoreTouchRing(false);
    }
    publishSwitchStates();
  }

  if (topicStr == mqttTopic(settingsManager.getAppSettings().topicLedRing))
  {
    if (isOnPayload(messageTemp))
    {
      fingerManager.setLedRingEnabled(true);
    }
    else if (isOffPayload(messageTemp))
    {
      fingerManager.setLedRingEnabled(false);
    }
    publishSwitchStates();
  }

  if (topicStr == mqttTopic(TOPIC_ENROLL))
  {
    if (!messageTemp.isEmpty())
    {
      handleMqttEnroll(messageTemp);
      mqttClient.publish(mqttTopic(TOPIC_ENROLL).c_str(), "", true); // clear a retained command, so it is not executed again after a reconnect
    }
  }

  if (topicStr == mqttTopic(settingsManager.getAppSettings().topicOpenDoor))
  {
    if (isOnPayload(messageTemp))
    {
      digitalWrite(dooropenerOutputPin, HIGH);
      Serial.println("MQTT message received: Open the door!");
      delay(1000);
      digitalWrite(dooropenerOutputPin, LOW);
    }
  }

#ifdef CUSTOM_GPIOS
  if (topicStr == mqttTopic("customOutput1"))
  {
    if (isOnPayload(messageTemp))
    {
      digitalWrite(customOutput1, HIGH);
    }
    else if (isOffPayload(messageTemp))
    {
      digitalWrite(customOutput1, LOW);
    }
  }
  if (topicStr == mqttTopic("customOutput2"))
  {
    if (isOnPayload(messageTemp))
    {
      digitalWrite(customOutput2, HIGH);
    }
    else if (isOffPayload(messageTemp))
    {
      digitalWrite(customOutput2, LOW);
    }
  }
#endif
}

void connectMqttClient()
{
  if (WiFi.status() != WL_CONNECTED)
    return; // would only block with a DNS lookup / connect timeout
  if (!mqttClient.connected() && mqttConfigValid)
  {
    Serial.print("(Re)connect to MQTT broker...");
    // Attempt to connect
    bool connectResult;

    // connect with or witout authentication (an empty password is allowed if a username is given)
    String lastWillTopic = mqttTopic(settingsManager.getAppSettings().topicLwt);
    String clientId = settingsManager.getAppSettings().mqttClientId.isEmpty() ? settingsManager.getWifiSettings().hostname : settingsManager.getAppSettings().mqttClientId;
    if (settingsManager.getAppSettings().mqttUsername.isEmpty())
      connectResult = mqttClient.connect(clientId.c_str(), lastWillTopic.c_str(), MQTT_LWT_QOS, MQTT_LWT_RETAIN, MQTT_LWT_PAYLOAD_OFFLINE);
    else
      connectResult = mqttClient.connect(clientId.c_str(), settingsManager.getAppSettings().mqttUsername.c_str(), settingsManager.getAppSettings().mqttPassword.c_str(), lastWillTopic.c_str(), MQTT_LWT_QOS, MQTT_LWT_RETAIN, MQTT_LWT_PAYLOAD_OFFLINE);

    if (connectResult)
    {
      // success
      Serial.println("connected");
      mqttConnectCount++;
      mqttWasConnected = true;
      // LWT publish
      mqttClient.publish(lastWillTopic.c_str(), MQTT_LWT_PAYLOAD_ONLINE, MQTT_LWT_RETAIN);
      // Subscribe
      mqttClient.subscribe(mqttTopic(settingsManager.getAppSettings().topicIgnoreTouchRing).c_str(), 1); // QoS = 1 (at least once)
      mqttClient.subscribe(mqttTopic(settingsManager.getAppSettings().topicOpenDoor).c_str(), 1); // QoS = 1 (at least once)
      mqttClient.subscribe(mqttTopic(settingsManager.getAppSettings().topicLedRing).c_str(), 1); // QoS = 1 (at least once)
      if (settingsManager.getAppSettings().mqttEnroll)
        mqttClient.subscribe(mqttTopic(TOPIC_ENROLL).c_str(), 1);
#ifdef CUSTOM_GPIOS
      mqttClient.subscribe(mqttTopic("customOutput1").c_str(), 1); // QoS = 1 (at least once)
      mqttClient.subscribe(mqttTopic("customOutput2").c_str(), 1); // QoS = 1 (at least once)
#endif
      publishSwitchStates();
      publishFeatureStates();
      publishHaDiscovery();
    }
    else
    {
      if (mqttClient.state() == 4 || mqttClient.state() == 5)
      {
        mqttConfigValid = false;
        notifyClients("Failed to connect to MQTT Server: bad credentials or not authorized. Will not try again, please check your settings.");
      }
      else
      {
        notifyClients(String("Failed to connect to MQTT Server, rc=") + mqttClient.state() + ", try again in 30 seconds");
      }
    }
  }
}

// (re)configures the MQTT client and connects, called at startup and when the MQTT settings are saved
void initMqtt()
{
  // PubSubClient only stores the pointer to the hostname, so it has to be kept in a global variable.
  // Using the hostname (instead of a once resolved IP) lets the client resolve it again on every reconnect.
  mqttHostname = splitIpAndPort(settingsManager.getAppSettings().mqttServer, mqttPort);
  mqttConfigValid = true;
  Serial.println("MQTT server: " + mqttHostname + ":" + mqttPort);
  mqttClient.setServer(mqttHostname.c_str(), mqttPort);
  mqttClient.setBufferSize(1536); // needed for Home Assistant discovery payloads
  mqttClient.setKeepAlive(settingsManager.getAppSettings().mqttKeepAlive);
  mqttClient.setCallback(mqttCallback);
  if (WiFi.status() == WL_CONNECTED)
  {
    connectMqttClient();
    mqttReconnectPreviousMillis = millis();
  }
  else
    mqttReconnectPreviousMillis = 0; // connect as soon as WiFi is up
}

void setupRadarPin()
{
  const AppSettings &appSettings = settingsManager.getAppSettings();
  presenceDetected = false;
  presenceChangeMillis = 0;
  if (appSettings.radarEnabled && isRadarPinAllowed(appSettings.radarPin))
  {
    // GPIO 34-39 have no internal pull resistors, the LD2410 output is push-pull anyway
    if (appSettings.radarPin >= 34)
      pinMode(appSettings.radarPin, INPUT);
    else
      pinMode(appSettings.radarPin, appSettings.radarActiveHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  }
}

// applies the settings of the settings page without a reboot (runs in the loop task, it talks to the sensor)
void applySettings()
{
  const AppSettings &settings = settingsManager.getAppSettings();
  accessControl.configure(settings.failMax, settings.failWindow, settings.lockoutTime);
  setupRadarPin();
  applyTimeSettings();
  if (settings.webPassword.isEmpty())
    updateServer.updateCredentials(emptyString, emptyString);
  else
    updateServer.updateCredentials(webServerUser, settings.webPassword);
  if (fingerManager.connected)
    fingerManager.configTouchRingActive(settings.touchRingActiveColor, settings.touchRingActiveSequence, settings.scanColor, settings.matchColor);
  updateNightMode();
  updateLedSuppression();
  if (fingerManager.connected)
    fingerManager.setLedRingReady();
  if (mqttClient.connected())
  {
    publishFeatureStates();
    publishHaDiscovery(); // e.g. presence entity appears/disappears with the radar setting
  }
  notifyClients("Settings saved and applied.");
}

// applies the MQTT settings without a reboot: disconnect and connect with the new settings
void applyMqttSettings()
{
  if (mqttClient.connected())
  {
    // the old LWT topic would otherwise stay "Online" forever if the topic name was changed
    mqttClient.publish(oldLwtTopic.c_str(), MQTT_LWT_PAYLOAD_OFFLINE, MQTT_LWT_RETAIN);
    mqttClient.disconnect();
  }
  mqttWasConnected = false; // no "connection lost" message for this intended disconnect
  if (settingsManager.getAppSettings().mqttServer.isEmpty())
  {
    mqttConfigValid = false;
    notifyClients("MQTT settings saved, no broker configured.");
    return;
  }
  notifyClients("MQTT settings saved, reconnecting...");
  initMqtt();
}

void publishMatchReset(const AppSettings &settings, bool retain, bool ring)
{
  mqttClient.publish(mqttTopic(settings.topicRing).c_str(), ring ? settings.payloadOn.c_str() : settings.payloadOff.c_str(), retain);
  mqttClient.publish(mqttTopic(settings.topicMatchId).c_str(), "-1", retain);
  mqttClient.publish(mqttTopic(settings.topicMatchName).c_str(), "", retain);
  mqttClient.publish(mqttTopic(settings.topicMatchConfidence).c_str(), "-1", retain);
}

void startLockout()
{
  const AppSettings &settings = settingsManager.getAppSettings();
  notifyClients(String("Too many failed attempts (") + settings.failMax + " within " + settings.failWindow + "s). Matches are ignored for " + settings.lockoutTime + "s.");
  logEvent(EventType::lockout, 0, 0, String(settings.failMax) + " failed attempts");
  fingerManager.setLockout(true);
  publishOnOff(TOPIC_LOCKOUT, true);
}

void denyAccess(const Match &match, const String &reason)
{
  notifyClients(String("Access denied for ") + match.matchId + " - " + match.matchName + ": " + reason);
  logEvent(EventType::accessDenied, match.matchId, match.matchConfidence, match.matchName + ": " + reason);
  mqttClient.publish(mqttTopic(TOPIC_ACCESS_DENIED).c_str(), (match.matchName.isEmpty() ? String(match.matchId) : match.matchName).c_str(), true);
  accessDeniedMillis = millis();
  fingerManager.showAccessDenied();
}

// common handling after the sensor answers again (automatic or manual reconnect)
void onSensorRestored(unsigned long now)
{
  sensorPingFails = 0;
  sensorStateSinceMillis = now;
  const AppSettings &settings = settingsManager.getAppSettings();
  fingerManager.configTouchRingActive(settings.touchRingActiveColor, settings.touchRingActiveSequence, settings.scanColor, settings.matchColor);
  updateLedSuppression();
  fingerManager.setLedRingReady();
  notifyClients("Fingerprint sensor connected.");
  logEvent(EventType::sensorRestored);
  updateClientsFingerlist(getFingerListHtml());
  if (!checkPairingValid())
    notifyClients("Security issue! Pairing with sensor is invalid. This could potentially be an attack! If the sensor is new or has been replaced by you use \"Pair a new sensor\" in the Sensor section of the Fingerprints page. MQTT messages regarding matching fingerprints will not been sent until pairing is valid again.");
  if (mqttClient.connected())
    publishInfo();
}

// manual reconnect requested in the web UI: restart the sensor serial port and connect again
void reconnectSensorNow()
{
  unsigned long now = millis();
  bool wasConnected = fingerManager.connected;
  sensorAutoRetryActive = false; // a manual attempt is a single try, no new automatic retry window
  notifyClients("Manual sensor reconnect requested...");
  sensorReconnecting = true;
  fingerManager.markDisconnected();
  fingerManager.restartSerial();
  bool ok = fingerManager.connect(false);
  sensorReconnecting = false;
  lastSensorCheckMillis = millis();
  if (ok)
  {
    onSensorRestored(now);
    return;
  }
  sensorStateSinceMillis = now;
  if (wasConnected)
  {
    sensorLostCount++;
    logEvent(EventType::sensorLost, 0, 0, "manual reconnect failed");
  }
  notifyClients("Sensor reconnect failed, the sensor does not answer. Check the wiring.");
  if (mqttClient.connected())
    publishInfo();
}

// checks the fingerprint sensor while running: detects a lost sensor and reconnects when it comes back
void superviseSensor()
{
  unsigned long now = millis();
  if (sensorReconnectRequested)
  {
    sensorReconnectRequested = false;
    reconnectSensorNow();
    return;
  }
  if (fingerManager.connected)
  {
    if (now - lastSensorCheckMillis < SENSOR_CHECK_INTERVAL)
      return;
    lastSensorCheckMillis = now;
    if (fingerManager.isFingerOnSensor())
    {
      sensorPingFails = 0; // finger on the sensor, it is obviously alive (and busy)
      return;
    }
    if (fingerManager.ping())
    {
      sensorPingFails = 0;
      return;
    }
    if (++sensorPingFails < SENSOR_MAX_FAILS)
      return;
    fingerManager.markDisconnected();
    sensorStateSinceMillis = now;
    sensorLostCount++;
    sensorAutoRetryActive = true;
    notifyClients("Fingerprint sensor does not answer anymore! Check the wiring. Reconnecting automatically for 3 minutes...");
    logEvent(EventType::sensorLost);
    if (mqttClient.connected())
    {
      mqttClient.publish((mqttTopic(TOPIC_INFO) + "/sensor").c_str(), "offline", true);
      mqttClient.publish((mqttTopic(TOPIC_INFO) + "/sensorLost").c_str(), String(sensorLostCount).c_str(), true);
    }
  }
  else
  {
    if (!sensorAutoRetryActive)
      return; // gave up, only a manual reconnect helps
    if (now - sensorStateSinceMillis >= SENSOR_RETRY_WINDOW)
    {
      sensorAutoRetryActive = false;
      notifyClients("Fingerprint sensor still not found after 3 minutes, automatic retries stopped. Check the wiring and use \"Reconnect sensor\" on the Log or Fingerprints page.");
      logEvent(EventType::sensorLost, 0, 0, "auto retry stopped");
      return;
    }
    if (now - lastSensorCheckMillis < SENSOR_RETRY_INTERVAL)
      return;
    lastSensorCheckMillis = now;
    sensorReconnecting = true;
    bool ok = fingerManager.connect(true);
    sensorReconnecting = false;
    if (ok)
      onSensorRestored(now);
  }
}

void doScan()
{
  Match match = fingerManager.scanFingerprint();
  const AppSettings &settings = settingsManager.getAppSettings();
  bool retain = settings.mqttRetain;
  switch (match.scanResult)
  {
  case ScanResult::noFinger:
    // standard case, occurs every iteration when no finger touchs the sensor
    if (match.scanResult != lastMatch.scanResult)
    {
      Serial.println("no finger");
      publishMatchReset(settings, retain, false);
    }
    break;
  case ScanResult::matchFound:
    if (match.scanResult != lastMatch.scanResult)
    {
      notifyClients(String("Match Found: ") + match.matchId + " - " + match.matchName + " with confidence of " + match.matchConfidence);
      String reason;
      if (accessControl.isLockedOut())
      {
        denyAccess(match, String("lockout active (") + accessControl.getLockoutRemainingSeconds() + "s left)");
      }
      else if (!accessControl.isAccessAllowed(match.matchId, reason))
      {
        denyAccess(match, reason);
      }
      else if (checkPairingValid())
      {
        bool opendoor = false;
        if (match.matchName.endsWith("#"))
          opendoor = true;
        if (opendoor)
          digitalWrite(dooropenerOutputPin, HIGH);
        mqttClient.publish(mqttTopic(settings.topicRing).c_str(), settings.payloadOff.c_str(), retain);
        mqttClient.publish(mqttTopic(settings.topicMatchId).c_str(), String(match.matchId).c_str(), retain);
        mqttClient.publish(mqttTopic(settings.topicMatchName).c_str(), match.matchName.c_str(), retain);
        mqttClient.publish(mqttTopic(settings.topicMatchConfidence).c_str(), String(match.matchConfidence).c_str(), retain);
        Serial.println("MQTT message sent: Open the door!");
        logEvent(opendoor ? EventType::doorOpened : EventType::match, match.matchId, match.matchConfidence, match.matchName);
        if (opendoor)
        {
          delay(1000);
          digitalWrite(dooropenerOutputPin, LOW);
        }
      }
      else
      {
        notifyClients("Security issue! Match was not sent by MQTT because of invalid sensor pairing! This could potentially be an attack! If the sensor is new or has been replaced by you use \"Pair a new sensor\" in the Sensor section of the Fingerprints page.");
        logEvent(EventType::accessDenied, match.matchId, match.matchConfidence, "invalid sensor pairing");
      }
    }
    delay(3000); // wait some time before next scan to let the LED blink
    break;
  case ScanResult::noMatchFound:
    if (match.scanResult != lastMatch.scanResult)
    {
      // a real finger that is not in the database (in contrast to a touch on the ring only) counts as failed attempt
      bool unknownFinger = (match.returnCode == FINGERPRINT_NOTFOUND);
      notifyClients(unknownFinger ? String("No Match Found (unknown finger)") : String("No Match Found (Code ") + match.returnCode + ")");
      if (unknownFinger && accessControl.recordFailure())
        startLockout();

      if (settings.ringCooldown > 0 && ringEverHappened && millis() - lastRingMillis < (unsigned long)settings.ringCooldown * 1000UL)
      {
        notifyClients(String("Ring suppressed (cooldown of ") + settings.ringCooldown + "s).");
      }
      else
      {
        lastRingMillis = millis();
        ringEverHappened = true;
        digitalWrite(doorbellOutputPin, HIGH);
        publishMatchReset(settings, retain, true);
        Serial.println("MQTT message sent: ring the bell!");
        logEvent(EventType::ring, 0, 0, unknownFinger ? "unknown finger" : "");
        delay(1000);
        digitalWrite(doorbellOutputPin, LOW);
      }
    }
    else
    {
      delay(1000); // wait some time before next scan to let the LED blink
    }
    break;
  case ScanResult::error:
    notifyClients(String("ScanResult Error (Code ") + match.returnCode + ")");
    break;
  };
  lastMatch = match;
}

void doEnroll()
{
  int id = enrollId.toInt();
  if (id < 1 || id > 200)
  {
    notifyClients("Invalid memory slot id '" + enrollId + "'");
    onEnrollProgress("failed: invalid id");
    return;
  }

  onEnrollProgress("running");
  NewFinger finger = fingerManager.enrollFinger(id, enrollName);
  if (finger.enrollResult == EnrollResult::ok)
  {
    notifyClients("Enrollment successfull. You can now use your new finger for scanning.");
    logEvent(EventType::enrolled, id, 0, enrollName);
    onEnrollProgress(String("success: #") + id);
    updateClientsFingerlist(getFingerListHtml());
  }
  else if (finger.enrollResult == EnrollResult::error)
  {
    notifyClients(String("Enrollment failed. (Code ") + finger.returnCode + ")");
    onEnrollProgress(String("failed: code ") + finger.returnCode);
  }
}

void reboot()
{
  notifyClients("System is rebooting now...");
  delay(1000);

  mqttClient.disconnect();
  espClient.stop();
  dnsServer.stop();
  webServer.end();
  WiFi.disconnect();
  ESP.restart();
}

void setup()
{
  // open serial monitor for debug infos
  Serial.begin(115200);
  while (!Serial)
    ; // For Yun/Leo/Micro/Zero/...
  delay(100);

  settingsManager.loadWifiSettings();
  settingsManager.loadAppSettings();

  // GPIOs from the settings; invalid or doubled pins (e.g. damaged settings) fall back to the original wiring
  {
    AppSettings pinSettings = settingsManager.getAppSettings();
    int8_t extraUsed[6];
    size_t extraCount = getExtraUsedPins(pinSettings, extraUsed, 6);
    String pinError;
    if (!GpioConfig::validate(pinSettings.pins, extraUsed, extraCount, pinError))
    {
      Serial.println("Invalid GPIO settings (" + pinError + "), using the default pins");
      pinSettings.pins = PinSet();
      settingsManager.saveAppSettings(pinSettings);
    }
  }
  activePins = settingsManager.getAppSettings().pins;
  doorbellOutputPin = activePins.doorbell;
  dooropenerOutputPin = activePins.doorOpener;
  fingerManager.setPins(activePins.sensorRx, activePins.sensorTx, activePins.touchRing);

  // initialize GPIOs
  pinMode(doorbellOutputPin, OUTPUT);
  digitalWrite(doorbellOutputPin, LOW);
  pinMode(dooropenerOutputPin, OUTPUT);
  digitalWrite(dooropenerOutputPin, LOW);
#ifdef CUSTOM_GPIOS
  pinMode(customOutput1, OUTPUT);
  pinMode(customOutput2, OUTPUT);
  pinMode(customInput1, INPUT_PULLDOWN);
  pinMode(customInput2, INPUT_PULLDOWN);
#endif

  const AppSettings &appSettings = settingsManager.getAppSettings();
  accessControl.configure(appSettings.failMax, appSettings.failWindow, appSettings.lockoutTime);
  accessControl.loadSchedules();
  setupRadarPin();

  fingerManager.connect();
  sensorStateSinceMillis = millis();
  sensorAutoRetryActive = !fingerManager.connected;
  if (!fingerManager.connected)
    notifyClients("Fingerprint sensor not found at startup. Check the wiring (power, TX/RX). Reconnecting automatically for 3 minutes, after that use \"Reconnect sensor\" on the Log or Fingerprints page.");

  if (fingerManager.connected && !checkPairingValid())
    notifyClients("Security issue! Pairing with sensor is invalid. This could potentially be an attack! If the sensor is new or has been replaced by you use \"Pair a new sensor\" in the Sensor section of the Fingerprints page. MQTT messages regarding matching fingerprints will not been sent until pairing is valid again.");

  if (fingerManager.isFingerOnSensor() || !settingsManager.isWifiConfigured())
  {
    // ring touched during startup or no wifi settings stored -> wifi config mode
    currentMode = Mode::wificonfig;
    Serial.println("Started WiFi-Config mode");
    fingerManager.setLedRingWifiConfig();
    initWiFiAccessPointForConfiguration();
    startWebserver();
  }
  else
  {
    Serial.println("Started normal operating mode");
    currentMode = Mode::scan;
    initWifi(); // connects in the background, the doorbell works without WiFi too
    startWebserver();
    if (settingsManager.getAppSettings().mqttServer.isEmpty())
    {
      mqttConfigValid = false;
      notifyClients("Error: No MQTT Broker is configured! Please go to settings and enter your server URL + user credentials.");
    }
    else
    {
      initMqtt(); // only configures the client, the connection is made by the loop as soon as WiFi is connected
    }
    if (fingerManager.connected)
    {
      fingerManager.configTouchRingActive(settingsManager.getAppSettings().touchRingActiveColor, settingsManager.getAppSettings().touchRingActiveSequence, settingsManager.getAppSettings().scanColor, settingsManager.getAppSettings().matchColor);
      updateLedSuppression();
      fingerManager.setLedRingReady();
    }
    else
      fingerManager.setLedRingError();
    logEvent(EventType::boot, 0, 0, String("firmware ") + VersionInfo);
    notifyClients(String("System started in ") + (millis() / 1000.0) + "s, waiting for WiFi...");
  }
}

void loop()
{
  // shouldReboot flag for supporting reboot through webui
  if (shouldReboot)
  {
    reboot();
  }

  // Reconnect handling
  if (currentMode != Mode::wificonfig)
  {
    unsigned long currentMillis = millis();

    // detect connection losses and log the reason (helps to find the cause of frequent reconnects)
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (wifiWasConnected && !wifiConnected)
    {
      wifiLostCount++;
      notifyClients("WiFi connection lost (status " + String(WiFi.status()) + ", reason: " + getWifiReasonString(lastWifiDisconnectReason) + ")");
    }
    else if (!wifiWasConnected && wifiConnected)
    {
      if (!wifiEverConnected)
        notifyClients("WiFi connected after " + String(millis() / 1000.0) + "s, IP " + WiFi.localIP().toString() + ", AP " + WiFi.BSSIDstr() + " channel " + String(WiFi.channel()) + ", RSSI " + String(WiFi.RSSI()) + " dBm");
      else
        notifyClients("WiFi reconnected, RSSI " + String(WiFi.RSSI()) + " dBm");
      wifiEverConnected = true;
      wifiConnectAttempts = 0;
      mqttReconnectPreviousMillis = 0; // connect MQTT right away instead of waiting for the 30s timer
    }
    wifiWasConnected = wifiConnected;

    if (mqttWasConnected && !mqttClient.connected())
    {
      mqttLostCount++;
      mqttWasConnected = false;
      notifyClients("MQTT connection lost: " + getMqttStateString(mqttClient.state()) + ", WiFi RSSI " + String(WiFi.RSSI()) + " dBm");
    }

    // retry WiFi if still not connected after 20s. A fresh WiFi.begin() is more reliable than reconnect() with this
    // framework version (the first connection after a reset sometimes hangs).
    if (!wifiConnected && (currentMillis - wifiReconnectPreviousMillis >= 15000ul))
    {
      wifiConnectAttempts++;
      notifyClients(String("No WiFi connection after ") + (millis() / 1000) + "s (status " + WiFi.status() + ", last reason: " + getWifiReasonString(lastWifiDisconnectReason) + ")");
      connectWifi(wifiConnectAttempts % 2 == 0); // every second retry with a complete restart of the WiFi stack
    }

    // reconnect mqtt if down
    if (!settingsManager.getAppSettings().mqttServer.isEmpty())
    {
      if (wifiConnected && !mqttClient.connected() && (mqttReconnectPreviousMillis == 0 || currentMillis - mqttReconnectPreviousMillis >= 30000ul))
      {
        connectMqttClient();
        mqttReconnectPreviousMillis = currentMillis;
      }
      mqttClient.loop();
    }

    // lockout expired?
    if (accessControl.pollLockoutEnded())
    {
      notifyClients("Lockout ended, matches are accepted again.");
      logEvent(EventType::lockoutEnded);
      fingerManager.setLockout(false);
      publishOnOff(TOPIC_LOCKOUT, false);
    }

    // reset the access denied topic after 3s (like matchId/matchName)
    if (accessDeniedMillis != 0 && currentMillis - accessDeniedMillis >= 3000ul)
    {
      mqttClient.publish(mqttTopic(TOPIC_ACCESS_DENIED).c_str(), "", true);
      accessDeniedMillis = 0;
    }

    if (currentMillis - lastNightCheckMillis >= 10000ul)
    {
      updateNightMode();
      lastNightCheckMillis = currentMillis;
    }

    updateRadar();

    if (mqttClient.connected() && currentMillis - lastInfoPublishMillis >= 60000ul)
      publishInfo();

    if (applySettingsRequested)
    {
      applySettingsRequested = false;
      applySettings();
    }

    if (ignoreRingRequest >= 0 || ledRingRequest >= 0)
    {
      int8_t ignoreValue = ignoreRingRequest;
      int8_t ledValue = ledRingRequest;
      ignoreRingRequest = -1;
      ledRingRequest = -1;
      if (ignoreValue >= 0)
        fingerManager.setIgnoreTouchRing(ignoreValue == 1);
      if (ledValue >= 0)
        fingerManager.setLedRingEnabled(ledValue == 1);
      if (ignoreValue >= 0 && fingerManager.connected)
        fingerManager.setLedRingReady(); // show the new state on the ring right away
      if (mqttClient.connected())
        publishSwitchStates();
    }

    if (applyMqttRequested)
    {
      applyMqttRequested = false;
      applyMqttSettings();
    }

    if (clearEventsRequested)
    {
      clearEventsRequested = false;
      eventLog.clear();
      publishLastEvent();
      notifyClients("Event history cleared.");
    }

    // jobs requested by the web server, they need exclusive access to the sensor
    if (currentMode == Mode::scan && backupRequested)
    {
      backupRequested = false;
      currentMode = Mode::backup;
    }
    if (currentMode == Mode::scan && restoreRequested)
    {
      restoreRequested = false;
      currentMode = Mode::restore;
    }
  }

  // do the actual loop work
  switch (currentMode)
  {
  case Mode::scan:
    superviseSensor();
    if (fingerManager.connected)
      doScan();
    break;

  case Mode::enroll:
    doEnroll();
    currentMode = Mode::scan; // switch back to scan mode after enrollment is done
    break;

  case Mode::wificonfig:
    dnsServer.processNextRequest(); // used for captive portal redirect
    break;

  case Mode::maintenance:
    // do nothing, give webserver exclusive access to sensor (not thread-safe for concurrent calls)
    break;

  case Mode::backup:
    if (backupManager.createBackup(fingerManager, accessControl, backupSingleId))
      logEvent(EventType::backup, 0, 0, String(backupManager.getProgress()) + " fingerprints");
    fingerManager.setLedRingReady();
    currentMode = Mode::scan;
    break;

  case Mode::restore:
  {
    bool ok = backupManager.restoreBackup(fingerManager, accessControl, restoreMode);
    logEvent(EventType::restore, 0, 0, ok ? String(backupManager.getProgress()) + " fingerprints" : String("failed"));
    updateClientsFingerlist(getFingerListHtml());
    fingerManager.setLedRingReady();
    currentMode = Mode::scan;
    break;
  }
  }

  // enter maintenance mode (no continous scanning) if requested
  if (needMaintenanceMode)
    currentMode = Mode::maintenance;

#ifdef CUSTOM_GPIOS
  // read custom inputs and publish by MQTT
  bool i1;
  bool i2;
  i1 = (digitalRead(customInput1) == HIGH);
  i2 = (digitalRead(customInput2) == HIGH);

  if (i1 != customInput1Value)
  {
    if (i1)
      mqttClient.publish(mqttTopic("customInput1").c_str(), onOffPayload(true));
    else
      mqttClient.publish(mqttTopic("customInput1").c_str(), onOffPayload(false));
  }

  if (i2 != customInput2Value)
  {
    if (i2)
      mqttClient.publish(mqttTopic("customInput2").c_str(), onOffPayload(true));
    else
      mqttClient.publish(mqttTopic("customInput2").c_str(), onOffPayload(false));
  }

  customInput1Value = i1;
  customInput2Value = i2;

#endif
}
