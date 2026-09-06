// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Konfigurationsmodell des Displays und dessen JSON-Codec.
// Enthaelt die gesamte Validierung -- die Firmware-Schale (SlotStore) rechnet nichts,
// sie liest und schreibt nur Dateien. Dadurch ist alles hier am Host testbar.
#include <ArduinoJson.h>

#include "smalltv_util.h"

// Schriftstufen des GFX-Zeichensatzes (Grundraster 6x8 Punkte je Stufe).
// Bewusst KEINE Auswahl aus "klein/mittel/gross": wer die Beschriftung groesser
// haben will als den Wert, darf das -- die Anzeige gehoert dem Nutzer, nicht uns.
static const uint8_t MIN_TEXT_STUFE = 1;
static const uint8_t MAX_TEXT_STUFE = 10;  // 10 => 60x80 Punkte je Zeichen
enum PageLayout { LAYOUT_EINS = 0, LAYOUT_ZWEI = 1, LAYOUT_VIER = 2 };

struct Slot {
    bool enabled;
    char url[URL_LEN];
    char field[FIELD_LEN];  // leer = ganze Antwort ist der Wert
    char label[LABEL_LEN];
    char unit[UNIT_LEN];
    uint8_t decimals;    // 0..3
    uint16_t refreshSec; // 5..3600
    uint8_t page;        // 1..MAX_PAGES
    uint8_t pos;         // 0..maxPosForLayout(Layout der Seite)
    uint8_t wertSize;    // Schriftstufe des Werts, MIN_TEXT_STUFE..MAX_TEXT_STUFE
    uint8_t labelSize;   // Schriftstufe der Beschriftung -- unabhaengig vom Wert
    uint8_t unitSize;    // Schriftstufe der Einheit -- unabhaengig vom Wert
    bool einheitDaneben; // Einheit neben dem Wert (statt in eigener Zeile darunter)
    uint16_t color;      // RGB565, Normalzustand
    uint8_t anzeige;     // SlotAnzeige: Zahl, Balken oder beides
    float barMin;        // Anfang der Balkenskala
    float barMax;        // Ende der Balkenskala
    Thresholds thr;
};

// Zeilen einer Kachel aus den EINSTELLUNGEN eines Slots. mitUnterzeile kommt vom
// Aufrufer, weil nur er weiss, ob eine Unterzeile ansteht: beim Zeichnen kann dort
// auch der Veraltet-Hinweis stehen, beim Aufteilen der Seite darf er NICHT mitzaehlen
// -- sonst spraenge die Aufteilung, sobald ein Wert veraltet.
inline KachelZeilen kachelZeilenAus(const Slot& s, bool mitUnterzeile) {
    KachelZeilen z = {};
    z.mitLabel = (s.label[0] != 0);
    z.mitZahl = (s.anzeige != ANZEIGE_BALKEN);
    z.mitBalken = (s.anzeige != ANZEIGE_ZAHL);
    z.einheitDaneben = s.einheitDaneben && z.mitZahl && (s.unit[0] != 0);
    z.mitUnterzeile = mitUnterzeile;
    z.labelStufe = s.labelSize;
    z.wertStufe = s.wertSize;
    z.unitStufe = s.unitSize;
    return z;
}

struct Config {
    Slot slots[MAX_SLOTS];
    uint8_t layout[MAX_PAGES];  // PageLayout je Seite
    uint8_t teilung[MAX_PAGES]; // Aufteilung bei zwei Werten: 0 = automatisch, sonst 20..80
    uint16_t rotateSec;         // Seiten-Rotationsintervall
    uint16_t colorWarn;         // global, nicht pro Slot
    uint16_t colorAlarm;
    uint8_t hellModus;          // 0 = fester Wert, 1 = ueber Datenpunkt geschaltet
    char hellUrl[URL_LEN];      // Schalt-Datenpunkt (nur bei hellModus 1)
    char hellField[FIELD_LEN];
    uint16_t hellSec;           // Abrufintervall des Schalt-Datenpunkts, 5..3600
    uint8_t hellAn;             // Helligkeit, wenn der Datenpunkt "an" meldet
    uint8_t hellAus;            // Helligkeit, wenn er "aus" meldet
    uint8_t helligkeit;         // 0..100, fester Wert
    bool nachtAn;               // Nachtmodus aktiv?
    uint8_t nachtVon;           // Stunde 0..23, ab der gedimmt wird
    uint8_t nachtBis;           // Stunde 0..23, ab der wieder normal
    uint8_t nachtHelligkeit;    // 0..100 waehrend der Nacht (0 = Display aus)
};

inline uint8_t maxPosForLayout(PageLayout l) {
    switch (l) {
        case LAYOUT_EINS: return 0;
        case LAYOUT_ZWEI: return 1;
        default: return 3;
    }
}

