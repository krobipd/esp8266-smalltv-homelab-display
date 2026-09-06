// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Homelab-Display fuer GeekMagic SmallTV
 * Copyright (C) 2026 krobi
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

#include "slots/SlotRuntime.h"

#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>

#include "Logger.h"
#include "value_extract.h"

namespace {
Config* g_cfg = nullptr;
SlotLaufzeit g_laufzeit[MAX_SLOTS];
uint8_t g_naechsterSlot = 0;

// Zustand des Helligkeits-Schalters. Startwert "an": Solange keine Antwort vorliegt,
// soll das Display normal hell sein.
bool g_hellSchalter = true;
uint32_t g_hellLetzterVersuchMs = 0;
uint8_t g_hellFehlversuche = 0;

// Das Zeitlimit des HTTP-Clients gilt je Phase (Verbindungsaufbau, Kopfdaten, Datenstrom),
// nicht fuer den gesamten Abruf; 2000 ms begrenzen den schlimmsten Fall auf rund 6 Sekunden.
// Die eigentliche Entschaerfung bei toten Zielen ist der Rueckzug (abrufIntervallMs).
//
// Der Antwortkoerper wird ueber writeToStream() der Bibliothek gelesen: Sie kennt
// Content-Length und chunked und weiss deshalb, wann die Antwort zu Ende ist. Frueher
// wurde am Socket vorbei mit readBytes() gelesen -- das wartete nach dem letzten Byte einer
// kurzen Antwort das VOLLE Zeitlimit ab: 2 s je Abruf, am Geraet nachgemessen (05.09.2026),
// in denen weder Webserver noch Anzeige liefen.
const uint32_t ANTWORT_TIMEOUT_MS = 2000;

// KOERPER_MAX (Groesse des eingelesenen Antwortkoerpers) liegt in SlotRuntime.h --
// der Testabruf (SlotApi) nutzt dieselbe Grenze. Bewusst begrenzt: Der Heap fasst
// keine beliebig grossen Antworten; wer eine riesige Sammelantwort anbindet, bekommt
// eine ehrliche Fehlermeldung statt eines stillen Absturzes.

// EIN gemeinsamer Antwortpuffer fuer alle drei Abrufwege (Slot-Abruf, Helligkeits-
// Schalter, Testabruf der API): loop() und Webhandler laufen strikt seriell, es sind
// nie zwei Abrufe gleichzeitig unterwegs. Drei eigene statische Puffer kosteten
// dauerhaft ~2,3 KB des knappen RAM fuer exakt denselben Zweck.
char g_koerper[KOERPER_MAX];

/// Nimmt den Antwortkoerper entgegen und behaelt die ersten groesse-1 Bytes, immer
/// nullterminiert. Alles darueber hinaus wird angenommen und verworfen: writeToStream()
/// braeche sonst mit einem Schreibfehler ab, obwohl der Anfang laengst da ist -- und
/// mehr als KOERPER_MAX behalten wir ohnehin nicht (Heap).
class BegrenzterPuffer : public Stream {
   public:
    BegrenzterPuffer(char* ziel, size_t groesse) : _ziel(ziel), _groesse(groesse) {
        if (_groesse > 0) {
            _ziel[0] = 0;
        }
    }
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t* daten, size_t n) override {
        const size_t frei = (_groesse > 0) ? (_groesse - 1 - _laenge) : 0;
        const size_t kopie = (n < frei) ? n : frei;
        if (kopie > 0) {
            memcpy(_ziel + _laenge, daten, kopie);
            _laenge += kopie;
            _ziel[_laenge] = 0;
        }
        return n;
    }
    // Der Sendepfad des Cores (StreamSend.cpp) fragt das Ziel nach freiem Platz und
    // schreibt nur so viel; bei 0 wartet er bis zum Zeitlimit. Wir nehmen immer alles.
    int availableForWrite() override { return 4096; }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    size_t laenge() const { return _laenge; }

   private:
    char* _ziel;
    size_t _groesse;
    size_t _laenge = 0;
};

/**
 * @brief Holt eine URL und legt den Antwortkoerper in einem Puffer ab.
 */
