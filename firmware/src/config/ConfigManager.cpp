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
#include <LittleFS.h>

#include <Logger.h>
#include "config/ConfigManager.h"
#include "config/SecureStorage.h"
#include "json_datei.h"

ConfigManager::ConfigManager(const char* filename) : filename(filename), secure() {}

/**
 * @brief Loads the configuration from a file stored in SPIFFS
 *
 * @return true if the configuration was successfully loaded and parsed false otherwise
 */
auto ConfigManager::load() -> bool {
    if (!LittleFS.begin()) {
        Logger::error("Failed to mount LittleFS", "ConfigManager");
        return false;
    }

    // Grundlage sind IMMER die Werte aus dem SecureStorage -- die Datei ergaenzt nur.
    // Frueher kehrte load() bei fehlender/leerer Datei um, BEVOR es den SecureStorage
    // las: Ein Dateisystem-Update (littlefs.bin enthaelt keine /config.json) haette
    // WLAN-Zugang und Passwort "vergessen", obwohl beide sicher gespeichert waren.
    this->ssid = secure.get("wifi_ssid").c_str();
    this->password = secure.get("wifi_password").c_str();
    this->api_token = secure.get("api_token").c_str();

    File file = LittleFS.open(filename.c_str(), "r");
    if (!file) {
        // Kein Fehler: Ein frisch installiertes Geraet hat keine /config.json, das
        // Dateisystem-Abbild bringt bewusst keine mit. Es gelten die Standardwerte.
        Logger::info("Keine /config.json -- Standardwerte gelten", "ConfigManager");
        return false;
    }

    size_t size = file.size();
    if (size == 0) {
        Logger::warn("Config file is empty", "ConfigManager");
        file.close();
        return false;
    }

    std::unique_ptr<char[]> buf(new char[size + 1]);
    file.readBytes(buf.get(), size);
    buf[size] = '\0';
    file.close();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, buf.get());
    if (error) {
        Logger::error(("Failed to parse config file : " + String(error.c_str())).c_str(), "ConfigManager");
        return false;
    }

    String ssid = doc["wifi_ssid"] | "";
    String password = doc["wifi_password"] | "";
    String api_token = doc["api_token"] | "";
    String ntp_server_cfg = doc["ntp_server"] | "";

    this->lcd_rotation = doc["lcd_rotation"] | lcd_rotation;

    // Der Zeitserver gehoert NICHT zur einmaligen Migration: Er lebt dauerhaft in der
    // Datei und muss auf JEDEM Boot uebernommen werden. Frueher stand diese Zuweisung
    // nur im Migrationszweig -- auf jedem normalen Boot blieb der Member leer, und das
    // naechste save() schrieb die Datei ohne den eingestellten Server: Die Einstellung
    // ueberlebte keinen Neustart.
    if (ntp_server_cfg.length() != 0) {
        this->ntp_server = ntp_server_cfg.c_str();
    }
    // Dasselbe fuer die Zeitzone: dauerhaft in der Datei, auf jedem Boot zu uebernehmen.
    String zeitzone_cfg = doc["zeitzone"] | "";
    if (zeitzone_cfg.length() != 0) {
        this->zeitzone = zeitzone_cfg.c_str();
    }

    // Die Vergleichswerte aus dem SecureStorage stehen seit dem Vorbelegen oben schon
    // in den Membern -- nicht erneut lesen. Migration: Werte aus der Datei wandern in
    // den SecureStorage und werden anschliessend per save() aus der (oeffentlich
    // ausgelieferten) Datei entfernt.
    if ((ssid.length() != 0 && this->ssid.empty()) ||
        (password.length() != 0 && this->password.empty())) {
        secure.put("wifi_ssid", ssid.c_str());
        secure.put("wifi_password", password.c_str());
        this->ssid = ssid.c_str();
        this->password = password.c_str();

        ConfigManager::save();

        Logger::info("WiFi credentials migrated to SecureStorage", "ConfigManager");
    }

    if (api_token.length() != 0 && this->api_token.empty()) {
        secure.put("api_token", api_token.c_str());
        this->api_token = api_token.c_str();

        ConfigManager::save();

        Logger::info("API token migrated to SecureStorage", "ConfigManager");
    } else if (api_token.length() != 0) {
        // Steht trotzdem noch ein Token in der Datei (z. B. nach einem erneuten
        // Dateisystem-Update mit ausgeliefertem Standardwert), wird er entfernt:
        // /config.json wird oeffentlich ausgeliefert und darf nie einen tragen.
        ConfigManager::save();
    }

    return true;
}

