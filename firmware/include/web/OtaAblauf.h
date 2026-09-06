// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WEB_OTA_ABLAUF_H
#define WEB_OTA_ABLAUF_H

// EIN Ablauf fuer jedes Einspielen eines Abbilds -- Normalbetrieb wie Rettungsmodus.
//
// Vorher gab es ihn zweimal, mit je anderer Teilmenge der Sicherungen: Der Rettungsmodus
// pruefte die Pruefsumme nicht, meldete Fehler nur ins Protokoll und haengte das
// Dateisystem nicht aus. Wer eine Sicherung ergaenzte, musste daran denken, sie an der
// zweiten Stelle nachzuziehen -- und genau das ist zweimal nicht passiert.
//
// Diese Klasse haelt den Zustand EINES laufenden Vorgangs. Es kann nur einen geben: Der
// Chip schreibt in genau eine Partition, und der Webserver bearbeitet eine Anfrage nach
// der anderen.
#include <Arduino.h>

#include <cstddef>
#include <cstdint>

class OtaAblauf {
   public:
    /// Beginnt einen Vorgang. modus ist U_FLASH oder U_FS.
    /// md5 darf leer sein; fuer U_FLASH ist sie Pflicht (ohne Rollback waere ein
    /// unvollstaendiges Abbild ein Geraet, das nicht mehr startet).
    /// Liefert false und setzt fehler()/meldung(), wenn schon der Start scheitert.
    static bool start(int modus, size_t gesamt, const char* md5);

    /// Ein Brocken aus dem Upload. Der erste wird auf die Abbild-Art geprueft.
    static bool schreiben(const uint8_t* daten, size_t laenge);

    /// Schliesst den Vorgang ab (Pruefsumme, Aktivierung).
    static bool abschliessen();

    /// Bricht ab -- vom Nutzer angefordert oder weil die Verbindung wegbrach.
    static void abbrechen(const char* grund);

    /// Der Nutzer hat Abbruch angefordert; der naechste Brocken beendet den Vorgang.
    static void abbruchAnfordern();
    static bool abbruchAngefordert();

    static bool laeuft();
    static bool fehler();
    static const String& meldung();
    static size_t geschrieben();
    static size_t gesamt();
    static int modus();

    /// Anteil 0..1, oder -1, wenn die Gesamtgroesse unbekannt ist.
    static float fortschritt();

    /// Setzt den Zustand zurueck, ohne etwas zu schreiben (nach dem Beantworten).
    static void abmelden();

   private:
    static void merkeFehler(const char* text);
};

#endif  // WEB_OTA_ABLAUF_H
