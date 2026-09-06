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

#include "slots/SlotApi.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

#include "project_version.h"
#include <uri/UriBraces.h>

#include "Logger.h"
#include "slots/SlotDisplay.h"
#include "slots/SlotRuntime.h"
#include "slots/SlotStore.h"
#include "web/Api.h"
#include "web/antwort.h"

// Fehlerformat-Regel: Unsere Slot-Endpunkte antworten {ok:false, error:"..."} plus
// HTTP-Status; die Basis-Endpunkte (Token, NTP, ...) antworten {status:"error",
// message:"..."}. Beides bleibt so -- die Basis umzubauen wuerde den Fork-Diff
// vergroessern. Regel fuer neuen Code: eigene Endpunkte NUR {ok,error}; wer Antworten
// der Basis auswertet, prueft res.ok/HTTP-Status statt Felder zu raten.
namespace {
Config* g_cfg = nullptr;

/// Warnhinweis fuer die Oberflaeche (z. B. "Konfiguration war unlesbar") -- gesetzt
/// beim Start, ausgeliefert mit GET /slots, bis der erste Speichervorgang gelingt.
char g_ladeWarnung[96] = {0};

/// Gemeinsamer Persistenz-Schritt ALLER schreibenden Handler: speichert die
/// Konfiguration; bei Erfolg ist ein Start-Warnhinweis Geschichte, bei einem
/// Schreibfehler wird der RAM-Stand aus der (dank atomarem Speichern intakten)
/// Datei zurueckgeladen und die Fehlerantwort gesendet. Vorher existierten dafuer
/// zwei Idiome nebeneinander -- Kopie-zurueck und Datei-Reload -- und die
/// Warnungs-Loeschung war nur in einem von drei Handlern implementiert.
bool persistiere(Webserver* webserver) {
    char fehler[96];
    if (SlotStore::save(*g_cfg, fehler, sizeof(fehler))) {
        g_ladeWarnung[0] = 0;
        return true;
    }
    char reloadFehler[96];
    if (!SlotStore::load(*g_cfg, reloadFehler, sizeof(reloadFehler))) {
        Logger::error(reloadFehler, "Slots");
    }
    sendeFehler(webserver, HTTP_CODE_INTERNAL_ERROR, fehler);
    return false;
}

auto zustandName(SlotState z) -> const char* {
    switch (z) {
        case STATE_ALARM: return "alarm";
        case STATE_WARN: return "warn";
        default: return "ok";
    }
}
}  // namespace

void SlotApi::begin(Config* cfg) {
    g_cfg = cfg;
}

void SlotApi::setLadeWarnung(const char* text) {
    snprintf(g_ladeWarnung, sizeof(g_ladeWarnung), "%s", text == nullptr ? "" : text);
}

auto SlotApi::konfigurationSichern() -> bool {
    if (g_cfg == nullptr) {
        return false;
    }
    char fehler[96];
    if (!SlotStore::save(*g_cfg, fehler, sizeof(fehler))) {
        Logger::error(fehler, "Slots");
        return false;
    }
    g_ladeWarnung[0] = 0;
    return true;
}

