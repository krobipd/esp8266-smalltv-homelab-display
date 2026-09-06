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
#include <LittleFS.h>
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <ESP8266HTTPUpdateServer.h>

#include <Logger.h>
#include "project_version.h"
#include "config/ConfigManager.h"
#include "hardware/Pins.h"
#include "wireless/WiFiManager.h"
#include "slots/SlotApi.h"
#include "slots/SlotDisplay.h"
#include "slots/SlotRuntime.h"
#include "slots/SlotStore.h"
#include "display/DisplayManager.h"
#include "web/Webserver.h"
#include "web/Api.h"
#include "ntp/NTPClient.h"
#include "boot/RescueMode.h"

ConfigManager configManager;
WiFiManager* wifiManager = nullptr;
ESP8266HTTPUpdateServer httpUpdater;
static constexpr const char* KV_SALT_STR = "GeekMagicOpenFirmwareIsAwesome";

static constexpr uint32_t SERIAL_BAUD_RATE = 115200;
static constexpr uint32_t BOOT_DELAY_MS = 200;
static constexpr int LOADING_BAR_TEXT_X = 50;
static constexpr int LOADING_BAR_TEXT_Y = 80;
static constexpr int LOADING_BAR_Y = 110;
static constexpr int LOADING_DELAY_MS = 1000;

Webserver* webserver = nullptr;
NTPClient* ntpClient = nullptr;

/**
 * @brief Check whether LittleFS contains at least one entry
 *
 * @return true if filesystem root has any file/dir entry
 */
static auto littleFsHasEntries() -> bool {
    Dir dir = LittleFS.openDir("/");
    return dir.next();
}

/**
 * @brief Initializes the system
 *
 */
/// Slot-Konfiguration: bewusst statisch angelegt, damit sie den Heap nicht
/// fragmentiert -- der ist auf diesem Chip der knappe Posten.
static Config g_slotConfig;

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(BOOT_DELAY_MS);
    Serial.println("");
    Logger::info(("GeekMagic Open Firmware " + String(PROJECT_VER_STR)).c_str());

    constexpr int TOTAL_STEPS = 5;
    int step = 0;
    const bool littleFsMounted = LittleFS.begin();
    bool littleFsReadyForStatic = littleFsMounted;

    if (!littleFsMounted) {
        Logger::error("Failed to mount LittleFS");
        Logger::warn("LittleFS unavailable, static web UI disabled", "Global");
    } else if (!littleFsHasEntries()) {
        littleFsReadyForStatic = false;
        Logger::warn("LittleFS mounted but empty, static web UI disabled", "Global");
    }

    SecureStorage::setSalt(KV_SALT_STR);

    if (configManager.secure.begin()) {
        Logger::info("SecureStorage initialized successfully", "ConfigManager");
    }

    if (configManager.load()) {
        Logger::info("Configuration loaded successfully");
    }

    if (RescueMode::checkBootLoop()) {
        RescueMode::run();
        EspClass::wdtEnable(WDTO_2S);

        return;
    }

    step += 2;

    DisplayManager::begin();

    DisplayManager::drawLoadingBar((float)step / TOTAL_STEPS, LOADING_BAR_Y);

    step++;

    DisplayManager::drawTextWrapped(LOADING_BAR_TEXT_X, LOADING_BAR_TEXT_Y, "Starting...", 2, LCD_WHITE, LCD_BLACK,
                                    true);
    DisplayManager::drawLoadingBar((float)step / TOTAL_STEPS, LOADING_BAR_Y);
    step++;

    wifiManager = new WiFiManager(configManager.getSSID(), configManager.getPassword(), AP_SSID, AP_PASSWORD);
    wifiManager->begin();

    ntpClient = new NTPClient();
    ntpClient->begin();

    DisplayManager::drawLoadingBar((float)step / TOTAL_STEPS, LOADING_BAR_Y);

    step++;

    webserver = new Webserver();
    webserver->begin();

    DisplayManager::drawLoadingBar((float)step / TOTAL_STEPS, LOADING_BAR_Y);

    registerApiEndpoints(webserver);

    {
        char slotFehler[96] = {0};
        if (!SlotStore::load(g_slotConfig, slotFehler, sizeof(slotFehler))) {
            Logger::error(slotFehler, "Slots");
            // Die beschaedigte Datei NICHT ueberschreiben, sondern beiseitelegen --
            // wer mag, kann sie sich per Update-Zugang noch ansehen. Ohne das Umbenennen
            // wuerde der erste Speichervorgang sie endgueltig vernichten.
            SlotStore::beiseitelegen();
            // Im RAM gelten ab jetzt die Standardwerte. Die genullte Config waere
            // Helligkeit 0 -- ein schwarzes Display, das wie gebrickt aussieht,
            // obwohl nur eine Datei kaputt ist.
            SlotStore::defaults(g_slotConfig);
            SlotApi::setLadeWarnung(
                "Die gespeicherte Konfiguration war unlesbar und wurde beiseitegelegt "
                "(/slots.json.defekt). Es gelten Standardwerte.");
        }
    }
    SlotApi::begin(&g_slotConfig);
    SlotRuntime::begin(&g_slotConfig);
    SlotDisplay::begin(&g_slotConfig);
    SlotApi::registerRoutes(webserver);

    if (!littleFsReadyForStatic) {
        httpUpdater.setup(&webserver->raw(), "/legacyupdate");
        Logger::warn("Enabled legacy OTA route because LittleFS is unavailable or empty", "Global");
    } else {
        webserver->serveStaticC("/", "/web/index.html", "text/html");
        webserver->registerGenericStaticFallback("/web", true);
    }

    DisplayManager::drawLoadingBar(1.0F, LOADING_BAR_Y);

    delay(LOADING_DELAY_MS);

    DisplayManager::drawStartup(wifiManager->getIP().toString());


    // Software-Watchdog des Cores einschalten. Seine Zeit ist fest vorgegeben; das
    // Argument wird vom Core ignoriert (Esp.cpp: "(void) timeout_ms").
    EspClass::wdtEnable(WDTO_2S);
}

