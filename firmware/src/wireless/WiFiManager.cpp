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

#include <ArduinoJson.h>
#include <Logger.h>

#include "wireless/WiFiManager.h"
#include "display/DisplayManager.h"
#include "smalltv_util.h"  // elapsed(): wrap-sichere Zeitvergleiche

static constexpr int LOADING_DELAY_MS = 1000;

/**
 * @brief Maximum number of attempts to connect to a wifi network
 */
static constexpr int MAX_CONNECTION_ATTEMPTS = 20;

/**
 * @brief Delay in milliseconds between wifi connection attempts
 */
static constexpr uint32_t CONNECTION_DELAY_MS = 500;

/// Abstand der Versuche, aus dem AP-Modus ins Heimnetz zurueckzukehren.
static constexpr uint32_t RUECKWEG_INTERVALL_MS = 60000;
/// Dauer eines solchen Versuchs.
static constexpr uint32_t RUECKWEG_VERSUCH_MS = 15000;
/// Nachlauf des AP nach einer Einrichtung ueber ihn -- damit die Antwort noch ankommt.
static constexpr uint32_t AP_NACHLAUF_MS = 10000;

/**
 * @brief WifiManager constructor
 *
 * @param staSsid The SSID for the WiFi station mode
 * @param staPass The password for the WiFi station mode
 * @param apSsid The SSID for the WiFi access point mode
 * @param apPass The password for the WiFi access point mode
 */
WiFiManager::WiFiManager(const char* staSsid, const char* staPass, const char* apSsid, const char* apPass)
    : _staSsid(staSsid != nullptr ? staSsid : ""),
      _staPass(staPass != nullptr ? staPass : ""),
      _apSsid(apSsid),
      _apPass(apPass) {}

auto WiFiManager::begin() -> void {
    // Die Zugangsdaten liegen im SecureStorage; der SDK-eigene Flash-Speicher soll sie
    // nicht bei jedem WiFi.begin() noch einmal schreiben (Flash-Verschleiss).
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    if (_staSsid.empty() || !startStationMode()) {
        startAccessPointMode();
    }

    Logger::info("Wifi active", "WiFiManager");
    Logger::info(String("Mode : " + String(_apMode ? "AP" : "STA")).c_str(), "WiFiManager");
    Logger::info(String("SSID : " + String(_apMode ? _apSsid : _staSsid.c_str())).c_str(), "WiFiManager");
    Logger::info(String("IP   : " + getIP().toString()).c_str(), "WiFiManager");
}

/**
 * @brief Attempts to connect the device to a WiFi network in station mode
 *
 * @return true if the device successfully connects to the WiFi network false otherwise
 */
auto WiFiManager::startStationMode() -> bool {
    WiFi.mode(WIFI_STA);
    WiFi.begin(_staSsid.c_str(), _staPass.c_str());
    int attempts = 0;

    Logger::info("Connecting to WiFi...", "WiFiManager");

    while (WiFi.status() != WL_CONNECTED && attempts < MAX_CONNECTION_ATTEMPTS) {
        delay(CONNECTION_DELAY_MS);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        _apMode = false;
        return true;
    }

    return false;
}

void WiFiManager::scanNetworks(JsonArray& out) {
    Logger::info("Scanning WiFi networks...", "WiFiManager");

    int8_t networks = WiFi.scanNetworks();

    Logger::info(String("Found networks: " + String(networks)).c_str(), "WiFiManager");

    for (int i = 0; i < networks; ++i) {
        JsonObject obj = out.add<JsonObject>();

        auto rssiVal = static_cast<int>(WiFi.RSSI(i));

        obj["ssid"] = WiFi.SSID(i);                             // NOLINT(readability-misplaced-array-index)
        obj["rssi"] = rssiVal;                                  // NOLINT(readability-misplaced-array-index)
        obj["enc"] = static_cast<int>(WiFi.encryptionType(i));  // NOLINT(readability-misplaced-array-index)
    }
}

