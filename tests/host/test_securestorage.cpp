// SPDX-License-Identifier: GPL-3.0-or-later
// Host-Test der Schreibentscheidungen von SecureStorage -- gegen eine EEPROM-Attrappe:
//   c++ -std=c++17 -DARDUINOJSON_ENABLE_ARDUINO_STRING=1
//       -I .../ArduinoJson/src -I firmware/include -I tests/host/attrappe
//       tests/host/test_securestorage.cpp -o /tmp/smalltv-securestorage
//
// Warum es diesen Test gibt: begin() ueberschrieb bei JEDEM Lesefehler den Sektor mit
// {} und vernichtete damit die WLAN-Zugangsdaten -- das einzige Exemplar, ohne das das
// Geraet nur noch im eigenen Zugangspunkt erreichbar ist. Und der Bootloop-Zaehler
// loeschte denselben Sektor mehrmals je Start. Beides sind Entscheidungen, keine
// Kryptografie: Genau die werden hier geprueft.
//
// NICHT geprueft: die Ableitung selbst (die Attrappe ersetzt BearSSL durch eine
// deterministische Funktion) und das Verhalten des echten Flash-Speichers.
#include "pruefe.h"
#include <cstring>

#include "../../firmware/src/config/SecureStorage.cpp"  // NOLINT(bugprone-suspicious-include)

namespace {
const size_t GROESSE = 2048;

/// Ein Sektor mit gueltigem, lesbarem Inhalt -- so, wie ihn ein eingerichtetes Geraet hat.
void legeEchteDatenAn() {
    EEPROM.setzeGroesse(GROESSE, 0xFF);
    SecureStorage s(GROESSE);
    PRUEFE(s.begin());
    PRUEFE(s.put("wifi_ssid", "MeinNetz"));
    PRUEFE(s.put("wifi_password", "geheim"));
}
}  // namespace

int main() {
    SecureStorage::setSalt("TestSalz");

    // --- Fabrikneu (0xFF): Kopf anlegen ist richtig, da ist nichts zu verlieren ---
    EEPROM.setzeGroesse(GROESSE, 0xFF);
    EEPROM.commits = 0;
    {
        SecureStorage s(GROESSE);
        PRUEFE(s.begin());
        PRUEFE(!s.datenUnlesbar());
        PRUEFE(EEPROM.commits == 1);
    }

    // --- Genullter Sektor gilt ebenfalls als leer ---
    EEPROM.setzeGroesse(GROESSE, 0x00);
    {
        SecureStorage s(GROESSE);
        PRUEFE(s.begin());
        PRUEFE(!s.datenUnlesbar());
    }

    // --- Rundlauf: Was geschrieben wurde, wird auch wieder gelesen ---
    legeEchteDatenAn();
    {
        SecureStorage s(GROESSE);
        PRUEFE(s.begin());
        PRUEFE(!s.datenUnlesbar());
        PRUEFE(s.get("wifi_ssid", "") == "MeinNetz");
        PRUEFE(s.get("wifi_password", "") == "geheim");
    }

    // --- DER FEHLER: unlesbarer Inhalt darf NICHT ueberschrieben werden ---
    // Frueher schrieb begin() hier {} hinein und die Zugangsdaten waren endgueltig weg.
    legeEchteDatenAn();
    // Nutzdaten wirklich zerstoeren: Ein einzelnes gekipptes Byte laesst das JSON oft
    // noch durchgehen (es trifft meist nur ein Zeichen INNERHALB eines Wertes).
    for (int i = 6; i < 30; ++i) {
        EEPROM.write(i, 0xAA);
    }
    {
        const uint8_t vorher0 = EEPROM.byteAt(0);
        const uint8_t vorher20 = EEPROM.byteAt(20);
        EEPROM.commits = 0;
        SecureStorage s(GROESSE);
        PRUEFE(s.begin());              // das Geraet laeuft weiter
        PRUEFE(s.datenUnlesbar());      // sagt aber, dass es nicht lesen konnte
        PRUEFE(EEPROM.commits == 0);    // und hat NICHTS geschrieben
        PRUEFE(EEPROM.byteAt(0) == vorher0);
        PRUEFE(EEPROM.byteAt(20) == vorher20);
    }

    // --- Auch ein zerstoerter Kopf fuehrt nicht mehr zum Ueberschreiben ---
    legeEchteDatenAn();
    EEPROM.write(0, 'X');  // Magic kaputt
    {
        EEPROM.commits = 0;
        SecureStorage s(GROESSE);
        PRUEFE(s.begin());
        PRUEFE(s.datenUnlesbar());
        PRUEFE(EEPROM.commits == 0);
    }

    // --- Nach einem Lesefehler darf der Nutzer natuerlich neu schreiben ---
    legeEchteDatenAn();
    for (int i = 6; i < 30; ++i) {
        EEPROM.write(i, 0xAA);
    }
    {
        SecureStorage s(GROESSE);
        PRUEFE(s.begin());
        PRUEFE(s.datenUnlesbar());
        PRUEFE(s.put("wifi_ssid", "NeuesNetz"));
        PRUEFE(!s.datenUnlesbar());
        PRUEFE(s.get("wifi_ssid", "") == "NeuesNetz");
    }

    // --- Leerlauf-Riegel: derselbe Wert loescht den Sektor nicht noch einmal ---
    // Der Bootloop-Zaehler tat genau das, mehrmals je Start.
    legeEchteDatenAn();
    {
        SecureStorage s(GROESSE);
        PRUEFE(s.begin());
        EEPROM.commits = 0;
        PRUEFE(s.put("wifi_ssid", "MeinNetz"));   // unveraendert
        PRUEFE(EEPROM.commits == 0);
        PRUEFE(s.put("wifi_ssid", "AnderesNetz"));  // veraendert
        PRUEFE(EEPROM.commits == 1);
        PRUEFE(s.put("wifi_ssid", "AnderesNetz"));  // wieder unveraendert
        PRUEFE(EEPROM.commits == 1);
    }

    // --- Ein fehlgeschlagener commit() meldet sich, statt Erfolg vorzutaeuschen ---
    EEPROM.setzeGroesse(GROESSE, 0xFF);
    EEPROM.commitGelingt = false;
    {
        SecureStorage s(GROESSE);
        PRUEFE(!s.begin());
    }
    EEPROM.commitGelingt = true;

    printf("SecureStorage-Schreibentscheidungen: OK\n");
    return 0;
}
