// SPDX-License-Identifier: GPL-3.0-or-later
#include "web/OtaAblauf.h"

#include <Logger.h>
#include <LittleFS.h>
#include <Updater.h>
#include <coredecls.h>

#include "abbild_art.h"

namespace {
bool g_laeuft = false;
bool g_fehler = false;
bool g_abbruch = false;
bool g_ersterBrocken = true;
size_t g_geschrieben = 0;
size_t g_gesamt = 0;
int g_modus = U_FLASH;
String g_meldung;

/// Platz in der Zielpartition. Fuer das Dateisystem die Groesse des Dateisystems, fuer
/// die Firmware der freie Programmspeicher abzueglich Sicherheitsabstand, auf
/// Sektorgrenze abgerundet.
size_t zielPlatz(int modus) {
    if (modus == U_FS) {
        FSInfo info;
        LittleFS.info(info);
        return info.totalBytes;
    }
    int constexpr sicherheitsabstand = 0x1000;
    uint32_t constexpr sektormaske = 0xFFFFF000;
    return (ESP.getFreeSketchSpace() - sicherheitsabstand) &  // NOLINT(readability-static-accessed-through-instance)
           sektormaske;
}
}  // namespace

void OtaAblauf::merkeFehler(const char* text) {
    g_fehler = true;
    g_meldung = text;
    g_laeuft = false;
    Logger::error(text, "OTA");
}

bool OtaAblauf::start(int modus, size_t gesamt, const char* md5) {
    g_laeuft = true;
    g_fehler = false;
    g_abbruch = false;
    g_ersterBrocken = true;
    g_geschrieben = 0;
    g_gesamt = gesamt;
    g_modus = modus;
    g_meldung = "";

    if (modus == U_FS) {
        // Die Partition wird gleich ueberschrieben: Dateisystem aushaengen, wie es der
        // Update-Server des Frameworks tut. Ohne das bliebe ein Mount mit veralteten
        // Metadaten stehen -- und LittleFS.begin() ist bei gemountetem Dateisystem ein
        // No-op, das Neu-Mounten hinterher faende also nie statt.
        close_all_fs();
    }

    if (!Update.begin(zielPlatz(modus), modus)) {
        if (modus == U_FS) {
            LittleFS.begin();  // Oberflaeche soll weiterlaufen
        }
        merkeFehler(Update.getErrorString().c_str());
        return false;
    }

    // Firmware nur mit Pruefsumme: Der Updater prueft sonst nur das erste Byte und
    // aktiviert auch ein unvollstaendiges Abbild -- ohne Rollback ein Geraet, das nicht
    // mehr startet. Fuer das Dateisystem ist die Summe willkommen, aber nicht Pflicht:
    // Ein kaputtes Dateisystem laesst sich noch reparieren, ein kaputtes Programm nicht.
    if (md5 != nullptr && strlen(md5) == 32) {
        Update.setMD5(md5);
    } else if (modus == U_FLASH) {
        Update.end();
        merkeFehler("Pruefsumme fehlt -- Update-Seite neu laden oder curl mit X-Abbild-MD5");
        return false;
    }
    return true;
}

bool OtaAblauf::schreiben(const uint8_t* daten, size_t laenge) {
    if (g_fehler || !g_laeuft) {
        return false;
    }
    if (g_abbruch) {
        abbrechen("Update abgebrochen");
        return false;
    }

    // Erster Brocken: Ist das ueberhaupt die Sorte Abbild, die hier hingehoert? Das
    // Geraet verlaesst sich NICHT auf die Angabe der Oberflaeche -- eine veraltete Seite
    // aus dem Zwischenspeicher hat genau das einmal falsch gemacht.
    if (g_ersterBrocken) {
        g_ersterBrocken = false;
        const AbbildArt erwartet = (g_modus == U_FS) ? ABBILD_DATEISYSTEM : ABBILD_FIRMWARE;
        const AbbildArt erkannt = erkenneAbbild(daten, laenge);
        if (erkannt != erwartet) {
            Update.end();
            if (g_modus == U_FS) {
                LittleFS.begin();  // nichts geschrieben, altes Dateisystem zurueck
            }
            merkeFehler(abbildFehlertext(erkannt, erwartet));
            return false;
        }
    }

    if (Update.write(const_cast<uint8_t*>(daten), laenge) != laenge) {
        merkeFehler(Update.getErrorString().c_str());
        return false;
    }
    g_geschrieben += laenge;
    return true;
}

bool OtaAblauf::abschliessen() {
    if (g_fehler) {
        return false;
    }
    if (!Update.end(true)) {
        merkeFehler(Update.getErrorString().c_str());
        return false;
    }
    g_laeuft = false;
    g_meldung = String("Update OK (") + String((unsigned)g_geschrieben) + " Byte)";
    Logger::info(g_meldung.c_str(), "OTA");
    return true;
}

void OtaAblauf::abbrechen(const char* grund) {
    Update.end();
    if (g_modus == U_FS) {
        LittleFS.begin();  // so weit noch moeglich; sonst hilft der Neustart
    }
    g_abbruch = false;
    merkeFehler(grund);
}

void OtaAblauf::abbruchAnfordern() {
    g_abbruch = true;
    g_meldung = "Abbruch angefordert";
}

bool OtaAblauf::abbruchAngefordert() { return g_abbruch; }
bool OtaAblauf::laeuft() { return g_laeuft; }
bool OtaAblauf::fehler() { return g_fehler; }
const String& OtaAblauf::meldung() { return g_meldung; }
size_t OtaAblauf::geschrieben() { return g_geschrieben; }
size_t OtaAblauf::gesamt() { return g_gesamt; }
int OtaAblauf::modus() { return g_modus; }

float OtaAblauf::fortschritt() {
    if (g_gesamt == 0) {
        return -1.0F;
    }
    return (float)g_geschrieben / (float)g_gesamt;
}

void OtaAblauf::abmelden() {
    g_laeuft = false;
    g_abbruch = false;
}
