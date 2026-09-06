// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Pure Logik des Homelab-Displays: keine Hardware, kein Netz, kein Arduino.
// Alles hier ist am Host unit-getestet (tests/host/test_util.cpp) -- deshalb
// duerfen in dieser Datei ausschliesslich Standard-C++-Header vorkommen.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const size_t URL_LEN = 128;
static const size_t FIELD_LEN = 32;
static const size_t LABEL_LEN = 24;
// 16 statt 8 (seit v0.2.6): Die Einheit wird auch als kleiner Zusatz benutzt --
// "% (Week)" sind schon 8 Zeichen und wurden vorher vom Eingabefeld abgeschnitten.
// Kosten: 8 Byte je Wert, bei 12 Werten also 96 Byte Arbeitsspeicher.
static const size_t UNIT_LEN = 16;
static const uint8_t MAX_SLOTS = 12;

// ---------- Grenzwerte an EINER Stelle ----------
//
// Dieselben Zahlen standen in der Firmware, in der Oberflaeche (als Zahl im Formular),
// im Mock und in den Fehlermeldungen -- vier Orte, die auseinanderlaufen konnten. Die
// Firmware liefert sie jetzt mit der Konfiguration aus (GET /slots, Feld "limits"), die
// Oberflaeche liest sie von dort, und ein Host-Test vergleicht sie mit dem Mock.
static const uint16_t REFRESH_SEC_MIN = 5;
static const uint16_t REFRESH_SEC_MAX = 3600;
static const uint16_t ROTATE_SEC_MIN = 3;      // 0 heisst "kein Wechsel" und ist erlaubt
static const uint16_t ROTATE_SEC_MAX = 3600;
static const uint8_t DECIMALS_MAX = 3;
static const uint16_t HELL_SEC_MIN = 5;
static const uint16_t HELL_SEC_MAX = 3600;
static const uint8_t MAX_PAGES = 4;
static const uint8_t STALE_FAILS = 3;

enum SlotState { STATE_OK = 0, STATE_WARN = 1, STATE_ALARM = 2 };

struct Thresholds {
    bool hasWarnAbove;
    float warnAbove;
    bool hasAlarmAbove;
    float alarmAbove;
    bool hasWarnBelow;
    float warnBelow;
    bool hasAlarmBelow;
    float alarmBelow;
};

// Alarm hat Vorrang vor Warnung; "auf der Schwelle" ist noch kein Ueberschreiten.
inline SlotState evalThresholds(float value, const Thresholds& t) {
    if (t.hasAlarmAbove && value > t.alarmAbove) return STATE_ALARM;
    if (t.hasAlarmBelow && value < t.alarmBelow) return STATE_ALARM;
    if (t.hasWarnAbove && value > t.warnAbove) return STATE_WARN;
    if (t.hasWarnBelow && value < t.warnBelow) return STATE_WARN;
    return STATE_OK;
}

// Delta-Arithmetik statt absoluter Vergleiche: ueberlebt den 32-bit-Wrap nach 49,7 Tagen.
inline bool elapsed(uint32_t now, uint32_t since, uint32_t interval) {
    return (uint32_t)(now - since) >= interval;
}

inline bool isStale(uint8_t failCount) { return failCount >= STALE_FAILS; }

// Veraltet ist ein Wert nach drei Fehlversuchen -- ODER wenn der letzte Erfolg zu lange
// zurueckliegt. Der zweite Teil greift, wenn gar keine Abrufe mehr stattfinden (WLAN weg,
// Geraet im AP-Modus): Der Fehlversuchszaehler bleibt dann bei null, und ohne Altersgrenze
// saehe ein stundenalter Wert fuer immer frisch aus. "Zu lange" = drei Abrufintervalle
// plus 30 s Karenz; alterSek 0 heisst "noch nie erfolgreich" und ist nicht veraltet,
// sondern "keine Daten" (eigene Darstellung).
inline bool istVeraltet(uint8_t fehlversuche, uint32_t alterSek, uint16_t refreshSec) {
    if (isStale(fehlversuche)) return true;
    if (alterSek == 0) return false;
    return alterSek > (uint32_t)refreshSec * (uint32_t)STALE_FAILS + 30U;
}

