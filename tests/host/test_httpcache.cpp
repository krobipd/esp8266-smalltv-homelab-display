// SPDX-License-Identifier: GPL-3.0-or-later
// Host-Unit-Tests der Cache-Regeln der Oberflaeche. Laufen ohne Geraet und ohne Arduino:
//   c++ -std=c++17 smalltv/tests/host/test_httpcache.cpp -o /tmp/smalltv-httpcache
// Warum eigene Tests: Der Verhaltenstest tests/web/test_cache.mjs laeuft gegen den MOCK.
// Der beweist, dass der Mock stimmt -- nicht, dass die Firmware dasselbe tut. Diese Datei
// prueft genau die Logik, die auf dem Geraet laeuft.
#include "pruefe.h"
#include <cstdio>
#include <cstring>
#include "../../firmware/include/http_cache.h"

int main() {
    // --- Kennung: Version + Dateigroesse, in doppelten Anfuehrungszeichen ---
    char etag[ETAG_BUF_SIZE];
    baueEtag(etag, sizeof(etag), "v0.2.3", 21623);
    PRUEFE(strcmp(etag, "\"v0.2.3-21623\"") == 0);

    // Verschiedene Dateien -> verschiedene Kennungen (sonst gilt nach einem Update
    // die falsche Datei als bekannt).
    char andere[ETAG_BUF_SIZE];
    baueEtag(andere, sizeof(andere), "v0.2.3", 8848);
    PRUEFE(strcmp(etag, andere) != 0);

    // Neue Version -> neue Kennung, auch bei gleicher Groesse. Das ist der ganze Zweck.
    char neu[ETAG_BUF_SIZE];
    baueEtag(neu, sizeof(neu), "v0.2.4", 21623);
    PRUEFE(strcmp(etag, neu) != 0);

    // --- Abgleich mit dem, was der Browser zurueckschickt ---
    PRUEFE(etagPasst("\"v0.2.3-21623\"", etag));          // Normalfall
    PRUEFE(etagPasst("W/\"v0.2.3-21623\"", etag));        // als schwach markiert
    PRUEFE(etagPasst(" \"v0.2.3-21623\" ", etag));        // mit Leerzeichen
    PRUEFE(etagPasst("\"alt-1\", \"v0.2.3-21623\"", etag));   // mehrere Kennungen
    PRUEFE(etagPasst("\"alt-1\", W/\"v0.2.3-21623\"", etag));
    PRUEFE(etagPasst("*", etag));                          // jede Fassung genuegt

    // Nichts davon darf passen -- sonst bekaeme der Browser ein 304 fuer eine Datei,
    // die er gar nicht hat, und die Seite bliebe leer.
    PRUEFE(!etagPasst("\"v0.2.3-2162\"", etag));           // kuerzer, sonst gleich
    PRUEFE(!etagPasst("\"v0.2.3-216230\"", etag));         // laenger, sonst gleich
    PRUEFE(!etagPasst("\"v0.2.2-21623\"", etag));          // andere Version
    PRUEFE(!etagPasst("", etag));                          // Kopfzeile fehlt
    PRUEFE(!etagPasst(nullptr, etag));
    PRUEFE(!etagPasst("\"v0.2.3-21623\"", ""));            // keine eigene Kennung

    // --- Die Nicht-geaendert-Antwort ---
    char kopf[KOPF304_BUF_SIZE];
    baue304Kopf(kopf, sizeof(kopf), etag);
    PRUEFE(strncmp(kopf, "HTTP/1.1 304 Not Modified\r\n", 27) == 0);
    PRUEFE(strstr(kopf, "ETag: \"v0.2.3-21623\"\r\n") != nullptr);
    PRUEFE(strstr(kopf, "Cache-Control: no-cache\r\n") != nullptr);
    // Diese beiden duerfen NICHT drinstehen: Der Browser uebernimmt die Kopfzeilen
    // eines 304 in seinen Speicher -- "Laenge 0" und "text/html" wuerden die Datei
    // dort verderben. Genau daran ist Safari am 02.09.2026 gescheitert.
    PRUEFE(strstr(kopf, "Content-Length") == nullptr);
    PRUEFE(strstr(kopf, "Content-Type") == nullptr);
    // Sauber abgeschlossen, sonst wartet der Browser auf mehr.
    PRUEFE(strlen(kopf) >= 4 && strcmp(kopf + strlen(kopf) - 4, "\r\n\r\n") == 0);

    printf("OK\n");
    return 0;
}
