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

#ifndef SLOT_API_H
#define SLOT_API_H

#include "config_codec.h"
#include "web/Webserver.h"

/**
 * @brief HTTP-Schnittstelle der Slot-Verwaltung.
 *
 * Folgt dem Namensschema der uebrigen Endpunkte (/api/v1/...) und deren
 * Zugangspruefung, damit die Oberflaeche und die OpenAPI-Beschreibung einheitlich bleiben.
 */
class SlotApi {
   public:
    /// Bindet die Konfiguration ein, die von den Endpunkten gelesen und geschrieben wird.
    static void begin(Config* cfg);

    /// Warnhinweis fuer die Oberflaeche setzen (z. B. Ladefehler beim Start).
    /// Wird mit GET /slots als "warnung" ausgeliefert, bis ein Speichern gelingt.
    static void setLadeWarnung(const char* text);

    /// Registriert alle Routen am Webserver.
    static void registerRoutes(Webserver* webserver);

    /// Schreibt die Konfiguration aus dem RAM in das Dateisystem -- nach einem
    /// Dateisystem-Update, dessen Abbild keine /slots.json mitbringt. true = geschrieben.
    static auto konfigurationSichern() -> bool;
};

void handleSlotsGet(Webserver* webserver);
void handleSlotsSave(Webserver* webserver);
void handleSlotsDelete(Webserver* webserver);
void handleSlotsTest(Webserver* webserver);
void handleSlotsStatus(Webserver* webserver);
void handleSlotsSettings(Webserver* webserver);

#endif  // SLOT_API_H
