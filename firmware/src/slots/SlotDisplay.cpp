// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Homelab-Display fuer GeekMagic SmallTV
 * Copyright (C) 2026 krobi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "slots/SlotDisplay.h"

#include <time.h>

#include "config/ConfigManager.h"
#include "display/DisplayManager.h"
#include "slots/SlotRuntime.h"

namespace {
Config* g_cfg = nullptr;

const int16_t BREITE = 240;
const int16_t HOEHE = 240;
const uint16_t FARBE_HG = 0x0000;      // schwarz
const uint16_t FARBE_LABEL = 0x8C71;   // gedaempftes grau
const uint16_t FARBE_VERALTET = 0x4A49;
const uint16_t FARBE_RAHMEN = 0x2124;

uint8_t g_seite = 1;
uint32_t g_letzterWechselMs = 0;
bool g_allesNeu = true;
uint8_t g_gesetzteHelligkeit = 255;  // 255 = noch nie gesetzt
uint32_t g_letzterHelligkeitsTest = 0;

/// Wertebereich der Software-PWM des ESP8266.
const uint16_t PWM_MAX = 1023;

// Die Basis-Firmware kann die Beleuchtung nur an- oder ausschalten. Zum Dimmen wird
// derselbe Anschluss mit einem Puls-Pausen-Signal angesteuert. Der Anschluss ist
// aktiv-low, das Signal also umgekehrt: 100 % Helligkeit = dauerhaft LOW.
void backlightSetzen(uint8_t prozent) {
    const uint16_t roh = (uint16_t)((uint32_t)prozent * PWM_MAX / 100U);
    const uint16_t wert = LCD_BACKLIGHT_ACTIVE_LOW ? (uint16_t)(PWM_MAX - roh) : roh;
    pinMode((uint8_t)LCD_BACKLIGHT_GPIO, OUTPUT);
    if (prozent == 0) {
        // Ganz aus: fest schalten statt mit 0 % zu takten -- das vermeidet ein
        // Restglimmen und spart der Software-PWM die Arbeit.
        digitalWrite((uint8_t)LCD_BACKLIGHT_GPIO, LCD_BACKLIGHT_ACTIVE_LOW ? HIGH : LOW);
        return;
    }
    if (prozent >= 100) {
        digitalWrite((uint8_t)LCD_BACKLIGHT_GPIO, LCD_BACKLIGHT_ACTIVE_LOW ? LOW : HIGH);
        return;
    }
    analogWriteRange(PWM_MAX);
    analogWrite((uint8_t)LCD_BACKLIGHT_GPIO, wert);
}

/// Welche Helligkeit gilt jetzt -- Tagwert oder Nachtwert?
uint8_t gewuenschteHelligkeit() {
    if (g_cfg == nullptr) {
        return 100;
    }

    // Grundhelligkeit: fester Wert oder ueber den Datenpunkt geschaltet.
    const uint8_t grund = (g_cfg->hellModus == 1)
                              ? (SlotRuntime::hellSchalterAn() ? g_cfg->hellAn : g_cfg->hellAus)
                              : g_cfg->helligkeit;

    if (!g_cfg->nachtAn) {
        return grund;
    }
    // Ohne gestellte Uhr bleibt es beim Tagwert -- lieber zu hell als ein dunkles
    // Display, dessen Ursache niemand findet.
    const time_t jetzt = time(nullptr);
    if (jetzt < 1000000000) {
        return grund;
    }
    const struct tm* t = localtime(&jetzt);
    if (t == nullptr) {
        return grund;
    }
    // Der Nachtmodus hat Vorrang vor dem Schalter: Was nachts dunkel sein soll, soll es
    // auch dann sein, wenn irgendwo noch ein Licht brennt.
    return imZeitfenster((uint8_t)t->tm_hour, g_cfg->nachtVon, g_cfg->nachtBis)
               ? g_cfg->nachtHelligkeit
               : grund;
}

/// Zuletzt gezeichneter Anzeigezustand je Slot -- nur bei Aenderung wird neu gemalt.
/// EIN struct statt vier paralleler Arrays: Ruecksetzen ist ein Ausdruck, und
/// Uebernahme und Vergleich koennen nicht auseinanderdriften.
struct GezeichneteKachel {
    char wert[WERT_LEN];
    SlotState zustand;
    bool veraltet;
    /// Minutenstufe der "vor X min"-Zeile. Ohne sie friere die Zeile ein: Wert,
    /// Zustand und Veraltet-Flag aendern sich bei weiter fehlschlagenden Abrufen
    /// nicht mehr, nur das Alter waechst.
    uint32_t alterMin;
};
GezeichneteKachel g_gezeichnet[MAX_SLOTS];

/// Minutenstufe einer Kachel -- EINE Formel fuer Zeichnung UND Neuzeichen-Vergleich;
/// zwei getrennte Fassungen koennten driften (eingefrorene Zeile oder Dauer-Malen).
auto alterMinStufe(uint8_t index, bool veraltet) -> uint32_t {
    return veraltet ? SlotRuntime::alterSek(index) / 60U : 0U;
}


auto kachelHoehe(PageLayout layout) -> int16_t {
    // Nur das Ein-Kachel-Layout nutzt die volle Hoehe; ZWEI und VIER teilen sie beide.
    return (layout == LAYOUT_EINS) ? HOEHE : HOEHE / 2;
}

auto kachelBreite(PageLayout layout) -> int16_t {
    return (layout == LAYOUT_VIER) ? BREITE / 2 : BREITE;
}

// Hoehe, die eine Kachel nach den EINSTELLUNGEN braucht -- bewusst ohne Laufzeitzustand.
// Der Veraltet-Hinweis kaeme und ginge, und mit ihm spraenge die Aufteilung der Seite.
auto inhaltHoehe(const Slot& s) -> int16_t {
    KachelZeilen z = {};
    z.mitLabel = (s.label[0] != 0);
    z.mitZahl = (s.anzeige != ANZEIGE_BALKEN);
    z.mitBalken = (s.anzeige != ANZEIGE_ZAHL);
    z.einheitDaneben = s.einheitDaneben && z.mitZahl && s.unit[0] != 0;
    z.mitUnterzeile = (s.unit[0] != 0) && !z.einheitDaneben;
    z.labelStufe = s.labelSize;
    z.wertStufe = s.wertSize;
    z.unitStufe = s.unitSize;
    return kachelInhaltHoehe(z);
}

// Inhaltshoehe des Werts auf einem bestimmten Platz einer Seite; 0, wenn dort nichts
// angezeigt wird (leer oder abgeschaltet).
auto inhaltHoeheAufPlatz(const Config& cfg, uint8_t seite, uint8_t pos) -> int16_t {
    for (const auto& s : cfg.slots) {
        if (s.enabled && s.url[0] != 0 && s.page == seite && s.pos == pos) {
            return inhaltHoehe(s);
        }
    }
    return 0;
}

void kachelRechteck(const Config& cfg, uint8_t seite, PageLayout l, uint8_t pos, int16_t& x,
                    int16_t& y, int16_t& w, int16_t& h) {
    w = kachelBreite(l);
    h = kachelHoehe(l);
    switch (l) {
        case LAYOUT_EINS:
            x = 0;
            y = 0;
            break;
        case LAYOUT_ZWEI: {
            // Die beiden Haelften muessen nicht gleich hoch sein: entweder nach einem
            // festen Prozentwert des Nutzers oder im Verhaeltnis der Inhalte.
            const int16_t obenH = teilungErsteKachel(HOEHE, inhaltHoeheAufPlatz(cfg, seite, 0),
                                                     inhaltHoeheAufPlatz(cfg, seite, 1),
                                                     cfg.teilung[seite - 1]);
            x = 0;
            y = (pos == 0) ? 0 : obenH;
            h = (pos == 0) ? obenH : (int16_t)(HOEHE - obenH);
            break;
        }
        default:
            x = (int16_t)((pos % 2) * w);
            y = (int16_t)((pos / 2) * h);
            break;
    }
}

auto farbeFuer(const Slot& s, const SlotLaufzeit& l, bool veraltet) -> uint16_t {
    if (veraltet || !l.hatDaten) {
        return FARBE_VERALTET;
    }
    if (l.zustand == STATE_ALARM) {
        return g_cfg->colorAlarm;
    }
    if (l.zustand == STATE_WARN) {
        return g_cfg->colorWarn;
    }
    return s.color;
}

/// Zeichnet Text linksbuendig ab x und schneidet ab, was ueber maxBreite hinausginge.
/// Gegenstueck zu textMittig fuer die Zeile "Wert Einheit", in der zwei Schriftstufen
/// nebeneinander stehen und deshalb nicht gemeinsam zentriert werden koennen.
void textAb(Arduino_GFX* gfx, const char* text, int16_t x, int16_t y, int16_t maxBreite,
            uint8_t groesse, uint16_t farbe) {
    if (text == nullptr || text[0] == 0 || maxBreite <= 0) {
        return;
    }
    const int16_t zeichenBreite = gfxZeichenBreite(groesse);
    if (zeichenBreite <= 0) {
        return;
    }
    const int16_t passt = (int16_t)(maxBreite / zeichenBreite);
    if (passt <= 0) {
        return;
    }

    char gekuerzt[24];
    size_t n = strlen(text);
    if (n > (size_t)passt) {
        n = (size_t)passt;
    }
    if (n > sizeof(gekuerzt) - 1) {
        n = sizeof(gekuerzt) - 1;
    }
    memcpy(gekuerzt, text, n);
    gekuerzt[n] = 0;

    gfx->setTextSize(groesse);
    gfx->setTextColor(farbe);
    gfx->setCursor(x, y);
    gfx->print(gekuerzt);
}

/// Zeichnet Text mittig in einem Bereich.
void textMittig(Arduino_GFX* gfx, const char* text, int16_t x, int16_t y, int16_t w, uint8_t groesse,
                uint16_t farbe) {
    if (text == nullptr || text[0] == 0) {
        return;
    }
    const int16_t zeichenBreite = (int16_t)(6 * groesse);
    if (zeichenBreite <= 0) {
        return;
    }

    // Auf die Kachelbreite kuerzen. Ohne diese Begrenzung laeuft ein langer Wert in die
    // Nachbarkachel oder wird von der Bibliothek am Bildschirmrand umgebrochen -- beides
    // zerstoert fremde Kacheln, die sich fuer den Nutzer nicht erklaeren lassen.
    const int16_t passt = (int16_t)(w / zeichenBreite);
    if (passt <= 0) {
        return;
    }

    char gekuerzt[24];
    size_t n = strlen(text);
    if (n > (size_t)passt) {
        n = (size_t)passt;
    }
    if (n > sizeof(gekuerzt) - 1) {
        n = sizeof(gekuerzt) - 1;
    }
    memcpy(gekuerzt, text, n);
    gekuerzt[n] = 0;

    const int16_t textBreite = (int16_t)((int16_t)n * zeichenBreite);
    int16_t startX = (int16_t)(x + (w - textBreite) / 2);
    if (startX < x) {
        startX = x;
    }
    gfx->setTextSize(groesse);
    gfx->setTextColor(farbe);
    gfx->setCursor(startX, y);
    gfx->print(gekuerzt);
}

void zeichneKachel(uint8_t index, bool rahmen) {
    Arduino_GFX* gfx = DisplayManager::getGfx();
    if (gfx == nullptr || g_cfg == nullptr) {
        return;
    }

    const Slot& s = g_cfg->slots[index];
    const auto layout = (PageLayout)g_cfg->layout[s.page - 1];

    int16_t x = 0;
    int16_t y = 0;
    int16_t w = 0;
    int16_t h = 0;
    kachelRechteck(*g_cfg, s.page, layout, s.pos, x, y, w, h);

    gfx->fillRect(x, y, w, h, FARBE_HG);
    if (rahmen && layout != LAYOUT_EINS) {
        gfx->drawRect(x, y, w, h, FARBE_RAHMEN);
    }

    const SlotLaufzeit& l = SlotRuntime::laufzeit(index);
    const bool veraltet = SlotRuntime::veraltet(index);
    const uint16_t farbe = farbeFuer(s, l, veraltet);

    const bool mitBalken = (s.anzeige != ANZEIGE_ZAHL);
    const bool mitZahl = (s.anzeige != ANZEIGE_BALKEN);

    // Wert -- oder ein Strich, solange nichts Belastbares vorliegt.
    // Bewusst kein "0": eine fehlgeschlagene Abfrage darf nie wie ein Messwert aussehen.
    const char* wert = l.hatDaten ? l.wert : "--";

    // Einheit und Alter. Das Alter in Minutenstufen -- Sekundengenauigkeit wuerde jede
    // Sekunde ein Neuzeichnen erzwingen, ohne etwas Nuetzliches zu sagen.
    // Neben dem Wert kann die Einheit nur stehen, wenn es einen Wert gibt (bei reiner
    // Balkenanzeige gibt es keinen) und wenn ueberhaupt eine eingetragen ist.
    const bool einheitDaneben = s.einheitDaneben && mitZahl && s.unit[0] != 0;

    char unten[32];
    const uint32_t alterMin = alterMinStufe(index, veraltet);
    // Steht die Einheit oben neben dem Wert, bleibt der unteren Zeile nur der
    // Veraltet-Hinweis -- sonst stuende die Einheit zweimal da.
    const char* einheitUnten = einheitDaneben ? "" : s.unit;
    const char* fuge = (einheitUnten[0] != 0) ? " " : "";
    if (veraltet) {
        if (alterMin >= 1) {
            snprintf(unten, sizeof(unten), "%s%svor %lumin", einheitUnten, fuge,
                     (unsigned long)alterMin);
        } else {
            snprintf(unten, sizeof(unten), "%s%sveraltet", einheitUnten, fuge);
        }
    } else {
        snprintf(unten, sizeof(unten), "%s", einheitUnten);
    }

    // Die drei Schriftstufen sind bewusst voneinander unabhaengig -- es wird nichts
    // heruntergerechnet, weil daneben ein Balken steht. Damit die Kachel trotzdem
    // ausgewogen bleibt, wird die Gesamthoehe aller vorhandenen Zeilen bestimmt und der
    // ganze Block mittig gesetzt, statt wie frueher jede Zeile einzeln an der Mitte
    // auszurichten. Sonst ueberdeckten sich Zeilen, sobald die Stufen auseinanderliegen.
    const int16_t ABSTAND = KACHEL_ABSTAND;
    const int16_t balkenH = (int16_t)(mitZahl ? 10 : 22);

    // Dieselbe Formel, die auch die Seite aufteilt (kachelInhaltHoehe) -- hier nur mit
    // dem Laufzeitzustand gefuettert, weil der Veraltet-Hinweis eine Zeile belegt.
    KachelZeilen zeilen = {};
    zeilen.mitLabel = (s.label[0] != 0);
    zeilen.mitZahl = mitZahl;
    zeilen.mitBalken = mitBalken;
    zeilen.mitUnterzeile = (unten[0] != 0);
    zeilen.einheitDaneben = einheitDaneben;
    zeilen.labelStufe = s.labelSize;
    zeilen.wertStufe = s.wertSize;
    zeilen.unitStufe = s.unitSize;

    const int16_t hLabel = zeilen.mitLabel ? (int16_t)(8 * s.labelSize + ABSTAND) : 0;
    const int16_t hWert =
        mitZahl ? (einheitDaneben ? (int16_t)((8 * s.wertSize > 8 * s.unitSize)
                                                  ? 8 * s.wertSize
                                                  : 8 * s.unitSize)
                                  : (int16_t)(8 * s.wertSize))
                : 0;
    const int16_t hBalken = mitBalken ? (int16_t)(balkenH + ABSTAND) : 0;

    const int16_t gesamt = kachelInhaltHoehe(zeilen);
    // Laeuft der Block ueber die Kachel hinaus, beginnt er am oberen Rand statt
    // hochgeschoben zu werden -- oben abgeschnitten ist schlechter lesbar als unten.
    int16_t cursorY = (int16_t)(y + (h - gesamt) / 2);
    if (cursorY < y) {
        cursorY = y;
    }

    if (hLabel != 0) {
        textMittig(gfx, s.label, x, cursorY, w, s.labelSize, FARBE_LABEL);
        cursorY = (int16_t)(cursorY + hLabel);
    }

    if (mitZahl) {
        if (einheitDaneben) {
            const ZeileWertEinheit z = layoutWertEinheit(x, cursorY, w, strlen(wert), s.wertSize,
                                                         strlen(s.unit), s.unitSize);
            textAb(gfx, wert, z.wertX, z.wertY, (int16_t)(x + w - z.wertX), s.wertSize, farbe);
            textAb(gfx, s.unit, z.einheitX, z.einheitY, (int16_t)(x + w - z.einheitX), s.unitSize,
                   veraltet ? FARBE_VERALTET : FARBE_LABEL);
        } else {
            textMittig(gfx, wert, x, cursorY, w, s.wertSize, farbe);
        }
        cursorY = (int16_t)(cursorY + hWert);
    }

    if (mitBalken) {
        const int16_t rand = (int16_t)(w / 8);
        const int16_t balkenB = (int16_t)(w - 2 * rand);
        const int16_t balkenX = (int16_t)(x + rand);
        // Der Balken ist eine Zeile des Stapels wie jede andere und sitzt damit
        // zwischen Wert und Einheit -- frueher wurde er aus der Wertgroesse gerechnet
        // und lag in der Einheiten-Zeile, sobald die Stufen nicht mehr zusammenpassten.
        const int16_t balkenY = (int16_t)(cursorY + ABSTAND);
        cursorY = (int16_t)(cursorY + hBalken);

        gfx->drawRect(balkenX, balkenY, balkenB, balkenH, FARBE_LABEL);

        // Ohne belastbaren Wert bleibt der Balken leer -- ein halb gefuellter Balken
        // aus einem alten Wert waere eine stille Falschaussage.
        if (l.hatDaten) {
            float zahl = 0.0F;
            if (parseNumber(wert, zahl)) {
                const float anteil = barFraction(zahl, s.barMin, s.barMax);
                const int16_t fuellB = (int16_t)((float)(balkenB - 2) * anteil);
                if (fuellB > 0) {
                    gfx->fillRect((int16_t)(balkenX + 1), (int16_t)(balkenY + 1), fuellB,
                                  (int16_t)(balkenH - 2), farbe);
                }
            }
        }
    }

    if (unten[0] != 0) {
        textMittig(gfx, unten, x, (int16_t)(cursorY + ABSTAND), w, s.unitSize,
                   veraltet ? FARBE_VERALTET : FARBE_LABEL);
    }

    snprintf(g_gezeichnet[index].wert, WERT_LEN, "%s", wert);
    g_gezeichnet[index].zustand = l.zustand;
    g_gezeichnet[index].veraltet = veraltet;
    g_gezeichnet[index].alterMin = alterMin;
}

auto seiteHatInhalt(uint8_t seite) -> bool {
    if (g_cfg == nullptr) {
        return false;
    }
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        const Slot& s = g_cfg->slots[i];
        if (s.enabled && s.url[0] != 0 && s.page == seite) {
            return true;
        }
    }
    return false;
}

