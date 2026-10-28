# CLAUDE.md

ESP32 firmware (Arduino framework, PlatformIO) for a combined fingerprint reader and doorbell using the Grow R503 sensor. Publishes ring/match events over MQTT, has a web UI for enrollment and settings, and supports Home Assistant MQTT discovery.

## Repository

- `origin` = Kalored/FingerprintDoorbell-NG (this fork), `upstream` = geronet1/FingerprintDoorbell. The original project is frickelzeugs/FingerprintDoorbell (unmaintained, many open issues worth checking).
- Work happens on `main`. Changes are documented in the "Changes in x.y" list at the top of `README.md`. The list is chronological: add a new version at the end, after the newest entry (the user reordered it by hand once). The changelog is the last section of the README ("📝 Changelog"). Only the newest version is shown open (`### x.y · Headline (latest)`); when adding one, turn the previous latest into `<details><summary><b>x.y</b> · Headline</summary>` like the older ones, and update the "New in x.y" line in the note box at the top. Every version gets a short headline with its most important change.
- Bump `VersionInfo` in `src/main.cpp` on releases. It is shown in the web UI and sent as `sw_version` in HA discovery.

## Build

PlatformIO is not installed system-wide. Use a venv (e.g. `uv venv pio && VIRTUAL_ENV=$PWD/pio uv pip install platformio`), then from the repo root:

```bash
pio run                 # firmware -> .pio/build/esp32doit-devkit-v1/firmware.bin
pio run -t buildfs      # web UI (data/) -> spiffs.bin
```

- Both images are needed for an OTA update: firmware.bin goes to "Firmware" and spiffs.bin to "Filesystem" on the `/update` page.
- `builds/` is gitignored and holds local copies of the binaries.
- The platform is pinned to `espressif32@3.5.0` (arduino-esp32 1.0.6), so library APIs are old. Check signatures in `.pio/libdeps/` and `~/.platformio/packages/framework-arduinoespressif32/` before using newer APIs.
- There are no automated tests and no hardware in this environment. Changes can only be checked by compiling, so say clearly that something is untested on hardware. Discovery JSON can be validated by evaluating the string literals with Python.

## Code layout

- `src/main.cpp`: Wi-Fi/AP config mode, web server routes, the template `processor()`, MQTT (connect, callback, HA discovery), and the main `loop()` state machine (`Mode::scan/enroll/wificonfig/maintenance`).
- `src/FingerprintManager.*`: all sensor communication (scan, enroll, LED ring, notepad-based sensor pairing). Finger names are stored in NVS namespace `fingerList`.
- `src/SerialLog.*`, `src/SerialRedirect.h`: `Serial` in the firmware sources is replaced by `SerialLog` (a `Print` that writes to the real serial port and keeps the last 8 KB in a ring buffer for the serial monitor on the Log page). Every `.cpp` that prints to `Serial` includes `SerialRedirect.h` as its **last** include (never in headers).
- `src/EventLog.*`: persistent event history (30 records, ring buffer file `/events.bin` on SPIFFS). Use `logEvent()` in main, it also publishes `lastEvent`.
- `src/AccessControl.*`: lockout after failed attempts (millis based) and per-finger access schedules (NVS namespace `fingerAccess`).
- `src/BackupManager.*`: backup/restore file format (`/backup.fpdb`, `/restore.fpdb` on SPIFFS).
- `src/SettingsManager.*`: `WifiSettings`/`AppSettings` stored in NVS (`Preferences`). A new setting needs four places: the struct default, `load`, `save`, and the `/settings` handler plus a `processor()` placeholder.
- `data/*.html`: pages served from SPIFFS (`index` fingerprints, sensor actions, backup; `settings` general/LED/security/radar, firmware update, factory reset; `mqtt` connection state, broker/topics/HA; `log` sensor status, events, system log, network/device info; `wificonfig` AP mode). There is no system page anymore, `/system` redirects to `/log`. Each page posts to its own route and must only touch its own settings fields. `%PLACEHOLDER%` is replaced by `processor()`. They use the local `bootstrap.min.css`; do not add CDN links, because the device must work without internet.

## Pitfalls (already caused bugs here)

