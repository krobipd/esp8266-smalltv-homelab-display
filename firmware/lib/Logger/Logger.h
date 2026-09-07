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

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

enum LogLevel { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

// Seit v0.4.0 auch INFO: Der Ringpuffer fasst 20 Eintraege und wurde frueher vom
// Heartbeat (alle zehn Sekunden eine Speicherzeile) leergeschrieben. Der ist weg,
// also ist Platz fuer das, was tatsaechlich passiert -- Start, WLAN, Update.
static constexpr LogLevel LOG_MIN_LEVEL = LOG_INFO;
// 32 statt 20 (v0.5.5): Ein vollstaendiger Startvorgang schreibt rund sechzehn Zeilen,
// und danach verdraengten Zeitabgleich und Stabilitaetsmeldung genau die frueheren --
// ConfigManager und SecureStorage waren bei JEDEM Abruf schon herausgefallen. Bei der
// Ursachensuche am 07.09.2026 fehlten dadurch die Zeilen, auf die es angekommen waere.
// Preis: 12 * 96 = 1152 Byte mehr im Heap. Bewusst bezahlt -- ein Protokoll, das den
// Start nicht vollstaendig fasst, ist bei genau der Sorte Fehler wertlos, fuer die es
// da ist.
static constexpr size_t LOG_BUFFER_MAX_ENTRIES = 32;
static constexpr size_t LOG_ENTRY_MAX_LEN = 96;

class Logger {
   public:
    static void log(LogLevel level, const char* message, const char* className = nullptr);
    static void info(const char* message, const char* className = nullptr);
    static void warn(const char* message, const char* className = nullptr);
    static void error(const char* message, const char* className = nullptr);

    static String getLogsAsString();
    static size_t getLogCount();
    static const char* getLogEntry(size_t index);
    static void clearLogs();

   private:
    static const char* levelToString(LogLevel level);
    static void addToBuffer(const char* entry);
    static void ensureBufferAllocated();

    static char* _logBuffer;
    static size_t _head;
    static size_t _count;
};

#endif  // LOGGER_H
