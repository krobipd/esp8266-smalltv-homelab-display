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

#include <Arduino.h>
#include <Logger.h>
#include <ArduinoJson.h>
#include <Updater.h>

#include "web/Webserver.h"
#include "abbild_art.h"
#include "web/Api.h"
#include "web/antwort.h"
#include "display/DisplayManager.h"
#include "slots/SlotApi.h"

#include "config/ConfigManager.h"
#include "wireless/WiFiManager.h"
#include "ntp/NTPClient.h"

extern ConfigManager configManager;
extern WiFiManager* wifiManager;
extern NTPClient* ntpClient;

static bool otaError = false;
static size_t otaSize = 0;
static String otaStatus;
static volatile bool otaInProgress = false;
static volatile bool otaCancelRequested = false;
// Bei einem Upload laufen ZWEI Handler: erst der Upload-Handler (Brocken fuer Brocken),
// danach der Abschluss-Handler. Lehnt der erste die Anfrage mangels Passwort ab, hat er
// die 401 bereits gesendet -- der Abschluss darf dann nichts mehr senden, sonst gingen
// zwei Antworten auf eine Anfrage hinaus.
static bool otaZugangAbgelehnt = false;
static size_t otaTotal = 0;

static void otaHandleStart(Webserver* webserver, HTTPUpload& upload, int mode);
static void otaHandleWrite(HTTPUpload& upload, int mode);
static void otaHandleEnd(HTTPUpload& upload, int mode);
static void otaHandleAborted(HTTPUpload& upload, int mode);

static constexpr int WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr int BEARER_LEN = 7;

/**
 * @brief Register API endpoints for the webserver
 * @param webserver Pointer to the Webserver instance
 *
 * @return void
 */
void registerApiEndpoints(Webserver* webserver) {
    Logger::info("Registering API endpoints", "API");

    // Kopfzeile mit der Pruefsumme des Abbilds einsammeln lassen. collectHeaders()
    // ersetzt die Liste, haelt Authorization und If-None-Match aber von sich aus fest.
    webserver->raw().collectHeaders("X-Abbild-MD5");

    // Alle einfachen Routen laufen ueber geschuetzt(): die Passwortpruefung haengt an
    // der Tabelle, nicht an einer wiederholten Zeile in jedem Handler. Die beiden
    // OTA-Uploads bleiben von Hand registriert -- sie brauchen einen zweiten
    // (Upload-)Handler und pruefen beim ersten Brocken.
    geschuetzt(webserver, "/api/v1/wifi/scan", HTTP_GET, handleWifiScan);
    geschuetzt(webserver, "/api/v1/wifi/connect", HTTP_POST, handleWifiConnect);
    geschuetzt(webserver, "/api/v1/wifi/status", HTTP_GET, handleWifiStatus);
    geschuetzt(webserver, "/api/v1/ntp/sync", HTTP_POST, handleNtpSync);
    geschuetzt(webserver, "/api/v1/ntp/status", HTTP_GET, handleNtpStatus);
    geschuetzt(webserver, "/api/v1/ntp/config", HTTP_GET, handleNtpConfigGet);
    geschuetzt(webserver, "/api/v1/ntp/config", HTTP_POST, handleNtpConfigSet);
    geschuetzt(webserver, "/api/v1/display/rotation", HTTP_GET, handleDisplayRotationGet);
    geschuetzt(webserver, "/api/v1/display/rotation", HTTP_POST, handleDisplayRotationSet);
    geschuetzt(webserver, "/api/v1/reboot", HTTP_POST, handleReboot);
    geschuetzt(webserver, "/api/v1/ota/status", HTTP_GET, handleOtaStatus);
    geschuetzt(webserver, "/api/v1/ota/cancel", HTTP_POST, handleOtaCancel);
    geschuetzt(webserver, "/api/v1/token/check", HTTP_GET, handleTokenCheck);
    geschuetzt(webserver, "/api/v1/token/save", HTTP_POST, handleTokenSave);
    geschuetzt(webserver, "/api/v1/logs", HTTP_GET, handleLogsGet);
    geschuetzt(webserver, "/api/v1/logs/download", HTTP_GET, handleLogsDownload);
    geschuetzt(webserver, "/api/v1/logs/clear", HTTP_POST, handleLogsClear);

    webserver->raw().on(
        "/api/v1/ota/fw", HTTP_POST, [webserver]() { handleOtaFinished(webserver); },
        [webserver]() { handleOtaUpload(webserver, U_FLASH); });
    webserver->raw().on(
        "/api/v1/ota/fs", HTTP_POST, [webserver]() { handleOtaFinished(webserver); },
        [webserver]() { handleOtaUpload(webserver, U_FS); });

    webserver->raw().onNotFound([webserver]() {
        // Auch antworten, wenn nichts passt: Im Erstinstallations-Modus (leeres
        // Dateisystem) ist dieser Handler der einzige -- ohne Antwort hinge jeder
        // 404-Aufruf bis zum Timeout des Clients.
        webserver->raw().send(HTTP_CODE_NOT_FOUND, "text/plain", "nicht gefunden");
    });
}