auto abrufen(const char* url, int& httpStatus, char* koerper, size_t koerperSize, char* fehler,
             size_t fehlerSize) -> bool {
    httpStatus = 0;
    if (koerperSize > 0) {
        koerper[0] = 0;
    }

    if (WiFi.status() != WL_CONNECTED) {
        setErr(fehler, fehlerSize, "kein WLAN");
        return false;
    }

    // EINE Verbindung, die zwischen Abrufen offen bleiben darf. Abrufe laufen strikt
    // seriell (ein Ziel nach dem anderen), deshalb genuegt genau ein Client -- die
    // fruehere Sorge "eine offene Verbindung JE SLOT kostet Heap" traf auf diesen
    // Ablauf nie zu. Fuer aufeinanderfolgende Abrufe desselben Ziels entfaellt damit
    // der TCP-Aufbau (Verbindung, Handshake, Slow Start); bei wechselnden Zielen
    // schliesst die Bibliothek die alte Verbindung selbst.
    //
    // Warum das frueher abgeschaltet war: Mit keep-alive und getString() lief jedes
    // Warten auf "mehr Daten" ins volle Zeitlimit, weil der Server die Verbindung
    // offen haelt (Befund N1, 2 s je Abruf). Das lag am Lesen ueber readBytes, nicht
    // am keep-alive; seit v0.3.0 liest writeToStream nach dem Framing der Bibliothek
    // und hoert auf, wenn Content-Length erreicht ist.
    static WiFiClient client;
    static HTTPClient http;
    // Der ESP8266-Client kennt nur EIN gemeinsames Zeitlimit, das je Phase gilt.
    http.setTimeout((uint16_t)ANTWORT_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setReuse(true);

    if (!http.begin(client, url)) {
        setErr(fehler, fehlerSize, "Adresse nicht verwendbar");
        return false;
    }

    httpStatus = http.GET();
    if (httpStatus != HTTP_CODE_OK) {
        char msg[FEHLER_LEN];
        if (httpStatus > 0) {
            snprintf(msg, sizeof(msg), "HTTP %d", httpStatus);
        } else {
            snprintf(msg, sizeof(msg), "keine Antwort (%d)", httpStatus);
        }
        setErr(fehler, fehlerSize, msg);
        http.end();
        return false;
    }

    // In den begrenzten Zielpuffer schreiben lassen. http.getString() wuerde die
    // gesamte Antwort am Stueck im Heap anlegen -- bei einer versehentlich riesigen
    // Antwort waere das ein Vielfaches dessen, was wir ueberhaupt behalten.
    BegrenzterPuffer puffer(koerper, koerperSize);
    const int ergebnis = http.writeToStream(&puffer);
    http.end();

    if (ergebnis < 0) {
        setErr(fehler, fehlerSize, "Antwort unvollstaendig");
        return false;
    }
    if (puffer.laenge() == 0) {
        setErr(fehler, fehlerSize, "leere Antwort");
        return false;
    }
    return true;
}

void fehlschlagVermerken(uint8_t i, const char* grund) {
    SlotLaufzeit& l = g_laufzeit[i];
    if (l.fehlversuche < 255) {
        l.fehlversuche++;
    }
    l.letzterVersuchMs = millis();
    snprintf(l.fehler, FEHLER_LEN, "%s", grund);
    // Der zuletzt gueltige Wert bleibt absichtlich stehen -- er wird nur als veraltet
    // gekennzeichnet. Ihn zu leeren wuerde einen Anzeigefehler in eine Falschinformation
    // verwandeln.
}
}  // namespace

void SlotRuntime::begin(Config* cfg) {
    g_cfg = cfg;
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        zuruecksetzen(i);
    }
}

void SlotRuntime::zuruecksetzen(uint8_t index) {
    if (index >= MAX_SLOTS) {
        return;
    }
    g_laufzeit[index] = SlotLaufzeit{};
    g_laufzeit[index].zustand = STATE_OK;
}

auto SlotRuntime::laufzeit(uint8_t index) -> const SlotLaufzeit& {
    static SlotLaufzeit leer = {};
    if (index >= MAX_SLOTS) {
        return leer;
    }
    return g_laufzeit[index];
}

auto SlotRuntime::veraltet(uint8_t index) -> bool {
    if (index >= MAX_SLOTS || g_cfg == nullptr) {
        return false;
    }
    return istVeraltet(g_laufzeit[index].fehlversuche, alterSek(index),
                       g_cfg->slots[index].refreshSec);
}

auto SlotRuntime::alterSek(uint8_t index) -> uint32_t {
    if (index >= MAX_SLOTS || g_laufzeit[index].letzterErfolgMs == 0) {
        return 0;
    }
    return (uint32_t)(millis() - g_laufzeit[index].letzterErfolgMs) / 1000U;
}

auto SlotRuntime::hellSchalterAn() -> bool {
    return g_hellSchalter;
}

void SlotRuntime::hellZuruecksetzen() {
    g_hellSchalter = true;
    g_hellLetzterVersuchMs = 0;
    g_hellFehlversuche = 0;
}