void handleSlotsGet(Webserver* webserver) {
    if (g_cfg == nullptr) {
        sendeFehler(webserver, HTTP_CODE_INTERNAL_ERROR, "Konfiguration nicht geladen");
        return;
    }

    JsonDocument doc;
    configToDoc(*g_cfg, doc);
    // Ein Startproblem (unlesbare Konfigurationsdatei) sichtbar machen: Die Oberflaeche
    // zeigt den Hinweis an, statt so zu tun, als waere die leere Konfiguration gewollt.
    if (g_ladeWarnung[0] != 0) {
        doc["warnung"] = g_ladeWarnung;
    }
    // Die Grenzen mitliefern, statt sie in der Oberflaeche noch einmal als Zahl zu
    // hinterlegen: Sie standen bisher in der Firmware, im Formular und im Mock
    // nebeneinander und konnten auseinanderlaufen (B8).
    JsonObject l = doc["limits"].to<JsonObject>();
    l["url"] = URL_LEN - 1;
    l["label"] = LABEL_LEN - 1;
    l["field"] = FIELD_LEN - 1;
    l["unit"] = UNIT_LEN - 1;
    l["slots"] = MAX_SLOTS;
    l["pages"] = MAX_PAGES;
    l["refreshMin"] = REFRESH_SEC_MIN;
    l["refreshMax"] = REFRESH_SEC_MAX;
    l["rotateMin"] = ROTATE_SEC_MIN;
    l["rotateMax"] = ROTATE_SEC_MAX;
    l["decimalsMax"] = DECIMALS_MAX;
    l["hellSecMin"] = HELL_SEC_MIN;
    l["hellSecMax"] = HELL_SEC_MAX;
    l["textStufeMin"] = MIN_TEXT_STUFE;
    l["textStufeMax"] = MAX_TEXT_STUFE;
    sendeJson(webserver, HTTP_CODE_OK, doc);
}

void handleSlotsSave(Webserver* webserver) {
    if (g_cfg == nullptr) {
        sendeFehler(webserver, HTTP_CODE_INTERNAL_ERROR, "Konfiguration nicht geladen");
        return;
    }
    if (!webserver->raw().hasArg("plain") || webserver->raw().arg("plain").length() == 0) {
        sendeFehler(webserver, HTTP_CODE_BAD_REQUEST, "leere Anfrage");
        return;
    }

    // Referenz statt Kopie: arg() liefert bereits eine Referenz, eine Kopie waere
    // der komplette Anfragekoerper ein zweites Mal im Heap.
    const String& koerper = webserver->raw().arg("plain");

    // EINMAL parsen -- Index und Slot-Daten kommen aus demselben Dokument. Frueher wurde
    // der Koerper zweimal gelesen, was zwei JSON-Speicherbereiche fuer eine Zahl kostete.
    JsonDocument doc;
    if (deserializeJson(doc, koerper)) {
        sendeFehler(webserver, HTTP_CODE_BAD_REQUEST, "Anfrage ist kein gueltiges JSON");
        return;
    }
    const int index = doc["index"] | -1;
    if (index < 0 || index >= (int)MAX_SLOTS) {
        sendeFehler(webserver, HTTP_CODE_BAD_REQUEST, "Slot-Nummer ausserhalb des Bereichs");
        return;
    }

    Slot neu = {};
    char fehler[96];
    if (!slotFromDoc(doc, *g_cfg, (uint8_t)index, neu, fehler, sizeof(fehler))) {
        sendeFehler(webserver, HTTP_CODE_BAD_REQUEST, fehler);
        return;
    }

    // Schlaegt das Schreiben fehl, stellt persistiere() den RAM-Stand aus der
    // unangetasteten Datei wieder her -- Datei und Speicher laufen nie auseinander.
    g_cfg->slots[index] = neu;
    if (!persistiere(webserver)) {
        return;
    }

    // Alte Messwerte gehoeren zur alten Adresse -- sie duerfen nicht weiterleben.
    SlotRuntime::zuruecksetzen((uint8_t)index);
    // Beschriftung, Platz oder Darstellung koennen sich geaendert haben -- das erkennt
    // der Kachelvergleich nicht, er sieht nur Werte. Also alles neu zeichnen.
    SlotDisplay::neuZeichnen();
    Logger::info("Slot gespeichert");

    JsonDocument antwort;
    setzeErgebnis(antwort, true, "Wert gespeichert");
    sendeJson(webserver, HTTP_CODE_OK, antwort);
}