// Die Schnittstelle bedient die eigene Oberflaeche auf demselben Geraet (same-origin).
// CORS-Kopfzeilen gab es nur als Erbe der Basis-Firmware; sie erlaubten jeder fremden
// Seite im Browser des Nutzers, das Geraet anzusprechen. Ersatzlos entfernt (E4).
void geschuetzt(Webserver* webserver, const char* uri, HTTPMethod methode,
                void (*handler)(Webserver*)) {
    webserver->raw().on(uri, methode, [webserver, handler]() {
        if (!requireBearerToken(webserver)) {
            return;
        }
        handler(webserver);
    });
}

/**
 * @brief Validate bearer token from Authorization header
 * @param webserver Pointer to the Webserver instance
 *
 * @return true if token is valid false otherwise
 */
static auto validateBearerToken(Webserver* webserver) -> bool {
    const char* storedToken = configManager.getApiToken();

    // Der Passwortschutz ist OPTIONAL (Nutzer-Vorgabe): Ist kein Passwort gesetzt,
    // ist das Geraet frei bedienbar -- so auch der Werkszustand. Erst ein ueber die
    // Oberflaeche gesetztes Passwort schaltet die Pruefung scharf. (Frueher hiess
    // "kein Token gespeichert" das Gegenteil: alles abgelehnt -- eine Erstinstallation
    // ohne mitgelieferte Konfigurationsdatei war damit komplett ausgesperrt.)
    if (storedToken == nullptr || storedToken[0] == '\0') {
        return true;
    }

    // Ohne String-Kopien: Die Pruefung laeuft bei aktivem Schutz fuer jeden API-Aufruf
    // (die Oberflaeche fragt alle 5 s den Status ab) -- drei Heap-Objekte je Anfrage
    // waeren unnoetiger Fragmentierungs-Verkehr auf dem knappen Heap.
    const String& authHeader = webserver->raw().header("Authorization");
    if (strncmp(authHeader.c_str(), "Bearer ", BEARER_LEN) != 0) {
        return false;
    }

    return strcmp(authHeader.c_str() + BEARER_LEN, storedToken) == 0;
}

/**
 * @brief Enforce bearer token check and send 401 response if invalid
 * @param webserver Pointer to the Webserver instance
 *
 * @return true if token is valid false otherwise
 */
auto requireBearerToken(Webserver* webserver) -> bool {
    if (validateBearerToken(webserver)) {
        return true;
    }

    sendeFehlerStatus(webserver, HTTP_CODE_UNAUTHORIZED, "Passwort fehlt oder ist falsch");

    Logger::warn(("Unauthorized request from " + webserver->raw().client().remoteIP().toString()).c_str(), "API");

    return false;
}

/**
 * @brief Passwort pruefen (die Pruefung selbst macht geschuetzt())
 */
void handleTokenCheck(Webserver* webserver) {
    sendeStatus(webserver, HTTP_CODE_OK, "ok", "Passwort ist gueltig");
}

