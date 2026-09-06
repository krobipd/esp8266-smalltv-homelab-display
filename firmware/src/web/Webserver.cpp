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
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <detail/mimetable.h>
#include <functional>
#include <Logger.h>
#include <cstring>
#include <cstdlib>
#include <array>

#include "http_cache.h"
#include "project_version.h"
#include "web/Webserver.h"

static constexpr size_t PATH_BUF_SIZE = 256;

namespace {

}  // namespace

Webserver::Webserver(uint16_t port) : _server(port) {}

// NOLINTBEGIN(readability-convert-member-functions-to-static)
/**
 * @brief Starts the webserver
 *
 * @return void
 */
void Webserver::begin() {
    Logger::info("Starting webserver", "Webserver");
    ladeDateisystemKennung();
    _server.begin();
}
// NOLINTEND(readability-convert-member-functions-to-static)

/**
 * @brief Handles incoming client requests
 *
 * @return void
 */
void Webserver::handleClient() { _server.handleClient(); }

/**
 * @brief Kennung einer Datei fuer den Browser-Cache
 *
 * Rechnet nicht selbst -- die Regel steht in http_cache.h und wird dort am Host
 * geprueft (tests/host/test_httpcache.cpp).
 *
 * @return void
 */
// Kennung des Dateisystems, beim Bauen aus dem Inhalt aller Web-Dateien erzeugt
// (scripts/fs_build_id.py). Leer, solange sie nicht gelesen wurde oder das Abbild sie
// nicht mitbringt -- dann gilt die Firmware-Version wie vor v0.5.0.
static char g_fsKennung[16] = {0};

void Webserver::ladeDateisystemKennung() {
    g_fsKennung[0] = 0;
    File f = LittleFS.open("/web/BUILD", "r");
    if (!f) {
        Logger::info("Keine Dateisystem-Kennung (/web/BUILD) -- Cache haengt an der Version",
                     "Webserver");
        return;
    }
    const size_t gelesen = f.readBytes(g_fsKennung, sizeof(g_fsKennung) - 1);
    f.close();
    g_fsKennung[gelesen] = 0;
    // Nur Hex-Ziffern gelten; alles andere (Zeilenende, Muell) beendet die Kennung.
    for (size_t i = 0; g_fsKennung[i] != 0; i++) {
        const char c = g_fsKennung[i];
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex) {
            g_fsKennung[i] = 0;
            break;
        }
    }
    if (g_fsKennung[0] == 0) {
        Logger::warn("Dateisystem-Kennung unlesbar -- Cache haengt an der Version", "Webserver");
        return;
    }
    Logger::info((String("Dateisystem-Kennung: ") + g_fsKennung).c_str(), "Webserver");
}

void Webserver::baueEtag(char* out, size_t outSize, size_t dateiGroesse) {
    ::baueEtag(out, outSize, g_fsKennung[0] != 0 ? g_fsKennung : PROJECT_VER_STR, dateiGroesse);
}

/**
 * @brief Beantwortet die Anfrage mit 304, wenn der Browser die Datei schon hat
 *
 * "If-None-Match" sammelt der Webserver der Basis von sich aus ein (neben
 * "Authorization") -- ein collectHeaders() waere ueberfluessig.
 *
 * @return true, wenn bereits geantwortet wurde
 */
bool Webserver::beantworteAusBrowserCache(const char* etag) {
    if (!etagPasst(_server.header("If-None-Match").c_str(), etag)) {
        return false;
    }
    // Von Hand geschrieben statt _server.send(304) -- Begruendung in http_cache.h
    // (die Basis wuerde Content-Type und Content-Length anhaengen, beides gehoert
    // nicht in eine Nicht-geaendert-Antwort).
    char kopf[KOPF304_BUF_SIZE];
    baue304Kopf(kopf, sizeof(kopf), etag);
    WiFiClient& client = _server.client();
    client.write(kopf, strlen(kopf));
    client.flush();
    client.stop();
    return true;
}

/**
 * @brief Cache-Kopfzeilen einer ausgelieferten Datei
 *
 * @return void
 */
void Webserver::sendeCacheKopfzeilen(const char* etag) {
    _server.sendHeader("Cache-Control", CACHE_RUECKFRAGEN);
    _server.sendHeader("ETag", etag);
}

/**
 * @brief Serve a static file from LittleFS using C-strings
 * @param uriC The URL path
 * @param pathC The filesystem path
 * @param contentTypeC The content type to use. If nullptr or empty string, it will be derived from the file extension
 *
 * Ausgeliefert wird immer mit Rueckfrage-Cache und Kennung (siehe CACHE_RUECKFRAGEN).
 *
 * @return void
 */