void handleSlotsDelete(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }
    if (g_cfg == nullptr) {
        sendeFehler(webserver, HTTP_CODE_INTERNAL_ERROR, "Konfiguration nicht geladen");
        return;
    }

    // Platzhalter-Wert der UriBraces-Route. NUR Ziffern akzeptieren: toInt() macht
    // aus "abc" eine 0 -- ein Tippfehler in der URL wuerde sonst still Slot 0 loeschen.
    const String& roh = webserver->raw().pathArg(0);
    if (roh.length() == 0 || roh.length() > 2) {
        sendeFehler(webserver, HTTP_CODE_BAD_REQUEST, "Slot-Nummer ungueltig");
        return;
    }
    for (size_t z = 0; z < roh.length(); z++) {
        if (roh[z] < '0' || roh[z] > '9') {
            sendeFehler(webserver, HTTP_CODE_BAD_REQUEST, "Slot-Nummer ungueltig");
            return;
        }
    }
    const int index = roh.toInt();
    if (index >= (int)MAX_SLOTS) {
        sendeFehler(webserver, HTTP_CODE_BAD_REQUEST, "Slot-Nummer ausserhalb des Bereichs");
        return;
    }

    g_cfg->slots[index] = Slot{};
    if (!persistiere(webserver)) {
        return;
    }
    SlotRuntime::zuruecksetzen((uint8_t)index);
    // Ohne dies bliebe die Kachel des geloeschten Slots mit ihrem alten Wert stehen.
    SlotDisplay::neuZeichnen();

    JsonDocument doc;
    setzeErgebnis(doc, true, "Wert geloescht");
    sendeJson(webserver, HTTP_CODE_OK, doc);
}

void handleSlotsTest(Webserver* webserver) {
    if (!webserver->raw().hasArg("plain")) {
        sendeFehler(webserver, HTTP_CODE_BAD_REQUEST, "leere Anfrage");
        return;
    }

    JsonDocument anfrage;
    if (deserializeJson(anfrage, webserver->raw().arg("plain"))) {
        sendeFehler(webserver, HTTP_CODE_BAD_REQUEST, "Anfrage ist kein gueltiges JSON");
        return;
    }
    const char* url = anfrage["url"] | "";

    // Gemeinsamer Puffer des Abrufwegs -- gleiche Grenze wie im Betrieb (KOERPER_MAX),
    // sonst lehnte der Assistent Antworten ab, die der Betrieb lesen koennte.
    char* vorschau = SlotRuntime::scratchPuffer();
    char fehler[FEHLER_LEN];
    int status = 0;
    const bool ok = SlotRuntime::probe(url, status, vorschau, KOERPER_MAX, fehler,
                                       sizeof(fehler));

    JsonDocument doc;
    setzeErgebnis(doc, ok, ok ? "Adresse erreichbar" : fehler);
    doc["httpStatus"] = status;
    // ArduinoJson 7 KOPIERT auch einen const char* ins Dokument (anders als Fassung 6,
    // auf die dieser Kommentar frueher baute). Die Vorschau liegt also kurz doppelt im
    // Heap -- bewusst hingenommen: KOERPER_MAX ist ein Kilobyte, und der Assistent
    // laeuft nur, wenn ein Mensch auf "Testen" drueckt.
    doc["preview"] = ok ? static_cast<const char*>(vorschau) : "";

    // Feldnamen nur anbieten, wenn die Antwort wirklich ein JSON-Objekt ist -- sonst
    // waehlt der Nutzer im Assistenten ein Feld, das es gar nicht gibt.
    JsonArray felder = doc["fields"].to<JsonArray>();
    if (ok) {
        JsonDocument antwort;
        // Als const char* parsen: Mit char* wuerde ArduinoJson IN-SITU parsen und den
        // Puffer mit Terminatoren zerschreiben -- den die Antwort oben per Zeiger traegt.
        if (!deserializeJson(antwort, static_cast<const char*>(vorschau)) &&
            antwort.is<JsonObject>()) {
            uint8_t n = 0;
            for (JsonPairConst kv : antwort.as<JsonObjectConst>()) {
                if (n++ >= 20) {
                    break;
                }
                felder.add(kv.key().c_str());
            }
        }
    }

    sendeJson(webserver, HTTP_CODE_OK, doc);
}