inline bool istLeerraum(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Nur vollstaendige Zahlen gelten. "12abc" ist ein Fehler, kein 12 -- sonst wird
// aus einer kaputten Antwort still ein plausibler Messwert.
//
// Bewusst OHNE strtof: dessen Unterbau (strtod) kostet auf dem ESP8266 rund 6 KB
// Programmspeicher, den wir nicht haben. Diese Fassung deckt genau das ab, was als
// Messwert vorkommt -- Vorzeichen, Ziffern, ein Dezimalpunkt. Exponentialschreibweise
// wird abgelehnt statt geraten.
inline bool parseNumber(const char* raw, float& out) {
    if (raw == nullptr) return false;
    const char* p = raw;
    while (istLeerraum(*p)) p++;

    bool negativ = false;
    if (*p == '+' || *p == '-') {
        negativ = (*p == '-');
        p++;
    }

    bool hatZiffer = false;
    uint64_t ganz = 0;
    while (*p >= '0' && *p <= '9') {
        if (ganz > 400000000ULL) return false;  // weit jenseits jedes Anzeigewerts
        ganz = ganz * 10U + (uint64_t)(*p - '0');
        hatZiffer = true;
        p++;
    }

    uint64_t nachkomma = 0;
    uint64_t teiler = 1;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            // Mehr als neun Nachkommastellen traegt ein float ohnehin nicht.
            if (teiler < 1000000000ULL) {
                nachkomma = nachkomma * 10U + (uint64_t)(*p - '0');
                teiler *= 10U;
            }
            hatZiffer = true;
            p++;
        }
    }

    if (!hatZiffer) return false;
    while (istLeerraum(*p)) p++;
    if (*p != 0) return false;  // Rest hinter der Zahl: ungueltig, nicht "so ungefaehr"

    // In EINEM Schritt teilen statt Ganzzahl und Bruch getrennt zu runden --
    // das trifft denselben Wert wie ein direkt geschriebenes Literal.
    const float v = (float)(ganz * teiler + nachkomma) / (float)teiler;
    out = negativ ? -v : v;
    return true;
}

