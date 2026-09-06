#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prueft die Kennung des Dateisystems (scripts/fs_build_id.py, Befund N20).

Sie ersetzt die Firmware-Version in der Cache-Kennung jeder ausgelieferten Datei. Damit
das trägt, muss sie genau zwei Dinge tun: sich aendern, sobald sich irgendeine Datei der
Oberflaeche aendert -- und gleich bleiben, wenn sich nichts aendert. Beides wird hier an
einem Wegwerf-Verzeichnis geprueft, nicht behauptet.
"""
import hashlib
import os
import shutil
import sys
import tempfile

BASIS = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def lade_baue_kennung():
    """Holt baue_kennung() aus dem Build-Skript, ohne PlatformIOs Import("env")."""
    pfad = os.path.join(BASIS, "firmware", "scripts", "fs_build_id.py")
    with open(pfad, encoding="utf-8") as f:
        quelle = f.read()
    # Die erste Zeile des Rumpfs ist PlatformIOs Import("env") -- die gibt es hier nicht.
    quelle = quelle.replace('Import("env")  # noqa: F821', "")
    quelle = quelle.split("projekt = env.get")[0]
    raum = {"__name__": "fs_build_id"}
    exec(compile(quelle, pfad, "exec"), raum)  # noqa: S102
    return raum["baue_kennung"]


def pruefe(bedingung, text):
    if not bedingung:
        print("FEHLGESCHLAGEN: " + text, file=sys.stderr)
        sys.exit(1)


def main():
    baue_kennung = lade_baue_kennung()
    quelle = os.path.join(BASIS, "firmware", "data")
    pruefe(os.path.isdir(quelle), "firmware/data fehlt")

    with tempfile.TemporaryDirectory() as tmp:
        a = os.path.join(tmp, "a")
        shutil.copytree(quelle, a)
        ausnahme = os.path.join(a, "web", "BUILD")

        k1 = baue_kennung(a, ausnahme)
        pruefe(len(k1) == 8 and all(c in "0123456789abcdef" for c in k1),
               "Kennung ist keine acht Hex-Stellen: %r" % k1)

        # Zweimal ueber denselben Inhalt: dieselbe Kennung.
        pruefe(baue_kennung(a, ausnahme) == k1, "Kennung ist nicht wiederholbar")

        # Die Kennungsdatei selbst darf nicht eingehen -- sonst haengt sie an sich selbst.
        os.makedirs(os.path.dirname(ausnahme), exist_ok=True)
        with open(ausnahme, "w", encoding="utf-8") as f:
            f.write("deadbeef")
        pruefe(baue_kennung(a, ausnahme) == k1, "Die Kennungsdatei geht in die Kennung ein")

        # Eine geaenderte Datei ergibt eine andere Kennung -- auch bei GLEICHER Groesse.
        ziel = os.path.join(a, "web", "js", "utils.js")
        pruefe(os.path.exists(ziel), "utils.js fehlt in der Kopie")
        vorher = os.path.getsize(ziel)
        with open(ziel, "rb") as f:
            roh = f.read()
        # Ein Byte tauschen, die Laenge lassen: Genau dieser Fall (gleich gross, anderer
        # Inhalt) lief frueher als 304 auf den alten Inhalt hinaus.
        getauscht = roh.replace(b"function apiFetch", b"function apiFetcH", 1)
        with open(ziel, "wb") as f:
            f.write(getauscht)
        pruefe(os.path.getsize(ziel) == vorher,
               "Testvoraussetzung verletzt: die Groesse hat sich geaendert")
        k2 = baue_kennung(a, ausnahme)
        pruefe(k2 != k1, "Gleich grosse, aber geaenderte Datei ergibt dieselbe Kennung (%s)" % k1)

        # Ein neuer Dateiname ergibt ebenfalls eine andere Kennung.
        with open(os.path.join(a, "web", "neu.txt"), "w", encoding="utf-8") as f:
            f.write("x")
        pruefe(baue_kennung(a, ausnahme) != k2, "Neue Datei ergibt dieselbe Kennung")

    print("Dateisystem-Kennung: 6x OK")


if __name__ == "__main__":
    main()
