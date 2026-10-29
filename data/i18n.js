// Translation of the web interface. The pages are written in English; if the device language is German
// (<html lang="de">, setting saved on the device), all visible texts are replaced in the browser.
// Keys are the English texts with whitespace collapsed. Elements with class "notranslate" (logs) are skipped.
// This file is served as it is (not by the template processor), so the percent sign may be used here.
(function () {
  var lang = document.documentElement.lang === 'de' ? 'de' : 'en';

  var de = {
    // navbar, footer, common
    'Fingerprints': 'Fingerabdrücke',
    'Settings': 'Einstellungen',
    'Log': 'Protokoll',
    'Save': 'Speichern',
    'Delete': 'Löschen',
    'Rename': 'Umbenennen',
    'Clear': 'Leeren',
    'Copy': 'Kopieren',
    'Github': 'GitHub',
    'within': 'innerhalb von',

    // Fingerprints page
    'Manage fingerprints': 'Fingerabdrücke verwalten',
    'Fingerprints in DB': 'Gespeicherte Finger',
    'Access times are shown in brackets, e.g. [Mo,Tu 08:00-12:00].': 'Zutrittszeiten stehen in Klammern, z.B. [Mo,Di 08:00-12:00].',
    'Access times of the selected finger': 'Zutrittszeiten des ausgewählten Fingers',
    'Finger': 'Finger',
    'Access': 'Zutritt',
    'Always': 'Immer',
    'Only at certain times': 'Nur zu bestimmten Zeiten',
    'Disabled': 'Gesperrt',
    'Weekdays': 'Wochentage',
    'Mo': 'Mo', 'Tu': 'Di', 'We': 'Mi', 'Th': 'Do', 'Fr': 'Fr', 'Sa': 'Sa', 'Su': 'So',
    'Time': 'Zeit',
    'From - to. A window over midnight (e.g. 22:00 - 06:00) is possible. Outside of the allowed times the finger is not accepted: no match is published, the door is not opened and "accessDenied" is published instead. Needs a working time (NTP).':
      'Von - bis. Ein Zeitfenster über Mitternacht (z.B. 22:00 - 06:00) ist möglich. Außerhalb der erlaubten Zeiten wird der Finger nicht akzeptiert: Es wird kein Treffer gemeldet, die Tür öffnet nicht und stattdessen wird "accessDenied" gesendet. Braucht eine funktionierende Uhrzeit (NTP).',
    'Save access times': 'Zutrittszeiten speichern',
    '(no finger selected)': '(kein Finger ausgewählt)',
    'Add/Replace fingerprint': 'Fingerabdruck hinzufügen/ersetzen',
    'Memory slot (1-200)': 'Speicherplatz (1-200)',
    'The sensor has 200 memory slots available for storing fingerprints. The choosen slot number will also be used as an ID when matches are published by MQTT.':
      'Der Sensor hat 200 Speicherplätze für Fingerabdrücke. Die Nummer des Speicherplatzes wird auch als ID verwendet, wenn ein Treffer per MQTT gemeldet wird.',
    'Name': 'Name',
    '(optional)': '(optional)',
    'Just for human readability you can additionally assign an name to your slot number. The name will also been published by MQTT.':
      'Zur besseren Lesbarkeit kannst du dem Speicherplatz zusätzlich einen Namen geben. Der Name wird ebenfalls per MQTT gesendet.',
    "If the name ends with a '#' the door opener output pin is activated, too.": "Endet der Name mit '#', wird zusätzlich der Ausgang für den Türöffner geschaltet.",
    'Start enrollment': 'Anlernen starten',
    'Backup & restore': 'Sichern & wiederherstellen',
    'Backup': 'Sicherung',
    'Create and download backup': 'Sicherung erstellen und herunterladen',
    'Saves all fingerprints (sensor templates), their names and access times to a file. Keep it in a safe place, it contains your fingerprint data!':
      'Speichert alle Fingerabdrücke (Sensor-Templates), ihre Namen und Zutrittszeiten in eine Datei. Bewahre sie sicher auf, sie enthält deine Fingerabdruckdaten!',
    'Restore': 'Wiederherstellen',
    'Delete all fingerprints on the sensor first': 'Vorher alle Fingerabdrücke auf dem Sensor löschen',
    'Restore backup': 'Sicherung wiederherstellen',
    'Switch between light and dark': 'Zwischen hell und dunkel wechseln',
    'Add / replace': 'Hinzufügen / ersetzen',
    'Slot 1-200': 'Platz 1-200',
    'Memory slot': 'Speicherplatz',
    'Just for human readability you can additionally assign an name to your slot number. The name will also been published by MQTT. If the name ends with a \'#\' the door opener output pin is activated, too.': 'Zur besseren Lesbarkeit kannst du dem Speicherplatz zusätzlich einen Namen geben. Der Name wird ebenfalls per MQTT gesendet. Endet der Name mit \'#\', wird zusätzlich der Ausgang für den Türöffner geschaltet.',
    'Also works with a new sensor. After replacing the sensor use "Pair a new sensor" in the sensor section below. A single fingerprint exported with "Export" is restored the same way and goes to the next free slot.': 'Funktioniert auch mit einem neuen Sensor. Nach einem Sensortausch unten im Bereich Sensor "Neuen Sensor koppeln" verwenden. Ein einzelner, mit "Exportieren" exportierter Fingerabdruck wird genauso wiederhergestellt und kommt in den nächsten freien Platz.',
    'Choose file': 'Datei wählen',
    'Overwrite same slots': 'Gleiche Plätze überschreiben',
    'Add to free slots': 'In freie Plätze einfügen',
    'Delete all first': 'Vorher alles löschen',
    '"Ignore touch ring" scans without the ring signal and stays set after a restart. More details under Settings, LED ring.': '"Touch-Ring ignorieren" scannt ohne das Ringsignal und bleibt auch nach einem Neustart eingeschaltet. Mehr dazu unter Einstellungen, LED-Ring.',
    'General': 'Allgemein',
    'GPIO pins': 'GPIO-Pins',
    'Touch the ring: it should change to "touched". If it never does, the wake-up wire or the touch power (3.3V touch) of the sensor is not connected. "Ignore touch ring" scans without that signal and stays set after a restart. The view shows how the ring should look, it does not read the real light of the sensor.': 'Berühre den Ring: Die Anzeige sollte auf "berührt" wechseln. Wenn nicht, ist die Wake-up-Leitung oder die Touch-Versorgung (3,3V Touch) des Sensors nicht angeschlossen. "Touch-Ring ignorieren" scannt ohne dieses Signal und bleibt auch nach einem Neustart eingeschaltet. Die Anzeige zeigt, wie der Ring aussehen sollte, sie liest nicht das echte Licht des Sensors aus.',
    'Only the idle light is switched off, while scanning the ring still shows its colors. Needs a working time (NTP).': 'Nur das Ruhelicht wird ausgeschaltet, beim Scannen zeigt der Ring weiter seine Farben. Braucht eine funktionierende Uhrzeit (NTP).',
    'For a presence radar like the HLK-LD2410: VCC to 5V, GND to GND, OUT to the GPIO below. Presence is published to MQTT ("presence") and Home Assistant.': 'Für ein Anwesenheitsradar wie den HLK-LD2410: VCC an 5V, GND an GND, OUT an den GPIO unten. Die Anwesenheit wird per MQTT ("presence") und an Home Assistant gemeldet.',
    'New pins are used after a restart.': 'Neue Pins werden nach einem Neustart verwendet.',
    'Only pins that work for the function are offered (classic ESP32, e.g. DevKit V1 with ESP32-WROOM-32). Every pin can only be used once. If the sensor is not found afterwards, set the original pins again (RX 16, TX 17, touch 5, doorbell 19, door opener 23).': 'Es werden nur Pins angeboten, die für die Funktion wirklich funktionieren (klassischer ESP32, z. B. DevKit V1 mit ESP32-WROOM-32). Jeder Pin kann nur einmal verwendet werden. Wird der Sensor danach nicht gefunden, stelle die ursprünglichen Pins wieder ein (RX 16, TX 17, Touch 5, Klingel 19, Türöffner 23).',
    'Export': 'Exportieren',
    'No fingerprint selected.': 'Kein Fingerabdruck ausgewählt.',
    'Add to free slots (keep existing fingerprints)': 'In freie Plätze einfügen (vorhandene Fingerabdrücke behalten)',
    'The fingerprints are added to the next free slots, nothing is overwritten.': 'Die Fingerabdrücke werden in die nächsten freien Plätze eingefügt, nichts wird überschrieben.',
    'Restore failed: no free slot left on the sensor.': 'Wiederherstellung fehlgeschlagen: Auf dem Sensor ist kein freier Platz mehr.',
    'Restore failed: could not read the template index from the sensor.': 'Wiederherstellung fehlgeschlagen: Die Template-Übersicht konnte nicht vom Sensor gelesen werden.',
    'Invalid slot number.': 'Ungültige Platznummer.',
    'Also works with a new sensor. After replacing the sensor use "Pair a new sensor" in the sensor section below. A single fingerprint exported with "Export" in the list above is restored the same way and goes to the next free slot.':
      'Funktioniert auch mit einem neuen Sensor. Nach einem Sensortausch unten im Bereich Sensor "Neuen Sensor koppeln" verwenden. Ein einzelner, über "Exportieren" in der Liste oben exportierter Fingerabdruck wird genauso wiederhergestellt und kommt in den nächsten freien Platz.',
    'Sensor': 'Sensor',
    'State': 'Status',
    'Connection': 'Verbindung',
    'Reconnect sensor': 'Sensor neu verbinden',
    'Touch ring': 'Touch-Ring',
    'LED ring on': 'LED-Ring an',
    'Ignore touch ring': 'Touch-Ring ignorieren',
    'Touch ring pin (GPIO 5):': 'Touch-Ring-Pin (GPIO 5):',
    '. "Ignore touch ring" scans without the ring signal and stays set after a restart. More details under Settings, LED ring.':
      '. "Touch-Ring ignorieren" scannt ohne das Signal des Rings und bleibt auch nach einem Neustart eingeschaltet. Mehr Details unter Einstellungen, LED-Ring.',
    'Pairing': 'Kopplung',
    'Pair a new sensor': 'Neuen Sensor koppeln',
    'Needed after replacing the sensor. The sensor only accepts this device afterwards.': 'Nötig nach einem Sensortausch. Danach akzeptiert der Sensor nur noch dieses Gerät.',
    'Delete all fingerprints': 'Alle Fingerabdrücke löschen',
    'reconnecting, this takes up to 10 seconds...': 'verbinde neu, das dauert bis zu 10 Sekunden...',
    'request failed': 'Anfrage fehlgeschlagen',
    'touched': 'berührt',
    'idle (not touched)': 'Ruhe (nicht berührt)',
    'New Name?': 'Neuer Name?',
    'Delete the selected fingerprint?': 'Den ausgewählten Fingerabdruck löschen?',
    'This will delete all fingerprints. Are you sure you wanna do that?': 'Damit werden alle Fingerabdrücke gelöscht. Bist du sicher?',
    'Restore the fingerprints from': 'Fingerabdrücke wiederherstellen aus',
    'All fingerprints currently stored on the sensor will be deleted first!': 'Alle Fingerabdrücke, die gerade auf dem Sensor gespeichert sind, werden vorher gelöscht!',
    'Fingerprints with the same slot number will be overwritten.': 'Fingerabdrücke mit derselben Platznummer werden überschrieben.',
    'Starting backup...': 'Starte Sicherung...',
    'Please choose a backup file first.': 'Bitte zuerst eine Sicherungsdatei auswählen.',
    'Uploading...': 'Lade hoch...',
    'Upload failed.': 'Hochladen fehlgeschlagen.',
    'Creating backup...': 'Erstelle Sicherung...',
    'Restoring backup...': 'Stelle Sicherung wieder her...',
    'Waiting for the sensor...': 'Warte auf den Sensor...',
    'Device is busy, try again later.': 'Das Gerät ist beschäftigt, bitte später erneut versuchen.',
    'Upload failed or file is empty.': 'Hochladen fehlgeschlagen oder Datei ist leer.',
    'Backup failed: could not read the template index from the sensor.': 'Sicherung fehlgeschlagen: Die Template-Liste konnte nicht vom Sensor gelesen werden.',
    'Backup failed: could not create file.': 'Sicherung fehlgeschlagen: Datei konnte nicht angelegt werden.',
    'Backup failed: out of memory.': 'Sicherung fehlgeschlagen: zu wenig Speicher.',
    'Backup failed: file system full?': 'Sicherung fehlgeschlagen: Dateisystem voll?',
    'Restore failed: no uploaded backup file found.': 'Wiederherstellung fehlgeschlagen: keine hochgeladene Sicherungsdatei gefunden.',
    'Restore failed: this is not a valid FingerprintDoorbell backup file.': 'Wiederherstellung fehlgeschlagen: Das ist keine gültige FingerprintDoorbell-Sicherung.',
    'Restore failed: out of memory.': 'Wiederherstellung fehlgeschlagen: zu wenig Speicher.',

    // Settings page
    'Web Password': 'Web-Passwort',
    'Password for website': 'Passwort für die Weboberfläche',
    'User name is "admin", leave empty if no authentication required': 'Benutzername ist "admin", leer lassen, wenn kein Passwort nötig ist',
    'NTP Server': 'NTP-Server',
    'URL to NTP server': 'Adresse des NTP-Servers',
    "Used for timestamps in log panel. If you don't specify any, time will be always null.": 'Für Zeitstempel im Protokoll und für Zeitfunktionen. Ohne NTP-Server ist die Uhrzeit unbekannt.',
    'Timezone': 'Zeitzone',
    'Custom (POSIX string)': 'Eigene (POSIX-String)',
    'e.g. CET-1CEST,M3.5.0,M10.5.0/3': 'z.B. CET-1CEST,M3.5.0,M10.5.0/3',
    'Current device time:': 'Aktuelle Gerätezeit:',
    'For "Custom" enter a POSIX TZ string, see': 'Für "Eigene" einen POSIX-TZ-String eintragen, siehe',
    '(second column).': '(zweite Spalte).',
    'LED ring & night mode': 'LED-Ring & Nachtmodus',
    'Ring live view': 'Ring-Liveansicht',
    '. Touch the ring: it should change to "touched". If it never does, the wake-up wire or the touch power (3.3V touch) of the sensor is not connected. "Ignore touch ring" scans without that signal and stays set after a restart. The view shows how the ring should look, it does not read the real light of the sensor.':
      '. Berühre den Ring: Es sollte auf "berührt" wechseln. Passiert das nie, ist die Wake-up-Leitung oder die Touch-Versorgung (3,3V Touch) des Sensors nicht angeschlossen. "Touch-Ring ignorieren" scannt ohne dieses Signal und bleibt auch nach einem Neustart eingeschaltet. Die Ansicht zeigt, wie der Ring leuchten sollte, sie liest nicht das echte Licht des Sensors aus.',
    'Touch Ring Active: LED Color': 'Touch-Ring bereit: LED-Farbe',
    'Touch Ring Active: LED Sequence': 'Touch-Ring bereit: LED-Muster',
    'Scan fingerprint: LED Color': 'Fingerabdruck scannen: LED-Farbe',
    'Matching fingerprint: LED Color': 'Fingerabdruck erkannt: LED-Farbe',
    'Red': 'Rot', 'Blue': 'Blau', 'Purple': 'Lila', 'Green': 'Grün', 'Yellow': 'Gelb', 'Cyan': 'Cyan', 'White': 'Weiß',
    'Off': 'Aus', 'On': 'An', 'Breath': 'Atmen', 'Blink': 'Blinken',
    'breathing': 'atmend', 'blinking': 'blinkend', 'steady': 'dauerhaft',
    'Not all versions of the R503 sensor supports all colors. Older ones only red, blue and purple!': 'Nicht jede Version des R503 kann alle Farben. Ältere nur Rot, Blau und Lila!',
    'Night mode': 'Nachtmodus',
    'Switch the LED ring off at night': 'LED-Ring nachts ausschalten',
    'Night from / to': 'Nacht von / bis',
    'sensor not connected': 'Sensor nicht verbunden',
    'ring touched, scanning': 'Ring berührt, scanne',
    'lockout after too many failed attempts': 'Sperre nach zu vielen Fehlversuchen',
    'LED ring switched off': 'LED-Ring ausgeschaltet',
    'idle light off (night mode or nobody near the door)': 'Ruhelicht aus (Nachtmodus oder niemand an der Tür)',
    'idle, waiting for a touch': 'bereit, wartet auf Berührung',
    'touch ring is ignored, scanning without ring signal': 'Touch-Ring wird ignoriert, scanne ohne Ring-Signal',
    'Security': 'Sicherheit',
    'Failed attempts': 'Fehlversuche',
    'After this number of unknown fingers within the time window all matches are ignored for the lockout time (protection against trying out fingers). 0 = disabled. Ringing still works during a lockout.':
      'Nach so vielen unbekannten Fingern innerhalb des Zeitfensters werden alle Treffer für die Sperrzeit ignoriert (Schutz gegen Durchprobieren). 0 = aus. Klingeln funktioniert auch während der Sperre.',
    'Lockout time (s)': 'Sperrzeit (s)',
    'Ring cooldown (s)': 'Klingel-Pause (s)',
    'Minimum time between two ring events (protection against ringing again and again). 0 = disabled.': 'Mindestzeit zwischen zwei Klingel-Ereignissen (Schutz gegen Dauerklingeln). 0 = aus.',
    'No lockout': 'Keine Sperre',
    'Radar sensor': 'Radarsensor',
    'Radar sensor connected': 'Radarsensor angeschlossen',
    'GPIO': 'GPIO',
    'Presence signal': 'Anwesenheitssignal',
    'High = presence (LD2410)': 'High = anwesend (LD2410)',
    'Low = presence': 'Low = anwesend',
    'LED ring': 'LED-Ring',
    'Switch the LED ring on only when somebody is near the door': 'LED-Ring nur einschalten, wenn jemand an der Tür ist',
    'GPIO pins (advanced)': 'GPIO-Pins (erweitert)',
    'Only pins that work for the function are offered (classic ESP32, e.g. DevKit V1 with ESP32-WROOM-32). Every pin can only be used once. The new pins are used after a restart. If the sensor is not found afterwards, set the original pins again (RX 16, TX 17, touch 5, doorbell 19, door opener 23).':
      'Es werden nur Pins angeboten, die für die Funktion geeignet sind (klassischer ESP32, z.B. DevKit V1 mit ESP32-WROOM-32). Jeder Pin kann nur einmal verwendet werden. Die neuen Pins gelten nach einem Neustart. Wird der Sensor danach nicht gefunden, die ursprünglichen Pins wieder einstellen (RX 16, TX 17, Touch 5, Klingel 19, Türöffner 23).',
    'Sensor RX (ESP32 receives)': 'Sensor RX (ESP32 empfängt)',
    'Sensor TX (ESP32 sends)': 'Sensor TX (ESP32 sendet)',
    'connected to TX of the sensor, default 16': 'verbunden mit TX des Sensors, Standard 16',
    'connected to RX of the sensor, default 17': 'verbunden mit RX des Sensors, Standard 17',
    'Touch ring (wake-up)': 'Touch-Ring (Wake-up)',
    'wake-up signal of the sensor, default 5': 'Wake-up-Signal des Sensors, Standard 5',
    'Doorbell output': 'Ausgang Klingel',
    'Door opener output': 'Ausgang Türöffner',
    'default 19': 'Standard 19',
    'default 23': 'Standard 23',
    'The new pins are saved and are used after a restart.': 'Die neuen Pins sind gespeichert und gelten nach einem Neustart.',
    'Restart now': 'Jetzt neu starten',
    'Maintenance': 'Wartung',
    'Firmware update': 'Firmware-Update',
    'Open update page': 'Update-Seite öffnen',
    'Upload firmware.bin (Firmware) and spiffs.bin (Filesystem) over the air.': 'firmware.bin (Firmware) und spiffs.bin (Filesystem) drahtlos hochladen.',
    'Factory reset': 'Werksreset',
    'Deletes all fingerprints, settings and the WiFi configuration. The device restarts in WiFi setup mode afterwards.': 'Löscht alle Fingerabdrücke, Einstellungen und die WLAN-Konfiguration. Das Gerät startet danach im WLAN-Einrichtungsmodus.',
    'This will delete all fingerprints, your settings and your WiFi configuration. Are you sure you wanna do that?': 'Damit werden alle Fingerabdrücke, deine Einstellungen und die WLAN-Konfiguration gelöscht. Bist du sicher?',

    // MQTT page
    'Server IP': 'Server-IP',
    'Connections': 'Verbindungen',
    'MQTT Server': 'MQTT-Server',
    'Hostname or IP of your MQTT broker': 'Hostname oder IP deines MQTT-Brokers',
    'MQTT Port': 'MQTT-Port',
    'MQTT Username': 'MQTT-Benutzer',
    'Username for connecting to your MQTT Broker': 'Benutzername für den MQTT-Broker',
    'Leave empty if your broker is not requiring authentication.': 'Leer lassen, wenn dein Broker keine Anmeldung verlangt.',
    'MQTT Password': 'MQTT-Passwort',
    'Password for connecting to your MQTT Broker': 'Passwort für den MQTT-Broker',
    'MQTT Root Topic': 'MQTT-Root-Topic',
    'e.g. fingerprintDoorbell': 'z.B. fingerprintDoorbell',
    'All topics below are published/subscribed under this root topic. In ioBroker it becomes the folder name (e.g. "mqtt.0.<root topic>").':
      'Alle Topics unten liegen unter diesem Root-Topic. In ioBroker wird daraus der Ordnername (z.B. "mqtt.0.<root topic>").',
    'Home Assistant': 'Home Assistant',
    'Enable MQTT discovery': 'MQTT-Discovery aktivieren',
    'Publishes discovery configs under "homeassistant/...", so the doorbell shows up as a device in Home Assistant automatically.':
      'Sendet Discovery-Konfigurationen unter "homeassistant/...", damit die Klingel automatisch als Gerät in Home Assistant erscheint.',
    'Enrollment': 'Anlernen',
    'Allow to start an enrollment over MQTT': 'Anlernen per MQTT erlauben',
    'Send e.g. "5:Alice" to "<root topic>/enroll" (in Home Assistant: text field "Enroll finger"). Progress is published to "<root topic>/enrollState". Only enable this if your broker is protected, everybody who can publish to it can add a finger.':
      'Sende z.B. "5:Alice" an "<root topic>/enroll" (in Home Assistant: Textfeld "Enroll finger"). Der Fortschritt wird an "<root topic>/enrollState" gesendet. Nur aktivieren, wenn dein Broker geschützt ist: Jeder, der dort senden darf, kann einen Finger hinzufügen.',
    'Advanced options': 'Erweiterte Optionen',
    'Client ID': 'Client-ID',
    'Name of the connection at the broker. Leave empty to use the hostname. Must be unique, two devices with the same id disconnect each other.':
      'Name der Verbindung beim Broker. Leer lassen, um den Hostnamen zu verwenden. Muss eindeutig sein, zwei Geräte mit derselben ID werfen sich gegenseitig raus.',
    'Keepalive (s)': 'Keepalive (s)',
    'Interval of the keepalive ping (5-300s, default 15). A higher value can help with unstable WiFi connections.': 'Intervall des Keepalive-Pings (5-300s, Standard 15). Ein höherer Wert kann bei instabilem WLAN helfen.',
    'Payload on / off': 'Payload an / aus',
    'Used for ring and switch states, e.g. "true"/"false" for ioBroker booleans. Commands always accept on/off, true/false and 1/0 too.':
      'Für Klingel- und Schalterzustände, z.B. "true"/"false" für Boolesche Werte in ioBroker. Befehle akzeptieren immer auch on/off, true/false und 1/0.',
    'Options': 'Optionen',
    'Publish ring and match topics retained': 'Klingel- und Treffer-Topics mit Retain senden',
    'Publish log messages': 'Protokollmeldungen senden',
    'Topic names': 'Topic-Namen',
    'Online status': 'Online-Status',
    'publish (retained): "Online" / "Offline" (last will)': 'senden (retained): "Online" / "Offline" (Last Will)',
    'Ring': 'Klingeln',
    'publish: on-payload for 1s when the bell rings, otherwise off-payload': 'senden: An-Payload für 1s beim Klingeln, sonst Aus-Payload',
    'Match ID': 'Treffer-ID',
    'publish: id of the matching finger for 3s, otherwise "-1"': 'senden: ID des erkannten Fingers für 3s, sonst "-1"',
    'Match name': 'Treffer-Name',
    'publish: name of the matching finger for 3s, otherwise empty': 'senden: Name des erkannten Fingers für 3s, sonst leer',
    'Match confidence': 'Treffer-Sicherheit',
    'publish: 1 (low) - 400 (very high) for 3s, otherwise "-1"': 'senden: 1 (niedrig) - 400 (sehr hoch) für 3s, sonst "-1"',
    'Log messages': 'Protokollmeldungen',
    'publish: every message of the log panel (if enabled under "Options")': 'senden: jede Protokollmeldung (wenn unter "Optionen" aktiviert)',
    'subscribe: on/off, e.g. when it rains. State is published to ".../state" (retained)': 'empfangen: an/aus, z.B. bei Regen. Der Zustand wird an ".../state" gesendet (retained)',
    'subscribe: on/off, switch the idle LED ring off (e.g. at night). State is published to ".../state" (retained)': 'empfangen: an/aus, schaltet das Ruhelicht des LED-Rings aus (z.B. nachts). Der Zustand wird an ".../state" gesendet (retained)',
    'Open door': 'Tür öffnen',
    'subscribe: on-payload pulses the door opener output (GPIO 23) for 1s': 'empfangen: An-Payload schaltet den Türöffner-Ausgang (GPIO 23) für 1s',
    'connected': 'verbunden',
    'disconnected': 'getrennt',
    'connection timeout (no answer to keepalive ping)': 'Zeitüberschreitung (keine Antwort auf Keepalive-Ping)',
    'connection lost (TCP connection closed)': 'Verbindung verloren (TCP-Verbindung geschlossen)',
    'connect failed (broker not reachable)': 'Verbindung fehlgeschlagen (Broker nicht erreichbar)',
    'bad protocol': 'falsches Protokoll',
    'bad client id': 'ungültige Client-ID',
    'server unavailable': 'Server nicht verfügbar',
    'bad credentials': 'falsche Zugangsdaten',
    'unauthorized': 'nicht berechtigt',

    // Log page
    'Fingerprint sensor': 'Fingerabdrucksensor',
    'Events': 'Ereignisse',
    'last 30, kept after a restart': 'letzte 30, bleiben nach Neustart erhalten',
    'Event': 'Ereignis',
    'Details': 'Details',
    'No events yet': 'Noch keine Ereignisse',
    'Clear events': 'Ereignisse löschen',
    'Delete the event history?': 'Den Ereignisverlauf löschen?',
    'System log': 'Systemprotokoll',
    'live, since the last restart': 'live, seit dem letzten Neustart',
    'Serial monitor': 'Serieller Monitor',
    'Everything the device writes to its serial port (last 8 KB since the last restart), with the time since boot in front of each line. Updates while this section is open. Output of the boot loader before the firmware starts is not included.':
      'Alles, was das Gerät auf seine serielle Schnittstelle schreibt (letzte 8 KB seit dem Neustart), mit der Zeit seit dem Start vor jeder Zeile. Aktualisiert sich, solange dieser Bereich offen ist. Ausgaben des Bootloaders vor dem Start der Firmware sind nicht enthalten.',
    'Scroll to the end': 'Ans Ende scrollen',
    'no answer from device': 'keine Antwort vom Gerät',
    'reconnecting...': 'verbinde neu...',
    'Network': 'Netzwerk',
    'IP address': 'IP-Adresse',
    'MAC address': 'MAC-Adresse',
    'Gateway': 'Gateway',
    'DNS server': 'DNS-Server',
    'WiFi SSID': 'WLAN-SSID',
    'WiFi BSSID': 'WLAN-BSSID',
    'WiFi channel': 'WLAN-Kanal',
    'WiFi connection losses': 'WLAN-Verbindungsabbrüche',
    'Last WiFi disconnect reason': 'Letzter WLAN-Trennungsgrund',
    'MQTT server': 'MQTT-Server',
    'MQTT server IP': 'MQTT-Server-IP',
    'MQTT state': 'MQTT-Status',
    'MQTT connections': 'MQTT-Verbindungen',
    'Device': 'Gerät',
    'Chip temp.': 'Chip-Temp.',
    'Flash size': 'Flash-Größe',
    'Free heap': 'Freier Speicher',
    'Reset reason': 'Grund des Neustarts',
    'Unknown': 'Unbekannt', 'Power on': 'Einschalten', 'External': 'Extern', 'Software': 'Software', 'Panic': 'Absturz',
    'Interrupt Watchdog': 'Interrupt-Watchdog', 'Task Watchdog': 'Task-Watchdog', 'Watchdog': 'Watchdog', 'Deepsleep': 'Tiefschlaf', 'Brownout': 'Unterspannung',
    // event types and details
    'Boot': 'Start', 'Match': 'Treffer', 'Failed attempt': 'Fehlversuch', 'Access denied': 'Zutritt verweigert', 'Lockout': 'Sperre',
    'Lockout ended': 'Sperre beendet', 'Door opened': 'Tür geöffnet', 'Enrolled': 'Angelernt', 'Deleted': 'Gelöscht',
    'Ring suppressed': 'Klingeln unterdrückt', 'Sensor lost': 'Sensor verloren', 'Sensor restored': 'Sensor wieder da',
    'unknown finger': 'unbekannter Finger',
    'manual reconnect failed': 'manuelles Neuverbinden fehlgeschlagen',
    'auto retry stopped': 'automatische Versuche beendet',
    'invalid sensor pairing': 'ungültige Sensor-Kopplung',
    'failed': 'fehlgeschlagen',

    // WiFi configuration page
    'WiFi Configuration': 'WLAN-Konfiguration',
    'You are currently in WiFi configuration mode!': 'Das Gerät ist gerade im WLAN-Einrichtungsmodus!',
    'In this mode the device acts as a WiFi access point to which you can temporarily connect to with your smartphone/pc for configuring the access to your regular WiFi network.':
      'In diesem Modus arbeitet das Gerät als WLAN-Access-Point. Verbinde dich vorübergehend mit Smartphone oder PC, um den Zugang zu deinem normalen WLAN einzurichten.',
    'After saving the new WiFi configuration the device will restart and try to connect to your network. If this fails the device will not go back to WiFi config mode automatically. You have to put the device manually in WiFi config mode by press and hold the sensor surface for 10 s while powering up the device.':
      'Nach dem Speichern startet das Gerät neu und verbindet sich mit deinem WLAN. Klappt das nicht, geht es nicht von selbst zurück in den Einrichtungsmodus. Dazu beim Einschalten den Finger 10 s auf den Sensor halten.',
    'SSID': 'SSID',
    'SSID of your WiFi network': 'SSID deines WLANs',
    'WiFi Password': 'WLAN-Passwort',
    'Password of your WiFi network': 'Passwort deines WLANs',
    'For security reason the current password is not displayed here, but you can set a new one.': 'Aus Sicherheitsgründen wird das aktuelle Passwort nicht angezeigt, du kannst aber ein neues setzen.',
    'Hostname': 'Hostname',
    'Hostname of your FingerprintDoorbell': 'Hostname deiner FingerprintDoorbell',
    'The name under which this device will be available in your network. Also used in the title of the web frontend.': 'Der Name, unter dem das Gerät in deinem Netzwerk erreichbar ist. Er steht auch im Titel der Weboberfläche.',
    'Hint: just leave it "FingerprintDoorbell" unless you have multiple devices and want to differentiate between them.': 'Tipp: Lass einfach "FingerprintDoorbell" stehen, außer du hast mehrere Geräte und willst sie unterscheiden.',
    'Save and restart': 'Speichern und neu starten'
  };

  var days = { Mo: 'Mo', Tu: 'Di', We: 'Mi', Th: 'Do', Fr: 'Fr', Sa: 'Sa', Su: 'So' };
  function since(text) { return text.replace(/(\d+) min/, '$1 Min.').replace(/(\d+) h/, '$1 Std.'); }

  // texts with variable parts
  var patterns = [
    [/^Manage fingerprints \((\d+)\)$/, 'Fingerabdrücke verwalten ($1)'],
    [/^FingerprintDoorbell, Version (.+)$/, 'FingerprintDoorbell, Version $1'],
    [/^Uptime (\d+) days (.+)$/, 'Laufzeit $1 Tage $2'],
    [/^connected \(since (.+), (\d+) losses\)$/, function (m, a, b) { return 'verbunden (seit ' + since(a) + ', ' + b + ' Abbrüche)'; }],
    [/^NOT CONNECTED since (.+) \(automatic retries stopped, use Reconnect sensor on the Log page\)$/, function (m, a) { return 'NICHT VERBUNDEN seit ' + since(a) + ' (automatische Versuche beendet, "Sensor neu verbinden" im Protokoll verwenden)'; }],
    [/^NOT CONNECTED \(since (.+), (\d+) losses, retrying automatically\)$/, function (m, a, b) { return 'NICHT VERBUNDEN (seit ' + since(a) + ', ' + b + ' Abbrüche, versuche automatisch)'; }],
    [/^Fingerprint sensor: (.+)$/, function (m, a) { return 'Fingerabdrucksensor: ' + t(a); }],
    [/^Currently: (.+)$/, function (m, a) { return 'Aktuell: ' + t(a); }],
    [/^(\d+) recent failed attempts$/, '$1 Fehlversuche zuletzt'],
    [/^Lockout active, (\d+)s left$/, 'Sperre aktiv, noch $1s'],
    [/^(\d+) connects, (\d+) lost$/, '$1 Verbindungen, $2 verloren'],
    [/^(.+) \((\d+) disconnect events\)$/, '$1 ($2 Trennungen)'],
    [/^GPIO (\d+) \(input only\)$/, 'GPIO $1 (nur Eingang)'],
    [/^GPIO (\d+) \(not on WROVER modules\)$/, 'GPIO $1 (nicht bei WROVER-Modulen)'],
    [/^GPIO settings not saved: (.+)$/, 'GPIO-Einstellungen nicht gespeichert: $1'],
    [/^uptime (\d+)s$/, 'Laufzeit $1s'],
    [/^(.*)confidence (\d+)$/, function (m, a, b) { return (a ? t(a.replace(/, $/, '')) + ', ' : '') + 'Sicherheit ' + b; }],
    [/^firmware (.+)$/, 'Firmware $1'],
    [/^(\d+) failed attempts$/, '$1 Fehlversuche'],
    [/^(\d+) fingerprints$/, '$1 Fingerabdrücke'],
    [/^(.+): (finger is disabled|time not synchronized, restricted fingers are not allowed|not allowed on this weekday|outside of allowed time window)$/, function (m, a, b) { return a + ': ' + t(b); }],
    [/^finger is disabled$/, 'Finger ist gesperrt'],
    [/^time not synchronized, restricted fingers are not allowed$/, 'Uhrzeit nicht synchronisiert, eingeschränkte Finger sind nicht erlaubt'],
    [/^not allowed on this weekday$/, 'an diesem Wochentag nicht erlaubt'],
    [/^outside of allowed time window$/, 'außerhalb des erlaubten Zeitfensters'],
    [/^disabled$/, 'aus'],
    [/^active now$/, 'gerade aktiv'],
    [/^not active now$/, 'gerade nicht aktiv'],
    [/^somebody detected$/, 'jemand erkannt'],
    [/^nobody detected$/, 'niemand erkannt'],
    [/^(Red|Blue|Purple|Green|Yellow|Cyan|White), (breathing|blinking|steady)$/, function (m, a, b) { return de[a] + ', ' + de[b]; }],
    [/^Fingerprints \((\d+)\)$/, 'Fingerabdrücke ($1)'],
    [/^Fingerprint exported from slot (\d+)\.$/, 'Fingerabdruck aus Platz $1 exportiert.'],
    [/^Export failed: there is no fingerprint in slot (\d+)\.$/, 'Export fehlgeschlagen: In Platz $1 ist kein Fingerabdruck gespeichert.'],
    [/^Backup created with (\d+) fingerprints\.$/, 'Sicherung mit $1 Fingerabdrücken erstellt.'],
    [/^Backup failed: could not read template #(\d+) from the sensor\.$/, 'Sicherung fehlgeschlagen: Template #$1 konnte nicht vom Sensor gelesen werden.'],
    [/^Restore completed: (\d+) fingerprints restored\.$/, 'Wiederherstellung abgeschlossen: $1 Fingerabdrücke wiederhergestellt.'],
    [/^Restore failed: (.+?)\.? (\d+) of (\d+) fingerprints were restored\.$/, function (m, a, b, c) { return t('Restore failed: ' + a + '.') + ' ' + b + ' von ' + c + ' Fingerabdrücken wurden wiederhergestellt.'; }],
    [/^Restore failed: backup file is damaged \(entry (\d+)\)\.$/, 'Wiederherstellung fehlgeschlagen: Sicherungsdatei ist beschädigt (Eintrag $1).'],
    [/^Restore failed: could not write template #(\d+) to the sensor\.$/, 'Wiederherstellung fehlgeschlagen: Template #$1 konnte nicht auf den Sensor geschrieben werden.'],
    [/^(Creating backup|Restoring backup)\.\.\. \((\d+)\/(\d+)\)$/, function (m, a, b, c) { return t(a + '...') + ' (' + b + '/' + c + ')'; }],
    // finger list entry "5 - Name [We 08:00-12:00]": translate the weekdays in the brackets
    [/^(\d+ - .*) \[([^\]]+)\]$/, function (m, a, b) {
      return a + ' [' + (b === 'disabled' ? 'gesperrt' : b.replace(/\b(Mo|Tu|We|Th|Fr|Sa|Su)\b/g, function (d) { return days[d]; })) + ']'; }]
  ];

  function translate(text) {
    if (lang !== 'de' || !text) return text;
    var n = text.replace(/\s+/g, ' ').trim();
    if (!n) return text;
    var result = null;
    if (Object.prototype.hasOwnProperty.call(de, n))
      result = de[n];
    else
      for (var i = 0; i < patterns.length; i++)
        if (patterns[i][0].test(n)) { result = n.replace(patterns[i][0], patterns[i][1]); break; }
    if (result === null) return text;
    // keep the surrounding white space of text nodes
    var lead = text.match(/^\s*/)[0], trail = text.match(/\s*$/)[0];
    return lead + result + trail;
  }
  window.t = translate;
  window.uiLanguage = lang;

  // language switch in the navbar
  window.setLanguage = function (newLang) {
    if (newLang !== lang)
      fetch('/language', { method: 'POST', body: new URLSearchParams({ lang: newLang }) }).then(function () { location.reload(); });
    return false;
  };

  var style = document.createElement('style');
  // in the dark navbar light colors, elsewhere (WiFi configuration page) the normal link colors
  style.textContent = '.lang-switch{display:inline-block;font-size:14px;color:#999}.lang-switch a{text-decoration:none;padding:0 2px}' +
    'html[lang=de] .lang-de,html[lang=en] .lang-en{font-weight:bold;color:#333}' +
    '.navbar .lang-switch{float:right;margin:15px 0 0 12px;color:#777}.navbar .lang-switch a{color:#9d9d9d}.navbar .lang-switch a:hover,' +
    'html[lang=de] .navbar .lang-de,html[lang=en] .navbar .lang-en{color:#fff}' +
    (lang === 'de' ? 'body{visibility:hidden}' : '');
  document.head.appendChild(style);

  // light / dark switch next to the language switch, the choice is only remembered by this browser (no setting on the device)
  var SUN = '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="5"/><line x1="12" y1="1" x2="12" y2="3"/><line x1="12" y1="21" x2="12" y2="23"/><line x1="4.22" y1="4.22" x2="5.64" y2="5.64"/><line x1="18.36" y1="18.36" x2="19.78" y2="19.78"/><line x1="1" y1="12" x2="3" y2="12"/><line x1="21" y1="12" x2="23" y2="12"/><line x1="4.22" y1="19.78" x2="5.64" y2="18.36"/><line x1="18.36" y1="5.64" x2="19.78" y2="4.22"/></svg>';
  var MOON = '<svg viewBox="0 0 24 24"><path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/></svg>';
  try {
    var savedTheme = localStorage.getItem('theme');
    if (savedTheme === 'light' || savedTheme === 'dark') document.documentElement.setAttribute('data-theme', savedTheme);
  } catch (e) { }
  function currentTheme() {
    var set = document.documentElement.getAttribute('data-theme');
    if (set) return set;
    return window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
  }
  document.addEventListener('DOMContentLoaded', function () {
    var host = document.querySelector('.lang-switch');
    if (!host) return;
    var button = document.createElement('button');
    button.type = 'button';
    button.className = 'theme-switch';
    function render() {
      var dark = currentTheme() === 'dark';
      button.innerHTML = dark ? SUN : MOON; // the icon shows what a click switches to
      button.title = translate('Switch between light and dark');
    }
    button.addEventListener('click', function () {
      var next = currentTheme() === 'dark' ? 'light' : 'dark';
      document.documentElement.setAttribute('data-theme', next);
      try { localStorage.setItem('theme', next); } catch (e) { }
      render();
    });
    render();
    host.appendChild(button);
  });

  if (lang !== 'de') return;

  var attributes = ['placeholder', 'title', 'alt', 'data-tip'];
  function skip(node) {
    for (var e = node.nodeType === 1 ? node : node.parentNode; e; e = e.parentNode)
      if (e.nodeType === 1 && (e.tagName === 'SCRIPT' || e.tagName === 'STYLE' || (e.classList && e.classList.contains('notranslate'))))
        return true;
    return false;
  }
  function translateElement(el) {
    for (var i = 0; i < attributes.length; i++) {
      var v = el.getAttribute && el.getAttribute(attributes[i]);
      if (v) { var tv = translate(v); if (tv !== v) el.setAttribute(attributes[i], tv); }
    }
    if (el.tagName === 'INPUT' && (el.type === 'submit' || el.type === 'button') && el.value) {
      var tb = translate(el.value); if (tb !== el.value) el.value = tb;
    }
  }
  function translateTree(root) {
    if (skip(root)) return;
    if (root.nodeType === 3) {
      var tt = translate(root.nodeValue); if (tt !== root.nodeValue) root.nodeValue = tt;
      return;
    }
    if (root.nodeType !== 1) return;
    translateElement(root);
    var walker = document.createTreeWalker(root, NodeFilter.SHOW_ELEMENT | NodeFilter.SHOW_TEXT, null, false);
    var node;
    while ((node = walker.nextNode())) {
      if (skip(node)) continue;
      if (node.nodeType === 3) { var x = translate(node.nodeValue); if (x !== node.nodeValue) node.nodeValue = x; }
      else translateElement(node);
    }
  }

  document.addEventListener('DOMContentLoaded', function () {
    document.title = translate(document.title);
    translateTree(document.body);
    document.body.style.visibility = 'visible';
    // texts changed later by the page scripts (status polling, backup progress, ...)
    new MutationObserver(function (mutations) {
      mutations.forEach(function (m) {
        if (m.type === 'characterData') translateTree(m.target);
        else if (m.type === 'attributes') translateElement(m.target);
        else for (var i = 0; i < m.addedNodes.length; i++) translateTree(m.addedNodes[i]);
      });
    }).observe(document.body, { childList: true, characterData: true, subtree: true, attributes: true, attributeFilter: attributes });
  });
})();
