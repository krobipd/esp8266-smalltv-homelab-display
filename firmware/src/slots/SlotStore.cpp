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

#include "slots/SlotStore.h"

#include <LittleFS.h>

#include "Logger.h"
#include "json_datei.h"

namespace {
// Bewusst NICHT /config.json: Diese Datei gehoert der Basis-Firmware (Display-Einstellungen)
// und wird von ihr sogar oeffentlich ausgeliefert. Die Slot-Konfiguration bekommt eine eigene
// Datei -- sonst wuerde sie fremde Einstellungen ueberschreiben und waere ohne Anmeldung lesbar.
const char* CONFIG_PATH = "/slots.json";
/// Ablage einer unlesbaren Konfiguration (siehe beiseitelegen()).
const char* DEFEKT_PATH = "/slots.json.defekt";
/// Zweitschrift, zeitversetzt geschrieben (siehe SlotStore::sicherungSchreiben).
const char* SICHERUNG_PATH = "/slots.bak.json";

/// Liest eine Konfigurationsdatei. Getrennt von load(), weil dieselbe Prozedur fuer
/// die Hauptdatei UND die Zweitschrift gilt -- zwei Kopien wuerden auseinanderlaufen.
/// nichtDa unterscheidet "Datei fehlt" (kein Fehler) von "Datei kaputt" (Fehler).
auto dateiLesen(const char* pfad, Config& out, char* errOut, size_t errSize,
                bool& nichtDa) -> bool {
    nichtDa = false;
    if (!LittleFS.exists(pfad)) {
        nichtDa = true;
        return false;
    }

    File f = LittleFS.open(pfad, "r");
    if (!f) {
        setErr(errOut, errSize, "Konfiguration nicht lesbar");
        return false;
    }
    if (f.size() == 0) {
        f.close();
        setErr(errOut, errSize, "Konfigurationsdatei ist leer");
        return false;
    }
    // Obergrenze mit Sicherheitsabstand: Eine volle Konfiguration mit zwoelf Slots und
    // maximal langen Adressen bleibt unter 5 KB. Der Deckel liegt weit darueber und
    // schuetzt nur davor, dass eine unsinnig grosse Datei beim Start den Heap zerlegt.
    // Er darf NIE so knapp sitzen, dass eine selbst geschriebene Datei daran scheitert --
    // genau das war der Datenverlust-Fehler.
    const size_t GROESSE_MAX = 16384;
    if (f.size() > GROESSE_MAX) {
        f.close();
        setErr(errOut, errSize, "Konfigurationsdatei ist unplausibel gross");
        return false;
    }

    // Direkt aus der Datei lesen, statt sie erst komplett in den Heap zu holen.
    JsonDocument doc;
    const DeserializationError e = deserializeJson(doc, f);
    f.close();
    if (e) {
        setErr(errOut, errSize, "Konfiguration ist kein gueltiges JSON");
        return false;
    }

    configFromDoc(doc, out);
    return true;
}

// Die STD_*-Auslieferungswerte liegen in config_codec.h -- EINE Autoritaet fuer
// defaults() hier und fuer fehlende Felder in configFromDoc().

}  // namespace

void SlotStore::defaults(Config& out) {
    out = Config{};
    for (uint8_t i = 0; i < MAX_PAGES; i++) {
        out.layout[i] = LAYOUT_VIER;
    }
    out.rotateSec = STD_ROTATE_SEC;
    out.colorWarn = FARBE_WARNUNG;
    out.colorAlarm = FARBE_ALARM;
    out.helligkeit = STD_HELLIGKEIT;
    out.hellModus = 0;
    out.hellSec = STD_SCHALTER_SEC;
    out.hellAn = STD_HELLIGKEIT;
    out.hellAus = STD_NACHT_HELLIGKEIT;
    out.nachtAn = false;
    out.nachtVon = STD_NACHT_VON;
    out.nachtBis = STD_NACHT_BIS;
    out.nachtHelligkeit = STD_NACHT_HELLIGKEIT;
    // Keine vorbelegten Slots -- ein ausgeliefertes Geraet zeigt nichts Fremdes an.
    // Die Schriftstufen bekommen trotzdem ihren Standard: Config{} nullt sie, und
    // Stufe 0 zeichnet ueberhaupt nichts. Ein spaeter belegter Slot waere unsichtbar.
    // Aufteilung bei zwei Werten je Seite: automatisch, bis der Nutzer etwas anderes will.
    for (auto& t : out.teilung) {
        t = TEILUNG_AUTOMATISCH;
    }
    for (auto& s : out.slots) {
        s.wertSize = STD_WERT_SIZE;
        s.labelSize = STD_LABEL_SIZE;
        s.unitSize = STD_UNIT_SIZE;
        s.einheitDaneben = STD_EINHEIT_DANEBEN;
    }
}

