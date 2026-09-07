// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Protokoll-Attrappe: merkt sich die Zeilen, damit die Tests pruefen koennen, dass ein
// stiller Rueckfall NICHT stillschweigend passiert.
#include <string>
#include <vector>

class Logger {
   public:
    static inline std::vector<std::string> zeilen;  // NOLINT(readability-identifier-naming)

    static void info(const char* m, const char* k = nullptr) { merke("INFO", m, k); }
    static void warn(const char* m, const char* k = nullptr) { merke("WARN", m, k); }
    static void error(const char* m, const char* k = nullptr) { merke("ERROR", m, k); }

    static void leeren() { zeilen.clear(); }
    static auto enthaelt(const char* teil) -> bool {
        for (const auto& z : zeilen) {
            if (z.find(teil) != std::string::npos) return true;
        }
        return false;
    }

   private:
    static void merke(const char* stufe, const char* m, const char* k) {
        zeilen.emplace_back(std::string(stufe) + " " + (k == nullptr ? "" : k) + " " +
                            (m == nullptr ? "" : m));
    }
};
