#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prueft, dass der Mock im Geraetekasten dieselben Felder liefert wie die Firmware.

Die uebrigen Tests pruefen jeweils die Felder, die sie selbst kennen -- ein neues Feld
in handleGeraet() faellt ihnen deshalb nicht auf, und der Mock waere stillschweigend
aermer als das Geraet. Genau das ist bei den Diagnosefeldern von v0.5.5 passiert: Die
Kette blieb gruen, obwohl der Mock resetGrund, flashEcht und dateien nicht kannte.

Hier wird deshalb nicht die Feldliste gepflegt, sondern aus dem Firmware-Quelltext
GELESEN: alles, was handleGeraet() in doc[...] schreibt, muss der Mock ebenfalls
schicken. Eine Testumgebung, die weniger kann als das echte System, verbirgt genau die
Fehler, fuer die sie da ist.
"""
import json
import os
import re
import sys
import urllib.request

BASIS = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
QUELLE = os.path.join(BASIS, "firmware", "src", "slots", "SlotApi.cpp")
BASIS_URL = os.environ.get("MOCK_URL", "http://127.0.0.1:8099")

# setzeErgebnis() setzt beide -- sie stehen nicht als doc[...] im Rumpf.
AUS_ERGEBNIS = {"ok", "message"}


def pruefe(bedingung, text):
    if not bedingung:
        print("FEHLGESCHLAGEN: " + text, file=sys.stderr)
        sys.exit(1)


def rumpf_von(quelltext, funktion):
    """Schneidet den Rumpf einer Funktion heraus (bis zur schliessenden Klammer in Spalte 0)."""
    start = quelltext.index("void " + funktion + "(")
    ende = quelltext.index("\n}\n", start)
    return quelltext[start:ende]


def felder_von(funktion):
    """Alle doc[...]-Schluessel einer Handler-Funktion, aus dem Quelltext gelesen."""
    with open(QUELLE, encoding="utf-8") as f:
        rumpf = rumpf_von(f.read(), funktion)
    return set(re.findall(r'doc\["([A-Za-z0-9_]+)"\]', rumpf)) | AUS_ERGEBNIS, rumpf


def hole(pfad):
    with urllib.request.urlopen(BASIS_URL + pfad, timeout=5) as antwort:  # noqa: S310
        return json.load(antwort)


def pruefe_endpunkt(funktion, pfad, mindestens):
    oben, rumpf = felder_von(funktion)
    pruefe(len(oben) >= mindestens,
           "Zu wenige Felder in %s gefunden (%d) — Test anpassen" % (funktion, len(oben)))
    mock = hole(pfad)
    fehlend = sorted(oben - set(mock))
    pruefe(not fehlend,
           "Der Mock liefert diese Felder von %s() NICHT: %s" % (funktion, ", ".join(fehlend)))
    # Andersherum ist ein zusaetzliches Mock-Feld kein Fehler (die Oberflaeche darf mehr
    # bekommen, als sie braucht) -- aber es wird genannt, damit es niemanden ueberrascht.
    zusatz = sorted(set(mock) - oben)
    if zusatz:
        print("  Hinweis: nur im Mock (%s): %s" % (pfad, ", ".join(zusatz)))
    return oben, rumpf, mock


def main():
    # --- /api/v1/geraet ---
    oben, rumpf, mock = pruefe_endpunkt("handleGeraet", "/api/v1/geraet", 15)
    # Die Unterschluessel von "dateien" stehen als {"name", "/pfad"} in der Pruefliste.
    dateien = set(re.findall(r'\{"([A-Za-z0-9_]+)",\s*"/[^"]+"\}', rumpf))
    pruefe(dateien, "Die Dateiliste aus handleGeraet() liess sich nicht lesen — Test anpassen")
    pruefe(isinstance(mock.get("dateien"), dict), "Feld 'dateien' fehlt im Mock oder ist kein Objekt")
    fehlendeDateien = sorted(dateien - set(mock["dateien"]))
    pruefe(not fehlendeDateien,
           "Der Mock prueft diese Dateien NICHT: " + ", ".join(fehlendeDateien))

    # --- /api/v1/dateien (Verzeichnis, seit v0.5.6) ---
    obenD, _, mockD = pruefe_endpunkt("handleDateien", "/api/v1/dateien", 6)
    # Die Listen tragen eine feste Form; ohne sie waere die Antwort nicht auswertbar.
    pruefe(isinstance(mockD.get("dateien"), list), "'dateien' muss eine Liste sein")
    pruefe(isinstance(mockD.get("ordner"), list), "'ordner' muss eine Liste sein")
    for eintrag in mockD["dateien"]:
        pruefe(set(eintrag) == {"name", "groesse"},
               "Dateieintrag hat die falsche Form: " + json.dumps(eintrag))
    for eintrag in mockD["ordner"]:
        pruefe(set(eintrag) == {"name", "anzahl", "groesse"},
               "Ordnereintrag hat die falsche Form: " + json.dumps(eintrag))
    # Die Rechnung muss aufgehen, und zwar gegen die GERUNDETE Summe: Gegen die rohen
    # Bytes gerechnet bestand der Ueberhang zu ueber 200 KB aus blosser Blockaufrundung
    # und meldete am echten Geraet verwaiste Daten, die es nicht gab.
    pruefe(mockD["summeBloecke"] >= mockD["summe"],
           "Die gerundete Summe kann nicht kleiner sein als die rohe")
    pruefe(mockD["ueberhang"] == max(0, mockD["fsBelegt"] - mockD["summeBloecke"]),
           "Der Ueberhang passt nicht zu belegt minus gerundeter Summe")

    print("Geraet-Paritaet: %dx OK (%d + %d Felder, %d Dateien — aus der Firmware gelesen)"
          % (len(oben) + len(obenD) + len(dateien), len(oben), len(obenD), len(dateien)))


if __name__ == "__main__":
    main()
