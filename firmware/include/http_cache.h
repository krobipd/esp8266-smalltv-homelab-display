// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Cache-Regeln der ausgelieferten Oberflaeche -- bewusst OHNE Arduino-Abhaengigkeit,
// damit sie am Host testbar sind (tests/host/test_httpcache.cpp). Die Firmware-Schale
// (Webserver.cpp) haengt nur noch die Kopfzeilen an, gerechnet wird hier.
//
// Anlass 02.09.2026: Jede Datei ging mit "public, max-age=86400" und ohne Kennung raus.
// Nach einem Dateisystem-Update hielt der Browser bis zu 24 Stunden an alten Dateien
// fest und mischte neues HTML mit altem JavaScript -- die Oberflaeche war unbedienbar.
#include <cstddef>
#include <cstdio>
#include <cstring>

// "no-cache" heisst NICHT "nicht speichern", sondern "vor jeder Benutzung rueckfragen".
// Genau das braucht ein Geraet, dessen Oberflaeche als Ganzes ausgetauscht wird.
static const char CACHE_RUECKFRAGEN[] = "no-cache";
// Platz fuer "\"vX.Y.Z-123456\"" mit Reserve.
static const size_t ETAG_BUF_SIZE = 48;

// Kennung einer Datei: Firmware-Version + Dateigroesse. Beides ist ohne Lesen der Datei
// zu haben -- ein Inhalts-Hash muesste bei jeder Anfrage die ganze Datei durchlesen
// (allein Alpine sind 45 KB). Die Version wechselt mit jedem Update, damit gilt danach
// jede im Browser liegende Datei als veraltet.
// Bewusste Grenze: gleiche Version UND gleiche Dateigroesse gelten als dieselbe Datei --
// wer beim Entwickeln eine neue Oberflaeche flasht, zieht die VERSION mit.
inline void baueEtag(char* out, size_t outSize, const char* version, size_t dateiGroesse) {
    snprintf(out, outSize, "\"%s-%u\"", version, (unsigned)dateiGroesse);
}

// Platz fuer die komplette Nicht-geaendert-Antwort.
static const size_t KOPF304_BUF_SIZE = 128;

// Die Antwort "nicht geaendert", vollstaendig und von Hand.
//
// Warum nicht send(304) der Basis: Die haengt an JEDE Antwort "Content-Type: text/html"
// und "Content-Length: 0". In einem 304 ist beides schaedlich. RFC 7230 verbietet eine
// Laenge, die von der einer 200-Antwort abweichen wuerde, und der Browser uebernimmt die
// Kopfzeilen eines 304 in seinen Speicher -- er merkt sich also "diese Datei ist leer
// und ist HTML". Safari hat daraufhin am 02.09.2026 das Menue verschluckt (header.html
// wird per fetch() nachgeladen und kam leer an), Firefox hat es ignoriert.
// Ein 304 traegt nur, was den gespeicherten Stand bestaetigt.
inline void baue304Kopf(char* out, size_t outSize, const char* etag) {
    snprintf(out, outSize,
             "HTTP/1.1 304 Not Modified\r\n"
             "Cache-Control: %s\r\n"
             "ETag: %s\r\n"
             "Connection: close\r\n"
             "\r\n",
             CACHE_RUECKFRAGEN, etag);
}

// Passt eine der Kennungen aus "If-None-Match" zu unserer?
// Der Kopf darf mehrere Kennungen tragen ("a", "b") und jede darf als schwach markiert
// sein (W/"a"). Ein strikter Zeichenkettenvergleich wuerde dann nie passen: die Seite
// bliebe zwar richtig, aber jede Anfrage schickte die Datei erneut -- die 304-Ersparnis
// waere still weg.
inline bool etagPasst(const char* kopfzeile, const char* etag) {
    if (kopfzeile == nullptr || etag == nullptr || etag[0] == 0) return false;
    const size_t etagLen = strlen(etag);
    const char* p = kopfzeile;
    while (*p != 0) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (*p == 0) break;
        // "*" heisst: jede vorhandene Fassung genuegt.
        if (*p == '*') return true;
        const char* ende = p;
        while (*ende != 0 && *ende != ',') ende++;
        const char* stop = ende;
        while (stop > p && (*(stop - 1) == ' ' || *(stop - 1) == '\t')) stop--;
        const char* wert = p;
        if ((size_t)(stop - wert) > 2 && wert[0] == 'W' && wert[1] == '/') wert += 2;
        const size_t len = (size_t)(stop - wert);
        if (len == etagLen && strncmp(wert, etag, len) == 0) return true;
        p = (*ende == ',') ? ende + 1 : ende;
    }
    return false;
}