/**
 * @brief Neues Passwort speichern
 */
void handleTokenSave(Webserver* webserver) {
    JsonDocument ddoc;
    if (!leseJsonKoerper(webserver, ddoc, true)) {
        return;
    }

    // Der Passwortschutz ist OPTIONAL (Nutzer-Vorgabe): Das Feld muss vorhanden sein,
    // darf aber leer sein -- ein leeres Passwort hebt den Schutz auf, das Geraet ist
    // dann wieder frei bedienbar (wie im Werkszustand).
    if (!ddoc["token"].is<const char*>()) {
        sendeFehlerStatus(webserver, HTTP_CODE_BAD_REQUEST, "Passwort-Feld fehlt");
        return;
    }

    const char* newToken = ddoc["token"] | "";
    const bool schutzAus = (strlen(newToken) == 0);

    configManager.setApiToken(newToken);
    if (!configManager.save()) {
        sendeFehlerStatus(webserver, HTTP_CODE_INTERNAL_ERROR, "Speichern fehlgeschlagen");
        return;
    }

    sendeStatus(webserver, HTTP_CODE_OK, "ok",
                schutzAus ? "Passwortschutz aufgehoben" : "Passwort gespeichert");
    Logger::info(schutzAus ? "API password protection disabled" : "API password updated", "API");
}

/**
 * @brief OTA status endpoint
 */
void handleOtaStatus(Webserver* webserver) {
    JsonDocument doc;
    doc["inProgress"] = otaInProgress;
    doc["bytesWritten"] = otaSize;
    doc["totalBytes"] = otaTotal;
    doc["error"] = otaError;
    doc["message"] = otaStatus;
    sendeJson(webserver, HTTP_CODE_OK, doc);
}

/**
 * @brief OTA cancel endpoint
 */
void handleOtaCancel(Webserver* webserver) {
    otaCancelRequested = true;
    otaStatus = "Abbruch angefordert";
    sendeStatus(webserver, HTTP_CODE_OK, "cancelling", "Abbruch angefordert");
}

/**
 * @brief Reboot endpoint
 */
void handleReboot(Webserver* webserver) {
    int constexpr rebootDelayMs = 1000;

    JsonDocument doc;
    doc["status"] = "rebooting";
    sendeJson(webserver, HTTP_CODE_OK, doc);

    delay(rebootDelayMs);
    ESP.restart();  // NOLINT(readability-static-accessed-through-instance)
}

/**
 * @brief Manual NTP sync trigger endpoint
 */
void handleNtpSync(Webserver* webserver) {
    if (ntpClient == nullptr) {
        sendeFehlerStatus(webserver, HTTP_CODE_INTERNAL_ERROR, "Zeitdienst nicht gestartet");
        return;
    }

    // Nur anstossen, nicht abwarten: syncNow() blockiert bis zu 5 Sekunden, in denen
    // weder das Display noch die Abrufe noch der Webserver etwas tun. loop() fuehrt den
    // Versuch zu Ende, das Ergebnis holt die Seite von /ntp/status (N25).
    ntpClient->syncAnstossen();

    JsonDocument doc;
    doc["status"] = "gestartet";
    doc["lastStatus"] = ntpClient->lastStatus();
    doc["lastSyncTime"] = ntpClient->lastSyncTime();
    sendeJson(webserver, HTTP_CODE_OK, doc);
}

/**
 * @brief Return NTP status
 */
void handleNtpStatus(Webserver* webserver) {
    if (ntpClient == nullptr) {
        sendeFehlerStatus(webserver, HTTP_CODE_INTERNAL_ERROR, "Zeitdienst nicht gestartet");
        return;
    }

    JsonDocument doc;
    doc["lastOk"] = ntpClient->lastSyncOk();
    doc["lastStatus"] = ntpClient->lastStatus();
    doc["lastSyncTime"] = ntpClient->lastSyncTime();
    sendeJson(webserver, HTTP_CODE_OK, doc);
}

/**
 * @brief Get NTP configuration
 */
