#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Statische Pruefung der Firmware (clang-tidy ueber PlatformIO).
#
# Bewusst eng eingestellt (platformio.ini, check_flags): nur die bugprone-Gruppe, also
# Muster, die wirklich zu Fehlern fuehren. Die vollen readability- und modernize-Regeln
# erzeugten hunderte Treffer aus der uebernommenen Basis-Firmware und haetten das
# Werkzeug nutzlos gemacht -- eine Liste, die niemand liest, findet nichts.
set -euo pipefail
BASIS="$(cd "$(dirname "$0")/.." && pwd)"
PIO="${PIO:-$HOME/Library/Python/3.9/bin/pio}"

if [ ! -x "$PIO" ]; then
    PIO="$(command -v pio || true)"
fi
if [ -z "$PIO" ] || [ ! -x "$PIO" ]; then
    echo "FEHLER: pio nicht gefunden. PIO=<pfad> setzen oder PlatformIO installieren."
    exit 1
fi

cd "$BASIS/firmware"
"$PIO" check --fail-on-defect high --skip-packages
