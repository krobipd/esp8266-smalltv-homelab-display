// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// EEPROM-Attrappe: ein Byte-Feld im Arbeitsspeicher. Die ESP8266-Emulation liest und
// schreibt einen Flash-Sektor; hier zaehlen wir zusaetzlich die commit()-Aufrufe --
// jeder davon loescht am echten Geraet den Sektor, in dem als EINZIGES Exemplar die
// WLAN-Zugangsdaten liegen.
#include <cstdint>
#include <vector>

class EEPROMAttrappe {
   public:
    void begin(int groesse) {
        if (daten_.size() != static_cast<size_t>(groesse)) {
            daten_.assign(static_cast<size_t>(groesse), 0xFF);
        }
    }
    auto read(int i) -> uint8_t {
        return (i >= 0 && static_cast<size_t>(i) < daten_.size()) ? daten_[static_cast<size_t>(i)] : 0xFF;
    }
    void write(int i, uint8_t b) {
        if (i >= 0 && static_cast<size_t>(i) < daten_.size()) {
            daten_[static_cast<size_t>(i)] = b;
        }
    }
    auto commit() -> bool {
        commits++;
        return commitGelingt;
    }

    // --- nur fuer die Tests ---
    unsigned commits = 0;
    bool commitGelingt = true;
    void fuelle(uint8_t b) { daten_.assign(daten_.size(), b); }
    void setzeGroesse(size_t n, uint8_t b) { daten_.assign(n, b); }
    auto byteAt(size_t i) -> uint8_t { return i < daten_.size() ? daten_[i] : 0xFF; }

   private:
    std::vector<uint8_t> daten_;
};

inline EEPROMAttrappe EEPROM;  // NOLINT(readability-identifier-naming)
