// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Erkennt an den ersten Bytes, WAS fuer ein Abbild hochgeladen wird.
// Bewusst ohne Arduino-Abhaengigkeit, damit es am Host pruefbar ist
// (tests/host/test_abbild.cpp).
//
// Warum das Geraet selbst pruefen muss, obwohl die Weboberflaeche es schon tut:
// Der Weg (/api/v1/ota/fw oder /ota/fs) kommt aus dem Browser. Am 02.09.2026 hat eine
// VERALTETE Seite aus dem Browser-Cache -- die noch ein Auswahlfeld statt der Erkennung
// hatte -- ein Dateisystem-Abbild an den Firmware-Weg geschickt. Das war der harmlose
// Fall (es passt nicht hinein, Abbruch bei 79 %). Die andere Richtung ist die stille:
// Ein Firmware-Abbild ist klein genug fuer die Dateisystem-Partition, laeuft ohne
// Fehler durch und loescht die komplette Oberflaeche. Danach hilft nur der Rettungsweg.
#include <cstddef>
#include <cstdint>
#include <cstring>

enum AbbildArt { ABBILD_UNBEKANNT = 0, ABBILD_FIRMWARE = 1, ABBILD_DATEISYSTEM = 2 };

// firmware.bin beginnt mit 0xE9 (Abbild-Kennung des Chips),
// littlefs.bin traegt ab Byte 8 die Zeichen "littlefs".
// Dieselbe Regel wie in data/web/js/otaUploadHandler.js -- laufen die beiden
// auseinander, weist das Geraet ab, was die Oberflaeche anbietet.
inline AbbildArt erkenneAbbild(const uint8_t* daten, size_t laenge) {
    if (daten == nullptr || laenge < 16) {
        return ABBILD_UNBEKANNT;
    }
    if (daten[0] == 0xE9) {
        return ABBILD_FIRMWARE;
    }
    if (memcmp(daten + 8, "littlefs", 8) == 0) {
        return ABBILD_DATEISYSTEM;
    }
    return ABBILD_UNBEKANNT;
}

// Klartext statt "Not Enough Space" bei 79 Prozent. Sagt ausdruecklich, dass nichts
// geschrieben wurde -- sonst muss der Nutzer raten, ob das Geraet noch heil ist.
inline const char* abbildFehlertext(AbbildArt erkannt, AbbildArt erwartet) {
    if (erwartet == ABBILD_FIRMWARE && erkannt == ABBILD_DATEISYSTEM) {
        return "Das ist die Oberflaeche (Dateisystem), kein Firmware-Abbild. "
               "Es wurde nichts geschrieben.";
    }
    if (erwartet == ABBILD_DATEISYSTEM && erkannt == ABBILD_FIRMWARE) {
        return "Das ist ein Firmware-Abbild, nicht die Oberflaeche. "
               "Es wurde nichts geschrieben.";
    }
    return "Diese Datei ist weder ein Firmware- noch ein Dateisystem-Abbild. "
           "Es wurde nichts geschrieben.";
}
