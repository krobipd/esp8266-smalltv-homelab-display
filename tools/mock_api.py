#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Mock des GESAMTEN Geraets fuer die geraetelose Entwicklung.

Der Mock ersetzt zwei Dinge gleichzeitig, damit die Oberflaeche vollstaendig ohne
Hardware bedienbar ist -- und liefert die Oberflaeche gleich selbst aus, sodass
alles auf einem Port laeuft wie beim echten Geraet (kein CORS-Sonderfall):

  1. die Geraete-API      /api/v1/slots[...]  und  /api/v1/wifi/[scan|status|connect]
  2. eine ioBroker-Quelle /rest-api/v1/state/<id>[/plain]   (Ziel des "Testen"-Knopfs)
  3. die Oberflaeche      /  ->  smalltv/firmware/data/web/  (dieselben Dateien wie auf dem Geraet)

Start:  python3 smalltv/tools/mock_api.py
Dann:   http://127.0.0.1:8099/

ACHTUNG: Die Zustandsbewertung unten ist eine bewusste Nachbildung der C++-Logik aus
smalltv_util.h -- reine Testinfrastruktur. Massgeblich ist immer die C++-Fassung samt
ihrer Host-Tests; diese Kopie existiert nur, damit die Oberflaeche etwas anzuzeigen hat.
"""
import json
import math
import os
import re
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = 8099
START = time.time()
UI_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "firmware", "data", "web")

# Firmware-Version wie auf dem Geraet -- sie steckt in der Cache-Kennung jeder Datei
# (Webserver::baueEtag). Ohne dieselbe Kennung koennte der Mock einen Cache-Fehler
# verbergen, den das Geraet zeigt.
def _fw_version():
    basis = os.path.dirname(os.path.abspath(__file__))
    # Zuerst die generierte Kopfdatei: genau diese Zeichenkette steckt in der gebauten
    # Firmware (PROJECT_VER_STR). Die VERSION-Datei ist nur die Quelle dafuer und kann
    # ihr voraus sein, wenn seitdem nicht gebaut wurde.
    kopf = os.path.join(basis, "..", "firmware", "include", "project_version.h")
    try:
        with open(kopf, encoding="utf-8") as f:
            treffer = re.search(r'PROJECT_VER_STR\[\]\s*=\s*"([^"]+)"', f.read())
        if treffer:
            return treffer.group(1)
    except OSError:
        pass
    try:
        with open(os.path.join(basis, "..", "firmware", "VERSION"), encoding="utf-8") as f:
            return f.read().strip() or "unknown"
    except OSError:
        return "unknown"


FW_VERSION = _fw_version()


ABBILD_UNBEKANNT, ABBILD_FIRMWARE, ABBILD_DATEISYSTEM = 0, 1, 2


def inhalt_aus_multipart(roh):
    """Die ersten Nutzbytes aus einem multipart-Koerper -- der Dateiinhalt beginnt
    nach der Leerzeile hinter den Teil-Kopfzeilen."""
    i = roh.find(b"\r\n\r\n")
    return roh[i + 4:] if i >= 0 else roh


def erkenne_abbild(daten):
    """1:1 wie erkenneAbbild() in firmware/include/abbild_art.h."""
    if daten is None or len(daten) < 16:
        return ABBILD_UNBEKANNT
    if daten[0] == 0xE9:
        return ABBILD_FIRMWARE
    if daten[8:16] == b"littlefs":
        return ABBILD_DATEISYSTEM
    return ABBILD_UNBEKANNT


def abbild_fehlertext(erkannt, erwartet):
    """Wortgleich mit abbildFehlertext() der Firmware."""
    if erwartet == ABBILD_FIRMWARE and erkannt == ABBILD_DATEISYSTEM:
        return ("Das ist die Oberflaeche (Dateisystem), kein Firmware-Abbild. "
                "Es wurde nichts geschrieben.")
    if erwartet == ABBILD_DATEISYSTEM and erkannt == ABBILD_FIRMWARE:
        return ("Das ist ein Firmware-Abbild, nicht die Oberflaeche. "
                "Es wurde nichts geschrieben.")
    return ("Diese Datei ist weder ein Firmware- noch ein Dateisystem-Abbild. "
            "Es wurde nichts geschrieben.")


def etag_passt(kopfzeile, etag):
    """1:1 wie etagPasst() in firmware/include/http_cache.h: mehrere Kennungen,
    schwache Markierung (W/) und "*" muessen passen -- sonst schickt das Geraet die
    Datei erneut, obwohl der Browser sie hat."""
    if not kopfzeile or not etag:
        return False
    for teil in kopfzeile.split(","):
        wert = teil.strip()
        if wert == "*":
            return True
        if wert.startswith("W/"):
            wert = wert[2:]
        if wert == etag:
            return True
    return False

MAX_SLOTS = 12
MAX_PAGES = 4

# Startzustand: bewusst LEER -- das ausgelieferte Geraet hat keine vorbelegten Slots.
CONFIG = {
    "rotateSec": 10,
    "colorWarn": 64800,
    "colorAlarm": 63488,
    "layout": [2, 2, 2, 2],  # 2 = vier Kacheln
    "teilung": [0, 0, 0, 0],  # Aufteilung bei zwei Werten: 0 = automatisch, sonst 20..80
    "helligkeit": 100,
    "hellModus": 0,
    "hellUrl": "",
    "hellField": "",
    "hellSec": 30,
    "hellAn": 100,
    "hellAus": 15,
    "nachtAn": False,
    "nachtVon": 22,
    "nachtBis": 7,
    "nachtHelligkeit": 15,
    "slots": [],
}

FEHLZAEHLER = {}  # Slot-Index -> Anzahl aufeinanderfolgender Fehlversuche

# Hoechster Rasterplatz je Seitenlayout (maxPosForLayout der Firmware) -- EINE Tabelle,
# die Belegungs- und Layoutpruefung teilen.
PLATZGRENZE = {0: 0, 1: 1, 2: 3}

# Passwortschutz wie am Geraet: OPTIONAL. Werkszustand = kein Passwort gesetzt, alles
# frei bedienbar. Ueber die Seite "Passwortschutz" laesst er sich einschalten (und mit
# leerem Passwort wieder aufheben) -- der Mock bildet beide Zustaende 1:1 nach.
# BEWUSST kein Notschluessel: Der Mock darf nicht laxer sein als das Geraet
# ([[mock-strenger-als-die-wirklichkeit]]). Ausgesperrt (Testpasswort vergessen)?
# Mock neu starten -- der Zustand lebt nur im RAM, danach ist der Schutz wieder aus.
AKTIVER_TOKEN = ""  # "" = Schutz aus (Werkszustand); setzbar ueber POST /api/v1/token/save

# Der letzte gelungene Abruf je Slot -- der Mock muss das Verhalten der Firmware
# nachbilden: Bei einem Fehlversuch bleibt der letzte gute Wert stehen, er verschwindet
# nicht. Ohne diese Nachbildung kann die Oberflaeche gegen den Mock richtig aussehen
# und auf dem Geraet trotzdem falsch sein.
LETZTER_WERT = {}   # index -> (angezeigter Wert, Zustand, Zeitpunkt)
ABRUF_CACHE = {}    # index -> (url, Zeitpunkt, (code, body, err)) -- Takt = refreshSec

# Zustand der Basis-Seiten (NTP, Rotation, Protokoll, OTA) -- Antwortformate 1:1 aus
# src/web/Api.cpp uebernommen. Fehlten anfangs komplett; die Seiten liefen gegen 404.
NTP_SERVER = "pool.ntp.org"
NTP_LETZTER_SYNC = int(START)
ROTATION = 0
LOGS = [
    "[boot] Mock-Geraet gestartet",
    "[wifi] Verbunden mit MOCK-WLAN (127.0.0.1)",
    "[ntp] Zeit synchronisiert",
]


def leerer_slot():
    return {
        "enabled": False, "url": "", "field": "", "label": "", "unit": "",
        "decimals": 0, "refreshSec": 30, "page": 1, "pos": 0,
        "wertSize": 3, "labelSize": 2, "unitSize": 2, "einheitDaneben": True, "color": 65535,
        "anzeige": 0, "barMin": 0, "barMax": 100,
        "warnAbove": None, "alarmAbove": None, "warnBelow": None, "alarmBelow": None,
    }


for _ in range(MAX_SLOTS):
    CONFIG["slots"].append(leerer_slot())


def mock_wert(name):
    """Sich langsam bewegende Werte je Datenpunkt -- laufen durch alle Schwellen."""
    t = time.time() - START
    if name == "mock.leistung":
        return round(3200 + 3000 * math.sin(t / 20.0), 1)
    if name == "mock.usv":
        return round(max(0.0, 100 - (t % 300) / 3.0), 1)
    if name == "mock.temperatur":
        return round(21.5 + 2 * math.sin(t / 45.0), 2)
    if name == "mock.schalter":
        return int(t) % 20 < 10
    return 0


def zahl_oder_none(rohwert):
    """Nachbildung von parseNumber(): nur vollstaendige Zahlen gelten."""
    try:
        return float(str(rohwert).strip())
    except (TypeError, ValueError):
        return None


def bewerte(wert, slot):
    """Nachbildung von evalThresholds(). Alarm vor Warnung, Schwelle selbst zaehlt nicht."""
    v = zahl_oder_none(wert)
    if v is None:
        return "ok"
    if slot.get("alarmAbove") is not None and v > slot["alarmAbove"]:
        return "alarm"
    if slot.get("alarmBelow") is not None and v < slot["alarmBelow"]:
        return "alarm"
    if slot.get("warnAbove") is not None and v > slot["warnAbove"]:
        return "warn"
    if slot.get("warnBelow") is not None and v < slot["warnBelow"]:
        return "warn"
    return "ok"


def hole(url, timeout=5):
    """Fuehrt den Abruf aus -- so wie es spaeter das Geraet tut, nicht der Browser."""
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            return r.status, r.read(512).decode("utf-8", "replace"), None
    except urllib.error.HTTPError as e:
        return e.code, "", f"HTTP {e.code}"
    except Exception as e:  # Timeout, DNS, Verbindung abgelehnt
        return 0, "", f"{type(e).__name__}"


def felder_aus(body):
    """Schluessel der obersten Ebene, hoechstens 20 -- fuer die Feldauswahl im Assistenten."""
    try:
        d = json.loads(body)
    except ValueError:
        return []
    if not isinstance(d, dict):
        return []
    return list(d.keys())[:20]


def wert_aus(body, field):
    """Nachbildung von extractValue(). Leeres Feld = ganze Antwort ist der Wert."""
    if not field:
        return body.strip(), None
    try:
        d = json.loads(body)
    except ValueError:
        return None, "Antwort ist kein gueltiges JSON"
    if "." in field:
        return None, "verschachtelte Felder nicht unterstuetzt"
    if not isinstance(d, dict) or field not in d:
        return None, "Feld nicht gefunden"
    v = d[field]
    if isinstance(v, (dict, list)):
        return None, "Feld ist kein einzelner Wert"
    if isinstance(v, bool):
        return "true" if v else "false", None
    return str(v), None


class Handler(BaseHTTPRequestHandler):
    # ---------- Hilfen ----------
    def _json(self, code, obj):
        self._raw(code, json.dumps(obj).encode(), "application/json")

    def _raw(self, code, raw, ctype, extra_headers=None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(raw)))
        for k, v in (extra_headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(raw)

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n <= 0:
            return {}
        try:
            return json.loads(self.rfile.read(n).decode())
        except ValueError:
            return {}

    def _fehler(self, code, text):
        self._json(code, {"ok": False, "error": text})

    def _tokenFehlt(self):
        """Bildet requireBearerToken() der Firmware nach -- inklusive Antwortformat.
        Ohne gesetztes Passwort ist alles frei (optionaler Schutz, wie am Geraet)."""
        if AKTIVER_TOKEN == "":
            return False
        kopf = self.headers.get("Authorization") or ""
        if kopf == f"Bearer {AKTIVER_TOKEN}":
            return False
        self._json(401, {"status": "error", "message": "Passwort fehlt oder ist falsch"})
        return True

    # ---------- GET ----------
    def do_GET(self):
        p = self.path.split("?")[0]

        if p.startswith("/api/v1/") and self._tokenFehlt():
            return None
        if p == "/api/v1/token/check":
            # Wie handleTokenCheck(): die Passwortpruefung ist oben schon gelaufen.
            return self._json(200, {"status": "ok", "message": "Passwort ist gueltig"})
        if p == "/api/v1/slots":
            return self._json(200, CONFIG)
        if p == "/api/v1/slots/status":
            return self._json(200, {"slots": self._status()})
        if p == "/api/v1/wifi/status":
            # Formate 1:1 aus handleWifiStatus() -- nur diese drei Schluessel.
            return self._json(200, {"connected": True, "ssid": "MOCK-WLAN",
                                    "ip": "127.0.0.1"})
        if p == "/api/v1/ntp/status":
            return self._json(200, {"lastOk": True, "lastStatus": "Synced",
                                    "lastSyncTime": NTP_LETZTER_SYNC})
        if p == "/api/v1/ntp/config":
            return self._json(200, {"ntp_server": NTP_SERVER})
        if p == "/api/v1/display/rotation":
            return self._json(200, {"rotation": ROTATION})
        if p == "/api/v1/logs":
            return self._json(200, {"logs": LOGS, "count": len(LOGS)})
        if p == "/api/v1/logs/download":
            return self._raw(200, "\n".join(LOGS).encode(), "text/plain",
                             {"Content-Disposition": 'attachment; filename="logs.log"'})
        if p == "/api/v1/ota/status":
            return self._json(200, {"inProgress": False, "bytesWritten": 0,
                                    "totalBytes": 0, "error": False, "message": ""})
        if p == "/api/v1/wifi/scan":
            # Wie handleWifiScan(): NACKTES Array mit ssid/rssi/enc (enc = Zahl des
            # Verschluesselungstyps, 7 = offen). Der Mock hatte hier frueher ein
            # Objekt mit "encrypted" -- die echte Oberflaeche scheiterte daran.
            return self._json(200, [
                {"ssid": "MOCK-WLAN", "rssi": -54, "enc": 4},
                {"ssid": "MOCK-Gast", "rssi": -71, "enc": 4},
                {"ssid": "MOCK-Offen", "rssi": -83, "enc": 7},
            ])
        if p.startswith("/rest-api/v1/state/"):
            return self._iobroker(p)
        if p.startswith("/fehler/"):
            return self._fehlerfall(p)
        return self._ui(p)

    def _status(self):
        out = []
        jetzt = time.time()
        for i, s in enumerate(CONFIG["slots"]):
            if not s["enabled"] or not s["url"]:
                out.append({"index": i, "enabled": False})
                ABRUF_CACHE.pop(i, None)
                continue

            # Wie die Firmware: hoechstens alle refreshSec wirklich abrufen. Sonst
            # holt JEDER 5-s-Poll der Oberflaeche ALLE Ziele live -- bei einem toten
            # Ziel (Timeout) stauen sich die Polls zu haengenden Threads.
            cache = ABRUF_CACHE.get(i)
            if cache is None or cache[0] != s["url"] or jetzt - cache[1] >= s["refreshSec"]:
                ABRUF_CACHE[i] = (s["url"], jetzt, hole(s["url"], timeout=2))
            code, body, err = ABRUF_CACHE[i][2]

            eintrag = {"index": i, "enabled": True, "label": s["label"], "unit": s["unit"]}

            def fehlschlag(grund):
                FEHLZAEHLER[i] = FEHLZAEHLER.get(i, 0) + 1
                eintrag.update({"ok": False, "error": grund,
                                "stale": FEHLZAEHLER[i] >= 3, "failCount": FEHLZAEHLER[i]})
                self._letztenWertAnhaengen(i, eintrag)

            if err or code != 200:
                fehlschlag(err or f"HTTP {code}")
            else:
                wert, werr = wert_aus(body, s["field"])
                if werr:
                    fehlschlag(werr)
                else:
                    FEHLZAEHLER[i] = 0
                    zahl = zahl_oder_none(wert)
                    gezeigt = f"{zahl:.{s['decimals']}f}" if zahl is not None else wert
                    zustand = bewerte(wert, s)
                    if i not in LETZTER_WERT or LETZTER_WERT[i][0] != gezeigt:
                        LETZTER_WERT[i] = (gezeigt, zustand, jetzt)
                    eintrag.update({"ok": True, "value": gezeigt, "raw": wert,
                                    "state": zustand, "stale": False, "ageSec": 0,
                                    "failCount": 0})
            out.append(eintrag)
        return out

    def _letztenWertAnhaengen(self, i, eintrag):
        """Wie die Firmware: der letzte gute Wert bleibt sichtbar, nur sein Alter waechst."""
        if i in LETZTER_WERT:
            wert, zustand, ts = LETZTER_WERT[i]
            eintrag["value"] = wert
            eintrag["state"] = zustand
            eintrag["ageSec"] = int(time.time() - ts)

    def _iobroker(self, p):
        rest = p[len("/rest-api/v1/state/"):]
        plain = rest.endswith("/plain")
        name = rest[:-len("/plain")] if plain else rest
        if not name:
            return self._raw(404, b"kein Datenpunkt", "text/plain")
        v = mock_wert(name)
        if plain:
            return self._raw(200, str(v).encode(), "text/plain")
        return self._json(200, {"val": v, "ack": True, "ts": int(time.time() * 1000),
                                "from": "system.adapter.mock.0", "q": 0})

    def _fehlerfall(self, p):
        if p.endswith("/500"):
            return self._raw(500, b"Interner Fehler", "text/plain")
        if p.endswith("/kaputt"):
            return self._raw(200, b'{"val":', "application/json")
        if p.endswith("/langsam"):
            time.sleep(8)  # laenger als das Abruf-Timeout
            return self._raw(200, b"1", "text/plain")
        if p.endswith("/riesig"):
            return self._json(200, {"fuell": "x" * 20000, "val": 42})
        return self._raw(404, b"unbekannter Fehlerfall", "text/plain")

    def _ui(self, p):
        rel = "index.html" if p in ("/", "") else p.lstrip("/")
        pfad = os.path.normpath(os.path.join(UI_DIR, rel))
        if not pfad.startswith(os.path.normpath(UI_DIR)) or not os.path.isfile(pfad):
            return self._raw(404, b"nicht gefunden", "text/plain")
        typ = {"html": "text/html", "js": "application/javascript",
               "css": "text/css"}.get(pfad.rsplit(".", 1)[-1], "text/plain")
        with open(pfad, "rb") as f:
            roh = f.read()
        # 1:1 wie Webserver::baueEtag/sendeCacheKopfzeilen: behalten darf der Browser,
        # ungeprueft benutzen nicht. Kennt er die Kennung schon, kommt nur ein 304.
        etag = f'"{FW_VERSION}-{len(roh)}"'
        if etag_passt(self.headers.get("If-None-Match"), etag):
            self.send_response(304)
            self.send_header("Cache-Control", "no-cache")
            self.send_header("ETag", etag)
            self.end_headers()
            return None
        return self._raw(200, roh, typ + "; charset=utf-8",
                         {"Cache-Control": "no-cache", "ETag": etag})

    # ---------- POST / DELETE ----------
    def do_POST(self):
        p = self.path.split("?")[0]
        if p.startswith("/api/v1/") and self._tokenFehlt():
            return None
        if p == "/api/v1/token/save":
            return self._token_speichern()
        if p == "/api/v1/slots/test":
            return self._test()
        if p == "/api/v1/slots":
            return self._slot_speichern()
        if p == "/api/v1/slots/settings":
            return self._einstellungen()
        if p == "/api/v1/wifi/connect":
            # Formate 1:1 aus handleWifiConnect() ({"status": "connected"/"error"}).
            ssid = (self._body().get("ssid") or "").strip()
            if not ssid:
                return self._json(500, {"status": "error", "message": "WLAN-Name fehlt"})
            return self._json(200, {"status": "connected", "ssid": ssid, "ip": "127.0.0.1"})
        if p == "/api/v1/ntp/sync":
            global NTP_LETZTER_SYNC
            NTP_LETZTER_SYNC = int(time.time())
            return self._json(200, {"status": "ok", "lastStatus": "Synced",
                                    "lastSyncTime": NTP_LETZTER_SYNC})
        if p == "/api/v1/ntp/config":
            return self._ntp_config()
        if p == "/api/v1/display/rotation":
            return self._rotation_setzen()
        if p == "/api/v1/reboot":
            LOGS.append("[api] Neustart angefordert (Mock: kein echter Neustart)")
            return self._json(200, {"status": "rebooting"})
        if p == "/api/v1/logs/clear":
            del LOGS[:]
            return self._json(200, {"status": "ok", "message": "Logs cleared"})
        if p == "/api/v1/ota/cancel":
            return self._json(200, {"status": "cancelling", "message": "Cancel request received"})
        if p in ("/api/v1/ota/fw", "/api/v1/ota/fs"):
            # Upload einlesen und verwerfen -- der Mock flasht nichts, sagt das aber ehrlich.
            # Die Abbild-Art wird trotzdem geprueft, 1:1 wie otaHandleWrite() der Firmware:
            # Ein Mock, der jede Datei annimmt, verbirgt genau den Fehlgriff, gegen den die
            # Pruefung gebaut wurde.
            n = int(self.headers.get("Content-Length") or 0)
            anfang = b""
            while n > 0:
                stueck = self.rfile.read(min(n, 65536))
                if not stueck:
                    break
                n -= len(stueck)
                if len(anfang) < 4096:
                    anfang += stueck[:4096]
            erwartet = ABBILD_DATEISYSTEM if p.endswith("/fs") else ABBILD_FIRMWARE
            erkannt = erkenne_abbild(inhalt_aus_multipart(anfang))
            if erkannt != erwartet:
                text = abbild_fehlertext(erkannt, erwartet)
                LOGS.append("[ota] abgelehnt: " + text)
                # Antwortform wie handleOtaFinished(): HTTP 200 mit status "Error".
                return self._json(200, {"status": "Error", "message": text})
            LOGS.append("[ota] Upload angenommen (Mock: nichts geschrieben)")
            return self._json(200, {"status": "Upload successful",
                                    "message": "Mock: nichts geflasht, Datei verworfen"})
        return self._fehler(404, "unbekannter Endpunkt")

    def _ntp_config(self):
        global NTP_SERVER, NTP_LETZTER_SYNC
        server = str(self._body().get("ntp_server") or "")
        if not server:
            return self._json(400, {"status": "error", "message": "ntp_server missing"})
        NTP_SERVER = server
        NTP_LETZTER_SYNC = int(time.time())  # wie die Firmware: Speichern loest einen Sync aus
        return self._json(200, {"status": "ok", "ntp_server": server})

    def _rotation_setzen(self):
        global ROTATION
        d = self._body()
        if not isinstance(d.get("rotation"), int):
            return self._json(400, {"status": "error", "message": "Invalid JSON or missing rotation"})
        r = d["rotation"]
        if not 0 <= r <= 7:
            return self._json(400, {"status": "error", "message": "rotation must be between 0 and 7"})
        ROTATION = r
        return self._json(200, {"status": "ok", "rotation": r})

    def _token_speichern(self):
        """Bildet handleTokenSave() nach: setzt oder loescht das Passwort zur Laufzeit.
        Leeres Passwort = Schutz aufheben."""
        global AKTIVER_TOKEN
        d = self._body()
        if not isinstance(d.get("token"), str):
            return self._json(400, {"status": "error", "message": "Passwort-Feld fehlt"})
        AKTIVER_TOKEN = d["token"]
        if AKTIVER_TOKEN == "":
            return self._json(200, {"status": "ok", "message": "Passwortschutz aufgehoben"})
        return self._json(200, {"status": "ok", "message": "Passwort gespeichert"})

    def _test(self):
        url = (self._body().get("url") or "").strip()
        if not url.startswith("http://"):
            return self._fehler(400, "URL ungueltig (nur http://)")
        code, body, err = hole(url)
        if err:
            return self._json(200, {"ok": False, "httpStatus": code, "error": err,
                                    "preview": "", "fields": []})
        return self._json(200, {"ok": code == 200, "httpStatus": code,
                                "preview": body[:512], "fields": felder_aus(body)})

    def _einstellungen(self):
        """Bildet settingsFromDoc der Firmware nach -- inklusive der Ablehnung."""
        d = self._body()
        r = int(d.get("rotateSec", CONFIG["rotateSec"]))
        if r != 0 and not 3 <= r <= 3600:
            return self._fehler(400, "Wechselintervall nur 0 (aus) oder 3 bis 3600 Sekunden")

        neu = list(CONFIG["layout"])
        for i, l in enumerate(d.get("layout", [])[:MAX_PAGES]):
            if int(l) not in (0, 1, 2):
                return self._fehler(400, "Unbekanntes Seitenlayout")
            neu[i] = int(l)

        for s in CONFIG["slots"]:
            if s["enabled"] and s["url"] and s["pos"] > PLATZGRENZE[neu[s["page"] - 1]]:
                return self._fehler(
                    400, "Ein Wert liegt auf einem Platz, den das neue Layout nicht hat")

        CONFIG["rotateSec"] = r
        # Aufteilung wie die Firmware pruefen: 0 (automatisch) oder 20..80. Ein laxerer
        # Mock liesse Werte durch, die das Geraet ablehnt.
        neueTeilung = list(CONFIG["teilung"])
        for i, t in enumerate(d.get("teilung", [])[:MAX_PAGES]):
            t = int(t)
            if t != 0 and not 20 <= t <= 80:
                return self._fehler(400, "Aufteilung nur 0 (automatisch) oder 20 bis 80 Prozent")
            neueTeilung[i] = t

        CONFIG["layout"] = neu
        CONFIG["teilung"] = neueTeilung
        CONFIG["colorWarn"] = int(d.get("colorWarn", CONFIG["colorWarn"]))
        CONFIG["colorAlarm"] = int(d.get("colorAlarm", CONFIG["colorAlarm"]))

        von = int(d.get("nachtVon", CONFIG["nachtVon"]))
        bis = int(d.get("nachtBis", CONFIG["nachtBis"]))
        if not (0 <= von <= 23 and 0 <= bis <= 23):
            return self._fehler(400, "Nachtmodus: Stunden nur 0 bis 23")
        modus = int(d.get("hellModus", CONFIG["hellModus"]))
        if modus not in (0, 1):
            return self._fehler(400, "Unbekannter Helligkeits-Modus")
        hurl = str(d.get("hellUrl", CONFIG["hellUrl"]))
        if modus == 1 and not hurl.startswith("http://"):
            return self._fehler(400, "Schalter braucht eine gueltige Adresse (nur http://)")
        hsec = int(d.get("hellSec", CONFIG["hellSec"]))
        if modus == 1 and not 5 <= hsec <= 3600:
            return self._fehler(400, "Schalter-Intervall nur 5 bis 3600 Sekunden")
        CONFIG["hellModus"] = modus
        CONFIG["hellUrl"] = hurl
        CONFIG["hellField"] = str(d.get("hellField", CONFIG["hellField"]))
        CONFIG["hellSec"] = hsec
        CONFIG["hellAn"] = max(0, min(100, int(d.get("hellAn", CONFIG["hellAn"]))))
        CONFIG["hellAus"] = max(0, min(100, int(d.get("hellAus", CONFIG["hellAus"]))))
        CONFIG["helligkeit"] = max(0, min(100, int(d.get("helligkeit", CONFIG["helligkeit"]))))
        CONFIG["nachtAn"] = bool(d.get("nachtAn", CONFIG["nachtAn"]))
        CONFIG["nachtVon"] = von
        CONFIG["nachtBis"] = bis
        CONFIG["nachtHelligkeit"] = max(
            0, min(100, int(d.get("nachtHelligkeit", CONFIG["nachtHelligkeit"]))))
        return self._json(200, {"ok": True})

    def _slot_speichern(self):
        d = self._body()
        i = d.get("index")
        if not isinstance(i, int) or not 0 <= i < MAX_SLOTS:
            return self._fehler(400, "Slot-Nummer ausserhalb des Bereichs")
        if not str(d.get("url", "")).startswith("http://"):
            return self._fehler(400, "URL ungueltig (nur http://, max 127 Zeichen)")
        # Wie die Firmware seit v0.2.6: Die Beschriftung darf leer bleiben, nur zu lang
        # nicht. Die Einheit hat eine eigene Grenze -- waere der Mock hier laxer, liesse
        # er Eingaben durch, die das Geraet ablehnt.
        if len(str(d.get("label", ""))) > 23:
            return self._fehler(400, "Beschriftung ist zu lang")
        if len(str(d.get("unit", ""))) > 15:
            return self._fehler(400, "Einheit ist zu lang (max 15 Zeichen)")
        r = int(d.get("refreshSec") or 0)
        if not 5 <= r <= 3600:
            return self._fehler(400, "Intervall nur 5 bis 3600 Sekunden")
        seite = int(d.get("page") or 0)
        if not 1 <= seite <= MAX_PAGES:
            return self._fehler(400, "Seite ausserhalb des Bereichs")
        grenze = PLATZGRENZE[CONFIG["layout"][seite - 1]]
        platz = int(d.get("pos") or 0)
        if platz > grenze:
            return self._fehler(400, "Rasterplatz gibt es in diesem Seitenlayout nicht")
        anzeige = int(d.get("anzeige") or 0)
        if anzeige not in (0, 1, 2):
            return self._fehler(400, "Darstellungsart unbekannt")
        # Die drei Schriftstufen wie in der Firmware: je 1 bis 10, voneinander
        # unabhaengig. Waere der Mock hier laxer, liesse er Werte durch, die das
        # Geraet ablehnt -- und die Oberflaeche saehe im Test besser aus als real.
        for feld, meldung in (("wertSize", "Werts"), ("labelSize", "Beschriftung"),
                              ("unitSize", "Einheit")):
            stufe = int(d.get(feld) or 0)
            if not 1 <= stufe <= 10:
                return self._fehler(
                    400, "Schriftstufe der %s ausserhalb des Bereichs" % meldung
                    if feld != "wertSize" else "Schriftstufe des Werts ausserhalb des Bereichs")
        if anzeige != 0 and not (float(d.get("barMax", 100)) > float(d.get("barMin", 0))):
            return self._fehler(400, "Balken-Ende muss groesser als der Anfang sein")
        for j, andere in enumerate(CONFIG["slots"]):
            # Wie die Firmware: ein eingerichteter Slot haelt seinen Platz auch abgeschaltet.
            if j != i and andere["url"] and andere["page"] == seite and andere["pos"] == platz:
                return self._fehler(400, "Rasterplatz ist bereits belegt")

        slot = leerer_slot()
        slot.update({k: v for k, v in d.items() if k in slot})
        slot["enabled"] = bool(d.get("enabled", True))
        # Wie die Firmware: alles ausser einer echten Falschmeldung heisst "daneben".
        slot["einheitDaneben"] = bool(d.get("einheitDaneben", True))
        # Ein abgeschalteter Wert behaelt seine Einstellungen -- nur angezeigt wird er nicht.
        CONFIG["slots"][i] = slot
        FEHLZAEHLER.pop(i, None)
        return self._json(200, {"ok": True})

    def do_DELETE(self):
        p = self.path.split("?")[0]
        if p.startswith("/api/v1/") and self._tokenFehlt():
            return None
        if p.startswith("/api/v1/slots/"):
            try:
                i = int(p.rsplit("/", 1)[1])
            except ValueError:
                return self._fehler(400, "Slot-Nummer ungueltig")
            if not 0 <= i < MAX_SLOTS:
                return self._fehler(400, "Slot-Nummer ausserhalb des Bereichs")
            CONFIG["slots"][i] = leerer_slot()
            FEHLZAEHLER.pop(i, None)
            return self._json(200, {"ok": True})
        return self._fehler(404, "unbekannter Endpunkt")

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    print(f"Mock-Geraet laeuft auf http://127.0.0.1:{PORT}/")
    print("  Oberflaeche :  /                     (aus smalltv/firmware/data/web/)")
    print("  Geraete-API :  /api/v1/slots[/status|/test|/<i>]   /api/v1/wifi/[scan|status|connect]")
    print("                 /api/v1/[ntp|logs|ota|reboot|display/rotation|token] -- alle UI-Seiten abgedeckt")
    print("  Datenquelle :  /rest-api/v1/state/<id>[/plain]")
    print("  Datenpunkte :  mock.leistung  mock.usv  mock.temperatur  mock.schalter")
    print("  Fehlerfaelle:  /fehler/500  /fehler/kaputt  /fehler/langsam  /fehler/riesig")
    print()
    print("  PASSWORTSCHUTZ: aus (Werkszustand) — alles frei bedienbar. Zum Testen des")
    print("  geschuetzten Modus auf der Seite \"Passwortschutz\" ein Passwort setzen;")
    print("  leeres Passwort speichern hebt ihn auf. Passwort vergessen? Mock neu")
    print("  starten — der Zustand lebt nur im RAM (das Geraet hat dafuer den Rescue-Modus).")
    # Mehrfaedig ist hier Pflicht, nicht Kosmetik: Der Testabruf und die simulierte
    # Datenquelle liegen im selben Prozess. Ein einfaediger Server wuerde sich beim
    # Selbstaufruf blockieren und jeden Test als Zeitueberschreitung melden.
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