void handleSlotsStatus(Webserver* webserver) {
    if (g_cfg == nullptr) {
        sendeFehler(webserver, HTTP_CODE_INTERNAL_ERROR, "Konfiguration nicht geladen");
        return;
    }

    JsonDocument doc;

    // Zustand des Geraets: dieselben Zahlen, die frueher alle zehn Sekunden ins
    // Protokoll geschrieben wurden und dort alles andere verdraengten (E11). Hier
    // holt sie ab, wer sie sehen will -- die Werte-Seite zeigt sie unter der Uebersicht.
    JsonObject g = doc["geraet"].to<JsonObject>();
    // Die Firmware-Version als echte Auskunft. Bis v0.4.3 war sie nur aus der
    // Cache-Kennung der Oberflaeche abzulesen -- und die haengt seit v0.5.0 am Inhalt
    // des Dateisystems, nicht mehr an der Version.
    g["version"] = PROJECT_VER_STR;
    g["freeHeap"] = ESP.getFreeHeap();              // NOLINT(readability-static-accessed-through-instance)
    g["heapFrag"] = ESP.getHeapFragmentation();     // NOLINT(readability-static-accessed-through-instance)
    g["uptimeSec"] = millis() / 1000U;
    g["rssi"] = WiFi.RSSI();

    JsonArray slots = doc["slots"].to<JsonArray>();

    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        const Slot& s = g_cfg->slots[i];
        JsonObject o = slots.add<JsonObject>();
        o["index"] = i;

        if (!s.enabled || s.url[0] == 0) {
            o["enabled"] = false;
            continue;
        }

        const SlotLaufzeit& l = SlotRuntime::laufzeit(i);
        o["enabled"] = true;
        o["label"] = s.label;
        o["unit"] = s.unit;
        o["stale"] = SlotRuntime::veraltet(i);
        o["failCount"] = l.fehlversuche;

        if (l.hatDaten) {
            o["ok"] = l.fehlversuche == 0;
            o["value"] = l.wert;
            o["state"] = zustandName(l.zustand);
            o["ageSec"] = SlotRuntime::alterSek(i);
        } else {
            o["ok"] = false;
        }
        if (l.fehler[0] != 0) {
            o["error"] = l.fehler;
        }
    }

    sendeJson(webserver, HTTP_CODE_OK, doc);
}

void handleSlotsSettings(Webserver* webserver) {
    if (g_cfg == nullptr) {
        sendeFehler(webserver, HTTP_CODE_INTERNAL_ERROR, "Konfiguration nicht geladen");
        return;
    }
    if (!webserver->raw().hasArg("plain")) {
        sendeFehler(webserver, HTTP_CODE_BAD_REQUEST, "leere Anfrage");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, webserver->raw().arg("plain"))) {
        sendeFehler(webserver, HTTP_CODE_BAD_REQUEST, "Anfrage ist kein gueltiges JSON");
        return;
    }

    // settingsFromDoc ist transaktional: Bei Ablehnung ist g_cfg garantiert unveraendert,
    // ein Rollback ist dafuer nicht mehr noetig (frueher wurde waehrend der Pruefung
    // zugewiesen und hier nur ein Teil der Felder zurueckgestellt -- die Liste driftete).
    char fehler[96];
    if (!settingsFromDoc(doc, *g_cfg, fehler, sizeof(fehler))) {
        sendeFehler(webserver, HTTP_CODE_BAD_REQUEST, fehler);
        return;
    }

    if (!persistiere(webserver)) {
        return;
    }

    // Neue Schalter-Adresse, neuer Anlauf -- der Rueckzug der alten gilt nicht weiter.
    SlotRuntime::hellZuruecksetzen();
    SlotDisplay::helligkeitAnwenden();
    SlotDisplay::neuZeichnen();
    Logger::info("Seiteneinstellungen gespeichert");

    JsonDocument antwort;
    setzeErgebnis(antwort, true, "Einstellungen gespeichert");
    sendeJson(webserver, HTTP_CODE_OK, antwort);
}

