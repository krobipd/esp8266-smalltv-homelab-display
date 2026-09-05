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

#include "ntp/NTPClient.h"
#include <ctime>
#include <array>
#include <lwip/apps/sntp.h>
#include <coredecls.h>  // settimeofday_cb
#include <Logger.h>
#include <wireless/WiFiManager.h>
#include "config/ConfigManager.h"
#include "smalltv_util.h"  // elapsed(): wrap-sichere Zeitvergleiche, eine Autoritaet

extern ConfigManager configManager;

static constexpr const char* TAG = "NTPClient";

/// Wird vom Core gesetzt, sobald SNTP die Uhr tatsaechlich gestellt hat (settimeofday).
/// Das ist das einzige belastbare Erfolgskriterium -- "die Uhr steht auf einem
/// plausiblen Datum" war nach dem ersten Abgleich immer wahr.
static volatile bool g_zeitGesetzt = false;

/**
 * @brief Default NTP server
 */
static constexpr const char* DEFAULT_NTP_SERVER1 = "pool.ntp.org";

/**
 * @brief Zeitzone des Geraets: Mitteleuropa (MEZ/MESZ mit automatischem Wechsel).
 *
 * Fest verdrahtet und bewusst KEINE Einstellung: Das Geraet haengt im Heimnetz seines
 * Besitzers. Ohne diese Regel lief die Uhr in UTC -- der Nachtmodus "22 bis 7 Uhr"
 * dimmte dann real von 23 bis 8 (Winter) bzw. 0 bis 9 Uhr (Sommer).
 */
static constexpr const char* TZ_LOKAL = "CET-1CEST,M3.5.0,M10.5.0/3";

/// Bis zum ERSTEN Erfolg wird im Minutentakt neu versucht -- ein Geraet, das nach dem
/// Boot 6 h ohne Uhrzeit laeuft, haette die ganze Nacht keinen Nachtmodus.
static constexpr uint32_t ERSTSYNC_INTERVALL_S = 60;

/// Begrenzte Wartezeit des von Hand angestossenen Syncs (Web-API).
static constexpr unsigned long SYNCNOW_WARTE_MS = 5000UL;

/**
 * @brief miliseconds per second
 */
static constexpr unsigned long MILLIS_PER_SECOND = 1000UL;

/**
 * @brief Default NTP timeout in milliseconds
 */
static constexpr unsigned long DEFAULT_NTP_TIMEOUT_MS = 10000UL;

/**
 * @brief Poll delay in milliseconds
 */
static constexpr unsigned long POLL_DELAY_MS = 200UL;

/**
 * @brief Reasonable epoch time to 2020/09/13
 */
static constexpr time_t REASONABLE_EPOCH = 1600000000UL;

/**
 * @brief Status buffer size
 */
static constexpr size_t STATUS_BUFFER_SIZE = 64U;

/**
 * @brief Year base for struct tm
 */
static constexpr int TM_YEAR_BASE = 1900;

NTPClient::NTPClient() = default;

/**
 * @brief Initialize the NTP client
 * @param syncIntervalSeconds Sync interval in seconds (default: 6 hours)
 * @param maxRetries Maximum number of retries on failure (default: 3)
 *
 * @return void
 */
void NTPClient::begin(uint32_t syncIntervalSeconds, uint8_t maxRetries) {
    _syncIntervalSeconds = syncIntervalSeconds;
    _maxRetries = maxRetries;
    _lastStatus = "noch nicht synchronisiert";

    // Zeitzone EINMAL setzen -- time() bleibt UTC, localtime() liefert Ortszeit.
    setTZ(TZ_LOKAL);
    settimeofday_cb([]() { g_zeitGesetzt = true; });

    Logger::info("NTP client initialized", TAG);
}

/**
 * @brief Startet einen Sync-Versuch: SNTP anstossen, nicht warten.
 */
void NTPClient::starteSync() {
    g_zeitGesetzt = false;
    // configTime(tz, ...) statt configTime(0, 0, ...): Die Offset-Fassung wuerde die
    // in begin() gesetzte Zeitzonenregel wieder mit "UTC+0" ueberschreiben.
    const char* srv = configManager.getNtpServer();
    if (srv != nullptr && srv[0] != '\0') {
        configTime(TZ_LOKAL, srv, DEFAULT_NTP_SERVER1);  // eigener Server + Fallback
    } else {
        configTime(TZ_LOKAL, DEFAULT_NTP_SERVER1);
    }

    _syncLaeuft = true;
    _jeVersucht = true;
    _syncStartMs = millis();
    _versuch++;
    _lastStatus = "Synchronisierung laeuft";
}

/**
 * @brief Abschluss eines Sync-Laufs verbuchen (Erfolg oder endgueltiger Fehlschlag).
 */