inline void trimInto(char* out, size_t outSize, const char* raw) {
    if (outSize == 0) return;
    if (raw == nullptr) {
        out[0] = 0;
        return;
    }
    while (*raw == ' ' || *raw == '\t' || *raw == '\r' || *raw == '\n') raw++;
    size_t n = strlen(raw);
    while (n > 0) {
        char c = raw[n - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            n--;
        else
            break;
    }
    if (n > outSize - 1) n = outSize - 1;
    memcpy(out, raw, n);
    out[n] = 0;
}

// Fehlermeldung setzen. Liegt hier und nicht in value_extract.h, weil config_codec.h
// sie ebenfalls braucht -- zwei inline-Definitionen desselben Namens waeren ein Konflikt.
inline void setErr(char* errOut, size_t errSize, const char* msg) {
    if (errOut != nullptr && errSize > 0) snprintf(errOut, errSize, "%s", msg);
}

// Zahlen werden mit der gewuenschten Genauigkeit gesetzt, alles andere bleibt Text
// (z.B. "on"/"offline") -- damit sind Schaltzustaende ohne Sonderfall anzeigbar.
//
// Bewusst OHNE "%f": die Fliesskomma-Ausgabe von printf zieht rund 4 KB Programmspeicher
// nach sich. Gerundet und zusammengesetzt wird daher ganzzahlig.
inline void formatValue(char* out, size_t outSize, const char* raw, uint8_t decimals) {
    if (outSize == 0) return;

    float v = 0.0f;
    if (!parseNumber(raw, v)) {
        trimInto(out, outSize, raw);
        return;
    }

    if (decimals > 3) decimals = 3;
    static const uint32_t ZEHNERPOTENZ[4] = {1U, 10U, 100U, 1000U};
    const uint32_t faktor = ZEHNERPOTENZ[decimals];

    const bool negativ = v < 0.0f;
    const float betrag = negativ ? -v : v;

    // +0,5 vor dem Abschneiden ergibt kaufmaennisches Runden.
    const float skaliertF = betrag * (float)faktor + 0.5f;
    if (skaliertF >= 4000000000.0f) {
        // Jenseits dessen, was ein 32-bit-Zaehler traegt. Lieber ehrlich abkuerzen,
        // als eine ueberlaufene Zahl anzuzeigen.
        snprintf(out, outSize, "%s", "zu gross");
        return;
    }

    const uint32_t skaliert = (uint32_t)skaliertF;
    const uint32_t ganz = skaliert / faktor;
    const uint32_t rest = skaliert % faktor;
    const char* vorzeichen = negativ ? "-" : "";

    if (decimals == 0) {
        snprintf(out, outSize, "%s%lu", vorzeichen, (unsigned long)ganz);
    } else {
        snprintf(out, outSize, "%s%lu.%0*lu", vorzeichen, (unsigned long)ganz, (int)decimals,
                 (unsigned long)rest);
    }
}

// Gilt dieser Antworttext als "an"?
//
// Der Schaltwert kommt als Text und sieht je nach Quelle anders aus: true/false,
// 1/0, on/off. Zahlen zaehlen wie ueblich -- alles ungleich null ist "an".
// Unverstaendliches gilt bewusst als "aus": Ein Display, das wegen einer unklaren
// Antwort dauerhaft hell bleibt, faellt niemandem als Fehler auf.
inline bool istWahr(const char* roh) {
    if (roh == nullptr) return false;
    char t[16];
    trimInto(t, sizeof(t), roh);
    if (t[0] == 0) return false;

    for (char* p = t; *p != 0; p++) {
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    }

    if (strcmp(t, "true") == 0 || strcmp(t, "on") == 0 || strcmp(t, "an") == 0) return true;
    if (strcmp(t, "false") == 0 || strcmp(t, "off") == 0 || strcmp(t, "aus") == 0) return false;

    float v = 0.0f;
    if (parseNumber(t, v)) return v != 0.0f;
    return false;
}

// Liegt die Stunde im Fenster von..bis? Das Fenster darf ueber Mitternacht gehen --
// genau das ist der uebliche Fall (22 bis 7 Uhr) und zugleich der fehleranfaellige.
// Die Startstunde zaehlt dazu, die Endstunde nicht mehr; von == bis heisst "kein
// Fenster", nicht "immer".
inline bool imZeitfenster(uint8_t stunde, uint8_t von, uint8_t bis) {
    if (von == bis) return false;
    if (von < bis) return stunde >= von && stunde < bis;
    return stunde >= von || stunde < bis;  // ueber Mitternacht
}

/// Bringt einen Helligkeitswert in den zulaessigen Bereich 0..100.
inline uint8_t helligkeitKlemmen(int wert) {
    if (wert < 0) return 0;
    if (wert > 100) return 100;
    return (uint8_t)wert;
}

/// Groesster Abstand zwischen zwei Abrufversuchen eines dauerhaft toten Ziels.
static const uint32_t RUECKZUG_MAX_MS = 300000U;  // 5 Minuten

// Abstand bis zum naechsten Abrufversuch.
//
// Jeder Versuch blockiert das Geraet fuer die Dauer des Zeitlimits -- bei einem toten
// Ziel und kurzem Intervall stuende es sonst dauerhaft still. Deshalb verdoppelt sich
// der Abstand mit jedem Fehlschlag, bis zu einer Obergrenze. Sobald wieder eine Antwort
// kommt, setzt der Zaehler zurueck und es gilt sofort wieder das eingestellte Intervall.
inline uint32_t abrufIntervallMs(uint16_t refreshSec, uint8_t fehlversuche) {
    const uint32_t basis = (uint32_t)refreshSec * 1000U;
    if (fehlversuche == 0) {
        return basis;
    }
    // Ab 32 Verdopplungen liefe ein 32-bit-Wert ohnehin ueber -- vorher abkuerzen.
    if (fehlversuche >= 32) {
        return basis > RUECKZUG_MAX_MS ? basis : RUECKZUG_MAX_MS;
    }
    const uint32_t faktor = 1UL << fehlversuche;
    if (basis > RUECKZUG_MAX_MS / faktor) {
        // Das Produkt waere groesser als die Obergrenze (oder wuerde ueberlaufen).
        // Ein ohnehin laengeres Grundintervall wird dabei nie verkuerzt.
        return basis > RUECKZUG_MAX_MS ? basis : RUECKZUG_MAX_MS;
    }
    return basis * faktor;
}

/// Darstellungsart einer Kachel.
enum SlotAnzeige { ANZEIGE_ZAHL = 0, ANZEIGE_BALKEN = 1, ANZEIGE_BALKEN_ZAHL = 2 };

// Anteil eines Werts an der Balkenskala, immer zwischen 0 und 1.
// Werte ausserhalb der Skala werden gekappt statt ueberzulaufen -- ein Balken, der
// aus seinem Rahmen liefe, waere schlimmer als einer, der voll anschlaegt.
// Eine unbrauchbare Skala (max <= min) ergibt 0, nicht etwa eine Division durch null.
inline float barFraction(float wert, float min, float max) {
    if (!(max > min)) return 0.0f;
    if (wert <= min) return 0.0f;
    if (wert >= max) return 1.0f;
    return (wert - min) / (max - min);
}

inline bool slotTextValid(const char* s, size_t maxLen) {
    if (s == nullptr) return false;
    size_t n = strlen(s);
    if (n == 0 || n > maxLen - 1) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}

// ---------- Nicht-ASCII auf dem Display ----------
//
// Der klassische GFX-Zeichensatz (5x7, glcdfont) ist nach Codepage 437 geordnet und
// kennt kein UTF-8: ein "ue" als 0xC3 0xBC ergaebe zwei Fremdzeichen. Umgesetzt wird
// deshalb vor dem Zeichnen, und zwar genau das, was der Zeichensatz hat und im Homelab
// vorkommt: Gradzeichen, Umlaute, sz. Alles andere Nicht-ASCII wird beim SPEICHERN
// abgewiesen (config_codec.h) -- lieber eine Meldung als Muell auf dem Display.
// Die Bibliothek verschiebt keine Codes (kein Adafruit-Versatz ab 0xB0; geprueft
// 06.09.2026 in Arduino_GFX.cpp: font[c * 5 + i]); die acht Glyphen liegen in
// glcdfont.h an genau diesen Stellen (auf dem Host gerendert).
struct UmsetzZeichen {
    unsigned char utf8Erst;   // alle acht sind Zwei-Byte-Sequenzen
    unsigned char utf8Zweit;
    unsigned char font;
};
static const UmsetzZeichen FONT_UMSETZUNG[] = {
    {0xC2, 0xB0, 0xF8},  // Grad
    {0xC3, 0xA4, 0x84},  // ae
    {0xC3, 0xB6, 0x94},  // oe
    {0xC3, 0xBC, 0x81},  // ue
    {0xC3, 0x84, 0x8E},  // AE
    {0xC3, 0x96, 0x99},  // OE
    {0xC3, 0x9C, 0x9A},  // UE
    {0xC3, 0x9F, 0xE1},  // sz
};

// UTF-8 -> Bytes des Zeichensatzes. false, wenn ein Zeichen nicht darstellbar ist
// oder out nicht reicht; out ist in jedem Fall terminiert und enthaelt, was bis
// dahin umgesetzt wurde.
inline bool utf8NachFont(const char* in, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) return false;
    size_t o = 0;
    bool ok = true;
    for (size_t i = 0; in != nullptr && in[i] != 0;) {
        unsigned char ziel = (unsigned char)in[i];
        size_t laenge = 1;
        if (ziel >= 0x80) {
            ok = false;
            for (const UmsetzZeichen& u : FONT_UMSETZUNG) {
                if (ziel == u.utf8Erst && (unsigned char)in[i + 1] == u.utf8Zweit) {
                    ziel = u.font;
                    laenge = 2;
                    ok = true;
                    break;
                }
            }
            if (!ok) break;
        }
        if (o + 1 >= outSize) {
            ok = false;
            break;
        }
        out[o++] = (char)ziel;
        i += laenge;
    }
    out[o] = 0;
    return ok;
}

