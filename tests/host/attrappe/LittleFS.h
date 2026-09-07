// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Dateisystem-Attrappe fuer die Host-Tests. KEIN Ersatz fuer LittleFS -- sie bildet
// nur die Schnittstelle nach, die SlotStore und json_datei.h benutzen, und haelt die
// Dateien im Arbeitsspeicher.
//
// Was sie prueft: die ENTSCHEIDUNGEN von SlotStore::load() -- Hauptdatei fehlt, ist
// kaputt, Zweitschrift springt ein oder eben nicht. Genau diese Logik hat am
// 07.09.2026 gefehlt und war bis dahin von keinem Test beruehrt.
//
// Was sie ausdruecklich NICHT prueft: das Verhalten des echten Flash-Speichers. Ein
// Metadaten-Commit, der Stunden ohne Strom nicht uebersteht, ist am Host nicht
// nachstellbar -- dagegen hilft nur die zweite Schreibung zu einem anderen Zeitpunkt.
#include <Arduino.h>  // String -- json_datei.h baut damit den Temporaernamen
#include <cstddef>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>

class FsAttrappe;

/// Datei als Stringstrom: ArduinoJson erkennt std::istream/std::ostream von selbst,
/// deshalb funktionieren serializeJson/deserializeJson ohne Sonderbehandlung.
class File : public std::stringstream {
   public:
    File() : gueltig_(false) {}
    File(FsAttrappe* fs, std::string pfad, bool schreiben, const std::string& inhalt)
        : std::stringstream(schreiben ? std::string() : inhalt),
          fs_(fs), pfad_(std::move(pfad)), schreiben_(schreiben), gueltig_(true),
          groesse_(schreiben ? 0 : inhalt.size()) {}
    ~File() override { close(); }

    File(const File&) = delete;
    auto operator=(const File&) -> File& = delete;

    explicit operator bool() const { return gueltig_; }
    auto operator!() const -> bool { return !gueltig_; }

    auto size() const -> size_t { return groesse_; }
    void close();

   private:
    FsAttrappe* fs_ = nullptr;
    std::string pfad_;
    bool schreiben_ = false;
    bool gueltig_ = false;
    size_t groesse_ = 0;

    friend class FsAttrappe;
};

class FsAttrappe {
   public:
    auto exists(const char* pfad) -> bool { return dateien_.count(pfad) != 0; }

    auto open(const char* pfad, const char* modus) -> File {
        const bool schreiben = (modus != nullptr && modus[0] == 'w');
        if (!schreiben && !exists(pfad)) {
            return File();
        }
        if (leseFehler_ == pfad && !schreiben) {
            return File();  // Datei da, laesst sich aber nicht oeffnen
        }
        // Bewusst direkt im return gebaut: File ist nicht kopierbar (ein Stringstrom
        // ist es nicht), und nur ein prvalue kommt ohne Kopie aus (C++17).
        return File(this, pfad, schreiben, schreiben ? std::string() : dateien_[pfad]);
    }

    auto rename(const char* von, const char* nach) -> bool {
        if (!exists(von)) {
            return false;
        }
        dateien_[nach] = dateien_[von];
        dateien_.erase(von);
        return true;
    }

    auto remove(const char* pfad) -> bool { return dateien_.erase(pfad) != 0; }

    // --- nur fuer die Tests ---
    void lege(const char* pfad, const std::string& inhalt) { dateien_[pfad] = inhalt; }
    auto inhalt(const char* pfad) -> std::string { return dateien_.count(pfad) != 0 ? dateien_[pfad] : ""; }
    void leeren() { dateien_.clear(); leseFehler_.clear(); }
    void setzeLeseFehler(const char* pfad) { leseFehler_ = pfad == nullptr ? "" : pfad; }

   private:
    std::map<std::string, std::string> dateien_;
    std::string leseFehler_;
    friend class File;
};

inline FsAttrappe LittleFS;  // NOLINT(readability-identifier-naming)

inline void File::close() {
    if (gueltig_ && schreiben_ && fs_ != nullptr) {
        fs_->dateien_[pfad_] = str();
    }
    gueltig_ = false;
}