auto SlotStore::load(Config& out, char* errOut, size_t errSize, bool* ausSicherung) -> bool {
    if (ausSicherung != nullptr) {
        *ausSicherung = false;
    }

    bool nichtDa = false;
    if (dateiLesen(CONFIG_PATH, out, errOut, errSize, nichtDa)) {
        return true;
    }
    // Beschaedigte Hauptdatei: Bevor Standardwerte gelten, wird die Zweitschrift
    // versucht -- sie ist fuer genau diesen Fall da. Die kaputte Hauptdatei wird
    // dabei beiseitegelegt, nicht ueberschrieben; wer mag, kann sie sich ansehen.
    //
    // Ohne diesen Zweig entstuende ein Datenverlust: Es gaeben Standardwerte, der
    // Nutzer speicherte irgendetwas, und die Zweitschrift wuerde kurz darauf mit
    // diesem leeren Stand ueberschrieben -- die echten Werte laegen dann nur noch in
    // einer Datei, die niemand liest.
    if (!nichtDa) {
        char sicherungFehler[96] = {0};
        bool sicherungFehlt = false;
        if (dateiLesen(SICHERUNG_PATH, out, sicherungFehler, sizeof(sicherungFehler),
                       sicherungFehlt)) {
            Logger::warn("Hauptdatei beschaedigt -- Konfiguration kommt aus der Zweitschrift",
                         "Slots");
            beiseitelegen();
            if (ausSicherung != nullptr) {
                *ausSicherung = true;
            }
            return true;
        }
        // Auch die Zweitschrift traegt nichts: Der Aufrufer bekommt den urspruenglichen
        // Fehler und legt die Hauptdatei beiseite.
        return false;
    }

    // Ab hier: Die Hauptdatei FEHLT. Genau dieser Fall trat am 07.09.2026 auf, nachdem
    // das Geraet 18 Stunden ohne Strom war -- und wurde von niemandem bemerkt, weil er
    // sich von einem fabrikneuen Geraet nicht unterscheiden liess.
    char sicherungFehler[96] = {0};
    bool sicherungNichtDa = false;
    if (dateiLesen(SICHERUNG_PATH, out, sicherungFehler, sizeof(sicherungFehler),
                   sicherungNichtDa)) {
        Logger::warn("Hauptdatei fehlt -- Konfiguration kommt aus der Zweitschrift", "Slots");
        if (ausSicherung != nullptr) {
            *ausSicherung = true;
        }
        return true;
    }
    if (!sicherungNichtDa) {
        Logger::error(sicherungFehler, "Slots");
    }

    defaults(out);
    Logger::info("SlotStore: keine Konfiguration vorhanden, Standardwerte werden verwendet");
    return true;
}

auto SlotStore::sicherungSchreiben(const Config& cfg) -> bool {
    JsonDocument doc;
    configToDoc(cfg, doc);
    if (!jsonAtomarSchreiben(SICHERUNG_PATH, doc)) {
        Logger::error("Zweitschrift konnte nicht geschrieben werden", "Slots");
        return false;
    }
    return true;
}

auto SlotStore::save(const Config& cfg, char* errOut, size_t errSize) -> bool {
    // Atomar ueber den gemeinsamen Helfer (json_datei.h): Eine fehlgeschlagene
    // Speicherung laesst die Zieldatei garantiert unangetastet -- darauf verlassen
    // sich die Fehlerpfade der API (Reload statt Rollback-Feldlisten).
    JsonDocument doc;
    configToDoc(cfg, doc);
    if (!jsonAtomarSchreiben(CONFIG_PATH, doc)) {
        setErr(errOut, errSize, "Konfiguration konnte nicht gespeichert werden");
        return false;
    }
    return true;
}

auto SlotStore::beiseitelegen() -> bool {
    // Eine unlesbare Konfiguration wird NICHT ueberschrieben, sondern umbenannt --
    // wer mag, kann sie sich noch ansehen; der naechste save() legt eine frische an.
    return LittleFS.rename(CONFIG_PATH, DEFEKT_PATH);
}
