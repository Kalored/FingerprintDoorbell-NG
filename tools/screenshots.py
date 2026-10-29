#!/usr/bin/env python3
"""Creates the screenshots of the web interface for the README (doc/images/web-*.png and thumb-*.png).

The pages in data/ are rendered with sample values instead of the %PLACEHOLDERS% of the firmware and
photographed with headless Chromium, so no device is needed.

Usage (from the repository root):  python3 tools/screenshots.py [--lang de] [--theme dark] [--out DIR]
"""
import argparse, functools, gzip, http.server, os, re, shutil, subprocess, sys, tempfile, threading

from PIL import Image

LIGHT = ('<span id="sensorLight" style="display:inline-flex;align-items:center;vertical-align:middle;margin-left:12px;'
         'padding:4px 6px;border-radius:9px;background:#111">' +
         ''.join('<span style="display:inline-block;width:10px;height:10px;border-radius:5px;margin:0 2px;background:%s;opacity:%s"></span>'
                 % (c, o) for c, o in (('#e53935', '.18'), ('#fdd835', '.18'), ('#43a047', '1'))) + '</span>')

def options(values, selected):
    return ''.join('<option value="%s"%s>%s</option>' % (v, ' selected' if v == selected else '', label) for v, label in values)

GPIO = [(str(p), 'GPIO %d' % p) for p in (4, 13, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33)]

SAMPLE = dict(
    HOSTNAME='FingerprintDoorbell', SENSOR_LIGHT=LIGHT, VERSIONINFO='', UPTIME='Uptime 2 days 04:12:33',
    FINGER_COUNT='3',
    FINGERLIST='<option value="1" selected data-name="Alice" data-days="127" data-from="0" data-to="1440">1 - Alice</option>'
               '<option value="2" data-name="Bob#" data-days="127" data-from="0" data-to="1440">2 - Bob#</option>'
               '<option value="5" data-name="Cleaner" data-days="4" data-from="480" data-to="720">5 - Cleaner [We 08:00-12:00]</option>',
    SENSOR_STATE='connected (since 2 h, 0 losses)', SENSOR_ALERT='success',
    IP_ADDRESS='192.168.1.57', MAC_ADDRESS='24:6F:28:AA:BB:CC', GATEWAY='192.168.1.1', DNS_SERVER='192.168.1.1',
    SSID='IoT', BSSID='9C:A2:F4:11:22:33', WIFI_CHANNEL='6', RSSI='-61 dBm', WIFI_LOST='0',
    WIFI_REASON='- (0 disconnect events)', MQTT_SERVER='mqtt.local', MQTT_SERVER_IP='192.168.1.20',
    MQTT_STATE='connected', MQTT_STATS='1 connects, 0 lost', CHIP='ESP32-D0WDQ6 rev 1', CPU='240 MHz',
    CHIP_TEMP='48 °C', FLASH_SIZE='4194304', FREE_HEAP='182344', RESET_REASON='Software',
    EVENTLIST='<tr><td>2026-10-28 14:02:11</td><td>Match</td><td>#1</td><td>Alice, confidence 187</td></tr>'
              '<tr><td>2026-10-28 13:40:52</td><td>Ring</td><td></td><td></td></tr>'
              '<tr><td>2026-10-28 09:12:40</td><td>Access denied</td><td>#5</td><td>Cleaner: outside of allowed time window, confidence 143</td></tr>'
              '<tr><td>2026-10-28 07:55:01</td><td>Boot</td><td></td><td>firmware 0.13.0</td></tr>',
    LOCKOUT_STATUS='<span class="label label-success">No lockout</span> 0 recent failed attempts',
    LOGMESSAGES_FULL='[28.10.2026 07:55:01]: Started<br>[28.10.2026 07:55:12]: WiFi connected (11 s)<br>'
                     '[28.10.2026 07:55:13]: MQTT connected<br>[28.10.2026 14:02:11]: Match: Alice (ID 1, confidence 187)<br>',
    WEB_PASSWORD='********', NTP_SERVER='pool.ntp.org', TIMEZONE='CET-1CEST,M3.5.0,M10.5.0/3',
    TIMEZONE_OPTIONS=options([('Europe/Berlin', 'Europe/Berlin'), ('Europe/Vienna', 'Europe/Vienna'), ('custom', 'Custom (POSIX string)')], 'Europe/Berlin'),
    LOCAL_TIME='2026-10-28 14:05:00', NIGHT_ENABLED='checked', NIGHT_START='22:00', NIGHT_END='06:00',
    NIGHT_STATUS='not active now', FAIL_MAX='5', FAIL_WINDOW='60', LOCKOUT_TIME='300', RING_COOLDOWN='5',
    RADAR_ENABLED='', RADAR_STATUS='disabled', RADAR_HIGH_SELECTED='selected', RADAR_LED_WAKE='',
    RADAR_PIN_OPTIONS=options([(str(p), 'GPIO %d' % p) for p in (25, 26, 27, 32, 33, 34, 35)], '32'),
    GPIO_RX_OPTIONS=options(GPIO, '16'), GPIO_TX_OPTIONS=options(GPIO, '17'), GPIO_TOUCH_OPTIONS=options([('5', 'GPIO 5')] + GPIO, '5'),
    GPIO_BELL_OPTIONS=options(GPIO, '19'), GPIO_OPENER_OPTIONS=options(GPIO, '23'),
    TOUCH_RING_ACTIVE_COLOR_2='selected', TOUCH_RING_ACTIVE_SEQUENCE_1='checked', SCAN_COLOR_1='selected', MATCH_COLOR_3='selected',
    MQTT_PORT='1883', MQTT_ROOTTOPIC='fingerprintDoorbell', MQTT_CLIENTID='FingerprintDoorbell', MQTT_KEEPALIVE='15',
    HA_DISCOVERY='checked', MQTT_RETAIN='', MQTT_PUBLISHLOG='checked', PAYLOAD_ON='on', PAYLOAD_OFF='off',
    T_LWT='LWT', T_RING='ring', T_MATCHID='matchId', T_MATCHNAME='matchName', T_MATCHCONF='matchConfidence',
    T_LASTLOG='lastLogMessage', T_IGNORERING='ignoreTouchRing', T_LEDRING='ledRing', T_OPENDOOR='openDoor',
    WIFI_SSID='IoT', WIFI_PASSWORD='********')