// Ganze Konfiguration auf einmal uebernehmen (Wiederherstellung einer Sicherung).
//
// Vorher lief das ueber Einzelaufrufe: bis zu zwoelf Loeschungen, die Einstellungen und
// bis zu zwoelf Anlagen -- fuenfundzwanzig Anfragen, von denen JEDE die Konfiguration in
// den Flash schrieb. Zwischen zwei Anfragen war der Bestand ausserdem halb entfernt und
// halb angelegt; brach der Vorgang dort ab, blieb genau das stehen.
//
// Jetzt: einmal pruefen, einmal uebernehmen, einmal schreiben. Der Einzelweg bleibt
// bestehen -- eine aeltere Oberflaeche im Fenster zwischen den beiden Flashs benutzt ihn
// weiter, und fuer das Aendern eines einzelnen Werts ist er der richtige.
void handleSlotsRestore(Webserver* webserver) {
    if (g_cfg == nullptr) {
        sendeFehler(webserver, HTTP_CODE_INTERNAL_ERROR, "Konfiguration nicht geladen");
        return;
    }

    JsonDocument doc;
    if (!leseJsonKoerper(webserver, doc)) {
        return;
    }
    if (!doc["slots"].is<JsonArrayConst>()) {
        sendeFehler(webserver, HTTP_CODE_BAD_REQUEST, "Sicherung ohne Werte-Liste");
        return;
    }

    // Direkt in den laufenden Bestand schreiben und ueber persistiere() sichern: Der
    // Helfer laedt bei einem Schreibfehler den Stand aus der (atomar geschriebenen und
    // damit intakten) Datei zurueck und antwortet selbst. Eine zweite Config im Speicher
    // zu halten haette 3,2 KB dauerhaft gekostet -- ein Zehntel des freien Speichers
    // fuer einen Vorgang, den ein Mensch alle paar Monate ausloest.
    configFromDoc(doc, *g_cfg);
    if (!persistiere(webserver)) {
        return;
    }

    // Jeder Platz hat jetzt einen anderen Wert -- alle Laufzeitstaende verwerfen,
    // sonst stuende an einer Kachel noch die Messung ihres Vorgaengers.
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        SlotRuntime::zuruecksetzen(i);
    }
    SlotRuntime::hellZuruecksetzen();
    SlotDisplay::helligkeitAnwenden();
    SlotDisplay::neuZeichnen();

    uint8_t anzahl = 0;
    for (const auto& s : g_cfg->slots) {
        if (s.url[0] != 0) anzahl++;
    }
    Logger::info("Konfiguration aus Sicherung uebernommen", "Slots");

    JsonDocument antwort;
    setzeErgebnis(antwort, true, "Sicherung uebernommen");
    antwort["werte"] = anzahl;
    sendeJson(webserver, HTTP_CODE_OK, antwort);
}

void SlotApi::registerRoutes(Webserver* webserver) {
    geschuetzt(webserver, "/api/v1/slots", HTTP_GET, handleSlotsGet);
    geschuetzt(webserver, "/api/v1/slots", HTTP_POST, handleSlotsSave);
    geschuetzt(webserver, "/api/v1/slots/test", HTTP_POST, handleSlotsTest);
    geschuetzt(webserver, "/api/v1/slots/status", HTTP_GET, handleSlotsStatus);
    geschuetzt(webserver, "/api/v1/slots/settings", HTTP_POST, handleSlotsSettings);
    geschuetzt(webserver, "/api/v1/slots/restore", HTTP_POST, handleSlotsRestore);

    // Loeschen adressiert den Slot ueber die URL (/api/v1/slots/<i>) mit einem
    // Platzhalter (UriBraces) -- EINE Route statt zwoelf einzeln registrierter
    // (jede kostete dauerhaft Handler + Pfad-String + zwei std::function im Heap,
    // zusammen gut ein Kilobyte). Der Handler prueft den Platzhalter selbst.
    webserver->raw().on(UriBraces("/api/v1/slots/{}"), HTTP_DELETE,
                        [webserver]() { handleSlotsDelete(webserver); });
}
