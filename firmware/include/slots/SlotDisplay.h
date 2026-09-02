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

#ifndef SLOT_DISPLAY_H
#define SLOT_DISPLAY_H

#include "config_codec.h"

/**
 * @brief Zeichnet die Slot-Seiten auf das Display und schaltet zwischen ihnen um.
 *
 * Gezeichnet wird immer nur die einzelne Kachel, deren Anzeige sich geaendert hat --
 * ein Vollbildpuffer waere mit 115 KB fuer diesen Chip unerreichbar.
 * Ein Alarm haelt die Rotation an und zeigt die betroffene Seite, bis er vorbei ist.
 */
class SlotDisplay {
   public:
    static void begin(Config* cfg);

    /// In jeden Schleifendurchlauf einhaengen.
    static void update();

    /// Rotationsschritt: naechste Seite mit Inhalt anzeigen. Wird intern von update()
    /// im Wechselintervall gerufen -- eine Geraetetaste gibt es nicht.
    static void naechsteSeite();

    /// Alles neu zeichnen erzwingen, etwa nach einer Konfigurationsaenderung.
    static void neuZeichnen();

    /// Helligkeit sofort neu anwenden (nach einer Aenderung der Einstellungen).
    static void helligkeitAnwenden();
};

#endif  // SLOT_DISPLAY_H