// Auslieferungszustand -- EINE Autoritaet fuer den Frisch-Zustand (SlotStore::defaults)
// UND fuer fehlende Felder beim Laden (configFromDoc). Frueher gab es zwei Wertesaetze
// (benannte Konstanten dort, nackte Literale hier), die stillschweigend auseinanderlaufen
// konnten. Wer hier aendert, aendert bewusst beides.
static const uint16_t STD_ROTATE_SEC = 10;
static const uint8_t STD_WERT_SIZE = 3;   // Schriftstufe des Werts
static const uint8_t STD_LABEL_SIZE = 2;  // Schriftstufe der Beschriftung
static const uint8_t STD_UNIT_SIZE = 2;   // Schriftstufe der Einheit
// Die Einheit steht neben dem Wert ("42 %"). Frueher war sie immer die letzte Zeile und
// rutschte damit UNTER den Balken -- weit weg von der Zahl, zu der sie gehoert.
static const bool STD_EINHEIT_DANEBEN = true;
static const uint16_t FARBE_WARNUNG = 0xFD20;    // Bernstein (RGB565)
static const uint16_t FARBE_ALARM = 0xF800;      // Rot (RGB565)
static const uint8_t STD_HELLIGKEIT = 100;       // Prozent
static const uint16_t STD_SCHALTER_SEC = 30;     // Abrufintervall des Helligkeits-Schalters
static const uint8_t STD_NACHT_VON = 22;         // Stunde
static const uint8_t STD_NACHT_BIS = 7;          // Stunde
static const uint8_t STD_NACHT_HELLIGKEIT = 15;  // Prozent

// Eine Schriftstufe aus der Datei lesen und in den erlaubten Bereich zwingen.
// Ein unsinniger Wert darf nie zum schwarzen Feld fuehren: Stufe 0 zeichnet nichts.
inline uint8_t leseStufe(JsonVariantConst v, uint8_t standard) {
    long wert = v | (long)standard;
    if (wert < MIN_TEXT_STUFE || wert > MAX_TEXT_STUFE) return standard;
    return (uint8_t)wert;
}

// Alte Stufe 0/1/2 auf die neue Skala heben. 0 heisst "war nicht da".
inline uint8_t uebernahmeAlteStufe(JsonVariantConst v) {
    if (v.isNull() || !v.is<long>()) return 0;
    switch (v.as<long>()) {
        case 0: return 2;   // war "klein"
        case 1: return 3;   // war "mittel"
        case 2: return 5;   // war "gross"
        default: return 0;
    }
}

// Eine Schwelle gilt als gesetzt, wenn der Schluessel da UND nicht null ist.
// Fehlender Schluessel heisst "nicht gesetzt" -- niemals "0", sonst waere jeder
// weggelassene Grenzwert stillschweigend eine Alarmschwelle bei null.
inline void leseSchwelle(JsonVariantConst v, bool& has, float& wert) {
    // Nur echte Zahlen zaehlen. Frueher genuegte "nicht null", sodass "" oder "abc" als
    // Schwelle 0.0 durchgingen -- ein Slot stuende dann bei jedem positiven Messwert
    // dauerhaft im Alarm und hielte damit die Seitenrotation an.
    if (v.isNull() || !v.is<float>()) {
        has = false;
        wert = 0.0f;
        return;
    }
    has = true;
    wert = v.as<float>();
}

inline void kopiereText(char* ziel, size_t zielGroesse, JsonVariantConst v) {
    const char* s = v.is<const char*>() ? v.as<const char*>() : "";
    snprintf(ziel, zielGroesse, "%s", s == nullptr ? "" : s);
}

// Laenge des Rohwerts PRUEFEN, bevor irgendetwas gekuerzt wird.
// Sonst schneidet kopiereText() zu lange Eingaben still zurecht, die Laengenpruefung
// sieht nur noch das gekuerzte Ergebnis und winkt es durch -- das Geraet riefe dann eine
// andere Adresse ab als eingetragen. Genau das ist bei einer URL nicht hinnehmbar.
inline bool rohlaengePasst(JsonVariantConst v, size_t maxLen) {
    if (!v.is<const char*>()) return true;  // fehlend oder anderer Typ: spaetere Pruefung faengt es
    const char* s = v.as<const char*>();
    if (s == nullptr) return true;
    return strlen(s) <= maxLen - 1;
}

