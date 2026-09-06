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

// @openapi {get} /slots version=v1 group=Slots summary="Read slot configuration" requiresAuth=true
void handleSlotsGet(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }
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
    sendeJson(webserver, HTTP_CODE_OK, doc);
}

// @openapi {post} /slots version=v1 group=Slots summary="Create or update one slot" requiresAuth=true
void handleSlotsSave(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }
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
    antwort["ok"] = true;
    sendeJson(webserver, HTTP_CODE_OK, antwort);
}

// @openapi {delete} /slots/{index} version=v1 group=Slots summary="Delete one slot" requiresAuth=true
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
    doc["ok"] = true;
    sendeJson(webserver, HTTP_CODE_OK, doc);
}

// @openapi {post} /slots/test version=v1 group=Slots summary="Fetch a URL from the device" requiresAuth=true
void handleSlotsTest(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }
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
    doc["ok"] = ok;
    doc["httpStatus"] = status;
    // Als const char*: ArduinoJson speichert den Zeiger statt einer Kopie -- der
    // Puffer ist statisch und lebt laenger als die Antwort. Die Kopie kostete den
    // Kilobyte-Inhalt sonst dreifach (Puffer + Dokument + Ausgabe-String).
    doc["preview"] = ok ? static_cast<const char*>(vorschau) : "";
    if (!ok) {
        doc["error"] = fehler;
    }

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

// @openapi {get} /slots/status version=v1 group=Slots summary="Read current slot values" requiresAuth=true
void handleSlotsStatus(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }
    if (g_cfg == nullptr) {
        sendeFehler(webserver, HTTP_CODE_INTERNAL_ERROR, "Konfiguration nicht geladen");
        return;
    }

    JsonDocument doc;
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

// @openapi {post} /slots/settings version=v1 group=Slots summary="Update page settings" requiresAuth=true
void handleSlotsSettings(Webserver* webserver) {
    if (!requireBearerToken(webserver)) {
        return;
    }
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
    antwort["ok"] = true;
    sendeJson(webserver, HTTP_CODE_OK, antwort);
}

void SlotApi::registerRoutes(Webserver* webserver) {
    webserver->raw().on("/api/v1/slots", HTTP_GET, [webserver]() { handleSlotsGet(webserver); });
    webserver->raw().on("/api/v1/slots", HTTP_POST, [webserver]() { handleSlotsSave(webserver); });
    webserver->raw().on("/api/v1/slots/test", HTTP_POST,
                        [webserver]() { handleSlotsTest(webserver); });
    webserver->raw().on("/api/v1/slots/status", HTTP_GET,
                        [webserver]() { handleSlotsStatus(webserver); });
    webserver->raw().on("/api/v1/slots/settings", HTTP_POST,
                        [webserver]() { handleSlotsSettings(webserver); });

    // Loeschen adressiert den Slot ueber die URL (/api/v1/slots/<i>) mit einem
    // Platzhalter (UriBraces) -- EINE Route statt zwoelf einzeln registrierter
    // (jede kostete dauerhaft Handler + Pfad-String + zwei std::function im Heap,
    // zusammen gut ein Kilobyte). Der Handler prueft den Platzhalter selbst.
    webserver->raw().on(UriBraces("/api/v1/slots/{}"), HTTP_DELETE,
                        [webserver]() { handleSlotsDelete(webserver); });
}