void handleNtpConfigGet(Webserver* webserver) {
    JsonDocument doc;
    doc["ntp_server"] = configManager.getNtpServer();
    sendeJson(webserver, HTTP_CODE_OK, doc);
}

/**
 * @brief Set NTP configuration
 */
void handleNtpConfigSet(Webserver* webserver) {
    JsonDocument ddoc;
    if (!leseJsonKoerper(webserver, ddoc, true)) {
        return;
    }

    const char* server = ddoc["ntp_server"] | "";
    if (strlen(server) == 0) {
        sendeFehlerStatus(webserver, HTTP_CODE_BAD_REQUEST, "Zeitserver fehlt");
        return;
    }

    configManager.setNtpServer(server);
    if (!configManager.save()) {
        sendeFehlerStatus(webserver, HTTP_CODE_INTERNAL_ERROR, "Speichern fehlgeschlagen");
        return;
    }

    // Sync nur anstossen, nicht abwarten: syncNow() wuerde bis zu 5 s blockieren
    // (Display, Abrufe und Webserver stuenden still), obwohl loop() den Versuch
    // ohnehin zu Ende fuehrt und /ntp/status das Ergebnis zeigt.
    if (ntpClient != nullptr) {
        ntpClient->syncAnstossen();
    }

    JsonDocument doc;
    doc["status"] = "ok";
    doc["ntp_server"] = server;
    sendeJson(webserver, HTTP_CODE_OK, doc);
}

/**
 * @brief Get display rotation configuration
 */
void handleDisplayRotationGet(Webserver* webserver) {
    JsonDocument doc;
    doc["rotation"] = configManager.getLCDRotationSafe();
    sendeJson(webserver, HTTP_CODE_OK, doc);
}

/**
 * @brief Set display rotation configuration
 */
void handleDisplayRotationSet(Webserver* webserver) {
    JsonDocument ddoc;
    if (!leseJsonKoerper(webserver, ddoc, true)) {
        return;
    }

    if (!ddoc["rotation"].is<int>()) {
        sendeFehlerStatus(webserver, HTTP_CODE_BAD_REQUEST, "Drehung fehlt oder ist keine Zahl");
        return;
    }

    const int rotation = ddoc["rotation"].as<int>();
    const int rotationMin = 0;
    const int rotationMax = 7;
    if (rotation < rotationMin || rotation > rotationMax) {
        sendeFehlerStatus(webserver, HTTP_CODE_BAD_REQUEST, "Drehung nur 0 bis 7");
        return;
    }

    auto newRotation = static_cast<uint8_t>(rotation);
    configManager.setLCDRotation(newRotation);

    // Die Kachelanzeige merkt die Drehung an der Fremdzeichnung und malt neu (A2).
    DisplayManager::setRotation(newRotation);

    if (!configManager.save()) {
        sendeFehlerStatus(webserver, HTTP_CODE_INTERNAL_ERROR, "Speichern fehlgeschlagen");
        return;
    }

    JsonDocument doc;
    doc["status"] = "ok";
    doc["rotation"] = newRotation;
    sendeJson(webserver, HTTP_CODE_OK, doc);

    Logger::info(("Display rotation updated to " + String(newRotation)).c_str(), "API");
}

/**
 * @brief Handle OTA upload
 * @param webserver Pointer to the Webserver instance
 * @param mode Update mode U_FLASH U_FS
 *
 * @return void
 */
void handleOtaUpload(Webserver* webserver, int mode) {
    HTTPUpload& upload = webserver->raw().upload();

    // requireBearerToken sendet die 401-Antwort selbst -- der frueher hier von Hand
    // nachgebaute JSON-Block war eine driftanfaellige Kopie davon.
    if (upload.status == UPLOAD_FILE_START && !requireBearerToken(webserver)) {
        otaError = true;
        otaStatus = "Nicht angemeldet";
        otaZugangAbgelehnt = true;
        return;
    }

    switch (upload.status) {
        case UPLOAD_FILE_START:
            otaHandleStart(webserver, upload, mode);
            break;
        case UPLOAD_FILE_WRITE:
            otaHandleWrite(upload, mode);
            break;
        case UPLOAD_FILE_END:
            otaHandleEnd(upload, mode);
            break;
        case UPLOAD_FILE_ABORTED:
            otaHandleAborted(upload, mode);
            break;
        default:
            break;
    }
}