namespace {
/// Fragt den Schalt-Datenpunkt ab. Gibt true zurueck, wenn dabei ein Abruf stattfand --
/// dann ist fuer diesen Durchlauf genug getan.
bool hellSchalterPruefen() {
    if (g_cfg == nullptr || g_cfg->hellModus != 1 || g_cfg->hellUrl[0] == 0) {
        return false;
    }
    const uint32_t intervallMs = abrufIntervallMs(g_cfg->hellSec, g_hellFehlversuche);
    if (g_hellLetzterVersuchMs != 0 && !elapsed(millis(), g_hellLetzterVersuchMs, intervallMs)) {
        return false;
    }

    char fehler[FEHLER_LEN];
    char roh[WERT_LEN];
    int status = 0;
    g_hellLetzterVersuchMs = millis();

    if (!abrufen(g_cfg->hellUrl, status, g_koerper, sizeof(g_koerper), fehler, sizeof(fehler)) ||
        !extractValue(g_koerper, g_cfg->hellField, roh, sizeof(roh), fehler, sizeof(fehler))) {
        if (g_hellFehlversuche < 255) g_hellFehlversuche++;
        return true;  // Der Versuch hat Zeit gekostet -- kein zweiter Abruf im selben Durchlauf.
    }

    // Der zuletzt bekannte Zustand bleibt bei einem Fehlschlag stehen -- ein Schalter,
    // der bei Netzproblemen umspringt, waere schlimmer als einer, der kurz nachhinkt.
    g_hellSchalter = istWahr(roh);
    g_hellFehlversuche = 0;
    return true;
}
}  // namespace

void SlotRuntime::update() {
    if (g_cfg == nullptr) {
        return;
    }

    // Der Schalter zaehlt wie ein Wert: genau ein Abruf je Durchlauf, nie parallel.
    if (hellSchalterPruefen()) {
        return;
    }

    // Reihum genau einen faelligen Slot bearbeiten. Das haelt die Schleife kurz und
    // verhindert, dass mehrere Verbindungen gleichzeitig offen sind.
    for (uint8_t versuch = 0; versuch < MAX_SLOTS; versuch++) {
        const uint8_t i = g_naechsterSlot;
        g_naechsterSlot = (uint8_t)((g_naechsterSlot + 1) % MAX_SLOTS);

        const Slot& s = g_cfg->slots[i];
        if (!s.enabled || s.url[0] == 0) {
            continue;
        }

        SlotLaufzeit& l = g_laufzeit[i];
        // Nach Fehlschlaegen wird der Abstand groesser (siehe abrufIntervallMs): Ein
        // dauerhaft totes Ziel haelt das Geraet sonst bei jedem Durchlauf erneut an.
        const uint32_t intervallMs = abrufIntervallMs(s.refreshSec, l.fehlversuche);
        if (l.letzterVersuchMs != 0 && !elapsed(millis(), l.letzterVersuchMs, intervallMs)) {
            continue;
        }

        int status = 0;
        // Gemeinsamer statischer Puffer statt Stack: Der Task-Stack fasst nur 4096 Byte,
        // und waehrend dieses Aufrufs laufen darunter noch Netz-Stack und JSON-Parser.
        char fehler[FEHLER_LEN];
        if (!abrufen(s.url, status, g_koerper, sizeof(g_koerper), fehler, sizeof(fehler))) {
            fehlschlagVermerken(i, fehler);
            return;
        }

        char roh[WERT_LEN];
        if (!extractValue(g_koerper, s.field, roh, sizeof(roh), fehler, sizeof(fehler))) {
            fehlschlagVermerken(i, fehler);
            return;
        }

        formatValue(l.wert, WERT_LEN, roh, s.decimals);

        float zahl = 0.0F;
        l.zustand = parseNumber(roh, zahl) ? evalThresholds(zahl, s.thr) : STATE_OK;

        l.hatDaten = true;
        l.fehlversuche = 0;
        l.fehler[0] = 0;
        l.letzterErfolgMs = millis();
        l.letzterVersuchMs = l.letzterErfolgMs;
        return;  // pro Durchlauf nur ein Abruf
    }
}

auto SlotRuntime::probe(const char* url, int& httpStatus, char* preview, size_t previewSize,
                        char* fehler, size_t fehlerSize) -> bool {
    if (!slotUrlValid(url)) {
        setErr(fehler, fehlerSize, "Adresse ungueltig (nur http://)");
        return false;
    }
    return abrufen(url, httpStatus, preview, previewSize, fehler, fehlerSize);
}

auto SlotRuntime::scratchPuffer() -> char* {
    // Fuer den Testabruf der API: derselbe Puffer wie im Betrieb (serielle Ausfuehrung,
    // siehe g_koerper) -- ein eigener 1-KB-Puffer dort waere reine RAM-Verschwendung.
    return g_koerper;
}

auto SlotRuntime::alarmSeite() -> uint8_t {
    if (g_cfg == nullptr) {
        return 0;
    }
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        const Slot& s = g_cfg->slots[i];
        if (!s.enabled || s.url[0] == 0) {
            continue;
        }
        // Ein veralteter Wert loest keinen Alarm aus -- sonst schlaegt ein
        // Netzwerkausfall faelschlich als Grenzwertueberschreitung durch.
        if (g_laufzeit[i].hatDaten && !veraltet(i) && g_laufzeit[i].zustand == STATE_ALARM) {
            return s.page;
        }
    }
    return 0;
}
