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
# Mit JEDEM verfuegbaren Compiler: clang und gcc warnen unterschiedlich, und mit -Werror
# heisst das, dass ein Fehler bei nur einem von beiden auffaellt. Genau so ist v0.4.2 in
# der CI gescheitert, waehrend lokal (nur clang) alles gruen war.
UEBERSETZER=""
for c in c++ g++ clang++; do
    if command -v "$c" >/dev/null 2>&1; then
        # Dieselbe Binaerdatei nicht zweimal: "c++" ist auf macOS clang++, auf Linux g++.
        pfad="$(command -v "$c")"
        ziel="$(readlink "$pfad" 2>/dev/null || echo "$pfad")"
        case " $UEBERSETZER " in
            *" $ziel "*) ;;
            *) UEBERSETZER="$UEBERSETZER $ziel" ;;
        esac
    fi
done
if [ -z "$UEBERSETZER" ]; then
    echo "FEHLER: kein C++-Compiler gefunden."
    exit 1
fi

for CXX in $UEBERSETZER; do
    for t in util extract config httpcache abbild; do
        "$CXX" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
            -fno-omit-frame-pointer -I "$ARDUINOJSON" \
            "$BASIS/tests/host/test_$t.cpp" -o "/tmp/smalltv-$t"
        "/tmp/smalltv-$t"
    done
    # Der SlotStore braucht zusaetzlich die Dateisystem-Attrappe (tests/host/attrappe):
    # Er ist die einzige Stelle, an der ueber den VERBLEIB der Einrichtung entschieden
    # wird -- fehlende Datei, kaputte Datei, Rueckfall auf die Zweitschrift. Bis v0.5.5
    # hat kein Test diese Entscheidungen je ausgefuehrt.
    "$CXX" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -fno-omit-frame-pointer -I "$ARDUINOJSON" -I "$BASIS/firmware/include" \
        -I "$BASIS/tests/host/attrappe" \
        "$BASIS/tests/host/test_slotstore.cpp" -o /tmp/smalltv-slotstore
    /tmp/smalltv-slotstore
    # SecureStorage entscheidet, ob der Sektor mit den WLAN-Zugangsdaten ueberschrieben
    # wird -- das einzige Exemplar auf dem Geraet. Bis v0.5.6 tat er das bei JEDEM
    # Lesefehler, und kein Test hat es je bemerkt.
    "$CXX" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -fno-omit-frame-pointer -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -I "$ARDUINOJSON" \
        -I "$BASIS/firmware/include" -I "$BASIS/tests/host/attrappe" \
        "$BASIS/tests/host/test_securestorage.cpp" -o /tmp/smalltv-securestorage
    /tmp/smalltv-securestorage
    echo "Host-Tests: 7x OK mit $(basename "$CXX") (Sanitizer, Warnungen als Fehler)"
done

# EINE Autoritaet fuer die Grenzwerte: Die Firmware druckt sie, der Mock haelt sie als
# Tabelle, die Oberflaeche bekommt sie vom Geraet. Laufen Firmware und Mock auseinander,
# lehnte das Geraet ab, was der Mock durchwinkt -- und der Test verbaerge genau das (B8).
c++ -std=c++17 -Wall -Wextra -Werror -I "$ARDUINOJSON" \
    "$BASIS/tests/host/limits_dump.cpp" -o /tmp/smalltv-limits
FIRMWARE_LIMITS="$(/tmp/smalltv-limits)"
MOCK_LIMITS="$(cd "$BASIS" && python3 -c 'import json,sys; sys.path.insert(0,"tools"); import mock_api; print(json.dumps(mock_api.LIMITS))')"
if ! python3 -c "import json,sys; a=json.loads(sys.argv[1]); b=json.loads(sys.argv[2]); sys.exit(0 if a==b else 1)" \
        "$FIRMWARE_LIMITS" "$MOCK_LIMITS"; then
    echo "FEHLER: Grenzwerte von Firmware und Mock laufen auseinander."
    echo "  Firmware: $FIRMWARE_LIMITS"
    echo "  Mock:     $MOCK_LIMITS"
    exit 1
fi
echo "Grenzwerte: Firmware und Mock stimmen ueberein"

# Die Kennung des Dateisystems ersetzt seit v0.5.0 die Firmware-Version in der
# Cache-Kennung. Sie muss sich bei jeder Inhaltsaenderung aendern -- auch bei gleicher
# Dateigroesse -- und sonst gleich bleiben (N20).
python3 "$BASIS/tests/tools/test_fs_kennung.py"

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
# Der Geraetekasten auf der Werte-Seite: Adresse, WLAN, Firmware, Uhrzeit, Speicher (E11).
node "$BASIS/tests/web/test_geraetekasten.mjs"

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

# EIN Antwortformat (D5): jeder Endpunkt liefert ok und message, und die Oberflaeche
# versteht zusaetzlich die beiden alten Formate -- das traegt das Fenster zwischen
# Firmware- und Dateisystem-Update.
node "$BASIS/tests/web/test_antwortformat.mjs"

# Der Mock darf im Geraetekasten nicht weniger liefern als die Firmware. Die Feldliste
# wird dafuer aus handleGeraet() GELESEN, nicht gepflegt -- sonst faellt ein neues
# Firmware-Feld niemandem auf (genau so entstand die Luecke bei v0.5.5).
python3 "$BASIS/tests/tools/test_geraet_paritaet.py"

# Cache-Regeln der Oberflaeche -- ebenfalls gegen den Mock, weil nur dort echte
# HTTP-Kopfzeilen entstehen.
node "$BASIS/tests/web/test_cache.mjs"

# Das Geraet muss die Abbild-Art selbst pruefen -- gegen den Mock, weil dafuer eine
# echte HTTP-Anfrage mit multipart-Koerper noetig ist.
node "$BASIS/tests/web/test_ota_ablehnung.mjs"
kill $MOCK_PID 2>/dev/null || true
trap - EXIT
