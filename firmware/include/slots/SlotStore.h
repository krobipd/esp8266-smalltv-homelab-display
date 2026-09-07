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

#ifndef SLOT_STORE_H
#define SLOT_STORE_H

#include "config_codec.h"

/**
 * @brief Laedt und speichert die Slot-Konfiguration in LittleFS.
 *
 * Bewusst eine duenne Schale: Die gesamte Pruef- und Umwandlungslogik liegt in
 * config_codec.h und ist dort am Host unit-getestet. Hier passiert nur Dateizugriff.
 */
class SlotStore {
   public:
    /// Liest /slots.json (NICHT /config.json -- die gehoert der Basis-Firmware und wird
    /// oeffentlich ausgeliefert). Fehlt die Datei, wird die Zweitschrift
    /// /slots.bak.json versucht; erst wenn auch die fehlt, gelten Standardwerte
    /// (Rueckgabe true). Eine vorhandene, aber kaputte Datei liefert false und laesst
    /// out unberuehrt.
    /// ausSicherung (optional) meldet, dass die Zweitschrift einspringen musste --
    /// der Aufrufer soll das sichtbar machen, statt es zu verschlucken.
    static auto load(Config& out, char* errOut, size_t errSize,
                     bool* ausSicherung = nullptr) -> bool;

    /// Schreibt die Zweitschrift /slots.bak.json.
    ///
    /// BEWUSST ein eigener, ZEITVERSETZTER Aufruf und nicht Teil von save():
    /// Am 06./07.09.2026 ging eine Konfiguration verloren, die nachweislich auf dem
    /// Flash lag (Abnahme 06.09., 14:56). Kein Pfad im Programm loescht sie -- die
    /// tragfaehigste Erklaerung ist ein Metadaten-Commit, der die 18 Stunden ohne
    /// Strom nicht ueberstanden hat. Gegen so etwas hilft KEINE Pruefung nach dem
    /// Schreiben (sie liest ueber dieselbe eingehaengte Instanz), sondern nur eine
    /// zweite Schreibung zu einem anderen Zeitpunkt: Ein einzelner schwacher Commit
    /// trifft dann nicht beide Dateien.
    static auto sicherungSchreiben(const Config& cfg) -> bool;

    /// Schreibt /slots.json atomar (Temporaerdatei + rename) -- bei jedem Aufruf,
    /// einen Nur-bei-Aenderung-Vergleich gibt es bewusst nicht (gespeichert wird
    /// ohnehin nur auf Nutzeraktion).
    static auto save(const Config& cfg, char* errOut, size_t errSize) -> bool;

    /// Standardkonfiguration: vier Seiten mit Vierer-Raster, KEINE vorbelegten Slots.
    static void defaults(Config& out);

    /// Unlesbare Konfigurationsdatei nach /slots.json.defekt umbenennen statt sie beim
    /// naechsten Speichern zu ueberschreiben. Der Dateipfad ist Privatwissen dieser
    /// Klasse -- deshalb liegt das hier und nicht beim Aufrufer.
    static auto beiseitelegen() -> bool;
};

#endif  // SLOT_STORE_H