void Webserver::serveStaticC(const char* uriC, const char* pathC, const char* contentTypeC) {
    _server.on(uriC, HTTP_GET, [this, pathC, contentTypeC, uriC]() {
        const char* chosenPath = pathC;
        const char* contentTypeStr = contentTypeC;

        // Direkt oeffnen statt erst exists() zu fragen: Beides laeuft ueber dieselbe
        // Verzeichnissuche im Dateisystem, und ein fehlgeschlagenes open() sagt genau
        // dasselbe wie ein negatives exists().
        File f = LittleFS.open(chosenPath, "r");
        if (!f) {
            char msg[320];
            snprintf(msg, sizeof(msg), "File not found: %s", chosenPath);
            Logger::error(msg, "Webserver");
            _server.send(HTTP_CODE_NOT_FOUND, "text/plain", "Not found");

            return;
        }

        size_t size = f.size();

        String contentTypeResolved = (contentTypeStr != nullptr && contentTypeStr[0] != '\0')
                                         ? String(contentTypeStr)
                                         : guessContentTypeC(chosenPath);

        char etag[ETAG_BUF_SIZE];
        baueEtag(etag, sizeof(etag), size);
        if (beantworteAusBrowserCache(etag)) {
            f.close();
            return;
        }
        sendeCacheKopfzeilen(etag);

        _server.setContentLength(size);
        _server.streamFile(f, contentTypeResolved);
        f.close();

    });
}


/**
 * @brief Register a generic static fallback route using onNotFound
 *
 * Serves GET requests from fsBasePath + request URI. API routes are excluded
 * Can optionally exclude '/' to keep an explicit root route
 */
void Webserver::registerGenericStaticFallback(  // NOLINT(readability-convert-member-functions-to-static)
    const String& fsBasePath, bool excludeRoot) {
    String basePath = fsBasePath;
    if (basePath.endsWith("/") && basePath.length() > 1) {
        basePath = basePath.substring(0, basePath.length() - 1);
    }

    _server.onNotFound([this, basePath, excludeRoot]() {
        if (_server.method() != HTTP_GET) {
            _server.send(HTTP_CODE_NOT_FOUND, "text/plain", "Not found");
            return;
        }

        const String& uri = _server.uri();

        if (excludeRoot && uri == "/") {
            _server.send(HTTP_CODE_NOT_FOUND, "text/plain", "Not found");
            return;
        }

        if (uri.startsWith("/api/")) {
            _server.send(HTTP_CODE_NOT_FOUND, "text/plain", "Not found");
            return;
        }

        // Punkt-Segmente abweisen. Ohne diese Pruefung liesse sich der Basispfad "/web"
        // verlassen: LittleFS loest ".." tatsaechlich auf, sodass "/web/../slots.json"
        // die Slot-Konfiguration ohne jede Anmeldung ausliefern wuerde.
        if (uri.indexOf("..") >= 0) {
            _server.send(HTTP_CODE_NOT_FOUND, "text/plain", "Not found");
            return;
        }

        // EIN Pfadpuffer statt dreier: uriBuf war eine Kopie von uri.c_str(), chosenPath
        // eine Kopie von fsPath. Das sind 448 Byte Stapelspeicher weniger in einem
        // Rueckruf, der im Webserver ohnehin tief steht (Befund F).
        char fsPath[PATH_BUF_SIZE] = {0};
        if (snprintf(fsPath, sizeof(fsPath), "%s%s", basePath.c_str(), uri.c_str()) <= 0) {
            _server.send(HTTP_CODE_INTERNAL_ERROR, "text/plain", "Path error");
            return;
        }

        // Direkt oeffnen statt erst exists(): dieselbe Verzeichnissuche, halb so oft.
        File f = LittleFS.open(fsPath, "r");
        if (!f) {
            _server.send(HTTP_CODE_NOT_FOUND, "text/plain", "Not found");
            return;
        }

        char etag[ETAG_BUF_SIZE];
        baueEtag(etag, sizeof(etag), f.size());
        if (beantworteAusBrowserCache(etag)) {
            f.close();
            return;
        }
        sendeCacheKopfzeilen(etag);

        _server.setContentLength(f.size());
        _server.streamFile(f, guessContentTypeC(fsPath));
        f.close();

    });
}

/**
 * @brief Expose underlying server where advanced config is needed
 *
 * @return reference to the underlying ESP8266WebServer
 */
auto Webserver::raw() -> ESP8266WebServer& { return _server; }

// Die Zuordnung Endung -> MIME-Typ bringt der Core schon mit (mimetable.h, dieselbe
// Tabelle, die serveStatic benutzt). Die eigene Kopie kannte zwoelf Endungen und musste
// bei jeder neuen von Hand nachgezogen werden.
auto Webserver::guessContentTypeC(const char* path) -> String {
    if (path == nullptr || path[0] == '\0') {
        return {mime::mimeTable[mime::none].mimeType};
    }

    const size_t len = strlen(path);
    if (path[len - 1] == '/') {
        return {mime::mimeTable[mime::html].mimeType};
    }

    return mime::getContentType(String(path));
}
