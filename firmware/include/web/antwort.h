// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WEB_ANTWORT_H
#define WEB_ANTWORT_H

// Antworten der Web-Schnittstelle an EINER Stelle. Vorher baute jeder Handler
// JsonDocument, String, serializeJson und send() von Hand -- rund 40-mal allein in
// Api.cpp, jedes Mal mit der Chance, den Content-Type oder den Code zu vertippen.
//
// Zwei Formate bleiben nebeneinander bestehen (so steht es in docs/API.md und so
// werten die Seiten es aus): die Basis-Schnittstelle antwortet {status, message},
// die Werte-Schnittstelle {ok, error}. Sie zu vereinheitlichen waere ein Bruch
// aller Seiten und der Sicherungsdateien -- deshalb zwei Helferpaare statt eines.
#include <ArduinoJson.h>

#include "web/Webserver.h"

inline void sendeJson(Webserver* webserver, int code, const JsonDocument& doc) {
    String ausgabe;
    // Ohne dies waechst der String in 16-Byte-Schritten mit, bei einer vollen
    // Konfiguration also ueber hundertmal -- auf dem knappen Heap teuer.
    ausgabe.reserve(measureJson(doc) + 1);
    serializeJson(doc, ausgabe);
    webserver->raw().send(code, "application/json", ausgabe);
}

/// Basis-Format {status, message}.
inline void sendeStatus(Webserver* webserver, int code, const char* status, const char* message) {
    JsonDocument doc;
    doc["status"] = status;
    doc["message"] = message;
    sendeJson(webserver, code, doc);
}

inline void sendeFehlerStatus(Webserver* webserver, int code, const char* message) {
    sendeStatus(webserver, code, "error", message);
}

/// Werte-Format {ok:false, error}.
inline void sendeFehler(Webserver* webserver, int code, const char* text) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = text;
    sendeJson(webserver, code, doc);
}

/// Liest den JSON-Koerper der Anfrage. Bei einem Fehler ist bereits mit 400
/// geantwortet -- der Aufrufer kehrt dann sofort zurueck, ohne selbst zu senden.
/// basisFormat waehlt zwischen {status,message} und {ok,error}.
inline bool leseJsonKoerper(Webserver* webserver, JsonDocument& doc, bool basisFormat) {
    if (!webserver->raw().hasArg("plain") || webserver->raw().arg("plain").length() == 0) {
        if (basisFormat) {
            sendeFehlerStatus(webserver, HTTP_CODE_BAD_REQUEST, "Anfrage ohne Inhalt");
        } else {
            sendeFehler(webserver, HTTP_CODE_BAD_REQUEST, "leere Anfrage");
        }
        return false;
    }
    if (deserializeJson(doc, webserver->raw().arg("plain"))) {
        if (basisFormat) {
            sendeFehlerStatus(webserver, HTTP_CODE_BAD_REQUEST, "Anfrage ist kein gueltiges JSON");
        } else {
            sendeFehler(webserver, HTTP_CODE_BAD_REQUEST, "Anfrage ist kein gueltiges JSON");
        }
        return false;
    }
    return true;
}

#endif  // WEB_ANTWORT_H