// Anzahl der Zeichen, die auf dem Display stehen -- nicht der Bytes. Fuer die
// Breitenrechnung: das Gradzeichen ist EIN Zeichen breit, braucht in UTF-8 aber zwei Bytes.
inline size_t fontZeichen(const char* s) {
    size_t n = 0;
    for (; s != nullptr && *s != 0; s++) {
        if (((unsigned char)*s & 0xC0) != 0x80) n++;  // Folgebytes zaehlen nicht
    }
    return n;
}

// Text, der auf dem Display erscheint (Beschriftung, Einheit): wie slotTextValid,
// und zusaetzlich muss jedes Zeichen darstellbar sein. Der Feldname wird nie
// gezeichnet und bleibt bei slotTextValid.
inline bool anzeigeTextValid(const char* s, size_t maxLen) {
    if (!slotTextValid(s, maxLen)) return false;
    char probe[LABEL_LEN];  // umgesetzt ist nie laenger als die Eingabe in Bytes
    static_assert(UNIT_LEN <= LABEL_LEN, "probe muss den laengsten Anzeigetext fassen");
    return maxLen <= sizeof(probe) && utf8NachFont(s, probe, sizeof(probe));
}

// Nur http: TLS ist auf diesem Chip ausgeschlossen (Heap), also gar nicht erst zulassen.
inline bool slotUrlValid(const char* url) {
    if (url == nullptr) return false;
    size_t n = strlen(url);
    if (n < 8 || n > URL_LEN - 1) return false;
    if (strncmp(url, "http://", 7) != 0) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)url[i];
        if (c < 0x20 || c == 0x7F || c == ' ') return false;
    }
    return true;
}

