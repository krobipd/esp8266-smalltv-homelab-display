#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Syntaxpruefung der Weboberflaeche. Gehoert zur Testkette: Die C++-Host-Tests sehen
# das JavaScript nicht, und ein einziges falsches Anfuehrungszeichen legt im Browser
# die KOMPLETTE Bedienlogik einer Seite lahm (real passiert am 01.09.2026 -- der
# Fehler lag unbemerkt im Baum, bis der erste Mensch klickte).
set -euo pipefail
cd "$(dirname "$0")/../firmware/data/web"

rc=0
for f in js/*.js; do
    if ! node --check "$f" >/dev/null 2>&1; then
        echo "SYNTAXFEHLER: $f"
        node --check "$f" || true
        rc=1
    fi
done

# Python-Syntax des Mocks gleich mit -- er ist Teil derselben Testumgebung.
python3 -c "import ast, sys; ast.parse(open(sys.argv[1]).read())" ../../../tools/mock_api.py \
    || { echo "SYNTAXFEHLER: tools/mock_api.py"; rc=1; }
# Das Abnahmeskript ebenso -- es laeuft nur gegen das Geraet, also muss wenigstens die
# Syntax vorher stimmen.
python3 -c "import ast, sys; ast.parse(open(sys.argv[1]).read())" ../../../tools/abnahme.py \
    || { echo "SYNTAXFEHLER: tools/abnahme.py"; rc=1; }
# Die Build-Skripte laufen nur unter PlatformIO -- die Syntax muss trotzdem stimmen,
# sonst bricht der Build erst beim naechsten Flash-Versuch ab.
for s in ../../scripts/*.py; do
    python3 -c "import ast, sys; ast.parse(open(sys.argv[1]).read())" "$s" \
        || { echo "SYNTAXFEHLER: $s"; rc=1; }
done

[ "$rc" -eq 0 ] && echo "Oberflaeche: Syntax OK ($(ls js/*.js | wc -l | tr -d ' ') JS-Dateien + Mock + Abnahmeskript + Build-Skripte)"
exit "$rc"