void NTPClient::syncAbschliessen(bool ok) {
    _syncLaeuft = false;
    _versuch = 0;
    _lastSyncAttemptMs = millis();
    sntp_stop();

    if (ok) {
        const time_t now = time(nullptr);
        _lastSync = now;
        _lastOk = true;
        std::array<char, STATUS_BUFFER_SIZE> buf;
        struct tm* tm_info = localtime(&now);
        snprintf(buf.data(), buf.size(), "Synchronisiert: %04d-%02d-%02d %02d:%02d:%02d",
                 tm_info->tm_year + TM_YEAR_BASE, tm_info->tm_mon + 1, tm_info->tm_mday,
                 tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
        _lastStatus = buf.data();
        Logger::info(_lastStatus.c_str(), TAG);
    } else {
        _lastOk = false;
        _lastStatus = "Synchronisierung fehlgeschlagen";
        Logger::error("NTP sync failed after retries", TAG);
    }
}

/**
 * @brief Prueft einen laufenden Sync. true = Lauf ist beendet (Erfolg oder Fehlschlag).
 */
auto NTPClient::ergebnisPruefen() -> bool {
    // Erfolg heisst: SNTP hat die Uhr GESTELLT (settimeofday-Rueckruf) -- nicht "die
    // Uhr steht auf einem plausiblen Datum". Letzteres war nach dem ersten Abgleich
    // immer wahr; jeder weitere war ein Scheinerfolg, und sntp_stop() wuergte die
    // Anfrage ab, bevor eine Antwort kam: Die Uhr wurde nach dem Boot nie nachgestellt.
    if (g_zeitGesetzt && time(nullptr) > REASONABLE_EPOCH) {
        syncAbschliessen(true);
        return true;
    }
    if (!elapsed(millis(), _syncStartMs, DEFAULT_NTP_TIMEOUT_MS)) {
        return false;  // weiter warten -- ohne zu blockieren
    }
    if (_versuch < _maxRetries) {
        std::array<char, STATUS_BUFFER_SIZE> buf;
        snprintf(buf.data(), buf.size(), "NTP sync attempt %d failed", (int)_versuch);
        Logger::warn(buf.data(), TAG);
        starteSync();  // naechster Versuch, Zaehler laeuft weiter
        return false;
    }
    syncAbschliessen(false);
    return true;
}

/**
 * @brief Main loop to handle periodic NTP sync
 *
 * Blockiert nie: stoesst an und prueft nur. Delta-Arithmetik statt absoluter
 * Vergleiche -- ueberlebt den 49,7-Tage-Ueberlauf von millis().
 *
 * @return void
 */
void NTPClient::loop() {
    if (!WiFiManager::isConnected()) {
        if (_syncLaeuft) {
            // Ohne Netz kann der Lauf nicht enden -- sauber abbrechen statt haengen.
            syncAbschliessen(false);
        }
        // Nur beim Zustandswechsel setzen: loop() laeuft zigtausendmal je Sekunde,
        // und eine String-Zuweisung in jedem Durchlauf waere reine CPU-Verschwendung.
        if (!_offlineGemeldet) {
            _lastStatus = "kein Netzwerk";
            _offlineGemeldet = true;
        }
        return;
    }
    _offlineGemeldet = false;

    if (_syncLaeuft) {
        ergebnisPruefen();
        return;
    }

    // Bis zum ersten Erfolg im Minutentakt, danach im eingestellten Intervall.
    // Der allererste Versuch startet sofort mit der ersten WLAN-Verbindung.
    const uint32_t intervallS = (_lastSync == 0) ? ERSTSYNC_INTERVALL_S : _syncIntervalSeconds;
    if (!_jeVersucht
        || elapsed(millis(), _lastSyncAttemptMs, intervallS * MILLIS_PER_SECOND)) {
        _versuch = 0;
        starteSync();
    }
}

/**
 * @brief Sync anstossen ohne zu warten -- das Ergebnis zeigt /ntp/status.
 */
void NTPClient::syncAnstossen() {
    if (!WiFiManager::isConnected() || _syncLaeuft) {
        return;
    }
    _versuch = 0;
    starteSync();
}

/**
 * @brief Trigger an immediate NTP sync
 *
 * Wartet hoechstens SYNCNOW_WARTE_MS auf das Ergebnis (der Aufrufer will eine Antwort
 * sehen); dauert es laenger, uebernimmt loop() den laufenden Versuch.
 *
 * @return true if sync was successful false otherwise
 */
auto NTPClient::syncNow() -> bool {
    if (!WiFiManager::isConnected()) {
        _lastStatus = "kein Netzwerk";
        _lastOk = false;

        Logger::warn("Manual NTP sync requested but network is unavailable", TAG);

        return false;
    }

    if (!_syncLaeuft) {
        _versuch = 0;
        starteSync();
    }

    // Die Warteschleife delegiert an ergebnisPruefen() -- "Sync ist fertig" hat
    // damit genau EINE Autoritaet (inklusive Wiederholversuchen und Abschluss).
    const unsigned long start = millis();
    while (!elapsed(millis(), start, SYNCNOW_WARTE_MS)) {
        if (ergebnisPruefen()) {
            return _lastOk;
        }
        delay(POLL_DELAY_MS);
    }
    // Kein Ergebnis im Wartefenster: Versuch laeuft im Hintergrund weiter (loop()).
    return false;
}

/**
 * @brief Check if the last sync was successful
 *
 * @return true if last sync was successful or false otherwise
 */
auto NTPClient::lastSyncOk() const -> bool { return _lastOk; }

/**
 * @brief Get the time of the last successful sync
 *
 * @return time_t of the last sync
 */
auto NTPClient::lastSyncTime() const -> time_t { return _lastSync; }

/**
 * @brief Get the last sync status message
 *
 * @return String containing the last status
 */
auto NTPClient::lastStatus() const -> String { return _lastStatus; }