/**
 * @brief Handle OTA finished
 * @param webserver Pointer to the Webserver instance
 *
 * @return void
 */
void handleOtaFinished(Webserver* webserver) {
    // Hat der Upload-Handler die Anfrage schon mit 401 beantwortet, ist hier Schluss --
    // ohne zweite Antwort auf dieselbe Anfrage.
    if (otaZugangAbgelehnt) {
        otaZugangAbgelehnt = false;
        otaInProgress = false;
        return;
    }
    // Kam gar kein Upload an (POST ohne Datei), hat noch niemand geprueft.
    if (!requireBearerToken(webserver)) {
        return;
    }

    int constexpr rebootDelayMs = 5000;

    JsonDocument doc;
    // "ok" oder "error" -- vorher stand hier ein englischer Satz ("Upload successful"),
    // den die Oberflaeche nicht auswerten konnte; sie zeigt weiterhin nur message an (E9).
    doc["status"] = otaError ? "error" : "ok";
    doc["message"] = otaStatus;

    otaInProgress = false;
    otaCancelRequested = false;

    sendeJson(webserver, HTTP_CODE_OK, doc);

    if (!otaError) {
        delay(rebootDelayMs);
        ESP.restart();  // NOLINT(readability-static-accessed-through-instance)
    }
}

/**
 * @brief Handle WiFi scan
 */
void handleWifiScan(Webserver* webserver) {
    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();

    if (wifiManager != nullptr) {
        WiFiManager::scanNetworks(networks);
    }

    // Die Seite erwartet das nackte Feld, nicht das Dokument darum herum.
    String out;
    serializeJson(doc["networks"], out);
    webserver->raw().send(HTTP_CODE_OK, "application/json", out);
}

/**
 * @brief Handle WiFi connect request
 */
void handleWifiConnect(Webserver* webserver) {
    JsonDocument ddoc;
    if (!leseJsonKoerper(webserver, ddoc, true)) {
        return;
    }

    const char* ssid = ddoc["ssid"] | "";
    const char* password = ddoc["password"] | "";
    if (strlen(ssid) == 0) {
        sendeFehlerStatus(webserver, HTTP_CODE_BAD_REQUEST, "WLAN-Name fehlt");
        return;
    }

    bool connectOk = false;
    if (wifiManager != nullptr) {
        connectOk = wifiManager->connectToNetwork(ssid, password, WIFI_CONNECT_TIMEOUT_MS);
    }

    JsonDocument resp;
    resp["status"] = connectOk ? "connected" : "error";
    resp["ssid"] = ssid;
    if (connectOk) {
        resp["ip"] = wifiManager->getIP().toString();
        configManager.setWiFi(ssid, password);
        configManager.save();
    } else {
        resp["message"] = "Verbindung fehlgeschlagen";
    }
    sendeJson(webserver, HTTP_CODE_OK, resp);
}

/**
 * @brief WiFi status
 */
void handleWifiStatus(Webserver* webserver) {
    const bool connected = (wifiManager != nullptr) && WiFiManager::isConnected();

    JsonDocument resp;
    resp["connected"] = connected;
    resp["ssid"] = connected ? WiFiManager::getConnectedSSID() : "";
    resp["ip"] = connected ? wifiManager->getIP().toString() : "";
    sendeJson(webserver, HTTP_CODE_OK, resp);
}

/**
 * @brief Handle OTA start
 *
 * @param upload Reference to the HTTPUpload object
 * @param mode Update mode U_FLASH or U_FS
 *
 * @return void
 */
