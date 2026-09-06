// SPDX-License-Identifier: GPL-3.0-or-later
// Host-Unit-Tests der Wert-Extraktion. Uebersetzen gegen die von PlatformIO geholte
// ArduinoJson-Version, damit hier genau das getestet wird, was spaeter auf dem Geraet laeuft:
//   c++ -std=c++17 -I smalltv/firmware/.pio/libdeps/esp12e/ArduinoJson/src
//       smalltv/tests/host/test_extract.cpp -o /tmp/smalltv-extract && /tmp/smalltv-extract
#include "pruefe.h"
#include <cstdio>
#include <cstring>
#include "../../firmware/include/value_extract.h"

int main() {
    char out[64];
    char err[64];

    // --- Leeres Feld: die ganze Antwort IST der Wert (der /plain-Fall) ---
    PRUEFE(extractValue("1234.5", "", out, sizeof(out), err, sizeof(err)));
    PRUEFE(strcmp(out, "1234.5") == 0);
    PRUEFE(extractValue("  42  ", "", out, sizeof(out), err, sizeof(err)));
    PRUEFE(strcmp(out, "42") == 0);  // getrimmt

    // --- Feld gesetzt: genau dieses Feld aus dem JSON ---
    const char* body = "{\"val\":1234.5,\"ack\":true,\"ts\":1756200000000}";
    PRUEFE(extractValue(body, "val", out, sizeof(out), err, sizeof(err)));
    PRUEFE(strcmp(out, "1234.5") == 0);

    // Zeichenketten und Wahrheitswerte kommen als Text heraus, ohne JSON-Anfuehrungszeichen
    PRUEFE(extractValue("{\"state\":\"on\"}", "state", out, sizeof(out), err, sizeof(err)));
    PRUEFE(strcmp(out, "on") == 0);
    PRUEFE(extractValue("{\"ack\":true}", "ack", out, sizeof(out), err, sizeof(err)));
    PRUEFE(strcmp(out, "true") == 0);

    // Ganzzahlen bleiben ganzzahlig (kein "42.00")
    PRUEFE(extractValue("{\"val\":42}", "val", out, sizeof(out), err, sizeof(err)));
    PRUEFE(strcmp(out, "42") == 0);

    // --- Fehlerfaelle: NIE ein stiller Nullwert, immer false + Meldung ---
    err[0] = 0;
    PRUEFE(!extractValue(body, "gibtsnicht", out, sizeof(out), err, sizeof(err)));
    PRUEFE(err[0] != 0);

    err[0] = 0;
    PRUEFE(!extractValue("{kaputt", "val", out, sizeof(out), err, sizeof(err)));
    PRUEFE(err[0] != 0);

    err[0] = 0;
    PRUEFE(!extractValue("", "", out, sizeof(out), err, sizeof(err)));  // leere Antwort
    PRUEFE(err[0] != 0);

    // Verschachteltes Feld wird NICHT unterstuetzt (kein JSONPath) -> sauberer Fehler
    err[0] = 0;
    PRUEFE(!extractValue("{\"a\":{\"b\":1}}", "a.b", out, sizeof(out), err, sizeof(err)));
    PRUEFE(err[0] != 0);

    // Ein Objekt als Wert ist kein anzeigbarer Wert
    err[0] = 0;
    PRUEFE(!extractValue("{\"a\":{\"b\":1}}", "a", out, sizeof(out), err, sizeof(err)));
    PRUEFE(err[0] != 0);

    printf("OK\n");
    return 0;
}