# answers of the device for the status requests of the pages
FETCH_STUB = '''<script>
window.fetch = function (url) {
  var data = {};
  if (String(url).indexOf('/sensor/status') === 0)
    data = { state: 'green', text: 'connected (since 2 h, 0 losses)', pin: false, ignore: false, led: true, color: 2, seq: 1, reason: 'idle, waiting for a touch' };
  else if (String(url).indexOf('/serial/log') === 0)
    data = { next: 0, more: false, text: '' };
  else
    return new Promise(function () {});
  return Promise.resolve({ json: function () { return Promise.resolve(data); } });
};
window.EventSource = function () { this.addEventListener = function () {}; };
</script>'''

PAGES = [  # page, published name, window width, crop height of the thumbnail
    ('index', 'fingerprints', 1000, 640),
    ('settings', 'settings', 1000, 640),
    ('mqtt', 'mqtt', 1000, 640),
    ('log', 'log', 1000, 640),
    ('wificonfig', 'wificonfig', 480, 0),
]


def render(src, dst, lang, theme, version):
    values = dict(SAMPLE, LANG=lang, VERSIONINFO=version)
    for page, _, _, _ in PAGES:
        html = open(os.path.join(src, page + '.html'), encoding='utf-8').read()
        html = re.sub(r'%([A-Z0-9_]+)%', lambda m: values.get(m.group(1), ''), html)
        html = html.replace('<head>', '<head>' + FETCH_STUB, 1)
        if theme:
            html = html.replace('<head>', '<head><script>try{localStorage.setItem("theme","%s")}catch(e){}</script>' % theme, 1)
        open(os.path.join(dst, page + '.html'), 'w', encoding='utf-8').write(html)
    for name in ('i18n.js', 'style.css'):
        if os.path.exists(os.path.join(src, name)):
            shutil.copy(os.path.join(src, name), dst)
    with gzip.open(os.path.join(src, 'bootstrap.min.css.gz'), 'rb') as f:
        open(os.path.join(dst, 'bootstrap.min.css'), 'wb').write(f.read())


def trim(path):
    """cuts the empty area below the page (same color as the bottom row)"""
    image = Image.open(path).convert('RGB')
    width, height = image.size
    pixels = image.load()
    background = pixels[width // 2, height - 1]
    bottom = height - 1
    while bottom > 0 and all(pixels[x, bottom] == background for x in range(0, width, 4)):
        bottom -= 1
    image.crop((0, 0, width, min(height, bottom + 16))).save(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--lang', default='en', choices=['en', 'de'])
    parser.add_argument('--theme', default='light', choices=['light', 'dark'])
    parser.add_argument('--out', default='doc/images')
    args = parser.parse_args()

    version = re.search(r'VersionInfo = "([^"]+)"', open('src/main.cpp').read()).group(1)
    chromium = shutil.which('chromium') or shutil.which('chromium-browser') or shutil.which('google-chrome')
    if not chromium:
        sys.exit('Chromium is needed for the screenshots')

    work = tempfile.mkdtemp()
    render('data', work, args.lang, args.theme, version)
    class QuietHandler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args):
            pass
    handler = functools.partial(QuietHandler, directory=work)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    os.makedirs(args.out, exist_ok=True)
    try:
        for page, name, width, thumb in PAGES:
            target = os.path.abspath(os.path.join(args.out, 'web-%s.png' % name))
            subprocess.run([chromium, '--headless', '--disable-gpu', '--hide-scrollbars', '--no-sandbox',
                            '--window-size=%d,2600' % width, '--virtual-time-budget=4000', '--screenshot=' + target,
                            'http://127.0.0.1:%d/%s.html' % (server.server_port, page)],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            trim(target)
            if thumb:
                # all thumbnails get the same height, a short page is filled up with the page background
                image = Image.open(target).convert('RGB')
                canvas = Image.new('RGB', (image.size[0], thumb), image.getpixel((image.size[0] // 2, image.size[1] - 1)))
                canvas.paste(image.crop((0, 0, image.size[0], min(thumb, image.size[1]))), (0, 0))
                canvas.save(os.path.join(args.out, 'thumb-%s.png' % name))
            print(target)
    finally:
        server.shutdown()
        shutil.rmtree(work)


if __name__ == '__main__':
    main()