/**
 * @brief Retrieves the current Wi-Fi SSID
 *
 * @return The SSID as a c style string
 */
auto ConfigManager::getSSID() const -> const char* { return ssid.c_str(); }

/**
 * @brief Retrieves the current Wi-Fi password
 *
 * @return The password as a c style string
 */
auto ConfigManager::getPassword() const -> const char* { return password.c_str(); }

/**
 * @brief Retrieves the current API token
 *
 * @return The API token as a c style string
 */
auto ConfigManager::getApiToken() const -> const char* { return api_token.c_str(); }

/**
 * @brief Retrieves the LCD rotation setting
 *
 * @return The rotation of the LCD
 */

/**
 * @brief Set LCD rotation in memory
 *
 * @param newRotation Rotation value in range [0, 7]
 *
 * @return void
 */
auto ConfigManager::setLCDRotation(uint8_t newRotation) -> void { lcd_rotation = newRotation; }

/**
 * @brief Set WiFi credentials in memory
 * @param newSsid The SSID
 * @param newPassword The password
 *
 * @return void
 */
auto ConfigManager::setWiFi(const char* newSsid, const char* newPassword) -> void {
    if (newSsid != nullptr) {
        ssid = newSsid;
    }
    if (newPassword != nullptr) {
        password = newPassword;
    }
}
/**
 * @brief Set the API password in memory (persisted on the next save())
 * @param newApiToken The new password; empty string disables the protection
 *
 * @return void
 */
auto ConfigManager::setApiToken(const char* newApiToken) -> void {
    if (newApiToken != nullptr) {
        api_token = newApiToken;
    }
}

/**
 * @brief Save the current configuration (file + SecureStorage)
 *
 * @return true if the configuration was successfully saved false otherwise
 */
auto ConfigManager::save() -> bool {
    if (!LittleFS.begin()) {
        Logger::error("Failed to mount LittleFS", "ConfigManager");

        return false;
    }

    // Nur bei tatsaechlicher Aenderung in den SecureStorage schreiben: Jedes put()
    // serialisiert dessen komplettes Dokument neu und verschluesselt es (SHA-256-
    // Schluesselableitung + XOR) -- drei ~2-KB-Heap-Spitzen fuer eine reine
    // Rotations- oder Zeitserver-Aenderung waeren vergeudet.
    //
    // Das Passwort gehoert dazu: Ohne die api_token-Zeile galt jede Passwort-
    // Aenderung (Web-API UND Rescue-Reset) nur bis zum naechsten Neustart --
    // danach kam der alte Wert zurueck, waehrend der Browser den neuen kannte:
    // dauerhaft ausgesperrt, und der dokumentierte Rescue-Weg war wirkungslos.
    bool sicherOk = true;
    if (secure.get("wifi_ssid", "") != this->ssid.c_str()) {
        sicherOk = secure.put("wifi_ssid", this->getSSID()) && sicherOk;
    }
    if (secure.get("wifi_password", "") != this->password.c_str()) {
        sicherOk = secure.put("wifi_password", this->getPassword()) && sicherOk;
    }
    if (secure.get("api_token", "") != this->api_token.c_str()) {
        sicherOk = secure.put("api_token", this->getApiToken()) && sicherOk;
    }
    if (!sicherOk) {
        // Sonst hiesse es "Passwort gespeichert", obwohl es nur bis zum Neustart gilt.
        Logger::error("SecureStorage konnte nicht schreiben", "ConfigManager");
        return false;
    }

    JsonDocument doc;
    doc["lcd_rotation"] = lcd_rotation;
    if (!this->ntp_server.empty()) {
        doc["ntp_server"] = this->ntp_server.c_str();
    }
    if (!this->zeitzone.empty()) {
        doc["zeitzone"] = this->zeitzone.c_str();
    }

    // Atomar ueber den gemeinsamen Helfer (json_datei.h) -- gleiche Autoritaet
    // wie SlotStore::save(), inklusive Vollstaendigkeitspruefung.
    if (!jsonAtomarSchreiben(filename.c_str(), doc)) {
        Logger::error("Failed to write config file", "ConfigManager");

        return false;
    }

    Logger::info("Configuration saved", "ConfigManager");

    return true;
}