// ---------- Anordnung von Wert und Einheit in EINER Zeile ----------
//
// Reine Rechnung, damit sie ohne Display pruefbar ist (SlotDisplay.cpp haengt an
// Arduino_GFX und laeuft nie auf dem Host). Der GFX-Zeichensatz misst je Stufe
// 6x8 Punkte.
//
// Wert und Einheit haben unabhaengige Schriftstufen. Sie stehen deshalb NICHT auf
// derselben Oberkante, sondern auf derselben **Unterkante** -- sonst haengt eine
// kleine Einheit neben einer grossen Zahl in der Luft.
//
// Passt beides zusammen nicht in die Kachel, wird NICHTS verkleinert: die Zeile
// beginnt am linken Rand und was rechts hinausragt, schneidet der Zeichner ab.
// Das sieht der Nutzer sofort und stellt die Stufe kleiner -- eine stille
// Verkleinerung wuerde ihm die Entscheidung wegnehmen.
struct ZeileWertEinheit {
    int16_t wertX;
    int16_t wertY;
    int16_t einheitX;
    int16_t einheitY;
    int16_t hoehe;
};

inline int16_t gfxZeichenBreite(uint8_t stufe) { return (int16_t)(6 * stufe); }
inline int16_t gfxZeilenHoehe(uint8_t stufe) { return (int16_t)(8 * stufe); }

inline ZeileWertEinheit layoutWertEinheit(int16_t x, int16_t y, int16_t w, size_t wertZeichen,
                                          uint8_t wertStufe, size_t einheitZeichen,
                                          uint8_t einheitStufe) {
    ZeileWertEinheit z = {};
    const int16_t wertB = (int16_t)(wertZeichen * (size_t)gfxZeichenBreite(wertStufe));
    const int16_t einheitB = (int16_t)(einheitZeichen * (size_t)gfxZeichenBreite(einheitStufe));
    // Halbes Zeichen Luft dazwischen -- ohne Abstand klebt "42%" zusammen, ein ganzes
    // Zeichen reisst die Zahl von ihrer Einheit los.
    const int16_t luft = (einheitZeichen > 0) ? (int16_t)(gfxZeichenBreite(einheitStufe) / 2) : 0;

    const int16_t gesamt = (int16_t)(wertB + luft + einheitB);
    int16_t startX = (int16_t)(x + (w - gesamt) / 2);
    if (startX < x) startX = x;

    const int16_t hWert = gfxZeilenHoehe(wertStufe);
    const int16_t hEinheit = (einheitZeichen > 0) ? gfxZeilenHoehe(einheitStufe) : 0;
    z.hoehe = (hWert > hEinheit) ? hWert : hEinheit;

    z.wertX = startX;
    z.wertY = (int16_t)(y + z.hoehe - hWert);
    z.einheitX = (int16_t)(startX + wertB + luft);
    z.einheitY = (int16_t)(y + z.hoehe - hEinheit);
    return z;
}

