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
import hashlib
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
# verbergen, den das Geraet zeigt. Quelle ist firmware/VERSION; genau daraus macht das
# Pre-Build-Skript das Makro PROJECT_VER der Firmware.
def _fw_version():
    basis = os.path.dirname(os.path.abspath(__file__))
    try:
        with open(os.path.join(basis, "..", "firmware", "VERSION"), encoding="utf-8") as f:
            return f.read().strip() or "unknown"
    except OSError:
        return "unknown"


def _dateizustand():
    """Dateizustand wie handleGeraet() seit v0.5.5.

    Groesse als Zahl heisst "vorhanden", None heisst "gibt es nicht" -- genau diese
    Unterscheidung fehlte bei der Ursachensuche am 07.09.2026 und ist der Grund fuer
    den Endpunkt. Mit MOCK_DATEIVERLUST=1 laesst sich der Fall nachstellen, in dem die
    Hauptdatei verschwunden ist und die Zweitschrift einspringt.
    """
    if os.environ.get("MOCK_DATEIVERLUST") == "1":
        return {"slots": None, "slotsSicherung": 3819, "slotsDefekt": None,
                "slotsTemp": None, "config": 96}
    return {"slots": 3819, "slotsSicherung": 3819, "slotsDefekt": None,
            "slotsTemp": None, "config": 96}


def _verzeichnis():
    """Verzeichnis wie handleDateien() seit v0.5.6.

    Der Mock kennt kein echtes Dateisystem und meldet deshalb glaubwuerdige Zahlen:
    die Konfigurationsdateien einzeln, /web als Summe. MOCK_DATEIVERLUST=1 nimmt die
    Hauptdatei heraus -- dann ist genau der Zustand vom 07.09.2026 nachstellbar,
    einschliesslich des Ueberhangs, der einen verwaisten Block anzeigen wuerde.
    """
    zustand = _dateizustand()
    dateien = [{"name": name, "groesse": groesse}
               for name, groesse in (("slots.json", zustand["slots"]),
                                     ("slots.bak.json", zustand["slotsSicherung"]),
                                     ("slots.json.defekt", zustand["slotsDefekt"]),
                                     ("slots.json.tmp", zustand["slotsTemp"]),
                                     ("config.json", zustand["config"]))
               if groesse is not None]
    WEB_ANZAHL, WEB_BYTES = 31, 196_608
    summe = WEB_BYTES + sum(d["groesse"] for d in dateien)
    belegt = 213000
    return {
        "dateien": dateien,
        "ordner": [{"name": "web", "anzahl": WEB_ANZAHL, "groesse": WEB_BYTES}],
        "anzahl": WEB_ANZAHL + len(dateien),
        "summe": summe,
        "fsGesamt": 2072576,
        "fsBelegt": belegt,
        "ueberhang": max(0, belegt - summe),
        "blockGroesse": 8192,
    }


FW_VERSION = _fw_version()


def _fs_kennung():
    """Kennung des Dateisystems wie am Geraet: Inhalt von data/web/BUILD.

    Seit v0.5.0 haengt die Cache-Kennung am INHALT der Oberflaeche, nicht mehr an der
    Firmware-Version (scripts/fs_build_id.py erzeugt sie beim Bauen). Fehlt die Datei,
    faellt auch das Geraet auf die Version zurueck -- der Mock macht dasselbe.
    """
    basis = os.path.dirname(os.path.abspath(__file__))
    try:
        with open(os.path.join(basis, "..", "firmware", "data", "web", "BUILD"),
                  encoding="utf-8") as f:
            kennung = f.read().strip()
        if kennung and all(c in "0123456789abcdef" for c in kennung):
            return kennung
    except OSError:
        pass
    return FW_VERSION


FS_KENNUNG = _fs_kennung()


ABBILD_UNBEKANNT, ABBILD_FIRMWARE, ABBILD_DATEISYSTEM = 0, 1, 2