static void otaHandleStart(Webserver* webserver, HTTPUpload& upload, int mode) {
    Logger::info((String("OTA start: ") + upload.filename).c_str(), "API::OTA");

    otaError = false;
    otaSize = 0;
    otaStatus = "";
    otaInProgress = true;
    otaCancelRequested = false;
    otaTotal = static_cast<size_t>(upload.contentLength);

    DisplayManager::clearScreen();
    DisplayManager::meldung("Update laeuft", mode == U_FS ? "Oberflaeche" : "Firmware", 0.0F);

    int constexpr security_space = 0x1000;
    u_int constexpr bin_mask = 0xFFFFF000;

    FSInfo fs_info;
    LittleFS.info(fs_info);
    size_t fsSize = fs_info.totalBytes;
    size_t maxSketchSpace =
        (ESP.getFreeSketchSpace() - security_space) &  // NOLINT(readability-static-accessed-through-instance)
        bin_mask;
    size_t place = (mode == U_FS) ? fsSize : maxSketchSpace;

    if (mode == U_FS) {
        // Die Partition wird gleich ueberschrieben: Dateisystem aushaengen, wie es der
        // Update-Server des Frameworks tut (close_all_fs). Ohne das bliebe ein Mount mit
        // veralteten Metadaten stehen -- und LittleFS.begin() ist bei gemountetem
        // Dateisystem ein No-op, das "Neu-Mounten" hinterher faende nie statt.
        close_all_fs();
    }

    if (!Update.begin(place, mode)) {
        otaError = true;
        otaStatus = Update.getErrorString();
        Logger::error((String("Update.begin failed: ") + otaStatus).c_str(), "API::OTA");
        if (mode == U_FS) {
            LittleFS.begin();  // Oberflaeche soll weiterlaufen
        }
        return;
    }

    // Firmware nur mit Pruefsumme: Der Updater prueft sonst nur das erste Byte und
    // aktiviert auch ein unvollstaendiges Abbild -- ohne Rollback ein Brick ohne Serial.
    // Fuer das Dateisystem ist die Summe willkommen, aber nicht Pflicht: Ein kaputtes
    // Dateisystem schaltet /legacyupdate frei, ein kaputtes Programm nicht.
    const String& md5 = webserver->raw().header("X-Abbild-MD5");
    if (md5.length() == 32) {
        Update.setMD5(md5.c_str());
    } else if (mode == U_FLASH) {
        Update.end();
        otaError = true;
        otaStatus = "Pruefsumme fehlt -- Update-Seite neu laden oder curl mit X-Abbild-MD5";
        Logger::error(otaStatus.c_str(), "API::OTA");
    }
}

/**
 * @brief Handle OTA write
 *
 * @param upload Reference to the HTTPUpload object
 *
 * @return void
 */
static void otaHandleWrite(HTTPUpload& upload, int mode) {
    // Erster Brocken: Sagt die Datei selbst, dass sie die falsche Sorte ist, wird sie
    // abgewiesen, BEVOR ein Byte in den Flash geht. Warum das Geraet das selbst pruefen
    // muss, obwohl die Oberflaeche es schon tut, steht in abbild_art.h.
    if (!otaError && otaSize == 0) {
        const AbbildArt erwartet = (mode == U_FS) ? ABBILD_DATEISYSTEM : ABBILD_FIRMWARE;
        const AbbildArt erkannt = erkenneAbbild(upload.buf, upload.currentSize);
        if (erkannt != erwartet) {
            Update.end();
            if (mode == U_FS) {
                LittleFS.begin();  // nichts geschrieben, altes Dateisystem wieder einhaengen
            }
            otaError = true;
            otaStatus = abbildFehlertext(erkannt, erwartet);
            otaInProgress = false;
            Logger::error((String("OTA abgelehnt: ") + otaStatus).c_str(), "API::OTA");

            // Die Kachelanzeige holt die Fremdzeichnung von selbst ab (A2).
            DisplayManager::meldung("Update abgelehnt", "", -1.0F);
            return;
        }
    }

    if (!otaError) {
        if (otaCancelRequested) {
            Update.end();
            if (mode == U_FS) {
                LittleFS.begin();  // so weit noch moeglich; sonst hilft der Neustart (Notfallroute)
            }
            otaError = true;
            otaStatus = "Update abgebrochen";
            otaInProgress = false;
            Logger::warn("OTA canceled by user", "API::OTA");

            DisplayManager::meldung("Update abgebrochen", "", -1.0F);
            return;
        }

        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            otaError = true;
            otaStatus = Update.getErrorString();
            Logger::error((String("Write failed: ") + otaStatus).c_str(), "API::OTA");
        }

        otaSize += upload.currentSize;

        float progress = 0.0F;
        if (otaTotal > 0) {
            progress = static_cast<float>(otaSize) / static_cast<float>(otaTotal);
        }

        DisplayManager::meldung(nullptr, nullptr, progress);
    }
}