void zeichneSeite() {
    Arduino_GFX* gfx = DisplayManager::getGfx();
    if (gfx == nullptr || g_cfg == nullptr) {
        return;
    }
    gfx->fillScreen(FARBE_HG);

    if (!seiteHatInhalt(g_seite)) {
        textMittig(gfx, "Kein Wert eingerichtet", 0, HOEHE / 2 - 4, BREITE, 1, FARBE_LABEL);
        return;
    }

    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        const Slot& s = g_cfg->slots[i];
        if (s.enabled && s.url[0] != 0 && s.page == g_seite) {
            zeichneKachel(i, true);
        }
    }
}

/// Hat sich seit dem letzten Zeichnen etwas geaendert, das man sehen wuerde?
auto kachelVeraendert(uint8_t index) -> bool {
    const SlotLaufzeit& l = SlotRuntime::laufzeit(index);
    const GezeichneteKachel& g = g_gezeichnet[index];
    const char* wert = l.hatDaten ? l.wert : "--";
    if (strncmp(g.wert, wert, WERT_LEN) != 0) {
        return true;
    }
    if (g.zustand != l.zustand) {
        return true;
    }
    const bool veraltet = SlotRuntime::veraltet(index);
    if (g.veraltet != veraltet) {
        return true;
    }
    // Bei veralteten Kacheln zaehlt auch die Altersstufe -- sonst stuende dort tagelang
    // "vor 2min".
    return g.alterMin != alterMinStufe(index, veraltet);
}
}  // namespace