def datei_aus_multipart(roh, content_type):
    """Der Inhalt des Datei-Teils: nach der Leerzeile hinter den Teil-Kopfzeilen bis zur
    schliessenden Grenze. Reicht fuer das, was Browser und curl schicken (ein Teil)."""
    m = re.search(r"boundary=([^;]+)", content_type or "")
    grenze = ("--" + m.group(1).strip().strip('"')).encode() if m else None
    i = roh.find(b"\r\n\r\n")
    daten = roh[i + 4:] if i >= 0 else roh
    if grenze:
        j = daten.rfind(b"\r\n" + grenze)
        if j >= 0:
            daten = daten[:j]
    return daten


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

START_ZEIT = time.time()
MAX_SLOTS = 12
# Dieselben Grenzen wie die Firmware (smalltv_util.h / config_codec.h). run_tests.sh
# vergleicht diese Tabelle mit tests/host/limits_dump.cpp -- laufen sie auseinander,
# lehnte das Geraet ab, was der Mock durchwinkt, und der Test verbirgt genau das.
# Standard-Schriftstufen wie in der Firmware (config_codec.h).
STD_WERT_SIZE, STD_LABEL_SIZE, STD_UNIT_SIZE = 3, 2, 2
LIMITS = {
    "url": 127, "label": 23, "field": 31, "unit": 15,
    "slots": 12, "pages": 4,
    "refreshMin": 5, "refreshMax": 3600,
    "rotateMin": 3, "rotateMax": 3600,
    "decimalsMax": 3, "hellSecMin": 5, "hellSecMax": 3600,
    "textStufeMin": 1, "textStufeMax": 10,
}
# Was das Display zeichnen kann (Firmware smalltv_util.h, FONT_UMSETZUNG): ASCII plus
# ° ä ö ü Ä Ö Ü ß. Steuerzeichen lehnt das Geraet in jedem Text ab.
DISPLAY_ZEICHEN = re.compile(r"^[\x20-\x7e\u00b0\u00e4\u00f6\u00fc\u00c4\u00d6\u00dc\u00df]*$")
OHNE_STEUERZEICHEN = re.compile(r"^[^\x00-\x1f\x7f]*$")
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
# Vorgabe wie in der Firmware (NTPClient.cpp, TZ_VORGABE): Mitteleuropa.
ZEITZONE_VORGABE = "CET-1CEST,M3.5.0,M10.5.0/3"
ZEITZONE = ZEITZONE_VORGABE
NTP_LETZTER_SYNC = int(START)
ROTATION = 0
LOGS = [
    "[boot] Mock-Geraet gestartet",
    "[wifi] Verbunden mit MOCK-WLAN (127.0.0.1)",
    "[ntp] Zeit synchronisiert",
]


def ergebnis(ok, message, **weitere):
    """EIN Antwortformat, wie setzeErgebnis() in der Firmware (D5).

    ok sagt, ob es geklappt hat, message sagt es in Worten. status und -- im Fehlerfall --
    error gehen vorerst weiter mit, damit eine aeltere Oberflaeche im Fenster zwischen
    Firmware- und Dateisystem-Update nicht blind ist.
    """
    d = {"ok": bool(ok), "message": message, "status": "ok" if ok else "error"}
    if not ok:
        d["error"] = message
    d.update(weitere)
    return d


def zeitzone_gueltig(tz):
    """Wie NTPClient::zeitzoneGueltig in der Firmware."""
    if not isinstance(tz, str) or not 1 <= len(tz) <= 47:
        return False
    if any(ord(c) <= 0x20 or ord(c) >= 0x7F for c in tz):
        return False
    return all(c.isascii() and c.isalpha() for c in tz[:3])


