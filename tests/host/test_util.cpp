// SPDX-License-Identifier: GPL-3.0-or-later
// Host-Unit-Tests der puren Logik. Laufen ohne Geraet und ohne Arduino:
//   c++ -std=c++17 smalltv/tests/host/test_util.cpp -o /tmp/smalltv-util && /tmp/smalltv-util
// Testadressen nutzen 192.0.2.x (RFC 5737, fuer Dokumentation reserviert) -- nie ein echtes Netz.
#include "pruefe.h"
#include <cstdio>
#include <cstring>
#include "../../firmware/include/smalltv_util.h"

int main() {
    // --- Schwellwerte: Alarm schlaegt Warnung, oben und unten ---
    Thresholds t = {};
    t.hasWarnAbove = true;
    t.warnAbove = 3000.0f;
    t.hasAlarmAbove = true;
    t.alarmAbove = 5000.0f;
    PRUEFE(evalThresholds(1000.0f, t) == STATE_OK);
    PRUEFE(evalThresholds(3500.0f, t) == STATE_WARN);
    PRUEFE(evalThresholds(6000.0f, t) == STATE_ALARM);
    // Grenzfall: exakt auf der Schwelle zaehlt noch NICHT als Ueberschreitung
    PRUEFE(evalThresholds(3000.0f, t) == STATE_OK);
    PRUEFE(evalThresholds(5000.0f, t) == STATE_WARN);

    // Untere Schwellen (z.B. Ladezustand)
    Thresholds u = {};
    u.hasWarnBelow = true;
    u.warnBelow = 50.0f;
    u.hasAlarmBelow = true;
    u.alarmBelow = 20.0f;
    PRUEFE(evalThresholds(80.0f, u) == STATE_OK);
    PRUEFE(evalThresholds(40.0f, u) == STATE_WARN);
    PRUEFE(evalThresholds(10.0f, u) == STATE_ALARM);
    PRUEFE(evalThresholds(50.0f, u) == STATE_OK);

    // Ohne gesetzte Schwellen ist alles ok
    Thresholds leer = {};
    PRUEFE(evalThresholds(-999.0f, leer) == STATE_OK);
    PRUEFE(evalThresholds(999999.0f, leer) == STATE_OK);

    // --- Zeit: wrap-sicher (der 49,7-Tage-Bug aus Projekt A) ---
    PRUEFE(!elapsed(1000, 500, 600));
    PRUEFE(elapsed(1100, 500, 600));
    PRUEFE(!elapsed(0x00000010u, 0xFFFFFF00u, 0x200));
    PRUEFE(elapsed(0x00000150u, 0xFFFFFF00u, 0x200));

    // --- Stale nach drei Fehlversuchen ---
    PRUEFE(!isStale(0));
    PRUEFE(!isStale(2));
    PRUEFE(isStale(3));
    PRUEFE(isStale(9));

    // --- Zahlen parsen: nur echte Zahlen, kein stiller Nullwert ---
    float v = -1.0f;
    PRUEFE(parseNumber("42", v) && v == 42.0f);
    PRUEFE(parseNumber("-3.5", v) && v == -3.5f);
    PRUEFE(parseNumber("  7.25 ", v) && v == 7.25f);
    PRUEFE(!parseNumber("", v));
    PRUEFE(!parseNumber("on", v));
    PRUEFE(!parseNumber("12abc", v));  // Rest hinter der Zahl -> ungueltig

    // Grenzfaelle, die beim Austausch der Implementierung stabil bleiben muessen
    PRUEFE(parseNumber("0", v) && v == 0.0f);
    PRUEFE(parseNumber("-0", v) && v == 0.0f);
    PRUEFE(parseNumber("+5", v) && v == 5.0f);
    PRUEFE(parseNumber(".5", v) && v == 0.5f);      // fuehrender Punkt
    PRUEFE(parseNumber("5.", v) && v == 5.0f);      // nachgestellter Punkt
    PRUEFE(parseNumber("000042", v) && v == 42.0f); // fuehrende Nullen
    PRUEFE(parseNumber("-273.15", v) && v == -273.15f);
    PRUEFE(parseNumber("100000", v) && v == 100000.0f);
    PRUEFE(parseNumber("0.001", v) && v == 0.001f);
    PRUEFE(!parseNumber("-", v));
    PRUEFE(!parseNumber("+", v));
    PRUEFE(!parseNumber(".", v));
    PRUEFE(!parseNumber("1.2.3", v));   // zwei Punkte
    PRUEFE(!parseNumber("1 2", v));     // Leerzeichen mittendrin
    PRUEFE(!parseNumber("nan", v));
    PRUEFE(!parseNumber("inf", v));
    // Exponentialschreibweise wird bewusst NICHT unterstuetzt: Messwerte kommen so nicht
    // vorbei, und der Verzicht spart auf diesem Chip mehrere Kilobyte Programmspeicher.
    PRUEFE(!parseNumber("1e5", v));

    // --- Formatierung: Zahlen mit Nachkommastellen, Text unveraendert ---
    char buf[32];
    formatValue(buf, sizeof(buf), "1234.567", 0);
    PRUEFE(strcmp(buf, "1235") == 0);
    formatValue(buf, sizeof(buf), "1234.567", 2);
    PRUEFE(strcmp(buf, "1234.57") == 0);
    formatValue(buf, sizeof(buf), "on", 1);
    PRUEFE(strcmp(buf, "on") == 0);
    formatValue(buf, sizeof(buf), "  true  ", 0);
    PRUEFE(strcmp(buf, "true") == 0);

    // Grenzfaelle der Formatierung
    formatValue(buf, sizeof(buf), "0", 0);        PRUEFE(strcmp(buf, "0") == 0);
    formatValue(buf, sizeof(buf), "0", 2);        PRUEFE(strcmp(buf, "0.00") == 0);
    formatValue(buf, sizeof(buf), "-3.456", 1);   PRUEFE(strcmp(buf, "-3.5") == 0);
    formatValue(buf, sizeof(buf), "-0.4", 0);     PRUEFE(strcmp(buf, "-0") == 0 || strcmp(buf, "0") == 0);
    formatValue(buf, sizeof(buf), "1234", 0);     PRUEFE(strcmp(buf, "1234") == 0);
    formatValue(buf, sizeof(buf), "99.95", 1);    PRUEFE(strcmp(buf, "100.0") == 0);  // Rundung traegt
    formatValue(buf, sizeof(buf), "0.5", 0);      PRUEFE(strcmp(buf, "1") == 0);      // kaufmaennisch
    formatValue(buf, sizeof(buf), "-0.5", 0);     PRUEFE(strcmp(buf, "-1") == 0);
    formatValue(buf, sizeof(buf), "7", 3);        PRUEFE(strcmp(buf, "7.000") == 0);
    formatValue(buf, sizeof(buf), "12.3456", 3);  PRUEFE(strcmp(buf, "12.346") == 0);
    formatValue(buf, sizeof(buf), "100000", 1);   PRUEFE(strcmp(buf, "100000.0") == 0);

    // --- URL-Validierung: nur http, Laenge, keine Steuerzeichen ---
    PRUEFE(slotUrlValid("http://192.0.2.10:8082/rest-api/v1/state/x/plain"));
    PRUEFE(!slotUrlValid(""));
    PRUEFE(!slotUrlValid("https://example.com"));  // TLS ist auf diesem Chip ausgeschlossen
    PRUEFE(!slotUrlValid("ftp://example.com"));
    PRUEFE(!slotUrlValid("http://a\nb"));  // Steuerzeichen
    char lang[200];
    memset(lang, 'a', sizeof(lang));
    lang[199] = 0;
    memcpy(lang, "http://", 7);
    PRUEFE(!slotUrlValid(lang));  // laenger als URL_LEN-1

    // --- Was gilt als "an"? ---
    // Der Schaltwert kommt als Text vom Server und sieht je nach Quelle anders aus.
    PRUEFE(istWahr("true"));
    PRUEFE(istWahr("TRUE"));
    PRUEFE(istWahr("1"));
    PRUEFE(istWahr("on"));
    PRUEFE(istWahr("ON"));
    PRUEFE(istWahr("an"));
    PRUEFE(istWahr(" true "));   // Leerraum stoert nicht
    PRUEFE(istWahr("1.0"));      // manche Quellen liefern Zahlen als Gleitkomma
    PRUEFE(istWahr("100"));      // jede Zahl ungleich null
    PRUEFE(istWahr("-3"));
    PRUEFE(!istWahr("false"));
    PRUEFE(!istWahr("0"));
    PRUEFE(!istWahr("0.0"));
    PRUEFE(!istWahr("off"));
    PRUEFE(!istWahr("aus"));
    PRUEFE(!istWahr(""));
    PRUEFE(!istWahr(nullptr));
    // Unverstaendliches gilt als "aus" -- nicht als "an". Ein Display, das wegen einer
    // unklaren Antwort dauerhaft hell bleibt, faellt niemandem als Fehler auf.
    PRUEFE(!istWahr("vielleicht"));

    // --- Nachtmodus: Zeitfenster, das ueber Mitternacht gehen darf ---
    // Der uebliche Fall ist genau der schwierige: 22 bis 7 Uhr laeuft ueber den Tageswechsel.
    PRUEFE(!imZeitfenster(12, 22, 7));   // mittags: aus
    PRUEFE(imZeitfenster(23, 22, 7));    // abends: an
    PRUEFE(imZeitfenster(0, 22, 7));     // Mitternacht: an
    PRUEFE(imZeitfenster(6, 22, 7));     // frueh: an
    PRUEFE(!imZeitfenster(7, 22, 7));    // Endstunde selbst zaehlt nicht mehr
    PRUEFE(imZeitfenster(22, 22, 7));    // Startstunde zaehlt
    // Fenster ohne Tageswechsel
    PRUEFE(imZeitfenster(14, 13, 18));
    PRUEFE(!imZeitfenster(12, 13, 18));
    PRUEFE(!imZeitfenster(18, 13, 18));
    // Start gleich Ende = kein Fenster (nicht etwa "immer")
    PRUEFE(!imZeitfenster(5, 9, 9));
    PRUEFE(!imZeitfenster(9, 9, 9));

    // --- Helligkeit wird immer auf 0..100 gebracht ---
    PRUEFE(helligkeitKlemmen(50) == 50);
    PRUEFE(helligkeitKlemmen(0) == 0);
    PRUEFE(helligkeitKlemmen(100) == 100);
    PRUEFE(helligkeitKlemmen(255) == 100);
    PRUEFE(helligkeitKlemmen(-20) == 0);

    // --- Rueckzug bei Fehlversuchen ---
    // Ein totes Ziel darf nicht im eingestellten Takt weiter angefragt werden: Jeder
    // Versuch blockiert das Geraet mehrere Sekunden. Also wird der Abstand nach jedem
    // Fehlschlag verdoppelt, bis zu einer Obergrenze.
    PRUEFE(abrufIntervallMs(30, 0) == 30000U);       // laeuft alles: unveraendert
    PRUEFE(abrufIntervallMs(30, 1) == 60000U);       // erster Fehlschlag: doppelt
    PRUEFE(abrufIntervallMs(30, 2) == 120000U);
    PRUEFE(abrufIntervallMs(5, 0) == 5000U);
    PRUEFE(abrufIntervallMs(5, 1) == 10000U);
    PRUEFE(abrufIntervallMs(5, 5) == 160000U);       // 5 s * 32, noch unter der Grenze
    // Obergrenze greift, sobald die Verdopplung darueber hinausginge (5 s * 64 = 320 s)
    PRUEFE(abrufIntervallMs(5, 6) == RUECKZUG_MAX_MS);
    PRUEFE(abrufIntervallMs(5, 7) == RUECKZUG_MAX_MS);
    PRUEFE(abrufIntervallMs(5, 200) == RUECKZUG_MAX_MS);
    // Ein Grundintervall oberhalb der Obergrenze bleibt bestehen -- die Obergrenze ist
    // eine Bremse gegen zu haeufige Versuche, kein Beschleuniger.
    PRUEFE(abrufIntervallMs(3600, 3) == 3600000U);
    // Ein langes Intervall wird durch den Rueckzug nie KUERZER
    for (uint8_t f = 0; f < 20; f++) {
        PRUEFE(abrufIntervallMs(600, f) >= 600000U);
    }

    // --- Balken-Anteil: immer 0..1, nie darueber hinaus ---
    PRUEFE(barFraction(0.0f, 0.0f, 100.0f) == 0.0f);
    PRUEFE(barFraction(50.0f, 0.0f, 100.0f) == 0.5f);
    PRUEFE(barFraction(100.0f, 0.0f, 100.0f) == 1.0f);
    PRUEFE(barFraction(-20.0f, 0.0f, 100.0f) == 0.0f);   // unterhalb -> gekappt
    PRUEFE(barFraction(150.0f, 0.0f, 100.0f) == 1.0f);   // oberhalb -> gekappt
    PRUEFE(barFraction(15.0f, 10.0f, 20.0f) == 0.5f);    // verschobene Skala
    PRUEFE(barFraction(-5.0f, -10.0f, 10.0f) == 0.25f);  // negative Skala
    // Unbrauchbare Skalen duerfen nicht zur Division durch null fuehren
    PRUEFE(barFraction(5.0f, 10.0f, 10.0f) == 0.0f);     // max == min
    PRUEFE(barFraction(5.0f, 20.0f, 10.0f) == 0.0f);     // max < min

    // --- Textfelder ---
    PRUEFE(slotTextValid("Verbrauch", LABEL_LEN));
    PRUEFE(!slotTextValid("", LABEL_LEN));                            // leer nicht erlaubt
    PRUEFE(!slotTextValid("abcdefghijklmnopqrstuvwxyz", LABEL_LEN));  // zu lang

    // --- Veraltet: Fehlversuche ODER Alter ---
    PRUEFE(!istVeraltet(0, 0, 30));        // noch nie abgerufen: "keine Daten", nicht veraltet
    PRUEFE(!istVeraltet(2, 10, 30));       // 2 Fehlversuche, frischer Wert: ok
    PRUEFE(istVeraltet(3, 10, 30));        // 3 Fehlversuche: veraltet
    PRUEFE(!istVeraltet(0, 120, 30));      // 120 s bei 30-s-Intervall: unter 3*30+30
    PRUEFE(istVeraltet(0, 121, 30));       // ...und knapp darueber: veraltet
    // WLAN weg: keine Versuche mehr (failCount bleibt 0), nur das Alter waechst --
    // genau dafuer existiert die Altersgrenze.
    PRUEFE(istVeraltet(0, 7200, 300));     // 2 h alt bei 5-min-Intervall
    PRUEFE(!istVeraltet(0, 900, 300));     // 15 min bei 5-min-Intervall: noch ok (Grenze 930)

    // --- Wert und Einheit in einer Zeile ---
    {
        // "42" auf Stufe 6 (72 Punkte breit), "%" auf Stufe 3 (18 breit), Luft 9.
        // Zusammen 99 Punkte in einer 240 breiten Kachel ab x=0.
        ZeileWertEinheit z = layoutWertEinheit(0, 0, 240, 2, 6, 1, 3);
        PRUEFE(z.hoehe == 48);                 // die groessere der beiden Zeilenhoehen
        PRUEFE(z.wertX == (240 - 99) / 2);     // der ganze Block mittig
        PRUEFE(z.einheitX == z.wertX + 72 + 9);
        PRUEFE(z.wertY == 0);                  // grosser Wert bestimmt die Zeile
        PRUEFE(z.einheitY == 48 - 24);         // kleine Einheit steht auf DERSELBEN Unterkante
    }
    {
        // Kachelversatz wird durchgereicht.
        ZeileWertEinheit z = layoutWertEinheit(120, 60, 120, 2, 2, 1, 2);
        PRUEFE(z.wertX >= 120 && z.einheitX > z.wertX);
        PRUEFE(z.wertY == 60 && z.einheitY == 60);   // gleiche Stufe: gleiche Oberkante
    }
    {
        // Zu breit: kein Verkleinern, kein Umbruch -- die Zeile beginnt am linken Rand
        // und der Zeichner schneidet rechts ab.
        ZeileWertEinheit z = layoutWertEinheit(0, 0, 120, 4, 6, 3, 6);
        PRUEFE(z.wertX == 0);
        PRUEFE(z.einheitX == 4 * 36 + 18);
    }
    {
        // Ohne Einheit gibt es auch keine Luft und keine zweite Spalte.
        ZeileWertEinheit z = layoutWertEinheit(0, 0, 240, 3, 4, 0, 2);
        PRUEFE(z.hoehe == 32);
        PRUEFE(z.wertX == (240 - 3 * 24) / 2);
        PRUEFE(z.einheitX == z.wertX + 3 * 24);
    }

    // --- Kachelhoehe und Aufteilung einer Seite auf zwei Werte ---
    {
        // Beschriftung Stufe 2 (16+4), Wert Stufe 6 (48), Balken mit Zahl (10+4),
        // Einheit daneben -> keine Unterzeile. Zusammen 82.
        KachelZeilen z = {};
        z.mitLabel = true; z.mitZahl = true; z.mitBalken = true;
        z.einheitDaneben = true; z.mitUnterzeile = false;
        z.labelStufe = 2; z.wertStufe = 6; z.unitStufe = 3;
        PRUEFE(kachelInhaltHoehe(z) == 20 + 48 + 14);

        // Dieselbe Kachel mit Einheit UNTER dem Wert: eine Zeile mehr.
        z.einheitDaneben = false; z.mitUnterzeile = true;
        PRUEFE(kachelInhaltHoehe(z) == 20 + 48 + 14 + (24 + 4));

        // Eine grosse Einheit neben einem kleinen Wert bestimmt die Zeilenhoehe.
        KachelZeilen g = {};
        g.mitZahl = true; g.einheitDaneben = true; g.wertStufe = 2; g.unitStufe = 7;
        PRUEFE(kachelInhaltHoehe(g) == 56);

        // Ohne Zahl darf der Balken dicker sein.
        KachelZeilen b = {};
        b.mitBalken = true;
        PRUEFE(kachelInhaltHoehe(b) == 22 + 4);
    }
    {
        // Automatisch: im Verhaeltnis der Inhalte.
        PRUEFE(teilungErsteKachel(240, 60, 60, TEILUNG_AUTOMATISCH) == 120);
        PRUEFE(teilungErsteKachel(240, 90, 30, TEILUNG_AUTOMATISCH) == 180);
        // Untergrenze: eine winzige Kachel wuerde nichts mehr zeigen.
        PRUEFE(teilungErsteKachel(240, 1, 200, TEILUNG_AUTOMATISCH) == 48);
        PRUEFE(teilungErsteKachel(240, 200, 1, TEILUNG_AUTOMATISCH) == 192);
        // Keine Inhalte (beide Plaetze leer): haelftig, nicht 0.
        PRUEFE(teilungErsteKachel(240, 0, 0, TEILUNG_AUTOMATISCH) == 120);
        // Fester Prozentwert schlaegt die Automatik -- auch gegen die Inhaltshoehen.
        PRUEFE(teilungErsteKachel(240, 200, 10, 30) == 72);
        PRUEFE(teilungErsteKachel(240, 10, 200, 70) == 168);
        // Und wird ebenfalls geklemmt.
        PRUEFE(teilungErsteKachel(240, 0, 0, 95) == 120);   // ungueltig -> automatisch
        PRUEFE(teilungErsteKachel(240, 100, 0, 20) == 48);
    }

    // --- Nicht-ASCII: nur, was der Zeichensatz des Displays hat (Grad, Umlaute, sz) ---
    {
        char f[32];
        PRUEFE(utf8NachFont("42\xc2\xb0" "C", f, sizeof(f)));
        PRUEFE((unsigned char)f[2] == 0xF8 && f[3] == 'C' && f[4] == 0);
        PRUEFE(utf8NachFont("K\xc3\xbc" "che", f, sizeof(f)));
        PRUEFE((unsigned char)f[1] == 0x81 && strcmp(f + 2, "che") == 0);
        PRUEFE(utf8NachFont("\xc3\x84\xc3\x96\xc3\x9c\xc3\x9f", f, sizeof(f)) && strlen(f) == 4);
        PRUEFE(!utf8NachFont("\xce\xa9", f, sizeof(f)));  // Omega: nicht darstellbar
        PRUEFE(!utf8NachFont("abc", f, 3));                 // Puffer zu klein: false, aber terminiert
        PRUEFE(f[2] == 0);
        PRUEFE(utf8NachFont("", f, sizeof(f)) && f[0] == 0);
        PRUEFE(fontZeichen("\xc2\xb0" "C") == 2);          // Anzeigebreite: 2 Zeichen, nicht 3 Bytes
        PRUEFE(anzeigeTextValid("K\xc3\xbc" "che", LABEL_LEN));
        PRUEFE(anzeigeTextValid("\xc2\xb0" "C", UNIT_LEN));
        PRUEFE(!anzeigeTextValid("\xce\xa9", LABEL_LEN));   // abgewiesen, nicht als Muell gezeichnet
        PRUEFE(!anzeigeTextValid("", LABEL_LEN));           // leer bleibt Sache des Aufrufers
        PRUEFE(slotTextValid("\xce\xa9", FIELD_LEN));       // Feldname wird nie gezeichnet: bleibt erlaubt
    }

    // --- Kachelzeilen: EINE Rechnung fuer Zeichnen und Seitenaufteilung ---
    {
        KachelZeilen z = {};
        z.mitLabel = true;
        z.mitZahl = true;
        z.mitBalken = true;
        z.einheitDaneben = true;
        z.labelStufe = 2;
        z.wertStufe = 6;
        z.unitStufe = 3;
        const KachelHoehen h = kachelHoehen(z);
        PRUEFE(h.label == 20 && h.wert == 48 && h.balken == 14 && h.unten == 0);
        PRUEFE(h.gesamt == h.label + h.wert + h.balken + h.unten);
        PRUEFE(h.balkenDicke == 10);
        // Ohne Zahl ist der Balken die einzige Aussage und darf dicker sein.
        KachelZeilen b = {};
        b.mitBalken = true;
        PRUEFE(kachelHoehen(b).balkenDicke == 22);
        // Die alte Einstiegsstelle liefert weiter dieselbe Zahl.
        PRUEFE(kachelInhaltHoehe(z) == h.gesamt);
    }

    // --- Zeitzonen-Regel (einstellbar seit v0.5.0) ---
    {
        PRUEFE(zeitzoneRegelGueltig("CET-1CEST,M3.5.0,M10.5.0/3"));
        PRUEFE(zeitzoneRegelGueltig("UTC0"));
        PRUEFE(zeitzoneRegelGueltig("EST5EDT,M3.2.0,M11.1.0"));
        PRUEFE(!zeitzoneRegelGueltig(nullptr));
        PRUEFE(!zeitzoneRegelGueltig(""));
        PRUEFE(!zeitzoneRegelGueltig("CE"));            // zu kurz
        PRUEFE(!zeitzoneRegelGueltig("1234"));          // faengt nicht mit Buchstaben an
        PRUEFE(!zeitzoneRegelGueltig("CET 1CEST"));     // Leerzeichen
        PRUEFE(!zeitzoneRegelGueltig("CET-1CEST,\tM3"));  // Steuerzeichen
        char lang[ZEITZONE_LEN + 8];
        memset(lang, 'A', sizeof(lang) - 1);
        lang[sizeof(lang) - 1] = 0;
        PRUEFE(!zeitzoneRegelGueltig(lang));            // laenger als der Puffer
    }

    printf("OK\n");
    return 0;
}
