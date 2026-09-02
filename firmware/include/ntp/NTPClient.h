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

#ifndef NTP_CLIENT_H
#define NTP_CLIENT_H

#include <Arduino.h>

// NICHT-BLOCKIEREND: Der fruehere performSync() wartete mit delay() bis zu ~31 s auf die
// Antwort und hielt dabei Webserver, Display und Abrufe an. Jetzt stoesst starteSync()
// nur SNTP an, und loop() prueft in jedem Durchlauf, ob die Zeit angekommen ist.
// Der erste Sync startet bei der ersten WLAN-Verbindung (nicht erst nach 6 h), und bis
// zum ersten Erfolg wird im Minutentakt neu versucht statt im 6-h-Intervall.
class NTPClient {
   public:
    NTPClient();
    void begin(uint32_t syncIntervalSeconds = 6 * 3600, uint8_t maxRetries = 3);
    void loop();
    /// Sofort-Sync fuer die Web-API: wartet begrenzt (~5 s) auf ein Ergebnis.
    /// Kommt keins, laeuft der Versuch im Hintergrund weiter (loop()).
    bool syncNow();

    /// Sync nur ANSTOSSEN, ohne zu warten -- fuer Aufrufer, denen /ntp/status als
    /// Ergebnisanzeige genuegt (z. B. nach dem Speichern des Zeitservers).
    void syncAnstossen();

    bool lastSyncOk() const;
    time_t lastSyncTime() const;
    String lastStatus() const;

   private:
    uint32_t _syncIntervalSeconds = 6 * 3600;
    uint8_t _maxRetries = 3;
    time_t _lastSync = 0;
    bool _lastOk = false;
    String _lastStatus = "noch nicht synchronisiert";
    unsigned long _lastSyncAttemptMs = 0;
    bool _syncLaeuft = false;
    bool _jeVersucht = false;
    bool _offlineGemeldet = false;
    unsigned long _syncStartMs = 0;
    uint8_t _versuch = 0;
    void starteSync();
    void syncAbschliessen(bool ok);
    bool ergebnisPruefen();
};

#endif  // NTP_CLIENT_H
