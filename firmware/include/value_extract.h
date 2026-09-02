// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Holt aus einer HTTP-Antwort den anzuzeigenden Wert.
// Zwei Faelle: leeres Feld = die Antwort ist der Wert (ioBroker /plain),
// gesetztes Feld = genau dieser eine Schluessel aus dem JSON.
// Bewusst KEIN JSONPath: ein Punktpfad wird als Fehler abgewiesen, nicht geraten.
#include <ArduinoJson.h>

#include "smalltv_util.h"  // liefert setErr(), trimInto(), FIELD_LEN

inline bool extractValue(const char* body, const char* field, char* out, size_t outSize,
                         char* errOut, size_t errSize) {
    if (out == nullptr || outSize == 0) return false;
    out[0] = 0;
    if (body == nullptr || body[0] == 0) {
        setErr(errOut, errSize, "leere Antwort");
        return false;
    }

    // Fall 1: die Antwort selbst ist der Wert
    if (field == nullptr || field[0] == 0) {
        char tmp[64];
        trimInto(tmp, sizeof(tmp), body);
        if (tmp[0] == 0) {
            setErr(errOut, errSize, "leere Antwort");
            return false;
        }
        snprintf(out, outSize, "%s", tmp);
        return true;
    }

    if (strchr(field, '.') != nullptr) {
        setErr(errOut, errSize, "verschachtelte Felder nicht unterstuetzt");
        return false;
    }

    // Fall 2: ein Feld aus dem JSON. Der Filter sorgt dafuer, dass nur dieses eine
    // Feld im Speicher landet -- eine grosse Antwort kostet damit fast nichts.
    JsonDocument filter;
    filter[field] = true;

    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (e) {
        setErr(errOut, errSize, "Antwort ist kein gueltiges JSON");
        return false;
    }

    JsonVariantConst v = doc[field];
    if (v.isNull()) {
        setErr(errOut, errSize, "Feld nicht gefunden");
        return false;
    }
    if (v.is<JsonObjectConst>() || v.is<JsonArrayConst>()) {
        setErr(errOut, errSize, "Feld ist kein einzelner Wert");
        return false;
    }

    // Zeichenketten und Wahrheitswerte von Hand, damit keine JSON-Anfuehrungszeichen
    // im Ergebnis landen. Zahlen ueber serializeJson -- das ist auch am Host verfuegbar,
    // anders als Arduinos String-Klasse, und formatiert Ganzzahlen ohne Nachkomma-Rest.
    if (v.is<const char*>()) {
        snprintf(out, outSize, "%s", v.as<const char*>());
    } else if (v.is<bool>()) {
        snprintf(out, outSize, "%s", v.as<bool>() ? "true" : "false");
    } else {
        size_t n = serializeJson(v, out, outSize);
        if (n == 0) {
            setErr(errOut, errSize, "Wert passt nicht in den Puffer");
            return false;
        }
    }

    if (out[0] == 0) {
        setErr(errOut, errSize, "Feld ist leer");
        return false;
    }
    return true;
}
