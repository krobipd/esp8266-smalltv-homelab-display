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

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <ESP8266WiFi.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <string>

class WiFiManager {
   public:
    WiFiManager(const char* staSsid, const char* staPass, const char* apSsid, const char* apPass);
    void begin();
    bool startStationMode();
    bool startAccessPointMode();
    bool isApMode() const;
    IPAddress getIP() const;
    static void scanNetworks(JsonArray& out);
    bool connectToNetwork(const char* ssid, const char* pass, uint32_t timeoutMs = 10000);
    static bool isConnected();
    static String getConnectedSSID();

    /// In jeden Schleifendurchlauf einhaengen. Im AP-Modus wird alle 60 s versucht, das
    /// gespeicherte Heimnetz zu erreichen; gelingt es, geht der AP wieder aus. Nach einer
    /// Einrichtung ueber den AP bleibt dieser noch 10 s an, damit die Antwort ankommt.
    void loop();

   private:
    // Kopien statt Zeiger: Die Zeiger zeigten in std::string-Member des ConfigManagers,
    // die jede Aenderung der Zugangsdaten neu belegt -- ein haengender Zeiger, sobald
    // hier zur Laufzeit gelesen wird (genau das tut loop()).
    std::string _staSsid;
    std::string _staPass;
    const char* _apSsid;
    const char* _apPass;
    bool _apMode = false;
    unsigned long _rueckwegSeitMs = 0;     // Start des laufenden Rueckweg-Versuchs
    unsigned long _rueckwegZuletztMs = 0;  // Start des letzten Versuchs (Abstand)
    bool _rueckwegLaeuft = false;
    unsigned long _apNachlaufSeitMs = 0;   // 0 = kein Abschalten des AP geplant
};

#endif  // WIFI_MANAGER_H
