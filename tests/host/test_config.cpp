// SPDX-License-Identifier: GPL-3.0-or-later
// Host-Unit-Tests des Konfigurationsmodells und seines JSON-Codecs:
//   c++ -std=c++17 -I smalltv/firmware/.pio/libdeps/esp12e/ArduinoJson/src \
//       smalltv/tests/host/test_config.cpp -o /tmp/smalltv-config && /tmp/smalltv-config
#include "pruefe.h"
#include <cstdio>
#include <cstring>
#include "../../firmware/include/config_codec.h"

static Config leereConfig() {
    Config c = {};
    for (uint8_t i = 0; i < MAX_PAGES; i++) c.layout[i] = LAYOUT_VIER;
    c.rotateSec = 10;
    return c;
}

int main() {
    char err[96];
    Config cfg = leereConfig();
    Slot s = {};

    // --- Gueltiger Slot wird uebernommen ---
    const char* ok =
        "{\"enabled\":true,"
        "\"url\":\"http://192.0.2.10:8082/rest-api/v1/state/x/plain\","
        "\"field\":\"\",\"label\":\"Verbrauch\",\"unit\":\"W\","
        "\"decimals\":0,\"refreshSec\":30,\"page\":1,\"pos\":0,"
        "\"wertSize\":7,\"labelSize\":4,\"unitSize\":2,"
        "\"color\":65535,\"warnAbove\":3000,\"alarmAbove\":5000}";
    PRUEFE(slotFromJson(ok, cfg, 0, s, err, sizeof(err)));
    PRUEFE(s.enabled && strcmp(s.label, "Verbrauch") == 0 && s.refreshSec == 30);
    // Die drei Stufen sind unabhaengig: eine Beschriftung darf groesser sein
    // als die Einheit, und nichts wird gegeneinander verrechnet.
    PRUEFE(s.wertSize == 7 && s.labelSize == 4 && s.unitSize == 2);
    // Feld nicht genannt -> Vorgabe "neben dem Wert". Aeltere Oberflaechen und
    // Sicherungsdateien kennen es nicht; sie duerfen deshalb nicht abgewiesen werden.
    PRUEFE(s.einheitDaneben == STD_EINHEIT_DANEBEN);
    PRUEFE(s.thr.hasWarnAbove && s.thr.warnAbove == 3000.0f);
    PRUEFE(s.thr.hasAlarmAbove && s.thr.alarmAbove == 5000.0f);
    PRUEFE(!s.thr.hasWarnBelow && !s.thr.hasAlarmBelow);  // nicht genannt = nicht gesetzt

    // --- Beschriftung darf leer bleiben (seit v0.2.6) ---
    {
        Slot leer = {};
        char e2[96] = {0};
        const char* ohneLabel =
            "{\"enabled\":true,"
            "\"url\":\"http://192.0.2.10:8082/rest-api/v1/state/x/plain\","
            "\"label\":\"\",\"unit\":\"% (Week)\","
            "\"refreshSec\":30,\"page\":1,\"pos\":0}";
        PRUEFE(slotFromJson(ohneLabel, cfg, 1, leer, e2, sizeof(e2)));
        PRUEFE(leer.label[0] == 0);
        // 8 Zeichen Einheit -- vor v0.2.6 passten nur 7, "% (Week)" wurde abgeschnitten.
        PRUEFE(strcmp(leer.unit, "% (Week)") == 0);
    }
    {
        // 15 Zeichen sind die Grenze, 16 nicht mehr -- und zu lang wird ABGEWIESEN,
        // nicht stillschweigend gekuerzt.
        Slot lang = {};
        char e3[96] = {0};
        char json[320];
        snprintf(json, sizeof(json),
                 "{\"enabled\":true,"
                 "\"url\":\"http://192.0.2.10/x\",\"label\":\"A\",\"unit\":\"%s\","
                 "\"refreshSec\":30,\"page\":1,\"pos\":0}",
                 "123456789012345");
        PRUEFE(slotFromJson(json, cfg, 1, lang, e3, sizeof(e3)));
        PRUEFE(strlen(lang.unit) == 15);

        snprintf(json, sizeof(json),
                 "{\"enabled\":true,"
                 "\"url\":\"http://192.0.2.10/x\",\"label\":\"A\",\"unit\":\"%s\","
                 "\"refreshSec\":30,\"page\":1,\"pos\":0}",
                 "1234567890123456");
        e3[0] = 0;
        PRUEFE(!slotFromJson(json, cfg, 1, lang, e3, sizeof(e3)));
        PRUEFE(e3[0] != 0);
    }

    // --- Jede Verletzung wird abgewiesen, mit Meldung ---
    err[0] = 0;
    PRUEFE(!slotFromJson("{\"url\":\"https://x\",\"label\":\"A\",\"refreshSec\":30,\"page\":1,\"pos\":0}",
                         cfg, 0, s, err, sizeof(err)));
    PRUEFE(err[0] != 0);  // TLS-URL

    // Eine LEERE Beschriftung ist seit v0.2.6 erlaubt (frueher abgewiesen) -- eine zu
    // lange bleibt ein Fehler.
    err[0] = 0;
    PRUEFE(slotFromJson("{\"url\":\"http://192.0.2.10/a\",\"label\":\"\",\"refreshSec\":30,\"page\":1,\"pos\":0}",
                        cfg, 0, s, err, sizeof(err)));
    err[0] = 0;
    PRUEFE(!slotFromJson("{\"url\":\"http://192.0.2.10/a\","
                         "\"label\":\"123456789012345678901234\",\"refreshSec\":30,\"page\":1,\"pos\":0}",
                         cfg, 0, s, err, sizeof(err)));
    PRUEFE(err[0] != 0);  // 24 Zeichen Beschriftung

    // refreshSec ausserhalb 5..3600
    err[0] = 0;
    PRUEFE(!slotFromJson("{\"url\":\"http://192.0.2.10/a\",\"label\":\"A\",\"refreshSec\":2,\"page\":1,\"pos\":0}",
                         cfg, 0, s, err, sizeof(err)));
    PRUEFE(err[0] != 0);
    err[0] = 0;
    PRUEFE(!slotFromJson("{\"url\":\"http://192.0.2.10/a\",\"label\":\"A\",\"refreshSec\":99999,\"page\":1,\"pos\":0}",
                         cfg, 0, s, err, sizeof(err)));
    PRUEFE(err[0] != 0);

    // Seite ausserhalb 1..MAX_PAGES
    err[0] = 0;
    PRUEFE(!slotFromJson("{\"url\":\"http://192.0.2.10/a\",\"label\":\"A\",\"refreshSec\":30,\"page\":9,\"pos\":0}",
                         cfg, 0, s, err, sizeof(err)));
    PRUEFE(err[0] != 0);

    // --- Rasterplatz haengt am Layout der Seite ---
    PRUEFE(maxPosForLayout(LAYOUT_EINS) == 0);
    PRUEFE(maxPosForLayout(LAYOUT_ZWEI) == 1);
    PRUEFE(maxPosForLayout(LAYOUT_VIER) == 3);

    cfg.layout[0] = LAYOUT_EINS;  // Seite 1 zeigt nur eine Kachel
    err[0] = 0;
    PRUEFE(!slotFromJson("{\"url\":\"http://192.0.2.10/a\",\"label\":\"A\",\"refreshSec\":30,\"page\":1,\"pos\":2}",
                         cfg, 0, s, err, sizeof(err)));
    PRUEFE(err[0] != 0);  // pos 2 gibt es dort nicht

    // --- Belegter Rasterplatz wird erkannt ---
    cfg.layout[0] = LAYOUT_VIER;
    cfg.slots[1].enabled = true;
    // Eingerichtet heisst: hat eine Adresse. Ohne sie ist der Slot leer und blockiert nichts.
    snprintf(cfg.slots[1].url, URL_LEN, "http://192.0.2.10/belegt");
    cfg.slots[1].page = 1;
    cfg.slots[1].pos = 0;
    err[0] = 0;
    PRUEFE(!slotFromJson(ok, cfg, 0, s, err, sizeof(err)));  // Slot 0 will auch Seite1/Platz0
    PRUEFE(err[0] != 0);
    // derselbe Slot darf seinen eigenen Platz behalten
    PRUEFE(slotFromJson(ok, cfg, 1, s, err, sizeof(err)));

    // --- Ein abgeschalteter Slot haelt seinen Platz ---
    // Sonst waere Abschalten eine Falle: Platz weg, Wiedereinschalten unmoeglich.
    cfg.slots[1].enabled = false;   // Slot 1 liegt weiterhin auf Seite 1 / Platz 0
    snprintf(cfg.slots[1].url, URL_LEN, "http://192.0.2.10/belegt");
    err[0] = 0;
    PRUEFE(!slotFromJson(ok, cfg, 0, s, err, sizeof(err)));
    PRUEFE(err[0] != 0);
    // Ein wirklich leerer Slot blockiert dagegen nichts
    cfg.slots[1].url[0] = 0;
    PRUEFE(slotFromJson(ok, cfg, 0, s, err, sizeof(err)));
    cfg.slots[1].enabled = true;
    snprintf(cfg.slots[1].url, URL_LEN, "http://192.0.2.10/belegt");

    // --- Balken-Darstellung ---
    const char* balken =
        "{\"url\":\"http://192.0.2.10/a\",\"label\":\"Fuellstand\",\"refreshSec\":30,"
        "\"page\":2,\"pos\":0,\"anzeige\":2,\"barMin\":0,\"barMax\":100}";
    PRUEFE(slotFromJson(balken, cfg, 0, s, err, sizeof(err)));
    PRUEFE(s.anzeige == ANZEIGE_BALKEN_ZAHL && s.barMin == 0.0f && s.barMax == 100.0f);

    // Unbrauchbare Skala wird nur beanstandet, wenn ueberhaupt ein Balken gezeichnet wird
    err[0] = 0;
    PRUEFE(!slotFromJson("{\"url\":\"http://192.0.2.10/a\",\"label\":\"A\",\"refreshSec\":30,"
                         "\"page\":2,\"pos\":1,\"anzeige\":1,\"barMin\":50,\"barMax\":50}",
                         cfg, 0, s, err, sizeof(err)));
    PRUEFE(err[0] != 0);
    // dieselbe Skala stoert bei einer reinen Zahlenkachel nicht
    PRUEFE(slotFromJson("{\"url\":\"http://192.0.2.10/a\",\"label\":\"A\",\"refreshSec\":30,"
                        "\"page\":2,\"pos\":1,\"anzeige\":0,\"barMin\":50,\"barMax\":50}",
                        cfg, 0, s, err, sizeof(err)));
    // unbekannte Darstellungsart
    err[0] = 0;
    PRUEFE(!slotFromJson("{\"url\":\"http://192.0.2.10/a\",\"label\":\"A\",\"refreshSec\":30,"
                         "\"page\":2,\"pos\":2,\"anzeige\":9}",
                         cfg, 0, s, err, sizeof(err)));
    PRUEFE(err[0] != 0);

    // --- Zu lange Eingaben werden ABGELEHNT, nicht still gekuerzt ---
    // Sonst riefe das Geraet eine andere Adresse ab als eingetragen.
    {
        char langeUrl[300];
        snprintf(langeUrl, sizeof(langeUrl),
                 "{\"url\":\"http://192.0.2.10/%0*d\",\"label\":\"A\",\"refreshSec\":30,"
                 "\"page\":3,\"pos\":0}", 200, 1);
        err[0] = 0;
        PRUEFE(!slotFromJson(langeUrl, cfg, 0, s, err, sizeof(err)));
        PRUEFE(err[0] != 0);
    }
    err[0] = 0;
    PRUEFE(!slotFromJson("{\"url\":\"http://192.0.2.10/a\",\"label\":\"viel-zu-lange-beschriftung-xyz\","
                         "\"refreshSec\":30,\"page\":3,\"pos\":0}", cfg, 0, s, err, sizeof(err)));
    PRUEFE(err[0] != 0);

    // --- Eine beschaedigte Datei darf keine unmoeglichen Werte durchlassen ---
    {
        // Datei aus v0.1.0: nur "fontSize" (0/1/2). Die Einstellung soll erhalten
        // bleiben statt kommentarlos auf den Standard zu fallen -- gross war 5.
        Config alt = {};
        err[0] = 0;
        PRUEFE(configFromJson("{\"slots\":[{\"enabled\":true,\"url\":\"http://192.0.2.10/a\","
                              "\"label\":\"A\",\"fontSize\":2}]}",
                              alt, err, sizeof(err)));
        PRUEFE(alt.slots[0].wertSize == 5);
        PRUEFE(alt.slots[0].labelSize == STD_LABEL_SIZE);
        PRUEFE(alt.slots[0].unitSize == STD_UNIT_SIZE);
    }

    {
        Config kaputt = {};
        err[0] = 0;
        PRUEFE(configFromJson("{\"slots\":[{\"enabled\":true,\"url\":\"http://192.0.2.10/a\","
                              "\"label\":\"A\",\"page\":99,\"pos\":99,\"decimals\":99,"
                              "\"wertSize\":99,\"labelSize\":0,\"anzeige\":99,\"refreshSec\":99999}]}",
                              kaputt, err, sizeof(err)));
        // geklemmt statt uebernommen -- sonst greift alarmSeite() auf layout[98] zu
        PRUEFE(kaputt.slots[0].page >= 1 && kaputt.slots[0].page <= MAX_PAGES);
        PRUEFE(kaputt.slots[0].pos <= 3);
        PRUEFE(kaputt.slots[0].decimals <= 3);
        PRUEFE(kaputt.slots[0].wertSize >= MIN_TEXT_STUFE &&
               kaputt.slots[0].wertSize <= MAX_TEXT_STUFE);
        PRUEFE(kaputt.slots[0].labelSize >= MIN_TEXT_STUFE);
        PRUEFE(kaputt.slots[0].unitSize >= MIN_TEXT_STUFE);
        PRUEFE(kaputt.slots[0].anzeige <= ANZEIGE_BALKEN_ZAHL);
        PRUEFE(kaputt.slots[0].refreshSec >= 5 && kaputt.slots[0].refreshSec <= 3600);
    }

    // --- Text und Wahrheitswerte sind keine Schwelle ---
    // Sonst wuerde "" oder "abc" als Schwelle 0.0 gelesen: Der Slot staende bei jedem
    // positiven Messwert dauerhaft im Alarm und hielte die Seitenrotation an.
    PRUEFE(slotFromJson("{\"url\":\"http://192.0.2.10/a\",\"label\":\"A\",\"refreshSec\":30,"
                        "\"page\":4,\"pos\":0,\"warnAbove\":\"\",\"alarmAbove\":\"abc\","
                        "\"warnBelow\":true}", cfg, 0, s, err, sizeof(err)));
    PRUEFE(!s.thr.hasWarnAbove && !s.thr.hasAlarmAbove && !s.thr.hasWarnBelow);
    PRUEFE(slotFromJson("{\"url\":\"http://192.0.2.10/a\",\"label\":\"A\",\"refreshSec\":30,"
                        "\"page\":4,\"pos\":1,\"warnAbove\":3000,\"alarmBelow\":-5.5}",
                        cfg, 0, s, err, sizeof(err)));
    PRUEFE(s.thr.hasWarnAbove && s.thr.warnAbove == 3000.0f);
    PRUEFE(s.thr.hasAlarmBelow && s.thr.alarmBelow == -5.5f);

    // --- Einheit mit Gradzeichen geht durch, ein nicht darstellbares Zeichen nicht (N6) ---
    {
        Config frei = leereConfig();
        Slot g = {};
        char e4[96] = {0};
        PRUEFE(slotFromJson("{\"url\":\"http://192.0.2.10/a\",\"label\":\"Aussen\",\"unit\":\"\\u00b0C\","
                            "\"refreshSec\":30,\"page\":3,\"pos\":0}", frei, 0, g, e4, sizeof(e4)));
        PRUEFE(strcmp(g.unit, "\xc2\xb0" "C") == 0);
        e4[0] = 0;
        PRUEFE(!slotFromJson("{\"url\":\"http://192.0.2.10/a\",\"label\":\"A\",\"unit\":\"\\u03a9\","
                             "\"refreshSec\":30,\"page\":3,\"pos\":0}", frei, 0, g, e4, sizeof(e4)));
        PRUEFE(strstr(e4, "Display") != nullptr);
        // Der Feldname wird nie gezeichnet -- ihn trifft die Display-Regel nicht.
        e4[0] = 0;
        PRUEFE(slotFromJson("{\"url\":\"http://192.0.2.10/a\",\"label\":\"A\",\"field\":\"\\u03a9\","
                            "\"refreshSec\":30,\"page\":3,\"pos\":0}", frei, 0, g, e4, sizeof(e4)));
    }

    // --- Kachelzeilen aus den Einstellungen eines Slots (B2) ---
    {
        Slot z = {};
        snprintf(z.label, LABEL_LEN, "A");
        snprintf(z.unit, UNIT_LEN, "%%");
        z.anzeige = ANZEIGE_BALKEN_ZAHL;
        z.einheitDaneben = true;
        z.labelSize = 2;
        z.wertSize = 6;
        z.unitSize = 3;
        const KachelZeilen k = kachelZeilenAus(z, false);
        PRUEFE(k.mitLabel && k.mitZahl && k.mitBalken && k.einheitDaneben && !k.mitUnterzeile);
        PRUEFE(k.labelStufe == 2 && k.wertStufe == 6 && k.unitStufe == 3);
        // Die Unterzeile kommt vom Aufrufer -- nur er kennt den Laufzeitzustand
        // (Veraltet-Hinweis). Wuerde sie aus den Einstellungen abgeleitet, spraenge
        // die Seitenaufteilung, sobald ein Wert veraltet.
        PRUEFE(kachelZeilenAus(z, true).mitUnterzeile);
        // Ohne Einheit steht sie auch nicht neben dem Wert.
        Slot ohne = z;
        ohne.unit[0] = 0;
        PRUEFE(!kachelZeilenAus(ohne, false).einheitDaneben);
        // Reine Balkenanzeige: keine Zahl, also auch keine Einheit daneben.
        Slot balken = z;
        balken.anzeige = ANZEIGE_BALKEN;
        PRUEFE(!kachelZeilenAus(balken, false).mitZahl && kachelZeilenAus(balken, false).mitBalken);
        PRUEFE(!kachelZeilenAus(balken, false).einheitDaneben);
    }

    // --- Rundlauf: Config -> JSON -> Config ergibt dasselbe ---
    Config a = leereConfig();
    a.rotateSec = 25;
    a.colorWarn = 64800;
    a.colorAlarm = 63488;
    a.layout[2] = LAYOUT_ZWEI;
    a.slots[0].enabled = true;
    snprintf(a.slots[0].url, URL_LEN, "http://192.0.2.10:8082/rest-api/v1/state/y/plain");
    snprintf(a.slots[0].label, LABEL_LEN, "USV");
    snprintf(a.slots[0].unit, UNIT_LEN, "%%");
    a.slots[0].page = 2;
    a.slots[0].pos = 1;
    a.slots[0].decimals = 1;
    a.slots[0].refreshSec = 60;
    a.slots[0].wertSize = 8;
    a.slots[0].labelSize = 5;
    a.slots[0].unitSize = 1;
    a.slots[0].einheitDaneben = false;
    a.slots[0].color = 65535;
    a.slots[0].thr.hasAlarmBelow = true;
    a.slots[0].thr.alarmBelow = 20.0f;
    a.slots[0].anzeige = ANZEIGE_BALKEN_ZAHL;
    a.slots[0].barMin = 0.0f;
    a.slots[0].barMax = 100.0f;

    char buf[4096];
    size_t n = configToJson(a, buf, sizeof(buf));
    PRUEFE(n > 0 && n < sizeof(buf));

    Config b = {};
    err[0] = 0;
    PRUEFE(configFromJson(buf, b, err, sizeof(err)));
    PRUEFE(b.rotateSec == 25 && b.colorWarn == 64800 && b.colorAlarm == 63488);
    PRUEFE(b.layout[2] == LAYOUT_ZWEI);
    PRUEFE(b.slots[0].enabled && strcmp(b.slots[0].label, "USV") == 0);
    PRUEFE(strcmp(b.slots[0].unit, "%") == 0);
    PRUEFE(b.slots[0].page == 2 && b.slots[0].pos == 1 && b.slots[0].refreshSec == 60);
    PRUEFE(b.slots[0].decimals == 1);
    PRUEFE(b.slots[0].wertSize == 8 && b.slots[0].labelSize == 5 &&
           b.slots[0].unitSize == 1);
    // Ein abgeschalteter Schalter muss den Weg durch die Datei ueberleben -- ginge er
    // beim Speichern verloren, spraenge die Einheit beim naechsten Start zurueck.
    PRUEFE(b.slots[0].einheitDaneben == false);
    PRUEFE(b.slots[0].thr.hasAlarmBelow && b.slots[0].thr.alarmBelow == 20.0f);
    PRUEFE(!b.slots[0].thr.hasWarnAbove);
    PRUEFE(b.slots[0].anzeige == ANZEIGE_BALKEN_ZAHL);
    PRUEFE(b.slots[0].barMin == 0.0f && b.slots[0].barMax == 100.0f);

    // --- Kaputte Datei fuehrt nicht zu halb gefuellter Config ---
    Config c = {};
    err[0] = 0;
    PRUEFE(!configFromJson("{kaputt", c, err, sizeof(err)));
    PRUEFE(err[0] != 0);

    // --- Ganzzahl-Haertung: Riesenwerte werden ABGELEHNT, nicht modulo gekuerzt ---
    // 65541 wuerde in uint16 zu 5 und bestuende die 5..3600-Pruefung; der Aufrufer
    // bekaeme "ok" fuer etwas, das er nie eingestellt hat.
    err[0] = 0;
    PRUEFE(!slotFromJson("{\"url\":\"http://192.0.2.10/a\",\"label\":\"A\",\"refreshSec\":65541,\"page\":1,\"pos\":0}",
                         cfg, 0, s, err, sizeof(err)));
    PRUEFE(err[0] != 0);
    err[0] = 0;
    PRUEFE(!slotFromJson("{\"url\":\"http://192.0.2.10/a\",\"label\":\"A\",\"refreshSec\":30,\"page\":257,\"pos\":0}",
                         cfg, 0, s, err, sizeof(err)));
    PRUEFE(err[0] != 0);

    // --- settingsFromDoc ist TRANSAKTIONAL: Ablehnung laesst cfg unveraendert ---
    {
        Config vorher = leereConfig();
        vorher.hellSec = 30;  // wie im Geraet: Defaults/Klemmung liefern nie 0
        vorher.helligkeit = 70;
        vorher.colorWarn = 111;
        Config nachher = vorher;
        JsonDocument doc;
        // rotateSec/colorWarn/helligkeit gueltig, aber nachtVon ungueltig -> Ablehnung.
        PRUEFE(!deserializeJson(doc,
            "{\"rotateSec\":600,\"colorWarn\":222,\"helligkeit\":10,\"nachtVon\":99}"));
        err[0] = 0;
        PRUEFE(!settingsFromDoc(doc, nachher, err, sizeof(err)));
        PRUEFE(err[0] != 0);
        PRUEFE(nachher.rotateSec == vorher.rotateSec);      // frueher: schon 600
        PRUEFE(nachher.colorWarn == vorher.colorWarn);      // frueher: schon 222
        PRUEFE(nachher.helligkeit == vorher.helligkeit);    // frueher: schon 10
    }

    // --- settingsFromDoc: Erfolg uebernimmt alles ---
    {
        Config zc = leereConfig();
        zc.hellSec = 30;  // wie im Geraet: Defaults/Klemmung liefern nie 0
        JsonDocument doc;
        PRUEFE(!deserializeJson(doc,
            "{\"rotateSec\":0,\"helligkeit\":55,\"nachtAn\":true,\"nachtVon\":21,\"nachtBis\":6}"));
        err[0] = 0;
        PRUEFE(settingsFromDoc(doc, zc, err, sizeof(err)));
        PRUEFE(zc.rotateSec == 0 && zc.helligkeit == 55);
        PRUEFE(zc.nachtAn && zc.nachtVon == 21 && zc.nachtBis == 6);
    }

    // --- Aufteilung der Seite (2 Werte uebereinander) ---
    {
        Config zc = leereConfig();
        zc.hellSec = 30;
        JsonDocument doc;
        PRUEFE(!deserializeJson(doc, "{\"teilung\":[0,30,80,0]}"));
        err[0] = 0;
        PRUEFE(settingsFromDoc(doc, zc, err, sizeof(err)));
        PRUEFE(zc.teilung[0] == TEILUNG_AUTOMATISCH && zc.teilung[1] == 30 &&
               zc.teilung[2] == 80);

        // Ausserhalb 20..80 wird abgewiesen, nicht stillschweigend geklemmt -- sonst
        // bekaeme der Nutzer "gespeichert" fuer etwas, das er nie eingestellt hat.
        JsonDocument doc2;
        PRUEFE(!deserializeJson(doc2, "{\"teilung\":[0,90,0,0]}"));
        err[0] = 0;
        PRUEFE(!settingsFromDoc(doc2, zc, err, sizeof(err)));
        PRUEFE(err[0] != 0);
        PRUEFE(zc.teilung[1] == 30);  // transaktional: der alte Wert steht noch

        // Eine Datei ohne das Feld (aelteres Geraet) heisst "automatisch".
        Config alt2 = {};
        PRUEFE(configFromJson("{\"rotateSec\":10,\"slots\":[]}", alt2, err, sizeof(err)));
        for (uint8_t i = 0; i < MAX_PAGES; i++) PRUEFE(alt2.teilung[i] == TEILUNG_AUTOMATISCH);
    }

    // --- Layout-Verkleinerung achtet AUCH auf abgeschaltete Slots ---
    // Ein abgeschalteter Slot behaelt seinen Rasterplatz (Zusage der Belegungspruefung);
    // ein Layout, das diesen Platz entfernt, muss deshalb abgelehnt werden.
    {
        Config zc = leereConfig();
        zc.hellSec = 30;
        zc.slots[0].enabled = false;
        snprintf(zc.slots[0].url, URL_LEN, "http://192.0.2.10/a");
        zc.slots[0].page = 1;
        zc.slots[0].pos = 3;  // gibt es nur im Vierer-Layout
        JsonDocument doc;
        PRUEFE(!deserializeJson(doc, "{\"layout\":[0,2,2,2]}"));
        err[0] = 0;
        PRUEFE(!settingsFromDoc(doc, zc, err, sizeof(err)));
        PRUEFE(err[0] != 0);
    }

    // --- Laden klemmt rotateSec (einziges frueher ungekapptes Bereichsfeld) ---
    {
        Config zc = {};
        err[0] = 0;
        PRUEFE(configFromJson("{\"rotateSec\":1}", zc, err, sizeof(err)));
        PRUEFE(zc.rotateSec == 3);  // 1..2 -> 3
        PRUEFE(configFromJson("{\"rotateSec\":0}", zc, err, sizeof(err)));
        PRUEFE(zc.rotateSec == 0);  // 0 = aus bleibt erlaubt
        PRUEFE(configFromJson("{\"rotateSec\":20000}", zc, err, sizeof(err)));
        PRUEFE(zc.rotateSec == 3600);
    }

    printf("OK\n");
    return 0;
}
