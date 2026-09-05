#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Packt die beiden gebauten Abbilder als Release-Paket nach dist/<VERSION>/ und legt
# Pruefsummen daneben. EIN Weg fuer lokale Releases und fuer die CI -- zwei Fassungen
# wuerden auseinanderlaufen, und die Tests der Update-Erkennung laufen gegen genau
# diese Dateien.
#
# Voraussetzung: "pio run" und "pio run -t buildfs" sind gelaufen.
set -euo pipefail
BASIS="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$BASIS/firmware/.pio/build/esp12e"
VERSION="$(tr -d '[:space:]' < "$BASIS/firmware/VERSION")"

if [ -z "$VERSION" ]; then
    echo "FEHLER: firmware/VERSION ist leer."
    exit 1
fi
for f in firmware.bin littlefs.bin; do
    if [ ! -f "$BUILD/$f" ]; then
        echo "FEHLER: $BUILD/$f fehlt — erst 'pio run' und 'pio run -t buildfs'."
        echo "Inhalt des Build-Verzeichnisses:"
        ls -la "$BUILD" 2>/dev/null || echo "  (Verzeichnis fehlt)"
        find "$BASIS/firmware" -name '*.bin' -not -path '*/backup/*' 2>/dev/null | sed 's/^/  gefunden: /'
        exit 1
    fi
done

ZIEL="$BASIS/dist/$VERSION"
mkdir -p "$ZIEL"
cp "$BUILD/firmware.bin" "$ZIEL/smalltv-firmware-$VERSION.bin"
cp "$BUILD/littlefs.bin" "$ZIEL/smalltv-littlefs-$VERSION.bin"
(cd "$ZIEL" && shasum -a 256 smalltv-*.bin > SHA256SUMS.txt)
# MD5 dazu: Die Firmware verlangt seit v0.3.2 die Pruefsumme des Abbilds (Kopfzeile
# X-Abbild-MD5). Die Update-Seite rechnet sie selbst, curl-Nutzer nehmen sie von hier.
(cd "$ZIEL" && python3 -c 'import hashlib, sys
for f in sys.argv[1:]:
    print(hashlib.md5(open(f, "rb").read()).hexdigest() + "  " + f)' smalltv-*.bin > MD5SUMS.txt)

echo "Paket: dist/$VERSION/"
ls -l "$ZIEL"
