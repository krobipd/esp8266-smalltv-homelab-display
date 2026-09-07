// SPDX-License-Identifier: GPL-3.0-or-later
// Host-Test der Ladeentscheidungen von SlotStore -- gegen eine Dateisystem-Attrappe:
//   c++ -std=c++17 -I smalltv/firmware/.pio/libdeps/esp12e/ArduinoJson/src
//       -I smalltv/firmware/include -I smalltv/tests/host/attrappe
//       smalltv/tests/host/test_slotstore.cpp -o /tmp/smalltv-slotstore
//
// Warum es diesen Test gibt: Am 07.09.2026 fehlte /slots.json nach 18 Stunden ohne
// Strom, und das Geraet sah aus wie fabrikneu. Die Zweitschrift und ihr Rueckfall sind
// die Antwort darauf -- und waeren sonst der einzige Teil der Rettung, den kein Test
// jemals ausfuehrt. Beim Schreiben ist mir darin prompt ein Datenverlust unterlaufen
// (kaputte Hauptdatei ohne Rueckfall), deshalb steht genau der Fall hier als erster.
#include "pruefe.h"
#include <cstring>

#include "../../firmware/src/slots/SlotStore.cpp"  // NOLINT(bugprone-suspicious-include)

namespace {

/// Eine Konfiguration mit einem wiedererkennbaren Wert -- daran laesst sich zeigen,
/// WELCHE Datei geladen wurde, nicht nur DASS eine geladen wurde.
auto mitAdresse(const char* url) -> std::string {
    Config c;
    SlotStore::defaults(c);
    snprintf(c.slots[0].url, sizeof(c.slots[0].url), "%s", url);
    c.slots[0].enabled = true;
    JsonDocument doc;
    configToDoc(c, doc);
    std::string s;
    serializeJson(doc, s);
    return s;
}

const char* HAUPT = "http://192.0.2.10:8082/rest-api/v1/state/haupt/plain";
const char* ZWEIT = "http://192.0.2.10:8082/rest-api/v1/state/zweit/plain";

void neu() {
    LittleFS.leeren();
    Logger::leeren();
}

}  // namespace