/**
 * @brief Handle OTA end
 *
 * @param upload Reference to the HTTPUpload object
 * @param mode Update mode U_FLASH or U_FS
 *
 * @return void
 */
static void otaHandleEnd(HTTPUpload& /*upload*/, int mode) {
    if (!otaError) {
        if (Update.end(true)) {
            otaStatus = String("Update OK (") + String(otaSize) + " Byte)";
            if (mode == U_FS) {
                Logger::info("OTA FS update complete, mounting file system...", "API::OTA");
                if (LittleFS.begin()) {
                    // Das neue Abbild bringt keine Konfiguration mit. Was im RAM steht, ist
                    // der gueltige Stand -- zurueckschreiben, bevor neu gestartet wird. Damit
                    // kostet ein Oberflaechen-Update keine eingerichteten Werte mehr.
                    const bool slotsOk = SlotApi::konfigurationSichern();
                    const bool cfgOk = configManager.save();
                    const bool uebernommen = slotsOk && cfgOk;
                    otaStatus += uebernommen ? ", Einrichtung uebernommen" : ", Einrichtung NICHT uebernommen";
                    Logger::info(uebernommen ? "Konfiguration ins neue Dateisystem uebernommen"
                                             : "Konfiguration NICHT uebernommen",
                                 "API::OTA");
                } else {
                    otaStatus += ", neues Dateisystem nicht mountbar";
                    Logger::error("Neues Dateisystem laesst sich nicht mounten", "API::OTA");
                }
            }
            Logger::info(otaStatus.c_str(), "API::OTA");

            DisplayManager::meldung("Update fertig", "Neustart ...", 1.0F);
        } else {
            otaError = true;
            otaStatus = Update.getErrorString();
        }
    }
}

/**
 * @brief Handle OTA aborted
 *
 * @param upload Reference to the HTTPUpload object
 *
 * @return void
 */
static void otaHandleAborted(HTTPUpload& /*upload*/, int mode) {
    Update.end();
    if (mode == U_FS) {
        LittleFS.begin();  // so weit noch moeglich; sonst hilft der Neustart (Notfallroute)
    }
    otaError = true;
    otaStatus = "Update abgebrochen";
    otaInProgress = false;
    otaCancelRequested = false;

    DisplayManager::meldung("Update abgebrochen", "", -1.0F);
}

/**
 * @brief Get recent logs
 */
void handleLogsGet(Webserver* webserver) {
    JsonDocument doc;
    JsonArray logsArray = doc["logs"].to<JsonArray>();

    const size_t count = Logger::getLogCount();
    for (size_t i = 0; i < count; i++) {
        const char* entry = Logger::getLogEntry(i);
        if (entry != nullptr) {
            logsArray.add(entry);
        }
    }
    doc["count"] = count;
    sendeJson(webserver, HTTP_CODE_OK, doc);
}

/**
 * @brief Download logs as a text file
 */
void handleLogsDownload(Webserver* webserver) {
    String logs = Logger::getLogsAsString();
    webserver->raw().sendHeader("Content-Disposition", "attachment; filename=\"logs.log\"");
    webserver->raw().send(HTTP_CODE_OK, "text/plain", logs);
}

/**
 * @brief Clear log buffer
 */
void handleLogsClear(Webserver* webserver) {
    Logger::clearLogs();
    sendeStatus(webserver, HTTP_CODE_OK, "ok", "Protokoll geleert");
}
