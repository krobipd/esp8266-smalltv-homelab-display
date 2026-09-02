// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Atomares Schreiben eines JSON-Dokuments nach LittleFS: erst vollstaendig in eine
// Temporaerdatei, dann rename -- LittleFS ersetzt das Ziel in einem Schritt. Ein Reset
// mitten im Schreiben zerstoert damit hoechstens die Temporaerdatei, nie die Zieldatei.
//
// EINE Autoritaet fuer SlotStore UND ConfigManager: Als das Muster zweimal von Hand
// existierte, fehlte einer Kopie bereits die Vollstaendigkeitspruefung -- genau die
// Drift, gegen die das Muster schuetzen soll.
#include <ArduinoJson.h>
#include <LittleFS.h>

inline bool jsonAtomarSchreiben(const char* pfad, const JsonDocument& doc) {
    const String tmp = String(pfad) + ".tmp";
    File f = LittleFS.open(tmp.c_str(), "w");
    if (!f) {
        return false;
    }

    const size_t erwartet = measureJson(doc);
    const size_t geschrieben = serializeJson(doc, f);
    f.close();

    // Vollstaendigkeit pruefen, nicht nur "irgendwas geschrieben": Laeuft das
    // Dateisystem mitten im Schreiben voll, ist geschrieben > 0 und trotzdem
    // kaputtes JSON auf der Platte.
    if (geschrieben == 0 || geschrieben != erwartet || !LittleFS.rename(tmp.c_str(), pfad)) {
        LittleFS.remove(tmp.c_str());
        return false;
    }
    return true;
}