def leere_konfiguration():
    """Werkszustand der Konfiguration -- Grundlage fuer die Wiederherstellung."""
    c = {
        "rotateSec": 10, "colorWarn": 64800, "colorAlarm": 63488,
        "layout": [2, 2, 2, 2], "teilung": [0, 0, 0, 0],
        "helligkeit": 100, "hellModus": 0, "hellUrl": "", "hellField": "",
        "hellSec": 30, "hellAn": 100, "hellAus": 15,
        "nachtAn": False, "nachtVon": 22, "nachtBis": 7, "nachtHelligkeit": 15,
        "slots": [],
    }
    for _ in range(MAX_SLOTS):
        c["slots"].append(leerer_slot())
    return c


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
            # 1024 wie KOERPER_MAX in der Firmware (SlotRuntime.h). Mit 512 haette der
            # Mock Antworten abgeschnitten, die das Geraet noch vollstaendig liest.
            return r.status, r.read(1024).decode("utf-8", "replace"), None
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
        self._json(code, ergebnis(False, text))

    def _tokenFehlt(self):
        """Bildet requireBearerToken() der Firmware nach -- inklusive Antwortformat.
        Ohne gesetztes Passwort ist alles frei (optionaler Schutz, wie am Geraet)."""
        if AKTIVER_TOKEN == "":
            return False
        kopf = self.headers.get("Authorization") or ""
        if kopf == f"Bearer {AKTIVER_TOKEN}":
            return False
        self._json(401, ergebnis(False, "Passwort fehlt oder ist falsch"))
        return True

    # ---------- GET ----------
    def do_GET(self):
        p = self.path.split("?")[0]

        if p.startswith("/api/v1/") and self._tokenFehlt():
            return None
        if p == "/api/v1/token/check":
            # Wie handleTokenCheck(): die Passwortpruefung ist oben schon gelaufen.
            return self._json(200, ergebnis(True, "Passwort ist gueltig"))
        if p == "/api/v1/slots":
            # Wie handleSlotsGet(): die Konfiguration samt Grenzen (B8).
            return self._json(200, dict(CONFIG, limits=LIMITS))
        if p == "/api/v1/geraet":
            # Wie handleGeraet(): alles fuer den Kasten auf der Uebersicht in EINEM
            # leichten Aufruf, ohne die Datenquellen anzufassen.
            belegt = [s for s in CONFIG["slots"] if s["url"]]
            seiten = {s["page"] for s in belegt if s["enabled"]}
            return self._json(200, ergebnis(
                True, "Geraetezustand",
                version=_fw_version(), freeHeap=18400, heapFrag=7,
                uptimeSec=int(time.time() - START_ZEIT),
                verbunden=True, rssi=-58, ssid="MOCK-WLAN", ip="127.0.0.1",
                zeitOk=True,
                zeitStatus=time.strftime("Synchronisiert: %Y-%m-%d %H:%M:%S",
                                         time.localtime(NTP_LETZTER_SYNC)),
                zeitzone=ZEITZONE, rotation=ROTATION,
                werte=len(belegt), maxWerte=MAX_SLOTS,
                seiten=len(seiten), maxSeiten=MAX_PAGES,
                # Speicherzustand wie handleGeraet() seit v0.5.5. Der Mock kennt kein
                # echtes Dateisystem; er meldet den Normalfall (Hauptdatei und
                # Zweitschrift vorhanden, keine Reste). Ueber MOCK_DATEIVERLUST=1
                # laesst sich der Verlustfall nachstellen -- sonst waere der neue
                # Anzeigepfad nie ohne echtes Geraet zu pruefen.
                resetGrund="Power on",
                flashEcht=4194304, flashKonfiguriert=4194304,
                fsGesamt=2072576, fsBelegt=213000,
                dateien=_dateizustand()))
        if p == "/api/v1/dateien":
            # Wie handleDateien(): was liegt wirklich auf dem Dateisystem, und wie
            # verhaelt sich der belegte Platz zur Summe der Dateien.
            return self._json(200, ergebnis(True, "Dateien", **_verzeichnis()))
        if p == "/api/v1/slots/status":
            # Antwortform wie handleSlotsStatus(): Werte plus Geraetezustand (E11).
            return self._json(200, {"slots": self._status(), "geraet": {
                "version": _fw_version(), "freeHeap": 18400, "heapFrag": 7,
                "uptimeSec": int(time.time() - START_ZEIT), "rssi": -58}})
        if p == "/api/v1/wifi/status":
            # Formate 1:1 aus handleWifiStatus() -- nur diese drei Schluessel.
            return self._json(200, {"connected": True, "ssid": "MOCK-WLAN",
                                    "ip": "127.0.0.1"})
        if p == "/api/v1/ntp/status":
            return self._json(200, {"lastOk": True, "lastStatus": "Synced",
                                    "lastSyncTime": NTP_LETZTER_SYNC})
        if p == "/api/v1/ntp/config":
            return self._json(200, {"ntp_server": NTP_SERVER, "zeitzone": ZEITZONE})
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
            alter = int(time.time() - ts)
            eintrag["value"] = wert
            eintrag["state"] = zustand
            eintrag["ageSec"] = alter
            # Dieselbe Regel wie istVeraltet() in smalltv_util.h: veraltet ist ein Wert
            # nach drei Fehlversuchen ODER wenn er aelter ist als das Dreifache seines
            # Intervalls plus 30 s. Der Mock kannte nur die Fehlversuche.
            refresh = CONFIG["slots"][i]["refreshSec"]
            if alter > refresh * 3 + 30:
                eintrag["stale"] = True

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
        etag = f'"{FS_KENNUNG}-{len(roh)}"'
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
        if p == "/api/v1/slots/restore":
            return self._wiederherstellen()
        if p == "/api/v1/slots/settings":
            return self._einstellungen()
        if p == "/api/v1/wifi/connect":
            # Formate 1:1 aus handleWifiConnect() ({"status": "connected"/"error"}).
            ssid = (self._body().get("ssid") or "").strip()
            if not ssid:
                return self._json(400, ergebnis(False, "WLAN-Name fehlt"))
            return self._json(200, ergebnis(True, "Verbunden", status="connected",
                                            ssid=ssid, ip="127.0.0.1"))
        if p == "/api/v1/ntp/sync":
            global NTP_LETZTER_SYNC
            NTP_LETZTER_SYNC = int(time.time())
            # Wie handleNtpSync(): nur angestossen, das Ergebnis holt /ntp/status.
            return self._json(200, ergebnis(True, "Abgleich angestossen", status="gestartet",
                                            lastStatus="Synced", lastSyncTime=NTP_LETZTER_SYNC))
        if p == "/api/v1/ntp/config":
            return self._ntp_config()
        if p == "/api/v1/display/rotation":
            return self._rotation_setzen()
        if p == "/api/v1/reboot":
            LOGS.append("[api] Neustart angefordert (Mock: kein echter Neustart)")
            return self._json(200, ergebnis(True, "Neustart", status="rebooting"))
        if p == "/api/v1/logs/clear":
            del LOGS[:]
            return self._json(200, ergebnis(True, "Protokoll geleert"))
        if p == "/api/v1/ota/cancel":
            return self._json(200, ergebnis(True, "Abbruch angefordert", status="cancelling"))
        if p in ("/api/v1/ota/fw", "/api/v1/ota/fs"):
            # Upload einlesen und verwerfen -- der Mock flasht nichts, sagt das aber ehrlich.
            # Abbild-Art und Pruefsumme werden trotzdem geprueft, 1:1 wie otaHandleStart()
            # und otaHandleWrite() der Firmware: Ein Mock, der jede Datei annimmt, verbirgt
            # genau die Fehlgriffe, gegen die die Pruefungen gebaut wurden.
            n = int(self.headers.get("Content-Length") or 0)
            roh = self.rfile.read(n) if n > 0 else b""
            daten = datei_aus_multipart(roh, self.headers.get("Content-Type", ""))
            erwartet = ABBILD_DATEISYSTEM if p.endswith("/fs") else ABBILD_FIRMWARE
            md5 = (self.headers.get("X-Abbild-MD5") or "").strip().lower()
            if len(md5) != 32 and erwartet == ABBILD_FIRMWARE:
                text = "Pruefsumme fehlt -- Update-Seite neu laden oder curl mit X-Abbild-MD5"
                LOGS.append("[ota] abgelehnt: " + text)
                return self._json(200, ergebnis(False, text))
            erkannt = erkenne_abbild(daten[:16])
            if erkannt != erwartet:
                text = abbild_fehlertext(erkannt, erwartet)
                LOGS.append("[ota] abgelehnt: " + text)
                # Antwortform wie handleOtaFinished(): HTTP 200 mit status "error".
                return self._json(200, ergebnis(False, text))
            if len(md5) == 32:
                ist = hashlib.md5(daten).hexdigest()
                if ist != md5:
                    # Wortlaut des Updaters (Updater.cpp, getErrorString).
                    text = "MD5 Failed: expected:%s, calculated:%s" % (md5, ist)
                    LOGS.append("[ota] abgelehnt: " + text)
                    return self._json(200, ergebnis(False, text))
            LOGS.append("[ota] Upload angenommen (Mock: nichts geschrieben)")
            return self._json(200, ergebnis(True, "Mock: nichts geflasht, Datei verworfen"))
        return self._fehler(404, "unbekannter Endpunkt")

    def _ntp_config(self):
        global NTP_SERVER, NTP_LETZTER_SYNC, ZEITZONE
        d = self._body()
        server = str(d.get("ntp_server") or "")
        if not server:
            return self._json(400, ergebnis(False, "Zeitserver fehlt"))
        # Zeitzone wie die Firmware: freiwillig, leer heisst Vorgabe, sonst geprueft.
        genannt = isinstance(d.get("zeitzone"), str)
        tz = str(d.get("zeitzone") or "")
        if genannt and tz and not zeitzone_gueltig(tz):
            return self._json(400, ergebnis(
                False, "Zeitzone ungueltig -- erwartet wird eine Regel wie "
                       "CET-1CEST,M3.5.0,M10.5.0/3"))
        NTP_SERVER = server
        if genannt:
            ZEITZONE = tz or ZEITZONE_VORGABE
        NTP_LETZTER_SYNC = int(time.time())  # wie die Firmware: Speichern loest einen Sync aus
        return self._json(200, ergebnis(True, "Gespeichert", ntp_server=server, zeitzone=ZEITZONE))

    def _rotation_setzen(self):
        global ROTATION
        d = self._body()
        if not isinstance(d.get("rotation"), int):
            return self._json(400, ergebnis(False, "Drehung fehlt oder ist keine Zahl"))
        r = d["rotation"]
        if not 0 <= r <= 7:
            return self._json(400, ergebnis(False, "Drehung nur 0 bis 7"))
        ROTATION = r
        return self._json(200, ergebnis(True, "Drehung uebernommen", rotation=r))

    def _token_speichern(self):
        """Bildet handleTokenSave() nach: setzt oder loescht das Passwort zur Laufzeit.
        Leeres Passwort = Schutz aufheben."""
        global AKTIVER_TOKEN
        d = self._body()
        if not isinstance(d.get("token"), str):
            return self._json(400, ergebnis(False, "Passwort-Feld fehlt"))
        AKTIVER_TOKEN = d["token"]
        if AKTIVER_TOKEN == "":
            return self._json(200, ergebnis(True, "Passwortschutz aufgehoben"))
        return self._json(200, ergebnis(True, "Passwort gespeichert"))

    def _test(self):
        url = (self._body().get("url") or "").strip()
        if not url.startswith("http://"):
            return self._fehler(400, "URL ungueltig (nur http://)")
        code, body, err = hole(url)
        if err:
            return self._json(200, {"ok": False, "httpStatus": code, "error": err,
                                    "preview": "", "fields": []})
        if code != 200:
            # Wie die Firmware: kein Erfolg heisst leere Vorschau, keine Feldliste und
            # eine Fehlermeldung. Der Mock lieferte hier frueher die Vorschau mit --
            # der Assistent haette Felder angeboten, die das Geraet nie sieht.
            return self._json(200, {"ok": False, "httpStatus": code,
                                    "error": "HTTP %d" % code, "preview": "", "fields": []})
        return self._json(200, {"ok": True, "httpStatus": code,
                                "preview": body[:1024], "fields": felder_aus(body)})

    def _wiederherstellen(self):
        """Wie handleSlotsRestore(): ganze Konfiguration in EINEM Zug, ein Schreibvorgang."""
        d = self._body()
        if not isinstance(d.get("slots"), list):
            return self._fehler(400, "Sicherung ohne Werte-Liste")
        # Wie configFromDoc in der Firmware: uebernehmen und klemmen, nicht ablehnen --
        # eine Sicherung kommt aus dem Geraet selbst und wurde beim Schreiben geprueft.
        neu = leere_konfiguration()
        for feld in ("rotateSec", "colorWarn", "colorAlarm", "helligkeit", "hellModus",
                     "hellUrl", "hellField", "hellSec", "hellAn", "hellAus",
                     "nachtAn", "nachtVon", "nachtBis", "nachtHelligkeit"):
            if feld in d:
                neu[feld] = d[feld]
        for i, l in enumerate(d.get("layout", [])[:MAX_PAGES]):
            neu["layout"][i] = int(l) if int(l) in (0, 1, 2) else 2
        for i, teil in enumerate(d.get("teilung", [])[:MAX_PAGES]):
            teil = int(teil)
            neu["teilung"][i] = teil if teil == 0 or 20 <= teil <= 80 else 0
        anzahl = 0
        for i, s in enumerate(d["slots"][:MAX_SLOTS]):
            if not isinstance(s, dict) or not str(s.get("url") or ""):
                continue
            eintrag = dict(neu["slots"][i])
            eintrag.update({k: v for k, v in s.items() if k in eintrag})
            neu["slots"][i] = eintrag
            anzahl += 1
        CONFIG.clear()
        CONFIG.update(neu)
        ABRUF_CACHE.clear()
        FEHLZAEHLER.clear()
        LETZTER_WERT.clear()
        return self._json(200, ergebnis(True, "Sicherung uebernommen", werte=anzahl))

    def _einstellungen(self):
        """Bildet settingsFromDoc der Firmware nach -- inklusive der Ablehnung.

        ERST pruefen, DANN zuweisen: Frueher wurde rotateSec schon uebernommen, waehrend
        eine spaetere Pruefung die Anfrage noch ablehnen konnte. Das Geraet arbeitet
        transaktional; ein Mock, der halb uebernimmt, verbirgt genau solche Fehler.
        """
        d = self._body()
        r = int(d.get("rotateSec", CONFIG["rotateSec"]))
        if r != 0 and not LIMITS["rotateMin"] <= r <= LIMITS["rotateMax"]:
            return self._fehler(400, "Wechselintervall nur 0 (aus) oder %d bis %d Sekunden"
                                % (LIMITS["rotateMin"], LIMITS["rotateMax"]))

        neu = list(CONFIG["layout"])
        for i, l in enumerate(d.get("layout", [])[:MAX_PAGES]):
            if int(l) not in (0, 1, 2):
                return self._fehler(400, "Unbekanntes Seitenlayout")
            neu[i] = int(l)

        # Gegen ALLE eingerichteten Werte pruefen -- auch gegen abgeschaltete: Sie halten
        # ihren Platz, genau wie in der Firmware.
        for s in CONFIG["slots"]:
            if s["url"] and s["pos"] > PLATZGRENZE[neu[s["page"] - 1]]:
                return self._fehler(
                    400, "Ein Wert liegt auf einem Platz, den das neue Layout nicht hat")

        neueTeilung = list(CONFIG["teilung"])
        for i, teil in enumerate(d.get("teilung", [])[:MAX_PAGES]):
            teil = int(teil)
            if teil != 0 and not 20 <= teil <= 80:
                return self._fehler(400, "Aufteilung nur 0 (automatisch) oder 20 bis 80 Prozent")
            neueTeilung[i] = teil

        warn = int(d.get("colorWarn", CONFIG["colorWarn"]))
        alarm = int(d.get("colorAlarm", CONFIG["colorAlarm"]))
        if not (0 <= warn <= 0xFFFF and 0 <= alarm <= 0xFFFF):
            return self._fehler(400, "Farbwert ausserhalb des Bereichs")

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
        # Immer pruefen, nicht nur bei aktivem Schalter -- so macht es die Firmware auch.
        if not LIMITS["hellSecMin"] <= hsec <= LIMITS["hellSecMax"]:
            return self._fehler(400, "Schalter-Intervall nur %d bis %d Sekunden"
                                % (LIMITS["hellSecMin"], LIMITS["hellSecMax"]))

        # ---- Ab hier wird uebernommen; ab hier kann nichts mehr scheitern. ----
        CONFIG["rotateSec"] = r
        CONFIG["layout"] = neu
        CONFIG["teilung"] = neueTeilung
        CONFIG["colorWarn"] = warn
        CONFIG["colorAlarm"] = alarm
        CONFIG["nachtVon"] = von
        CONFIG["nachtBis"] = bis
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
        return self._json(200, ergebnis(True, "Gespeichert"))

    def _slot_speichern(self):
        d = self._body()
        i = d.get("index")
        if not isinstance(i, int) or not 0 <= i < MAX_SLOTS:
            return self._fehler(400, "Slot-Nummer ausserhalb des Bereichs")
        # Wie das Geraet (config_codec.h, gleiche Reihenfolge, gleiche Meldungen):
        # Laengen in BYTES (UTF-8, Umlaute zaehlen doppelt); Beschriftung und Einheit nur
        # aus Zeichen, die das Display zeichnen kann (N6). Der Feldname wird nie gezeichnet
        # und muss nur ohne Steuerzeichen sein. Die Beschriftung darf leer bleiben (v0.2.6).
        # Waere der Mock hier laxer, liesse er Eingaben durch, die das Geraet ablehnt.
        url = str(d.get("url", ""))
        label = str(d.get("label", ""))
        feld = str(d.get("field", ""))
        einheit = str(d.get("unit", ""))
        if len(url.encode("utf-8")) > LIMITS["url"]:
            return self._fehler(400, "URL ist zu lang (max %d Zeichen)" % LIMITS["url"])
        if len(label.encode("utf-8")) > LIMITS["label"]:
            return self._fehler(400, "Beschriftung ist zu lang (max %d Zeichen, Umlaute zaehlen doppelt)"
                                % LIMITS["label"])
        if len(feld.encode("utf-8")) > LIMITS["field"]:
            return self._fehler(400, "Feldname ist zu lang (max %d Zeichen)" % LIMITS["field"])
        if len(einheit.encode("utf-8")) > LIMITS["unit"]:
            return self._fehler(400, "Einheit ist zu lang (max %d Zeichen, Umlaute zaehlen doppelt)"
                                % LIMITS["unit"])
        if not url.startswith("http://"):
            return self._fehler(400, "URL ungueltig (nur http://, max 127 Zeichen)")
        if label and not DISPLAY_ZEICHEN.match(label):
            return self._fehler(400, "Beschriftung ungueltig (zu lang oder Zeichen, das das Display nicht kennt)")
        if feld and not OHNE_STEUERZEICHEN.match(feld):
            return self._fehler(400, "Feldname ungueltig")
        if einheit and not DISPLAY_ZEICHEN.match(einheit):
            return self._fehler(400, "Einheit ungueltig (zu lang oder Zeichen, das das Display nicht kennt)")
        r = int(d.get("refreshSec") or 0)
        if not LIMITS["refreshMin"] <= r <= LIMITS["refreshMax"]:
            return self._fehler(400, "Intervall nur %d bis %d Sekunden"
                                % (LIMITS["refreshMin"], LIMITS["refreshMax"]))
        # Was die Firmware ebenfalls prueft, der Mock aber nicht nachbildete:
        # Nachkommastellen, Farbwert und ein negativer Rasterplatz.
        nk = int(d.get("decimals") or 0)
        if not 0 <= nk <= LIMITS["decimalsMax"]:
            return self._fehler(400, "Nachkommastellen nur 0 bis %d" % LIMITS["decimalsMax"])
        farbe = int(d.get("color", 0xFFFF))
        if not 0 <= farbe <= 0xFFFF:
            return self._fehler(400, "Farbwert ausserhalb des Bereichs")
        seite = int(d.get("page") or 0)
        if not 1 <= seite <= MAX_PAGES:
            return self._fehler(400, "Seite ausserhalb des Bereichs")
        grenze = PLATZGRENZE[CONFIG["layout"][seite - 1]]
        platz = int(d.get("pos") or 0)
        if platz < 0 or platz > grenze:
            return self._fehler(400, "Rasterplatz gibt es in diesem Seitenlayout nicht")
        anzeige = int(d.get("anzeige") or 0)
        if anzeige not in (0, 1, 2):
            return self._fehler(400, "Darstellungsart unbekannt")
        # Die drei Schriftstufen wie in der Firmware: je 1 bis 10, voneinander unabhaengig.
        # FEHLT ein Feld, gilt der Standardwert -- genau wie leseBereich() in der Firmware.
        # Der Mock lehnte fehlende Felder frueher ab und war damit STRENGER als das Geraet:
        # eine Sicherung aus einer aelteren Fassung liess sich gegen ihn nicht einspielen,
        # gegen das Geraet schon.
        for feld, meldung, standard in (("wertSize", "Werts", STD_WERT_SIZE),
                                        ("labelSize", "Beschriftung", STD_LABEL_SIZE),
                                        ("unitSize", "Einheit", STD_UNIT_SIZE)):
            roh = d.get(feld)
            stufe = standard if roh in (None, "") else int(roh)
            if not LIMITS["textStufeMin"] <= stufe <= LIMITS["textStufeMax"]:
                return self._fehler(
                    400, "Schriftstufe der %s ausserhalb des Bereichs" % meldung
                    if feld != "wertSize" else "Schriftstufe des Werts ausserhalb des Bereichs")
            d[feld] = stufe
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
        return self._json(200, ergebnis(True, "Gespeichert"))

    def do_DELETE(self):
        p = self.path.split("?")[0]
        if p.startswith("/api/v1/") and self._tokenFehlt():
            return None
        if p.startswith("/api/v1/slots/"):
            rest = p[len("/api/v1/slots/"):]
            # Wie UriBraces in der Firmware: EIN Pfadstueck, und der Handler liest daraus
            # eine Zahl. "/slots/1/2" oder "/slots/007" trifft dort keine Route.
            if not rest.isdigit() or not 1 <= len(rest) <= 2:
                return self._fehler(400, "Slot-Nummer ungueltig")
            i = int(rest)
            if not 0 <= i < MAX_SLOTS:
                return self._fehler(400, "Slot-Nummer ausserhalb des Bereichs")
            CONFIG["slots"][i] = leerer_slot()
            FEHLZAEHLER.pop(i, None)
            return self._json(200, ergebnis(True, "Gespeichert"))
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
