// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TESTS_HOST_PRUEFE_H
#define TESTS_HOST_PRUEFE_H

// Eigene Pruefung statt assert(): assert wird mit -DNDEBUG zu nichts -- die Tests
// liefen dann durch, ohne irgendetwas zu pruefen, und meldeten trotzdem "OK".
// Diese Fassung haengt an keinem Schalter und nennt Datei, Zeile und Bedingung.
#include <cstdio>
#include <cstdlib>

#define PRUEFE(bedingung)                                                            \
    do {                                                                             \
        if (!(bedingung)) {                                                          \
            fprintf(stderr, "FEHLGESCHLAGEN %s:%d: %s\n", __FILE__, __LINE__,        \
                    #bedingung);                                                     \
            exit(1);                                                                 \
        }                                                                            \
    } while (0)

#endif  // TESTS_HOST_PRUEFE_H
