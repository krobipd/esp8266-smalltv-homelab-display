// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Nur so viel WiFi, wie die Schluesselableitung braucht: eine feste MAC-Adresse.
#include <Arduino.h>

class WiFiAttrappe {
   public:
    static auto macAddress() -> String { return String("02:00:00:00:00:01"); }
};

inline WiFiAttrappe WiFi;  // NOLINT(readability-identifier-naming)