// Zahl als long lesen, Bereich pruefen, erst DANN giesst der Aufrufer sie in den
// schmalen Zieltyp. Direkt in uint8/uint16 gelesen wuerde ein Riesenwert modulo
// gekuerzt und bestuende die Bereichspruefung: 65541 Sekunden wuerden still zu 5 --
// der Aufrufer bekaeme "ok" fuer etwas, das er nie eingestellt hat.
inline bool leseBereich(JsonVariantConst v, long standard, long min, long max,
                        const char* meldung, long& out, char* errOut, size_t errSize) {
    out = v | standard;
    if (out < min || out > max) {
        setErr(errOut, errSize, meldung);
        return false;
    }
    return true;
}

// Prueft und uebernimmt einen einzelnen Slot. slotIndex ist der Platz, der beschrieben
// wird -- er wird bei der Belegungspruefung ausgenommen, damit ein Slot seinen eigenen
// Rasterplatz behalten darf.
// Nimmt ein bereits eingelesenes Dokument. So muss der Aufrufer den Anfragekoerper nur
// EINMAL parsen -- er braucht ja auch den Slot-Index daraus.
inline bool slotFromDoc(JsonDocument& doc, const Config& cfg, uint8_t slotIndex, Slot& out,
                        char* errOut, size_t errSize) {
    if (slotIndex >= MAX_SLOTS) {
        setErr(errOut, errSize, "Slot-Nummer ausserhalb des Bereichs");
        return false;
    }

    // Erst die Rohlaengen pruefen, dann kopieren (siehe rohlaengePasst).
    if (!rohlaengePasst(doc["url"], URL_LEN)) {
        setErr(errOut, errSize, "URL ist zu lang (max 127 Zeichen)");
        return false;
    }
    if (!rohlaengePasst(doc["label"], LABEL_LEN)) {
        setErr(errOut, errSize, "Beschriftung ist zu lang (max 23 Zeichen, Umlaute zaehlen doppelt)");
        return false;
    }
    if (!rohlaengePasst(doc["field"], FIELD_LEN)) {
        setErr(errOut, errSize, "Feldname ist zu lang (max 31 Zeichen)");
        return false;
    }
    if (!rohlaengePasst(doc["unit"], UNIT_LEN)) {
        setErr(errOut, errSize, "Einheit ist zu lang (max 15 Zeichen, Umlaute zaehlen doppelt)");
        return false;
    }

    Slot s = {};
    s.enabled = doc["enabled"] | true;
    kopiereText(s.url, sizeof(s.url), doc["url"]);
    kopiereText(s.field, sizeof(s.field), doc["field"]);
    kopiereText(s.label, sizeof(s.label), doc["label"]);
    kopiereText(s.unit, sizeof(s.unit), doc["unit"]);

    if (!slotUrlValid(s.url)) {
        setErr(errOut, errSize, "URL ungueltig (nur http://, max 127 Zeichen)");
        return false;
    }
    // Die Beschriftung darf leer bleiben (seit v0.2.6): Wer eine Seite mit einem
    // einzigen, offensichtlichen Wert baut, braucht keine Ueberschrift darueber --
    // und eine erzwungene ist nur Platzverbrauch.
    // Beschriftung und Einheit werden gezeichnet: nur Zeichen, die der Zeichensatz des
    // Displays hat (smalltv_util.h, FONT_UMSETZUNG). Ein fremdes Zeichen wird hier
    // abgewiesen, nicht still ersetzt -- sonst stuende Muell auf dem Display, und der
    // Nutzer wuesste nicht, warum. Der Feldname wird nie gezeichnet.
    if (s.label[0] != 0 && !anzeigeTextValid(s.label, LABEL_LEN)) {
        setErr(errOut, errSize, "Beschriftung ungueltig (zu lang oder Zeichen, das das Display nicht kennt)");
        return false;
    }
    if (s.field[0] != 0 && !slotTextValid(s.field, FIELD_LEN)) {
        setErr(errOut, errSize, "Feldname ungueltig");
        return false;
    }
    if (s.unit[0] != 0 && !anzeigeTextValid(s.unit, UNIT_LEN)) {
        setErr(errOut, errSize, "Einheit ungueltig (zu lang oder Zeichen, das das Display nicht kennt)");
        return false;
    }

    long wert = 0;
    if (!leseBereich(doc["decimals"], 0, 0, 3, "Nachkommastellen nur 0 bis 3",
                     wert, errOut, errSize)) return false;
    s.decimals = (uint8_t)wert;

    if (!leseBereich(doc["refreshSec"], 0, 5, 3600, "Intervall nur 5 bis 3600 Sekunden",
                     wert, errOut, errSize)) return false;
    s.refreshSec = (uint16_t)wert;

    if (!leseBereich(doc["page"], 0, 1, MAX_PAGES, "Seite ausserhalb des Bereichs",
                     wert, errOut, errSize)) return false;
    s.page = (uint8_t)wert;

    PageLayout layout = (PageLayout)cfg.layout[s.page - 1];
    if (!leseBereich(doc["pos"], 0, 0, maxPosForLayout(layout),
                     "Rasterplatz gibt es in diesem Seitenlayout nicht",
                     wert, errOut, errSize)) return false;
    s.pos = (uint8_t)wert;

    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        if (i == slotIndex) continue;  // der eigene Platz zaehlt nicht als Kollision
        const Slot& andere = cfg.slots[i];
        // Ein eingerichteter Slot haelt seinen Platz, AUCH wenn er abgeschaltet ist.
        // Sonst waere Abschalten eine Falle: Der Platz wuerde neu vergeben, und beim
        // Wiedereinschalten koennte der Wert nicht mehr zurueck an seine Stelle.
        // Wer den Platz wirklich braucht, loescht oder verschiebt den alten Slot.
        if (andere.url[0] != 0 && andere.page == s.page && andere.pos == s.pos) {
            setErr(errOut, errSize, "Rasterplatz ist bereits belegt");
            return false;
        }
    }

    if (!leseBereich(doc["wertSize"], STD_WERT_SIZE, MIN_TEXT_STUFE, MAX_TEXT_STUFE,
                     "Schriftstufe des Werts ausserhalb des Bereichs", wert, errOut, errSize))
        return false;
    s.wertSize = (uint8_t)wert;

    if (!leseBereich(doc["labelSize"], STD_LABEL_SIZE, MIN_TEXT_STUFE, MAX_TEXT_STUFE,
                     "Schriftstufe der Beschriftung ausserhalb des Bereichs", wert, errOut, errSize))
        return false;
    s.labelSize = (uint8_t)wert;

    if (!leseBereich(doc["unitSize"], STD_UNIT_SIZE, MIN_TEXT_STUFE, MAX_TEXT_STUFE,
                     "Schriftstufe der Einheit ausserhalb des Bereichs", wert, errOut, errSize))
        return false;
    s.unitSize = (uint8_t)wert;

    // Kein Bereich zu pruefen: alles ausser einer echten Falschmeldung heisst "daneben".
    s.einheitDaneben = doc["einheitDaneben"] | STD_EINHEIT_DANEBEN;

    if (!leseBereich(doc["color"], 0xFFFFL, 0, 0xFFFFL, "Farbwert ausserhalb des Bereichs",
                     wert, errOut, errSize)) return false;
    s.color = (uint16_t)wert;

    if (!leseBereich(doc["anzeige"], ANZEIGE_ZAHL, 0, ANZEIGE_BALKEN_ZAHL,
                     "Darstellungsart unbekannt", wert, errOut, errSize)) return false;
    s.anzeige = (uint8_t)wert;
    s.barMin = doc["barMin"] | 0.0f;
    s.barMax = doc["barMax"] | 100.0f;
    // Nur pruefen, wenn ueberhaupt ein Balken gezeichnet wird -- sonst waeren
    // Vorgabewerte fuer eine reine Zahlenkachel ein Grund zur Ablehnung.
    if (s.anzeige != ANZEIGE_ZAHL && !(s.barMax > s.barMin)) {
        setErr(errOut, errSize, "Balken-Ende muss groesser als der Anfang sein");
        return false;
    }

    leseSchwelle(doc["warnAbove"], s.thr.hasWarnAbove, s.thr.warnAbove);
    leseSchwelle(doc["alarmAbove"], s.thr.hasAlarmAbove, s.thr.alarmAbove);
    leseSchwelle(doc["warnBelow"], s.thr.hasWarnBelow, s.thr.warnBelow);
    leseSchwelle(doc["alarmBelow"], s.thr.hasAlarmBelow, s.thr.alarmBelow);

    out = s;
    return true;
}

