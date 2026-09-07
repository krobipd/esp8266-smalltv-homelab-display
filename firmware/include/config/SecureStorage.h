// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * GeekMagic Open Firmware
 * Copyright (C) 2026 Times-Z
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

#ifndef SECURE_STORAGE_H
#define SECURE_STORAGE_H

#include <Arduino.h>
#include <ArduinoJson.h>

class SecureStorage {
   public:
    SecureStorage(size_t eepromSize = 2048);
    bool begin();
    bool put(const char* key, const char* value);
    String get(const char* key, const char* defaultValue = nullptr);

    // Set the public salt (should be called before begin())
    static void setSalt(const String& salt);

    /// true, wenn im Sektor etwas steht, das sich nicht lesen liess. Dann wurde er
    /// BEWUSST nicht ueberschrieben -- ein Lesefehler kann voruebergehend sein, die
    /// WLAN-Zugangsdaten sind es nicht. Ein Neustart darf es also nochmal versuchen.
    auto datenUnlesbar() const -> bool { return _unlesbar; }

   private:
    size_t _eepromSize;
    bool loadToMemory();
    bool flushToEEPROM();
    /// Sieht der Sektor fabrikneu aus (nur 0xFF oder nur 0x00)? Nur dann darf begin()
    /// von sich aus hineinschreiben.
    bool sektorLeer();
    JsonDocument _doc;
    bool _ready = false;
    bool _unlesbar = false;
};

#endif  // SECURE_STORAGE_H