// ---------- Hoehe einer Kachel und Aufteilung einer Seite ----------
//
// EINE Formel fuer beides: das senkrechte Zentrieren beim Zeichnen und das Aufteilen
// der Seite auf zwei Kacheln. Zwei Fassungen wuerden auseinanderlaufen, und dann teilte
// die Seite nach anderen Hoehen auf, als sie hinterher zeichnet.
static const int16_t KACHEL_ABSTAND = 4;
// Grenzen der Aufteilung in Prozent. Keine Kachel faellt unter MIN_TEILUNG -- auch
// nicht automatisch: eine 5-Prozent-Kachel zeigt nichts mehr, sieht aber nach Fehler aus.
static const uint8_t MIN_TEILUNG = 20;
static const uint8_t MAX_TEILUNG = 80;
static const uint8_t TEILUNG_AUTOMATISCH = 0;

struct KachelZeilen {
    bool mitLabel;
    bool mitZahl;
    bool mitBalken;
    bool mitUnterzeile;    // Einheit unter dem Wert und/oder Veraltet-Hinweis
    bool einheitDaneben;   // dann ist die Wertzeile so hoch wie die groessere Stufe
    uint8_t labelStufe;
    uint8_t wertStufe;
    uint8_t unitStufe;
};

// Alle Teilhoehen einer Kachel aus EINER Rechnung. Der Zeichner brauchte sie frueher
// einzeln und rechnete sie neben kachelInhaltHoehe() ein zweites Mal nach -- zwei
// Fassungen derselben Formel, die bei jeder Aenderung auseinanderlaufen konnten.
struct KachelHoehen {
    int16_t label;
    int16_t wert;
    int16_t balken;       // Dicke plus Abstand, 0 ohne Balken
    int16_t unten;
    int16_t gesamt;
    int16_t balkenDicke;  // nur der Balken selbst, zum Zeichnen
};

inline KachelHoehen kachelHoehen(const KachelZeilen& z) {
    KachelHoehen h = {};
    h.label = z.mitLabel ? (int16_t)(gfxZeilenHoehe(z.labelStufe) + KACHEL_ABSTAND) : 0;
    if (z.mitZahl) {
        const int16_t hZahl = gfxZeilenHoehe(z.wertStufe);
        const int16_t hEinheit = z.einheitDaneben ? gfxZeilenHoehe(z.unitStufe) : 0;
        h.wert = (hZahl > hEinheit) ? hZahl : hEinheit;
    }
    // Ohne Zahl darf der Balken dicker sein -- er ist dann die einzige Aussage.
    h.balkenDicke = (int16_t)(z.mitZahl ? 10 : 22);
    h.balken = z.mitBalken ? (int16_t)(h.balkenDicke + KACHEL_ABSTAND) : 0;
    h.unten = z.mitUnterzeile ? (int16_t)(gfxZeilenHoehe(z.unitStufe) + KACHEL_ABSTAND) : 0;
    h.gesamt = (int16_t)(h.label + h.wert + h.balken + h.unten);
    return h;
}

inline int16_t kachelInhaltHoehe(const KachelZeilen& z) { return kachelHoehen(z).gesamt; }

// Hoehe der OBEREN Kachel bei zwei Werten uebereinander.
//
// prozent == TEILUNG_AUTOMATISCH: im Verhaeltnis der beiden Inhaltshoehen. Die haengen
// nur an den Schriftstufen des Nutzers, nicht am gemessenen Wert -- die Aufteilung
// springt also nicht herum, wenn aus 9 % eine 100 % wird. Sie folgt seinen
// Einstellungen, statt sie gegenzurechnen.
// Sonst: fester Prozentwert. Beides wird auf MIN_TEILUNG..MAX_TEILUNG geklemmt.
inline int16_t teilungErsteKachel(int16_t gesamt, int16_t inhalt0, int16_t inhalt1,
                                  uint8_t prozent) {
    const int16_t untergrenze = (int16_t)((int32_t)gesamt * MIN_TEILUNG / 100);
    int16_t h0 = 0;
    if (prozent >= MIN_TEILUNG && prozent <= MAX_TEILUNG) {
        h0 = (int16_t)((int32_t)gesamt * prozent / 100);
    } else {
        const int32_t summe = (int32_t)inhalt0 + (int32_t)inhalt1;
        h0 = (summe <= 0) ? (int16_t)(gesamt / 2)
                          : (int16_t)((int32_t)gesamt * inhalt0 / summe);
    }
    if (h0 < untergrenze) h0 = untergrenze;
    if (h0 > (int16_t)(gesamt - untergrenze)) h0 = (int16_t)(gesamt - untergrenze);
    return h0;
}