// Bequemlichkeitsfassung fuer die Host-Tests: JSON als Zeichenkette.
inline bool slotFromJson(const char* json, const Config& cfg, uint8_t slotIndex, Slot& out,
                         char* errOut, size_t errSize) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        setErr(errOut, errSize, "Eingabe ist kein gueltiges JSON");
        return false;
    }
    return slotFromDoc(doc, cfg, slotIndex, out, errOut, errSize);
}

// Uebernimmt die seitenweiten Einstellungen (Rotationsintervall, Layout je Seite,
// Warn- und Alarmfarbe). Getrennt von den Slots, weil sie fuer alle gelten.
//
// Verkleinert ein neues Layout die Zahl der Plaetze einer Seite, wuerden dort liegende
// Slots auf einen Platz zeigen, den es nicht mehr gibt. Deshalb wird das abgelehnt statt
// stillschweigend verschoben -- sonst laegen hinterher mehrere Kacheln uebereinander.
// TRANSAKTIONAL: Erst werden ALLE Werte gelesen und geprueft, erst danach wird EIN
// einziger Block zugewiesen. Frueher wurde waehrend der Pruefung zugewiesen -- eine
// abgelehnte Anfrage hinterliess dann einen halb geaenderten Zustand im RAM, den weder
// die Datei noch die Fehlermeldung kannte. Wer hier eine Pruefung ergaenzt: VOR den
// Zuweisungsblock, niemals dazwischen.
inline bool settingsFromDoc(JsonDocument& doc, Config& cfg, char* errOut, size_t errSize) {
    // ---- Lesen (als long: schmale Typen wuerden Riesenwerte modulo kuerzen) ----
    const long rotate = doc["rotateSec"] | (long)cfg.rotateSec;
    const long colorWarn = doc["colorWarn"] | (long)cfg.colorWarn;
    const long colorAlarm = doc["colorAlarm"] | (long)cfg.colorAlarm;
    const int helligkeit = doc["helligkeit"] | (int)cfg.helligkeit;
    const long modus = doc["hellModus"] | (long)cfg.hellModus;
    const long hellSec = doc["hellSec"] | (long)cfg.hellSec;
    const int hellAn = doc["hellAn"] | (int)cfg.hellAn;
    const int hellAus = doc["hellAus"] | (int)cfg.hellAus;
    const bool nachtAn = doc["nachtAn"] | cfg.nachtAn;
    const int nachtHell = doc["nachtHelligkeit"] | (int)cfg.nachtHelligkeit;
    const long von = doc["nachtVon"] | (long)cfg.nachtVon;
    const long bis = doc["nachtBis"] | (long)cfg.nachtBis;

    uint8_t neuesLayout[MAX_PAGES];
    uint8_t neueTeilung[MAX_PAGES];
    for (uint8_t i = 0; i < MAX_PAGES; i++) {
        neuesLayout[i] = cfg.layout[i];
        neueTeilung[i] = cfg.teilung[i];
    }

    char neueUrl[URL_LEN];
    snprintf(neueUrl, sizeof(neueUrl), "%s", cfg.hellUrl);
    char neuesFeld[FIELD_LEN];
    snprintf(neuesFeld, sizeof(neuesFeld), "%s", cfg.hellField);

    // ---- Pruefen ----
    if (rotate != 0 && (rotate < 3 || rotate > 3600)) {
        setErr(errOut, errSize, "Wechselintervall nur 0 (aus) oder 3 bis 3600 Sekunden");
        return false;
    }
    if (colorWarn < 0 || colorWarn > 0xFFFFL || colorAlarm < 0 || colorAlarm > 0xFFFFL) {
        setErr(errOut, errSize, "Farbwert ausserhalb des Bereichs");
        return false;
    }

    JsonArrayConst teilungen = doc["teilung"].as<JsonArrayConst>();
    if (!teilungen.isNull()) {
        for (uint8_t i = 0; i < MAX_PAGES && i < teilungen.size(); i++) {
            const long t = teilungen[i] | (long)TEILUNG_AUTOMATISCH;
            if (t != TEILUNG_AUTOMATISCH && (t < MIN_TEILUNG || t > MAX_TEILUNG)) {
                setErr(errOut, errSize,
                       "Aufteilung nur 0 (automatisch) oder 20 bis 80 Prozent");
                return false;
            }
            neueTeilung[i] = (uint8_t)t;
        }
    }

    JsonArrayConst layouts = doc["layout"].as<JsonArrayConst>();
    if (!layouts.isNull()) {
        for (uint8_t i = 0; i < MAX_PAGES && i < layouts.size(); i++) {
            const long l = layouts[i] | (long)LAYOUT_VIER;
            if (l < 0 || l > (long)LAYOUT_VIER) {
                setErr(errOut, errSize, "Unbekanntes Seitenlayout");
                return false;
            }
            neuesLayout[i] = (uint8_t)l;
        }
    }

    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        const Slot& s = cfg.slots[i];
        // AUCH abgeschaltete Slots zaehlen: Sie behalten ihren Rasterplatz (das ist die
        // Zusage der Belegungspruefung). Wuerden sie hier uebergangen, koennte ein
        // Layoutwechsel ihren Platz wegnehmen -- beim naechsten Laden landete der Slot
        // auf Platz 0, kollidierte dort und liesse sich nie wieder einschalten.
        if (s.url[0] == 0) continue;
        if (s.pos > maxPosForLayout((PageLayout)neuesLayout[s.page - 1])) {
            setErr(errOut, errSize,
                   "Ein Wert liegt auf einem Platz, den das neue Layout nicht hat");
            return false;
        }
    }

    if (modus < 0 || modus > 1) {
        setErr(errOut, errSize, "Unbekannter Helligkeits-Modus");
        return false;
    }
    if (!rohlaengePasst(doc["hellUrl"], URL_LEN) || !rohlaengePasst(doc["hellField"], FIELD_LEN)) {
        setErr(errOut, errSize, "Adresse oder Feld des Schalters ist zu lang");
        return false;
    }
    if (!doc["hellUrl"].isNull()) {
        kopiereText(neueUrl, sizeof(neueUrl), doc["hellUrl"]);
    }
    if (!doc["hellField"].isNull()) {
        kopiereText(neuesFeld, sizeof(neuesFeld), doc["hellField"]);
    }
    // Nur pruefen, wenn der Schalter ueberhaupt genutzt wird -- sonst waere ein leeres
    // Feld im festen Modus ein Grund zur Ablehnung.
    if (modus == 1 && !slotUrlValid(neueUrl)) {
        setErr(errOut, errSize, "Schalter braucht eine gueltige Adresse (nur http://)");
        return false;
    }
    // Immer pruefen, nicht nur bei aktivem Schalter: Ein Wert ausserhalb des Bereichs
    // wuerde beim Giessen in uint16 modulo gekuerzt und stuende dann falsch in der Datei.
    if (hellSec < 5 || hellSec > 3600) {
        setErr(errOut, errSize, "Schalter-Intervall nur 5 bis 3600 Sekunden");
        return false;
    }
    if (von < 0 || von > 23 || bis < 0 || bis > 23) {
        setErr(errOut, errSize, "Nachtmodus: Stunden nur 0 bis 23");
        return false;
    }

    // ---- Uebernehmen (ab hier kann nichts mehr scheitern) ----
    cfg.rotateSec = (uint16_t)rotate;
    for (uint8_t i = 0; i < MAX_PAGES; i++) {
        cfg.layout[i] = neuesLayout[i];
        cfg.teilung[i] = neueTeilung[i];
    }
    cfg.colorWarn = (uint16_t)colorWarn;
    cfg.colorAlarm = (uint16_t)colorAlarm;
    cfg.helligkeit = helligkeitKlemmen(helligkeit);
    cfg.hellModus = (uint8_t)modus;
    snprintf(cfg.hellUrl, sizeof(cfg.hellUrl), "%s", neueUrl);
    snprintf(cfg.hellField, sizeof(cfg.hellField), "%s", neuesFeld);
    cfg.hellSec = (uint16_t)hellSec;
    cfg.hellAn = helligkeitKlemmen(hellAn);
    cfg.hellAus = helligkeitKlemmen(hellAus);
    cfg.nachtAn = nachtAn;
    cfg.nachtHelligkeit = helligkeitKlemmen(nachtHell);
    cfg.nachtVon = (uint8_t)von;
    cfg.nachtBis = (uint8_t)bis;
    return true;
}