void loop() {
    if (RescueMode::isActive()) {
        RescueMode::loop();
        return;
    }

    static bool bootStableMarked = false;
    if (!bootStableMarked && millis() >= BOOT_STABLE_MS) {
        RescueMode::markBootStable();
        bootStableMarked = true;
    }

    if (webserver != nullptr) {
        webserver->handleClient();
    }

    if (wifiManager != nullptr) {
        wifiManager->loop();
    }

    if (ntpClient != nullptr) {
        ntpClient->loop();
    }

    // Beide im Zehntelsekunden-Takt statt in jeder Schleifeniteration: Die Anzeige kann
    // sich hoechstens im Sekundenraster aendern, und der Abrufplaner muesste sonst
    // zigtausendmal je Sekunde alle Slots durchgehen, nur um festzustellen,
    // dass nichts faellig ist.
    // Adresse fuer die Leerseite -- alle 2 s reicht, sie aendert sich praktisch nie.
    static uint32_t letzteNetzInfo = 0;
    if (wifiManager != nullptr && elapsed(millis(), letzteNetzInfo, 2000U)) {
        letzteNetzInfo = millis();
        SlotDisplay::netzInfo(wifiManager->getIP().toString().c_str(), wifiManager->isApMode());
    }

    static uint32_t letzterSlotTakt = 0;
    if (elapsed(millis(), letzterSlotTakt, 100U)) {
        letzterSlotTakt = millis();
        if (wifiManager != nullptr && WiFiManager::isConnected() && !wifiManager->isApMode()) {
            SlotRuntime::update();
        }
        SlotDisplay::update();
    }


    // Kein Heartbeat mehr im Protokoll: Er schrieb alle zehn Sekunden den freien
    // Speicher hinein und verdraengte damit im Ringpuffer alles, was wirklich passiert
    // war. Dieselben Zahlen stehen jetzt im Status (/api/v1/slots/status, Feld
    // "geraet") und damit auf der Werte-Seite (E11).

    EspClass::wdtFeed();  // kick watchdog
}
