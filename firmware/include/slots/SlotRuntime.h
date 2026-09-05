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

#ifndef SLOT_RUNTIME_H
#define SLOT_RUNTIME_H

#include "config_codec.h"

/// Groesse des angezeigten Werts inklusive Abschluss.
static const size_t WERT_LEN = 24;
/// Laenge der zuletzt gemeldeten Fehlerursache.
static const size_t FEHLER_LEN = 48;
/// Groesse des eingelesenen Antwortkoerpers -- gilt fuer Betrieb UND Testabruf.
/// Zwei verschiedene Grenzen hatten zur Folge, dass der Assistent Antworten ablehnte
/// (512-Byte-Vorschau abgeschnitten -> "kein JSON"), die der Betrieb problemlos las.
static const size_t KOERPER_MAX = 1024;

/**
 * @brief Laufzeitzustand eines Slots -- nichts davon wird gespeichert.
 */
struct SlotLaufzeit {
    bool hatDaten;              ///< Es liegt mindestens ein erfolgreicher Abruf vor
    char wert[WERT_LEN];        ///< Bereits formatierter Anzeigewert
    SlotState zustand;          ///< Ergebnis der Schwellwertpruefung
    uint8_t fehlversuche;       ///< Aufeinanderfolgende Fehlschlaege
    uint32_t letzterErfolgMs;   ///< Zeitpunkt des letzten gelungenen Abrufs
    uint32_t letzterVersuchMs;  ///< Zeitpunkt des letzten Versuchs
    char fehler[FEHLER_LEN];    ///< Ursache des letzten Fehlschlags
};

/**
 * @brief Ruft die Slot-Werte ab und haelt ihren Zustand.
 *
 * Grundsatz: Es laeuft immer nur EIN Abruf, nie mehrere gleichzeitig -- jede offene
 * Verbindung kostet Heap, und davon ist auf diesem Chip kaum etwas uebrig.
 * Zweiter Grundsatz: Ein fehlgeschlagener Abruf erzeugt NIE einen Wert. Der letzte
 * gute Wert bleibt stehen und wird nach drei Fehlversuchen als veraltet gekennzeichnet.
 */
class SlotRuntime {
   public:
    static void begin(Config* cfg);

    /// In jeden Schleifendurchlauf einhaengen. Ruft hoechstens einen faelligen Slot ab.
    static void update();

    static auto laufzeit(uint8_t index) -> const SlotLaufzeit&;

    /// Gilt der Wert als veraltet? Drei Fehlversuche in Folge ODER der letzte Erfolg
    /// liegt laenger als drei Abrufintervalle zurueck (greift auch, wenn ohne WLAN
    /// gar keine Versuche mehr stattfinden).
    static auto veraltet(uint8_t index) -> bool;

    /// Alter des letzten guten Werts in Sekunden; 0 wenn noch nie erfolgreich.
    static auto alterSek(uint8_t index) -> uint32_t;

    /// Laufzeitdaten eines Slots verwerfen (nach Aenderung oder Loeschen).
    static void zuruecksetzen(uint8_t index);

    /// Einmaliger Probeabruf fuer den "Testen"-Knopf der Oberflaeche.
    /// Liefert Statuscode, gekuerzte Antwort und -- bei JSON -- die Feldnamen.
    static auto probe(const char* url, int& httpStatus, char* preview, size_t previewSize,
                      char* fehler, size_t fehlerSize) -> bool;

    /// Gemeinsamer KOERPER_MAX grosser Antwortpuffer -- Abrufe laufen strikt seriell,
    /// der Testabruf der API darf ihn mitbenutzen statt einen eigenen zu halten.
    static auto scratchPuffer() -> char*;

    /// Zustand des Helligkeits-Schalters: true = "an". Ohne Datenpunkt oder solange
    /// noch keine Antwort vorliegt, gilt "an" -- lieber zu hell als ein dunkles Display,
    /// dessen Ursache niemand findet.
    static auto hellSchalterAn() -> bool;

    /// Laufzeitzustand des Helligkeits-Schalters verwerfen -- nach einer geaenderten
    /// Einstellung, sonst erbt die neue Adresse den Rueckzug der alten (bis 5 min).
    static void hellZuruecksetzen();

    /// Erste Seite mit einem aktiven Alarm, sonst 0.
    static auto alarmSeite() -> uint8_t;
};

#endif  // SLOT_RUNTIME_H
