#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# EIN Einstiegspunkt fuer die geraetelose Testkette: Host-Tests (C++-Logik) plus
# Syntaxpruefung der Oberflaeche (check_web.sh). Vorher existierten die Host-Tests
# nur als Copy-Paste-Schleife in der README -- nach der eigenen Lehre "Was niemand
# ausfuehrt, ist nicht geprueft" gehoert die ganze Kette in ein ausfuehrbares Skript.
# (Die Builds -- pio run / pio run -t buildfs -- bleiben eigene Schritte, weil sie
# die PlatformIO-Toolchain brauchen; dieses Skript kommt mit c++, node, python3 aus.)
set -euo pipefail
BASIS="$(cd "$(dirname "$0")/.." && pwd)"
ARDUINOJSON="$BASIS/firmware/.pio/libdeps/esp12e/ArduinoJson/src"

if [ ! -d "$ARDUINOJSON" ]; then
    echo "FEHLER: ArduinoJson nicht gefunden ($ARDUINOJSON) — einmal 'pio run' bauen."
    exit 1
fi

# Mit Sanitizern und Warnungen als Fehler: Die Logik dieses Projekts rechnet mit
# rohen Puffern (char[24], memcpy, snprintf). Ein Ueberlauf faellt auf dem Host sonst
# gar nicht auf und schlaegt erst auf dem Geraet zu -- dort ohne Fehlermeldung.
for t in util extract config httpcache abbild; do
    c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -fno-omit-frame-pointer -I "$ARDUINOJSON" \
        "$BASIS/tests/host/test_$t.cpp" -o "/tmp/smalltv-$t"
    "/tmp/smalltv-$t"
done
echo "Host-Tests: 5x OK (mit Sanitizern)"

# Fliesskomma-printf zieht rund 4 KB Programmspeicher nach sich; ein post-Skript
# haelt es draussen (scripts/strip_float_printf.py). Kommt es zurueck, faellt es hier
# auf statt erst bei der naechsten Speicherknappheit (N24).
NM="$HOME/.platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-nm"
ELF="$BASIS/firmware/.pio/build/esp12e/firmware.elf"
if [ -x "$NM" ] && [ -f "$ELF" ]; then
    if "$NM" "$ELF" | grep -q "_printf_float"; then
        echo "FEHLER: Fliesskomma-printf ist wieder im Abbild."
        exit 1
    fi
    echo "Abbild: kein Fliesskomma-printf"
fi

"$BASIS/tools/check_web.sh"

# Jede Seite so laden, wie der Browser es taete: alle eingebundenen Skripte in einen
# Kontext, dann jede x-data-Komponente aufbauen. Die Syntaxpruefung oben saehe eine
# Seite nicht, die ein Skript gar nicht einbindet oder eine Komponente nennt, die es
# nicht gibt (N23).
node "$BASIS/tests/web/test_seiten.mjs"

# Die Tests der Update-Erkennung laufen gegen die echten Abbilder aus dist/. Fehlt der
# Ordner, meldeten sie frueher einen unverstaendlichen Dateifehler (N14).
if [ ! -d "$BASIS/dist" ]; then
    echo "FEHLER: dist/ fehlt — erst 'tools/paket.sh' laufen lassen."
    exit 1
fi

# Verhaltenstest der Werte-Seite: was schickt sie wirklich ans Geraet?
# Die Syntaxpruefung oben sagt darueber nichts.
node "$BASIS/tests/web/test_slots_payload.mjs"

# Assistent: ein abgeschalteter Wert ist nicht frei (N4). Anmeldehinweis auf jeder Seite (B7).
node "$BASIS/tests/web/test_slots_assistent.mjs"
node "$BASIS/tests/web/test_anmeldung_hinweis.mjs"
# Eingabepruefung wie am Geraet: Bytes statt Zeichen, nur darstellbare Zeichen (N6).
node "$BASIS/tests/web/test_slots_eingabe.mjs"
# Zustandszeile des Geraets auf der Werte-Seite (E11).
node "$BASIS/tests/web/test_geraetzeile.mjs"

# Erkennung der Abbild-Art auf der Update-Seite -- gegen die echten Abbilder
# aus dist/, weil ein Fehlgriff hier den falschen Flash-Bereich beschreibt.
node "$BASIS/tests/web/test_ota_erkennung.mjs"

# Die Pruefsumme, die die Update-Seite selbst rechnet -- gegen Node und die echten Abbilder.
node "$BASIS/tests/web/test_md5.mjs"

# Sichern/Wiederherstellen laeuft gegen die echten Endpunkte -- dafuer braucht es den
# Mock. Er wird hier gestartet und danach zuverlaessig wieder beendet (auch im Fehlerfall).
# Laeuft schon ein Mock (etwa aus einem anderen Fenster), spraeche der Test mit
# DESSEN Zustand -- und der hier gestartete stuerbe still am belegten Port (H5).
if lsof -iTCP:8099 -sTCP:LISTEN >/dev/null 2>&1; then
    echo "FEHLER: Port 8099 ist belegt — laeuft bereits ein Mock?"
    exit 1
fi
python3 "$BASIS/tools/mock_api.py" >/dev/null 2>&1 &
MOCK_PID=$!
trap 'kill $MOCK_PID 2>/dev/null || true' EXIT
for _ in $(seq 1 30); do
    curl -s -o /dev/null -m 1 http://127.0.0.1:8099/api/v1/slots && break
    sleep 0.2
done
node "$BASIS/tests/web/test_sicherung.mjs"

# Cache-Regeln der Oberflaeche -- ebenfalls gegen den Mock, weil nur dort echte
# HTTP-Kopfzeilen entstehen.
node "$BASIS/tests/web/test_cache.mjs"

# Das Geraet muss die Abbild-Art selbst pruefen -- gegen den Mock, weil dafuer eine
# echte HTTP-Anfrage mit multipart-Koerper noetig ist.
node "$BASIS/tests/web/test_ota_ablehnung.mjs"
kill $MOCK_PID 2>/dev/null || true
trap - EXIT