inline void schreibeSchwelle(JsonObject o, const char* name, bool has, float wert) {
    if (has)
        o[name] = wert;
    else
        o[name] = nullptr;
}

// Ein Slot als JSON-Objekt. Liegt hier, damit Persistenz und Web-Schnittstelle
// dieselbe Darstellung verwenden -- zwei Fassungen wuerden frueher oder spaeter
// auseinanderlaufen.
inline void slotToObject(const Slot& s, JsonObject o) {
    o["enabled"] = s.enabled;
    o["url"] = s.url;
    o["field"] = s.field;
    o["label"] = s.label;
    o["unit"] = s.unit;
    o["decimals"] = s.decimals;
    o["refreshSec"] = s.refreshSec;
    o["page"] = s.page;
    o["pos"] = s.pos;
    o["wertSize"] = s.wertSize;
    o["labelSize"] = s.labelSize;
    o["unitSize"] = s.unitSize;
    o["einheitDaneben"] = s.einheitDaneben;
    o["color"] = s.color;
    o["anzeige"] = s.anzeige;
    o["barMin"] = s.barMin;
    o["barMax"] = s.barMax;
    schreibeSchwelle(o, "warnAbove", s.thr.hasWarnAbove, s.thr.warnAbove);
    schreibeSchwelle(o, "alarmAbove", s.thr.hasAlarmAbove, s.thr.alarmAbove);
    schreibeSchwelle(o, "warnBelow", s.thr.hasWarnBelow, s.thr.warnBelow);
    schreibeSchwelle(o, "alarmBelow", s.thr.hasAlarmBelow, s.thr.alarmBelow);
}