void SlotDisplay::begin(Config* cfg) {
    g_cfg = cfg;
    g_seite = 1;
    g_letzterWechselMs = millis();
    helligkeitAnwenden();
    neuZeichnen();
}

void SlotDisplay::helligkeitAnwenden() {
    const uint8_t soll = gewuenschteHelligkeit();
    if (soll != g_gesetzteHelligkeit) {
        backlightSetzen(soll);
        g_gesetzteHelligkeit = soll;
    }
}

void SlotDisplay::neuZeichnen() {
    g_allesNeu = true;
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        g_gezeichnet[i] = GezeichneteKachel{};
    }
}

void SlotDisplay::naechsteSeite() {
    if (g_cfg == nullptr) {
        return;
    }
    const uint8_t vorher = g_seite;
    for (uint8_t versuch = 0; versuch < MAX_PAGES; versuch++) {
        g_seite = (uint8_t)((g_seite % MAX_PAGES) + 1);
        if (seiteHatInhalt(g_seite)) {
            break;
        }
    }
    g_letzterWechselMs = millis();
    // Nur bei einem echten Seitenwechsel alles neu zeichnen. Hat nur eine Seite Inhalt,
    // landet die Suche wieder auf derselben -- ein Vollbild-Neuaufbau waere dann ein
    // sichtbares Blinken im Takt des Wechselintervalls, ohne dass sich etwas aendert.
    if (g_seite != vorher) {
        g_allesNeu = true;
    }
}