- **Pointer lifetimes**: `settingsManager.getAppSettings()` returns a *copy*. Never pass `.c_str()` of it to an API that keeps the pointer. `configTime()`/SNTP and `PubSubClient::setServer(const char*)` store pointers, so use static or global `String`s (see `ntpServer`, `mqttHostname`). A dangling NTP pointer previously caused random DNS lookups.
- **PubSubClient callback**: `topic`/`payload` point into the client's shared buffer. Publishing inside `mqttCallback` overwrites them, so copy them first (`topicStr`). The buffer is enlarged to 1536 bytes for discovery payloads.
- **Percent signs in pages**: the web server template processor replaces *everything* between two `%` with the processor result (empty for unknown names), even across lines. Never use a literal `%` in HTML/CSS/JS of `data/` (no `%` modulo in JS, no `100%` in CSS); write `%%` if one is really needed. Check with: `python3 -c "import re,glob;[print(f,re.sub(r'%[A-Z0-9_]+%','',open(f).read()).count('%')) for f in glob.glob('data/*.html')]"`
- **Sensor packets**: `Adafruit_Fingerprint_Packet` holds only 64 payload bytes, template data packets are up to 256 bytes. Use `readRawPacket()/writeRawPacket()` in `FingerprintManager` for template transfer, never `getStructuredPacket()` for data packets.
- **Long sensor jobs** (backup/restore) run in the loop task (`Mode::backup/restore`), requested via flags by the web server; the browser polls `/backup/status`.
- **Template processor**: always pass user-defined values through `htmlEscape()`, which also escapes `%`. A raw `%` in a value breaks the rest of the page. Passwords are never sent to the browser; they appear as `********`, and the save handler keeps the old value when it receives that string.
- **Auth**: every normal-mode route must check `webPassword` the same way the existing ones do. Register the OTA `updateServer.setup()` exactly once, because an earlier unauthenticated registration wins.
- **Concurrency**: web handlers run in the AsyncTCP task, while `loop()` and `mqttCallback` run in the Arduino loop task. Only the loop task may talk to the sensor. Web handlers that need the sensor must call `waitForMaintenanceMode()` first (see delete finger).
- **Blocking**: `delay()` calls in `doScan`/enrollment stall `mqttClient.loop()`. Keep them short, or MQTT keepalive (15 s) can time out.
- Time zone: the C library silently falls back to UTC for an invalid TZ string (shows up as 1-2 h offset). Always pass it through `resolveTimezone()` (accepts names from the `timezones[]` table or POSIX strings) and apply it with `configTzTime()`.
- Saving settings must not reboot: the handler saves and sets a flag (`applySettingsRequested`, `applyMqttRequested`), the loop task applies it (`applySettings()`, `applyMqttSettings()`). A new setting has to be handled there too.
- `bootstrap.min.css` is only stored as `data/bootstrap.min.css.gz` and served with `Content-Encoding: gzip` and a long cache time. If it is ever replaced, rename it so browsers don't keep the old one.
- Never block the boot on WiFi or MQTT and never reboot because of a missing connection: `initWifi()` only starts the connection, the loop retries WiFi (`WiFi.begin()` every 20s) and connects MQTT once WiFi is up. `connectMqttClient()` returns immediately without WiFi.
- WiFi modem sleep is disabled on purpose (`WiFi.setSleep(false)`), it made the web UI slow.
- Settings forms use POST (`HTTP_GET | HTTP_POST` routes) so passwords do not end up in URLs.

## MQTT interface

Root topic, every sub topic and the on/off payloads are configurable in the web UI (`AppSettings::topic*`, `payloadOn/Off`). Always build topics with `mqttTopic(settings.topicX)` and command checks with `isOnPayload()/isOffPayload()`, never hardcode them. Defaults:

- Published: `LWT` (Online/Offline, retained), `ring`, `matchId`, `matchName`, `matchConfidence`, `lastLogMessage`, `ignoreTouchRing/state`, `ledRing/state` (states retained).
- Subscribed: `ignoreTouchRing` and `ledRing` (on/off), `openDoor` (`true` pulses GPIO 23).
- A finger name ending in `#` also pulses the door-opener GPIO on match.
- HA discovery (setting `haDiscovery`) publishes retained configs to `homeassistant/<component>/fpdoorbell_<mac>/<object>/config`. Disabling it publishes empty retained payloads to remove them. The user runs both Home Assistant and ioBroker (ioBroker's MQTT adapter acts as the broker).

GPIOs (configurable in Settings, "GPIO pins", used after a restart; defaults): sensor UART on Serial2 (RX 16, TX 17), touch ring pin 5, doorbell output 19, door opener 23. `src/GpioConfig.h` holds the per-function lists of usable pins for a classic ESP32 (no strapping/flash/USB-serial pins, no PWM-at-boot pins for outputs, no input-only pins for outputs) and `validate()` (allowed + no pin used twice, also not by the radar pin or the `CUSTOM_GPIOS` pins). The Adafruit library's `begin()` always opens Serial2 on its default pins, so `FingerprintManager::startSerial()` opens the port itself. Invalid stored pins fall back to the defaults at boot. Optional custom I/O is behind `CUSTOM_GPIOS`.

## Open topics

- WiFi (user runs TP-Link Omada, multiple APs): with 0.7.2 the first connection took ~200s (status 4 CONNECT_FAILED, RSSI -65 dBm). 0.7.3 (connect to strongest AP by BSSID/channel, WiFi stack restart on retries) connected after 11s. Recommended Omada settings for ESP SSIDs: WPA2-PSK AES, PMF disabled, no 802.11r, no band steering. Frequent MQTT reconnects in ioBroker (~every 30 s) were probably caused by the same WiFi issues plus modem sleep (disabled since 0.7.1); not confirmed yet.
- Fork issue #2 (pairing fails with `.`/`-` in the MQTT password) could not be explained from the code.