inline void configToDoc(const Config& cfg, JsonDocument& doc) {
    doc["rotateSec"] = cfg.rotateSec;
    doc["colorWarn"] = cfg.colorWarn;
    doc["colorAlarm"] = cfg.colorAlarm;
    doc["helligkeit"] = cfg.helligkeit;
    doc["hellModus"] = cfg.hellModus;
    doc["hellUrl"] = cfg.hellUrl;
    doc["hellField"] = cfg.hellField;
    doc["hellSec"] = cfg.hellSec;
    doc["hellAn"] = cfg.hellAn;
    doc["hellAus"] = cfg.hellAus;
    doc["nachtAn"] = cfg.nachtAn;
    doc["nachtVon"] = cfg.nachtVon;
    doc["nachtBis"] = cfg.nachtBis;
    doc["nachtHelligkeit"] = cfg.nachtHelligkeit;

    JsonArray layouts = doc["layout"].to<JsonArray>();
    for (uint8_t i = 0; i < MAX_PAGES; i++) layouts.add(cfg.layout[i]);

    JsonArray teilungen = doc["teilung"].to<JsonArray>();
    for (uint8_t i = 0; i < MAX_PAGES; i++) teilungen.add(cfg.teilung[i]);

    JsonArray slots = doc["slots"].to<JsonArray>();
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        slotToObject(cfg.slots[i], slots.add<JsonObject>());
    }
}

