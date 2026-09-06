# SPDX-License-Identifier: GPL-3.0-or-later
"""Schreibt eine inhaltsgebundene Kennung des Dateisystem-Abbilds nach data/web/BUILD.

Warum: Die Cache-Kennung jeder ausgelieferten Datei bestand aus Firmware-Version und
Dateigroesse. Zwei Folgen hatte das. Erstens galt eine gleich grosse, aber geaenderte
Datei ohne Versionssprung als unveraendert -- der Browser bekam ein 304 auf alten
Inhalt. Zweitens verwarf jede neue Firmware die komplette Oberflaeche im Browser, auch
wenn sich dort nichts geaendert hatte.

Die Kennung hier haengt am INHALT: SHA-256 ueber alle Dateien unter data/ (Pfad und
Inhalt, in fester Reihenfolge), davon die ersten acht Hex-Stellen. Gleiche Oberflaeche
heisst gleiche Kennung, eine geaenderte Datei heisst eine neue.

Die Datei selbst bleibt aussen vor -- sonst haenge ihre Kennung von ihrem eigenen
Inhalt ab. Ihre Laenge ist fest (8 Zeichen), damit sie das Abbild nicht verschiebt.
"""
Import("env")  # noqa: F821
import hashlib
import os

KENNUNG_DATEI = os.path.join("data", "web", "BUILD")


def baue_kennung(datenverzeichnis, ausnahme):
    hasher = hashlib.sha256()
    for wurzel, verzeichnisse, dateien in sorted(os.walk(datenverzeichnis)):
        verzeichnisse.sort()
        for name in sorted(dateien):
            pfad = os.path.join(wurzel, name)
            if os.path.abspath(pfad) == os.path.abspath(ausnahme):
                continue
            rel = os.path.relpath(pfad, datenverzeichnis).replace(os.sep, "/")
            hasher.update(rel.encode("utf-8"))
            with open(pfad, "rb") as f:
                for block in iter(lambda: f.read(65536), b""):
                    hasher.update(block)
    return hasher.hexdigest()[:8]


projekt = env.get("PROJECT_DIR")  # noqa: F821
daten = os.path.join(projekt, "data")
ziel = os.path.join(projekt, KENNUNG_DATEI)

if os.path.isdir(daten):
    kennung = baue_kennung(daten, ziel)
    alt = None
    if os.path.exists(ziel):
        with open(ziel, encoding="utf-8") as f:
            alt = f.read().strip()
    if alt != kennung:
        os.makedirs(os.path.dirname(ziel), exist_ok=True)
        with open(ziel, "w", encoding="utf-8") as f:
            f.write(kennung)
    print("[fs_build_id] Dateisystem-Kennung: %s" % kennung)
