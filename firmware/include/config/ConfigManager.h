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

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <ArduinoJson.h>
#include "config/SecureStorage.h"
#include <string>
#include <cstdint>

// Die Anschluesse der Platine stehen in hardware/Pins.h -- diese Klasse liest
// Einstellungen aus einer Datei und hat mit der Verdrahtung nichts zu tun.
class ConfigManager {
   public:
    ConfigManager(const char* filename = "/config.json");
    bool load();
    bool save();
    void setWiFi(const char* newSsid, const char* newPassword);
    const char* getSSID() const;
    const char* getPassword() const;
    const char* getApiToken() const;
    void setApiToken(const char* newApiToken);
    void setLCDRotation(uint8_t newRotation);
    uint8_t getLCDRotationSafe() const { return lcd_rotation; }
    const char* getNtpServer() const { return ntp_server.c_str(); }
    void setNtpServer(const char* s) {
        if (s) ntp_server = s;
    }
    /// Zeitzone als POSIX-TZ-Regel (z. B. "CET-1CEST,M3.5.0,M10.5.0/3").
    /// Leer heisst: die Vorgabe des Geraets, Mitteleuropa.
    const char* getZeitzone() const { return zeitzone.c_str(); }
    void setZeitzone(const char* s) {
        if (s) zeitzone = s;
    }

    // Bewusst oeffentlich: der Rettungsmodus zaehlt darin seine Startversuche, bevor
    // ueberhaupt eine Konfiguration gelesen wurde. Ein Umweg ueber diese Klasse waere
    // eine Fassade ohne Aussage.
    SecureStorage secure;

   private:
    std::string ssid;
    std::string password;
    std::string api_token;
    std::string filename;
    // 0 = ungedreht und ungespiegelt. Stand bis v0.2.0 auf 4 -- unveraendert aus der
    // Times-Z-Basis uebernommen. Die Stufen ab 4 sind die GESPIEGELTEN Varianten:
    // Ein frisch installiertes Geraet zeigte alles seitenverkehrt, weil littlefs.bin
    // bewusst keine config.json mitbringt und deshalb dieser Wert hier greift.
    uint8_t lcd_rotation = 0;
    std::string ntp_server;
    std::string zeitzone;
};

#endif  // CONFIG_MANAGER_H
