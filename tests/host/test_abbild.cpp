// SPDX-License-Identifier: GPL-3.0-or-later
// Host-Unit-Tests der Abbild-Erkennung. Laufen ohne Geraet und ohne Arduino.
// Die Gegenprobe an den ECHTEN Abbildern aus dist/ macht tests/web/test_ota_erkennung.mjs
// fuer dieselbe Regel in der Oberflaeche -- hier steht die Regel des Geraets.
#include "pruefe.h"
#include <cstdio>
#include <cstring>
#include "../../firmware/include/abbild_art.h"

int main() {
    // Kopf eines echten firmware.bin: 0xE9 als erstes Byte.
    const uint8_t firmware[16] = {0xE9, 0x02, 0x02, 0x40, 0x80, 0xF4, 0x10, 0x40,
                                  0x00, 0xF0, 0x10, 0x40, 0x60, 0x0D, 0x00, 0x00};
    // Kopf eines echten littlefs.bin: ab Byte 8 stehen die Zeichen "littlefs".
    const uint8_t dateisystem[16] = {0x01, 0x00, 0x00, 0x00, 0xF0, 0x0F, 0xFF, 0xF7,
                                     'l',  'i',  't',  't',  'l',  'e',  'f',  's'};

    PRUEFE(erkenneAbbild(firmware, sizeof(firmware)) == ABBILD_FIRMWARE);
    PRUEFE(erkenneAbbild(dateisystem, sizeof(dateisystem)) == ABBILD_DATEISYSTEM);

    // Nichts von beidem wird geraten -- lieber ablehnen als den falschen Bereich schreiben.
    const uint8_t fremd[16] = {'P', 'K', 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    PRUEFE(erkenneAbbild(fremd, sizeof(fremd)) == ABBILD_UNBEKANNT);
    // Zu kurz zum Erkennen: ebenfalls unbekannt, nicht "vielleicht Firmware".
    PRUEFE(erkenneAbbild(dateisystem, 15) == ABBILD_UNBEKANNT);
    PRUEFE(erkenneAbbild(nullptr, 16) == ABBILD_UNBEKANNT);

    // Die Meldungen muessen die Richtung benennen und sagen, dass nichts geschrieben wurde.
    const char* t1 = abbildFehlertext(ABBILD_DATEISYSTEM, ABBILD_FIRMWARE);
    PRUEFE(strstr(t1, "Firmware-Abbild") != nullptr);
    PRUEFE(strstr(t1, "nichts geschrieben") != nullptr);
    const char* t2 = abbildFehlertext(ABBILD_FIRMWARE, ABBILD_DATEISYSTEM);
    PRUEFE(strstr(t2, "Oberflaeche") != nullptr);
    PRUEFE(strstr(t2, "nichts geschrieben") != nullptr);
    PRUEFE(strcmp(t1, t2) != 0);
    const char* t3 = abbildFehlertext(ABBILD_UNBEKANNT, ABBILD_FIRMWARE);
    PRUEFE(strstr(t3, "weder") != nullptr);

    printf("OK\n");
    return 0;
}