void SlotDisplay::update() {
    if (g_cfg == nullptr) {
        return;
    }

    // Nachtmodus und Schalter aendern sich ohne Ereignis -- also regelmaessig nachsehen.
    // Alle zwei Sekunden: Der Schaltvorgang soll zeitnah wirken, und die Pruefung selbst
    // kostet nichts (sie vergleicht nur Zahlen, gesetzt wird nur bei Aenderung).
    if (elapsed(millis(), g_letzterHelligkeitsTest, 2000U)) {
        g_letzterHelligkeitsTest = millis();
        helligkeitAnwenden();
    }

    // Ein Alarm hat Vorrang vor der Rotation: Er zieht seine Seite nach vorn und
    // haelt sie dort, solange er ansteht.
    const uint8_t alarm = SlotRuntime::alarmSeite();
    if (alarm != 0 && alarm != g_seite) {
        g_seite = alarm;
        g_allesNeu = true;
    } else if (alarm == 0) {
        const uint32_t intervall = (uint32_t)g_cfg->rotateSec * 1000U;
        if (g_cfg->rotateSec > 0 && elapsed(millis(), g_letzterWechselMs, intervall)) {
            naechsteSeite();
        }
    }

    if (g_allesNeu) {
        zeichneSeite();
        g_allesNeu = false;
        return;
    }

    // Im Normalfall nur die Kacheln neu malen, deren Anzeige sich geaendert hat.
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        const Slot& s = g_cfg->slots[i];
        if (s.enabled && s.url[0] != 0 && s.page == g_seite && kachelVeraendert(i)) {
            zeichneKachel(i, true);
        }
    }
}