auto WiFiManager::connectToNetwork(const char* ssid, const char* pass, uint32_t timeoutMs) -> bool {
    Logger::info(String("Connecting to " + String(ssid)).c_str(), "WiFiManager");

    constexpr int total_steps = 2;
    int step = 0;

    DisplayManager::clearScreen();
    DisplayManager::meldung("Verbinde mit WLAN", ssid, static_cast<float>(step) / static_cast<float>(total_steps));

    // Im AP-Modus laeuft der Versuch NEBEN dem AP (AP_STA): Wer gerade ueber das
    // Einrichtungsnetz verbunden ist, soll die Antwort noch bekommen.
    WiFi.mode(_apMode ? WIFI_AP_STA : WIFI_STA);
    WiFi.begin(ssid, pass);

    uint32_t start = millis();

    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
        delay(CONNECTION_DELAY_MS);
        DisplayManager::meldung(nullptr, nullptr, static_cast<float>(step) / static_cast<float>(total_steps));
    }

    step++;

    if (WiFi.status() == WL_CONNECTED) {
        _staSsid = (ssid != nullptr) ? ssid : "";
        _staPass = (pass != nullptr) ? pass : "";
        _rueckwegLaeuft = false;
        if (_apMode) {
            // Der AP bleibt noch AP_NACHLAUF_MS an, damit die Antwort im Einrichtungsnetz
            // ankommt; loop() schaltet ihn dann ab.
            _apNachlaufSeitMs = millis();
        }
        _apMode = false;

        Logger::info(String("Connected: " + WiFi.localIP().toString()).c_str(), "WiFiManager");
        DisplayManager::meldung("Verbunden", WiFi.localIP().toString().c_str(), 1.0F);

        return true;
    }

    DisplayManager::meldung("Keine Verbindung", ssid, 1.0F);
    Logger::warn("Failed to connect to WiFi", "WiFiManager");

    if (!_apMode && !_staSsid.empty()) {
        // Das Geraet hing im Heimnetz: dorthin zurueck statt in den AP. Ein Tippfehler
        // im Passwort darf es nicht bis zum Neustart vom Netz nehmen.
        WiFi.mode(WIFI_STA);
        WiFi.begin(_staSsid.c_str(), _staPass.c_str());
    } else {
        startAccessPointMode();
    }

    return false;
}

auto WiFiManager::isConnected() -> bool { return WiFi.status() == WL_CONNECTED; }

auto WiFiManager::getConnectedSSID() -> String { return WiFi.SSID(); }

/**
 * @brief Starts the WiFi Access Point (AP) mode
 *
 * @return true Always returns true to indicate the AP mode was started
 */
auto WiFiManager::startAccessPointMode() -> bool {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(_apSsid, _apPass);

    _apMode = true;
    _rueckwegLaeuft = false;
    _rueckwegZuletztMs = millis();  // erster Rueckweg-Versuch nach RUECKWEG_INTERVALL_MS

    return true;
}

auto WiFiManager::isApMode() const -> bool { return _apMode; }

auto WiFiManager::getIP() const -> IPAddress { return _apMode ? WiFi.softAPIP() : WiFi.localIP(); }

/**
 * @brief Rueckweg aus dem AP-Modus und Nachlauf des AP nach einer Einrichtung.
 *
 * Bis v0.2.7 war "der AP laeuft nur, solange kein bekanntes WLAN erreichbar ist" nicht
 * umgesetzt: Einmal im AP-Modus blieb das Geraet bis zum Neustart dort -- nach einem
 * Stromausfall mit langsam bootendem Router also fuer immer.
 */
auto WiFiManager::loop() -> void {
    // Nachlauf nach einer Einrichtung ueber den AP: Antwort ist raus, AP kann weg.
    if (_apNachlaufSeitMs != 0 && elapsed(millis(), _apNachlaufSeitMs, AP_NACHLAUF_MS)) {
        _apNachlaufSeitMs = 0;
        WiFi.mode(WIFI_STA);
        Logger::info("Einrichtung abgeschlossen, AP aus", "WiFiManager");
        return;
    }
    if (!_apMode || _staSsid.empty()) {
        return;
    }
    if (!_rueckwegLaeuft) {
        if (!elapsed(millis(), _rueckwegZuletztMs, RUECKWEG_INTERVALL_MS)) {
            return;
        }
        // Der AP bleibt an, die Station versucht es daneben -- wer gerade im
        // Einrichtungsnetz haengt, fliegt nicht mitten im Versuch raus.
        WiFi.mode(WIFI_AP_STA);
        WiFi.begin(_staSsid.c_str(), _staPass.c_str());
        _rueckwegSeitMs = millis();
        _rueckwegZuletztMs = _rueckwegSeitMs;
        _rueckwegLaeuft = true;
        Logger::info("AP-Modus: versuche das Heimnetz", "WiFiManager");
        return;
    }
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.mode(WIFI_STA);
        _apMode = false;
        _rueckwegLaeuft = false;
        Logger::info(String("Heimnetz wieder da: " + WiFi.localIP().toString()).c_str(), "WiFiManager");
        return;
    }
    if (elapsed(millis(), _rueckwegSeitMs, RUECKWEG_VERSUCH_MS)) {
        WiFi.mode(WIFI_AP);  // Versuch vorbei, zurueck auf reinen AP
        _rueckwegLaeuft = false;
    }
}