int main() {
    char err[96];
    Config cfg;

    // --- Nichts da: Standardwerte, KEINE Zweitschrift-Meldung ---
    neu();
    bool ausSicherung = true;
    PRUEFE(SlotStore::load(cfg, err, sizeof(err), &ausSicherung));
    PRUEFE(!ausSicherung);
    PRUEFE(cfg.slots[0].url[0] == 0);

    // --- Hauptdatei gueltig: sie gilt, auch wenn eine Zweitschrift daneben liegt ---
    neu();
    LittleFS.lege("/slots.json", mitAdresse(HAUPT));
    LittleFS.lege("/slots.bak.json", mitAdresse(ZWEIT));
    ausSicherung = true;
    PRUEFE(SlotStore::load(cfg, err, sizeof(err), &ausSicherung));
    PRUEFE(!ausSicherung);
    PRUEFE(strcmp(cfg.slots[0].url, HAUPT) == 0);

    // --- Hauptdatei FEHLT: die Zweitschrift springt ein und sagt es ---
    // Genau der Fall vom 07.09.2026.
    neu();
    LittleFS.lege("/slots.bak.json", mitAdresse(ZWEIT));
    ausSicherung = false;
    PRUEFE(SlotStore::load(cfg, err, sizeof(err), &ausSicherung));
    PRUEFE(ausSicherung);
    PRUEFE(strcmp(cfg.slots[0].url, ZWEIT) == 0);
    PRUEFE(Logger::enthaelt("Hauptdatei fehlt"));

    // --- Hauptdatei KAPUTT, Zweitschrift gut: Rueckfall UND die kaputte wird bewahrt ---
    // Ohne diesen Zweig entstand ein Datenverlust: Standardwerte, der Nutzer speichert,
    // und die Zweitschrift wird kurz darauf mit dem leeren Stand ueberschrieben.
    neu();
    LittleFS.lege("/slots.json", "{kaputt");
    LittleFS.lege("/slots.bak.json", mitAdresse(ZWEIT));
    ausSicherung = false;
    PRUEFE(SlotStore::load(cfg, err, sizeof(err), &ausSicherung));
    PRUEFE(ausSicherung);
    PRUEFE(strcmp(cfg.slots[0].url, ZWEIT) == 0);
    PRUEFE(Logger::enthaelt("Hauptdatei beschaedigt"));
    // Die kaputte Datei ist umbenannt, nicht geloescht -- sie bleibt ansehbar.
    PRUEFE(!LittleFS.exists("/slots.json"));
    PRUEFE(LittleFS.inhalt("/slots.json.defekt") == "{kaputt");

    // --- Hauptdatei kaputt, keine Zweitschrift: FEHLER, Datei unangetastet ---
    // Der Aufrufer legt sie beiseite und zeigt eine Warnung; die Zweitschrift darf
    // in diesem Zustand NICHT geschrieben werden. Dieser Riegel sitzt in
    // SlotApi::sicherungPruefen() und ist hier NICHT mitgeprueft -- SlotApi haengt am
    // Webserver und ist am Host nicht baubar. Er wird nur am Geraet ausgefuehrt.
    neu();
    LittleFS.lege("/slots.json", "{kaputt");
    err[0] = 0;
    ausSicherung = true;
    PRUEFE(!SlotStore::load(cfg, err, sizeof(err), &ausSicherung));
    PRUEFE(!ausSicherung);
    PRUEFE(err[0] != 0);
    PRUEFE(LittleFS.exists("/slots.json"));

    // --- Beide kaputt: ebenfalls Fehler, kein stilles Zurueckfallen auf Standardwerte ---
    neu();
    LittleFS.lege("/slots.json", "{kaputt");
    LittleFS.lege("/slots.bak.json", "auch kaputt");
    PRUEFE(!SlotStore::load(cfg, err, sizeof(err), nullptr));

    // --- Leere Hauptdatei zaehlt als kaputt, nicht als "nicht da" ---
    neu();
    LittleFS.lege("/slots.json", "");
    PRUEFE(!SlotStore::load(cfg, err, sizeof(err), nullptr));

    // --- Unplausibel grosse Datei wird abgelehnt, nicht gelesen ---
    neu();
    LittleFS.lege("/slots.json", std::string(16385, 'x'));
    PRUEFE(!SlotStore::load(cfg, err, sizeof(err), nullptr));

    // --- Schreiben und Zweitschrift: beide Wege sind lesbar und getrennt ---
    neu();
    SlotStore::defaults(cfg);
    snprintf(cfg.slots[0].url, sizeof(cfg.slots[0].url), "%s", HAUPT);
    PRUEFE(SlotStore::save(cfg, err, sizeof(err)));
    PRUEFE(LittleFS.exists("/slots.json"));
    PRUEFE(!LittleFS.exists("/slots.bak.json"));
    PRUEFE(SlotStore::sicherungSchreiben(cfg));
    PRUEFE(LittleFS.exists("/slots.bak.json"));
    // Die Temporaerdatei des atomaren Schreibens bleibt nicht liegen.
    PRUEFE(!LittleFS.exists("/slots.json.tmp"));
    PRUEFE(!LittleFS.exists("/slots.bak.json.tmp"));
    // Und die Zweitschrift traegt wirklich denselben Stand.
    Config zurueck;
    LittleFS.remove("/slots.json");
    PRUEFE(SlotStore::load(zurueck, err, sizeof(err), &ausSicherung));
    PRUEFE(ausSicherung);
    PRUEFE(strcmp(zurueck.slots[0].url, HAUPT) == 0);

    printf("SlotStore-Ladeentscheidungen: OK\n");
    return 0;
}
