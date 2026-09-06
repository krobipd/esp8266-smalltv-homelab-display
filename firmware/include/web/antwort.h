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

/// Legt die gemeinsamen Antwortfelder an.
///
/// EIN Format seit v0.5.0: `ok` sagt, ob es geklappt hat, `message` sagt es in Worten.
/// Vorher gab es drei -- {status,message} aus der Basis-Firmware, {ok,error} aus diesem
/// Projekt und beim Abschluss eines Updates einen englischen Satz im Feld status. Die
/// Oberflaeche musste je Handler wissen, welches davon kam, und pruefte entsprechend
/// `data.message || data.error`.
///
/// Die alten Felder gehen VORERST WEITER MIT (`status` und, im Fehlerfall, `error`).
/// Grund: Zwischen dem Flashen der Firmware und dem des Dateisystems laeuft die alte
/// Oberflaeche auf der neuen Firmware. Ohne die Altfelder waere sie in diesem Fenster
/// blind. Sie verschwinden, wenn dieses Fenster Geschichte ist.
inline void setzeErgebnis(JsonDocument& doc, bool ok, const char* message) {
    doc["ok"] = ok;
    doc["message"] = message;
    doc["status"] = ok ? "ok" : "error";
    if (!ok) {
        doc["error"] = message;
    }
}

inline void sendeErgebnis(Webserver* webserver, int code, bool ok, const char* message) {
    JsonDocument doc;
    setzeErgebnis(doc, ok, message);
    sendeJson(webserver, code, doc);
}

/// Erfolg mit Text.
inline void sendeStatus(Webserver* webserver, int code, const char* status, const char* message) {
    JsonDocument doc;
    setzeErgebnis(doc, strcmp(status, "error") != 0, message);
    // Ein Zwischenstand wie "rebooting" oder "cancelling" ist weder ok noch Fehler --
    // er steht weiterhin in status, damit die Seiten ihn unterscheiden koennen.
    doc["status"] = status;
    sendeJson(webserver, code, doc);
}

inline void sendeFehlerStatus(Webserver* webserver, int code, const char* message) {
    sendeErgebnis(webserver, code, false, message);
}

/// Fehler im Werte-Format -- seit v0.5.0 dasselbe wie sendeFehlerStatus.
inline void sendeFehler(Webserver* webserver, int code, const char* text) {
    sendeErgebnis(webserver, code, false, text);
}

/// Liest den JSON-Koerper der Anfrage. Bei einem Fehler ist bereits mit 400
/// geantwortet -- der Aufrufer kehrt dann sofort zurueck, ohne selbst zu senden.
inline bool leseJsonKoerper(Webserver* webserver, JsonDocument& doc) {
    if (!webserver->raw().hasArg("plain") || webserver->raw().arg("plain").length() == 0) {
        sendeFehlerStatus(webserver, HTTP_CODE_BAD_REQUEST, "Anfrage ohne Inhalt");
        return false;
    }
    if (deserializeJson(doc, webserver->raw().arg("plain"))) {
        sendeFehlerStatus(webserver, HTTP_CODE_BAD_REQUEST, "Anfrage ist kein gueltiges JSON");
        return false;
    }
    return true;
}

#endif  // WEB_ANTWORT_H
