#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pruefungen nach einem Update -- ueber die Schnittstelle, ohne Hand am Geraet.

    python3 tools/abnahme.py <adresse> [--passwort P] [--messen] [--zeit] [--drehung]
                             [--werte] [--vorher DATEI] [--speichern DATEI]
                             [--wlan] [--ota FIRMWARE.bin]

Ohne Schalter: nur Erreichbarkeit und Version. Jede Probe meldet OK oder FEHLER mit
Zahlen; Exit 1, sobald eine Probe scheitert.

Sicherheitsregeln (das Skript prueft die Version aus der Cache-Kennung und verweigert
sonst):
  --wlan  erst ab Firmware v0.3.0 -- vorher bliebe das Geraet im AP-Modus haengen.
  --ota   erst ab Firmware v0.3.2, und immer mit einem GUELTIGEN Abbild: Schluege die
          Pruefsummen-Kontrolle fehl, wuerde dieselbe Firmware erneut geflasht, kein Schaden.
"""
import argparse
import json
import re
import sys
import time
import urllib.error
import urllib.request
import uuid

FEHLER = 0


def melde(ok, text):
    global FEHLER
    print(("OK      " if ok else "FEHLER  ") + text)
    if not ok:
        FEHLER += 1


class Geraet:
    def __init__(self, adresse, passwort):
        self.basis = "http://" + adresse.rstrip("/")
        self.kopf = {"Connection": "close"}
        if passwort:
            self.kopf["Authorization"] = "Bearer " + passwort

    def _anfrage(self, pfad, daten=None, kopf=None, timeout=10):
        koepfe = dict(self.kopf)
        if kopf:
            koepfe.update(kopf)
        if daten is not None and not isinstance(daten, bytes):
            daten = json.dumps(daten).encode()
            koepfe["Content-Type"] = "application/json"
        req = urllib.request.Request(self.basis + pfad, data=daten, headers=koepfe,
                                     method="POST" if daten is not None else "GET")
        t0 = time.perf_counter()
        with urllib.request.urlopen(req, timeout=timeout) as r:
            body = r.read()
            return time.perf_counter() - t0, r.status, dict(r.headers), body

    def hole(self, pfad, timeout=10):
        dauer, status, _, body = self._anfrage(pfad, timeout=timeout)
        return dauer, status, json.loads(body) if body.strip().startswith(b"{") or body.strip().startswith(b"[") else body

    def sende(self, pfad, obj, timeout=15):
        dauer, status, _, body = self._anfrage(pfad, daten=obj, timeout=timeout)
        return dauer, status, json.loads(body) if body.strip().startswith(b"{") else body

    def version(self):
        _, _, kopf, _ = self._anfrage("/slots.html", timeout=10)
        etag = kopf.get("ETag", "")
        m = re.match(r'"(v\d+\.\d+\.\d+)-\d+"', etag)
        return (m.group(1) if m else None), etag


def version_tupel(v):
    return tuple(int(x) for x in v.lstrip("v").split("."))


# ---------- Proben ----------

def probe_messen(g, sekunden=60, takt=0.3):
    """Antwortzeit der Statusabfrage. Vor v0.3.0: je Wertabruf ~2 s Stillstand (N1)."""
    zeiten, langsam = [], []
    start = time.perf_counter()
    while time.perf_counter() - start < sekunden:
        try:
            dauer, _, _ = g.hole("/api/v1/slots/status", timeout=8)
            zeiten.append(dauer)
            if dauer > 0.5:
                langsam.append((time.perf_counter() - start, dauer))
        except Exception as e:  # noqa: BLE001 -- jede Stoerung ist ein Befund
            langsam.append((time.perf_counter() - start, None))
        time.sleep(takt)
    zeiten.sort()
    if not zeiten:
        melde(False, "Messung: keine einzige Antwort")
        return
    median = zeiten[len(zeiten) // 2] * 1000
    p90 = zeiten[int(len(zeiten) * 0.9)] * 1000
    melde(not langsam, "Messung: %d Anfragen in %d s, Median %.0f ms, p90 %.0f ms, ueber 500 ms: %d"
          % (len(zeiten), sekunden, median, p90, len(langsam)))
    for t, d in langsam[:10]:
        print("          %6.1f s  %s" % (t, ("%.0f ms" % (d * 1000)) if d else "keine Antwort"))


def probe_zeit(g):
    """Zeitabgleich. Vor v0.3.1 meldete jeder Abgleich nach dem ersten sofort Erfolg (N2).

    Ein nicht erreichbarer Zeitserver (192.0.2.1, RFC 5737) darf NICHT binnen 3 s als
    "Synchronisiert" gelten. Danach wird der alte Server zurueckgestellt und ein neuer
    Abgleich erwartet, dessen Zeitstempel juenger ist als der Start der Probe.
    """
    _, _, alt = g.hole("/api/v1/ntp/config")
    server_alt = alt.get("ntp_server") or ""
    _, _, vorher = g.hole("/api/v1/ntp/status")
    start = int(time.time())
    g.sende("/api/v1/ntp/config", {"ntp_server": "192.0.2.1"})
    schein = False
    for _ in range(3):
        time.sleep(1)
        _, _, st = g.hole("/api/v1/ntp/status")
        if st.get("lastOk") and int(st.get("lastSyncTime") or 0) >= start:
            schein = True
            break
    melde(not schein, "Zeit: unerreichbarer Server binnen 3 s als Erfolg gemeldet" if schein
          else "Zeit: unerreichbarer Server wird nicht als Erfolg gemeldet")
    # Zuruecksetzen -- immer, auch wenn die Probe gescheitert ist.
    g.sende("/api/v1/ntp/config", {"ntp_server": server_alt} if server_alt else {"ntp_server": "pool.ntp.org"})
    ok = False
    frist = time.time() + 120
    while time.time() < frist:
        try:
            _, _, st = g.sende("/api/v1/ntp/sync", {}, timeout=20)
        except Exception:  # noqa: BLE001
            st = {}
        if st.get("status") == "ok" and int(st.get("lastSyncTime") or 0) >= start:
            ok = True
            break
        time.sleep(5)
    melde(ok, "Zeit: neuer Abgleich mit %s %s" % (server_alt or "pool.ntp.org",
          "gelungen, Zeitstempel frisch" if ok else "in 120 s nicht gelungen"))


def probe_drehung(g):
    """Drehung um eins weiter und zurueck; danach muss das Geraet antworten. Ob die
    Kacheln zurueckkommen (A2), sieht nur das Auge."""
    _, _, alt = g.hole("/api/v1/display/rotation")
    r = int(alt.get("rotation", 0))
    _, _, a = g.sende("/api/v1/display/rotation", {"rotation": (r + 1) % 8}, timeout=20)
    time.sleep(4)
    _, _, b = g.sende("/api/v1/display/rotation", {"rotation": r}, timeout=20)
    time.sleep(4)
    dauer, status, _ = g.hole("/api/v1/slots/status")
    melde(a.get("status") == "ok" and b.get("status") == "ok" and status == 200,
          "Drehung: %d -> %d -> %d, Geraet antwortet danach in %.0f ms" % (r, (r + 1) % 8, r, dauer * 1000))


def probe_werte(g, vorher_datei, speichern_datei):
    """Eingerichtete Werte zaehlen, optional gegen einen frueheren Stand vergleichen
    (beweist nach einem Dateisystem-Flash, dass nichts verloren ging, N15)."""
    _, _, cfg = g.hole("/api/v1/slots")
    belegt = [s for s in cfg.get("slots", []) if s.get("url")]
    kurz = {"anzahl": len(belegt), "labels": [s.get("label", "") for s in belegt],
            "rotateSec": cfg.get("rotateSec"), "layout": cfg.get("layout")}
    print("        Werte: %d eingerichtet, Wechsel %s s, Layout %s" % (kurz["anzahl"], kurz["rotateSec"], kurz["layout"]))
    if speichern_datei:
        with open(speichern_datei, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=1)
        print("        Stand gespeichert: " + speichern_datei)
    if vorher_datei:
        with open(vorher_datei, encoding="utf-8") as f:
            alt = json.load(f)
        alt_belegt = [s for s in alt.get("slots", []) if s.get("url")]
        gleich = (len(alt_belegt) == len(belegt)
                  and [s.get("label") for s in alt_belegt] == kurz["labels"]
                  and alt.get("rotateSec") == kurz["rotateSec"]
                  and alt.get("layout") == kurz["layout"])
        melde(gleich, "Werte: Stand gleich wie in %s" % vorher_datei if gleich
              else "Werte: Stand weicht ab von %s (vorher %d Werte)" % (vorher_datei, len(alt_belegt)))
    else:
        melde(True, "Werte: gelesen")


def probe_wlan(g, version):
    """Falsche SSID senden; das Geraet muss binnen 90 s unter der alten Adresse zurueck
    sein (A1). Vor v0.3.0 bliebe es im AP-Modus -- deshalb die Versionssperre."""
    if version is None or version_tupel(version) < (0, 3, 0):
        melde(False, "WLAN-Probe verweigert: braucht Firmware v0.3.0 oder neuer (gefunden %s)" % version)
        return
    _, _, vorher = g.hole("/api/v1/wifi/status")
    ssid = vorher.get("ssid", "")
    start = time.time()  # ab hier zaehlt die Zeit -- der Versuch selbst blockiert das Geraet bis 15 s
    try:
        g.sende("/api/v1/wifi/connect", {"ssid": "abnahme-gibt-es-nicht", "password": "x"}, timeout=25)
    except Exception:  # noqa: BLE001 -- die Antwort darf ausbleiben, das Netz ist gerade weg
        pass
    frist = start + 120
    zurueck = None
    while time.time() < frist:
        try:
            _, _, st = g.hole("/api/v1/wifi/status", timeout=5)
            if st.get("connected") and st.get("ssid") == ssid:
                zurueck = time.time() - start
                break
        except Exception:  # noqa: BLE001
            pass
        time.sleep(3)
    melde(zurueck is not None, "WLAN: nach falscher SSID %s" % (
        "in %.0f s unter der alten Adresse zurueck (%s)" % (zurueck, ssid) if zurueck is not None
        else "nach 120 s nicht zurueck -- Geraet vermutlich im AP-Modus, Stecker ziehen"))


def probe_ota(g, version, datei):
    """Gueltiges Firmware-Abbild mit FALSCHER Pruefsumme hochladen -- muss abgelehnt werden
    (A3). Vor v0.3.2 kennt das Geraet keine Pruefsumme und wuerde das Abbild flashen."""
    if version is None or version_tupel(version) < (0, 3, 2):
        melde(False, "OTA-Probe verweigert: braucht Firmware v0.3.2 oder neuer (gefunden %s)" % version)
        return
    with open(datei, "rb") as f:
        inhalt = f.read()
    if not inhalt or inhalt[0] != 0xE9:
        melde(False, "OTA: %s ist kein Firmware-Abbild (beginnt nicht mit 0xE9)" % datei)
        return
    grenze = "----abnahme" + uuid.uuid4().hex
    koerper = (("--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"abnahme.bin\"\r\n"
                "Content-Type: application/octet-stream\r\n\r\n") % grenze).encode() + inhalt + ("\r\n--%s--\r\n" % grenze).encode()
    kopf = {"Content-Type": "multipart/form-data; boundary=" + grenze, "X-Abbild-MD5": "0" * 32}
    try:
        _, _, _, body = g._anfrage("/api/v1/ota/fw", daten=koerper, kopf=kopf, timeout=120)
        antwort = json.loads(body)
    except Exception as e:  # noqa: BLE001
        melde(False, "OTA: keine auswertbare Antwort (%s)" % e)
        return
    status = str(antwort.get("status", "")).lower()
    meldung = str(antwort.get("message", ""))
    melde(status == "error" and "md5" in meldung.lower(),
          "OTA: falsche Pruefsumme -> status %r, %s" % (antwort.get("status"), meldung))
    time.sleep(3)
    v2, _ = g.version()
    melde(v2 == version, "OTA: Version danach %s (vorher %s)" % (v2, version))


def main():
    p = argparse.ArgumentParser(description="Pruefungen nach einem Update, ueber die Schnittstelle.")
    p.add_argument("adresse", help="IP oder Hostname des Displays")
    p.add_argument("--passwort", help="Passwortschutz, falls eingeschaltet")
    p.add_argument("--messen", action="store_true", help="Antwortzeiten 60 s lang")
    p.add_argument("--zeit", action="store_true", help="Zeitabgleich mit falschem und richtigem Server")
    p.add_argument("--drehung", action="store_true", help="Drehung weiter und zurueck")
    p.add_argument("--werte", action="store_true", help="eingerichtete Werte lesen")
    p.add_argument("--vorher", metavar="DATEI", help="mit diesem gespeicherten Stand vergleichen")
    p.add_argument("--speichern", metavar="DATEI", help="aktuellen Stand fuer spaeteren Vergleich sichern")
    p.add_argument("--wlan", action="store_true", help="Rueckkehr nach falscher SSID (ab v0.3.0)")
    p.add_argument("--ota", metavar="FIRMWARE.bin", help="Ablehnung bei falscher Pruefsumme (ab v0.3.2)")
    a = p.parse_args()

    g = Geraet(a.adresse, a.passwort)
    try:
        version, etag = g.version()
    except urllib.error.HTTPError as e:
        if e.code == 401:
            print("FEHLER  Passwortschutz aktiv -- mit --passwort aufrufen")
        else:
            print("FEHLER  Geraet antwortet mit HTTP %d" % e.code)
        sys.exit(1)
    except Exception as e:  # noqa: BLE001
        print("FEHLER  Geraet nicht erreichbar: %s" % e)
        sys.exit(1)
    melde(version is not None, "Geraet: Version %s (Kennung %s)" % (version, etag))

    if a.messen:
        probe_messen(g)
    if a.zeit:
        probe_zeit(g)
    if a.drehung:
        probe_drehung(g)
    if a.werte or a.vorher or a.speichern:
        probe_werte(g, a.vorher, a.speichern)
    if a.wlan:
        probe_wlan(g, version)
    if a.ota:
        probe_ota(g, version, a.ota)

    print("\n%s" % ("alle Proben OK" if FEHLER == 0 else "%d Probe(n) FEHLGESCHLAGEN" % FEHLER))
    sys.exit(1 if FEHLER else 0)


if __name__ == "__main__":
    main()