inline size_t configToJson(const Config& cfg, char* out, size_t outSize) {
    JsonDocument doc;
    configToDoc(cfg, doc);
    return serializeJson(doc, out, outSize);
}

// Schreibt das Ergebnis erst nach vollstaendigem Erfolg nach out: eine kaputte Datei
// darf keine halb befuellte Konfiguration hinterlassen.
// Uebernimmt ein bereits eingelesenes Dokument. Getrennt von configFromJson, damit die
// Firmware direkt aus der Datei lesen kann, ohne sie vorher komplett in den Heap zu holen.
//
// Geschrieben wird DIREKT nach out -- ohne Zwischenstand.
// Ein lokaler wäre mit 2940 Byte über zwei Drittel des 4096 Byte grossen Task-Stacks,
// ein statischer würde dieselbe Menge dauerhaft belegen. Beides ist unnötig: Nach dem
// erfolgreichen Parsen kann diese Funktion nicht mehr scheitern, sie klemmt nur noch
// Werte. Es gibt also keinen Fall, in dem out halb gefüllt zurückbliebe. Deshalb auch
// void ohne Fehlerparameter -- die fruehere bool/errOut-Signatur suggerierte den
// Aufrufern eine Fehlerbehandlung, die es nie gab.
inline void configFromDoc(JsonDocument& doc, Config& out) {
    Config& c = out;
    c = Config{};
    c.rotateSec = doc["rotateSec"] | STD_ROTATE_SEC;
    // Klemmen wie alle anderen Bereichsfelder auch: 0 bleibt "aus", sonst 3..3600.
    // rotateSec war das einzige ungekappte Feld -- eine von Hand editierte Datei mit
    // rotateSec 1 haette sekuendliche Vollbild-Wechsel erzeugt.
    if (c.rotateSec != 0 && c.rotateSec < 3) c.rotateSec = 3;
    if (c.rotateSec > 3600) c.rotateSec = 3600;
    c.helligkeit = helligkeitKlemmen(doc["helligkeit"] | (int)STD_HELLIGKEIT);
    c.hellModus = (doc["hellModus"] | 0) == 1 ? 1 : 0;
    kopiereText(c.hellUrl, sizeof(c.hellUrl), doc["hellUrl"]);
    kopiereText(c.hellField, sizeof(c.hellField), doc["hellField"]);
    c.hellSec = doc["hellSec"] | STD_SCHALTER_SEC;
    if (c.hellSec < 5) c.hellSec = 5;
    if (c.hellSec > 3600) c.hellSec = 3600;
    c.hellAn = helligkeitKlemmen(doc["hellAn"] | (int)STD_HELLIGKEIT);
    c.hellAus = helligkeitKlemmen(doc["hellAus"] | (int)STD_NACHT_HELLIGKEIT);
    c.nachtAn = doc["nachtAn"] | false;
    c.nachtVon = (uint8_t)(doc["nachtVon"] | STD_NACHT_VON);
    c.nachtBis = (uint8_t)(doc["nachtBis"] | STD_NACHT_BIS);
    c.nachtHelligkeit = helligkeitKlemmen(doc["nachtHelligkeit"] | (int)STD_NACHT_HELLIGKEIT);
    if (c.nachtVon > 23) c.nachtVon = STD_NACHT_VON;
    if (c.nachtBis > 23) c.nachtBis = STD_NACHT_BIS;
    c.colorWarn = doc["colorWarn"] | FARBE_WARNUNG;
    c.colorAlarm = doc["colorAlarm"] | FARBE_ALARM;

    JsonArrayConst layouts = doc["layout"].as<JsonArrayConst>();
    for (uint8_t i = 0; i < MAX_PAGES; i++) {
        uint8_t l = (i < layouts.size()) ? (uint8_t)(layouts[i] | (uint8_t)LAYOUT_VIER)
                                         : (uint8_t)LAYOUT_VIER;
        c.layout[i] = (l > LAYOUT_VIER) ? (uint8_t)LAYOUT_VIER : l;
    }

    // Aeltere Dateien kennen die Aufteilung nicht -- dort gilt "automatisch".
    JsonArrayConst teilungen = doc["teilung"].as<JsonArrayConst>();
    for (uint8_t i = 0; i < MAX_PAGES; i++) {
        const uint8_t t = (i < teilungen.size())
                              ? (uint8_t)(teilungen[i] | (uint8_t)TEILUNG_AUTOMATISCH)
                              : (uint8_t)TEILUNG_AUTOMATISCH;
        c.teilung[i] = (t != TEILUNG_AUTOMATISCH && (t < MIN_TEILUNG || t > MAX_TEILUNG))
                           ? (uint8_t)TEILUNG_AUTOMATISCH
                           : t;
    }

    JsonArrayConst slots = doc["slots"].as<JsonArrayConst>();
    for (uint8_t i = 0; i < MAX_SLOTS && i < slots.size(); i++) {
        JsonObjectConst o = slots[i].as<JsonObjectConst>();
        if (o.isNull()) continue;
        Slot& s = c.slots[i];
        s.enabled = o["enabled"] | false;
        kopiereText(s.url, sizeof(s.url), o["url"]);
        kopiereText(s.field, sizeof(s.field), o["field"]);
        kopiereText(s.label, sizeof(s.label), o["label"]);
        kopiereText(s.unit, sizeof(s.unit), o["unit"]);
        // Aus der Datei gelesene Werte werden geklemmt, nicht blind uebernommen:
        // eine beschaedigte oder von Hand bearbeitete Datei darf keinen Zugriff
        // ausserhalb der Arrays ausloesen (page speist layout[page-1]).
        s.decimals = o["decimals"] | 0;
        if (s.decimals > 3) s.decimals = 0;
        s.refreshSec = o["refreshSec"] | (uint16_t)30;
        if (s.refreshSec < 5) s.refreshSec = 5;
        if (s.refreshSec > 3600) s.refreshSec = 3600;
        s.page = o["page"] | (uint8_t)1;
        if (s.page < 1 || s.page > MAX_PAGES) s.page = 1;
        s.pos = o["pos"] | (uint8_t)0;
        if (s.pos > maxPosForLayout((PageLayout)c.layout[s.page - 1])) s.pos = 0;
        // Bis v0.1.0 gab es EINE Stufe "fontSize" mit den Werten 0/1/2 (klein/mittel/gross),
        // die erst beim Zeichnen auf 2/3/5 abgebildet wurde. Wer so eine Datei liegen hat,
        // soll seine Einstellung behalten statt kommentarlos auf den Standard zu fallen.
        const uint8_t altStufe = uebernahmeAlteStufe(o["fontSize"]);
        s.wertSize = leseStufe(o["wertSize"], altStufe != 0 ? altStufe : STD_WERT_SIZE);
        s.labelSize = leseStufe(o["labelSize"], STD_LABEL_SIZE);
        s.unitSize = leseStufe(o["unitSize"], STD_UNIT_SIZE);
        // Aeltere Dateien kennen das Feld nicht -- dort gilt die neue Vorgabe.
        s.einheitDaneben = o["einheitDaneben"] | STD_EINHEIT_DANEBEN;
        s.color = o["color"] | (uint16_t)0xFFFF;
        s.anzeige = o["anzeige"] | (uint8_t)ANZEIGE_ZAHL;
        if (s.anzeige > ANZEIGE_BALKEN_ZAHL) s.anzeige = ANZEIGE_ZAHL;
        s.barMin = o["barMin"] | 0.0f;
        s.barMax = o["barMax"] | 100.0f;
        leseSchwelle(o["warnAbove"], s.thr.hasWarnAbove, s.thr.warnAbove);
        leseSchwelle(o["alarmAbove"], s.thr.hasAlarmAbove, s.thr.alarmAbove);
        leseSchwelle(o["warnBelow"], s.thr.hasWarnBelow, s.thr.warnBelow);
        leseSchwelle(o["alarmBelow"], s.thr.hasAlarmBelow, s.thr.alarmBelow);
    }
}

// Bequemlichkeitsfassung fuer die Host-Tests: JSON als Zeichenkette.
inline bool configFromJson(const char* json, Config& out, char* errOut, size_t errSize) {
    if (json == nullptr || json[0] == 0) {
        setErr(errOut, errSize, "leere Konfiguration");
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        setErr(errOut, errSize, "Konfiguration ist kein gueltiges JSON");
        return false;
    }
    configFromDoc(doc, out);
    return true;
}
