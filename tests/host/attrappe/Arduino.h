// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Minimaler Arduino-Ersatz fuer die Host-Tests: nur String, und nur so viel davon, wie
// die geprueften Quellen und ArduinoJson tatsaechlich benutzen.
#include <cstddef>
#include <cstdint>
#include <string>

class String {
   public:
    String() = default;
    String(const char* s) : s_(s == nullptr ? "" : s) {}          // NOLINT(runtime/explicit)
    explicit String(unsigned long v) : s_(std::to_string(v)) {}   // NOLINT(runtime/int)
    explicit String(uint32_t v) : s_(std::to_string(v)) {}
    explicit String(int v) : s_(std::to_string(v)) {}

    auto operator=(const char* s) -> String& {
        s_ = (s == nullptr ? "" : s);
        return *this;
    }
    // ArduinoJsons Writer<String> baut die Ausgabe stueckweise ueber concat().
    auto concat(const char* s) -> bool {
        if (s != nullptr) {
            s_ += s;
        }
        return true;
    }
    auto operator+(const String& rhs) const -> String { return String((s_ + rhs.s_).c_str()); }
    auto operator+(const char* rhs) const -> String {
        return String((s_ + (rhs == nullptr ? "" : rhs)).c_str());
    }
    auto operator==(const char* rhs) const -> bool { return s_ == (rhs == nullptr ? "" : rhs); }
    auto operator!=(const char* rhs) const -> bool { return !(*this == rhs); }

    auto c_str() const -> const char* { return s_.c_str(); }
    auto length() const -> size_t { return s_.size(); }
    auto charAt(size_t i) const -> char { return s_[i]; }
    void reserve(size_t n) { s_.reserve(n); }
    auto toInt() const -> long { return s_.empty() ? 0 : std::stol(s_); }  // NOLINT(runtime/int)

   private:
    std::string s_;
};

inline auto operator+(const char* lhs, const String& rhs) -> String {
    return String(lhs) + rhs;
}
