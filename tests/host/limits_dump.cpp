// SPDX-License-Identifier: GPL-3.0-or-later
// Druckt die Grenzwerte der Firmware als JSON. run_tests.sh vergleicht die Ausgabe mit
// den Grenzwerten des Mocks -- laufen sie auseinander, lehnt das Geraet Eingaben ab,
// die der Mock durchgewunken hat (oder umgekehrt), und der Test verbirgt genau den
// Fehler, fuer den er da ist.
//   c++ -std=c++17 tests/host/limits_dump.cpp -o /tmp/limits && /tmp/limits
#include <cstdio>

#include "../../firmware/include/config_codec.h"

int main() {
    printf(
        "{\"url\": %u, \"label\": %u, \"field\": %u, \"unit\": %u, "
        "\"slots\": %u, \"pages\": %u, "
        "\"refreshMin\": %u, \"refreshMax\": %u, "
        "\"rotateMin\": %u, \"rotateMax\": %u, "
        "\"decimalsMax\": %u, \"hellSecMin\": %u, \"hellSecMax\": %u, "
        "\"textStufeMin\": %u, \"textStufeMax\": %u}\n",
        (unsigned)(URL_LEN - 1), (unsigned)(LABEL_LEN - 1), (unsigned)(FIELD_LEN - 1),
        (unsigned)(UNIT_LEN - 1), (unsigned)MAX_SLOTS, (unsigned)MAX_PAGES,
        (unsigned)REFRESH_SEC_MIN, (unsigned)REFRESH_SEC_MAX,
        (unsigned)ROTATE_SEC_MIN, (unsigned)ROTATE_SEC_MAX,
        (unsigned)DECIMALS_MAX, (unsigned)HELL_SEC_MIN, (unsigned)HELL_SEC_MAX,
        (unsigned)MIN_TEXT_STUFE, (unsigned)MAX_TEXT_STUFE);
    return 0;
}
