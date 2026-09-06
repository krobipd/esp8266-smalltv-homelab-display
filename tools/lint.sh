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
# --pattern statt --skip-packages: Letzteres nahm clang-tidy auch die Include-Pfade des
# Frameworks. Jede Datei brach dann mit "Arduino.h file not found" ab, PlatformIO meldete
# trotzdem "No defects found", und der Lauf war in zweieinhalb Sekunden durch -- eine
# Pruefung, die nichts prueft und gruen meldet. Mit --pattern bleiben die Pfade, und nur
# die eigenen Quellen werden analysiert (rund 20 Sekunden).
# Der Cache wird verworfen: Sonst wiederholt PlatformIO das Ergebnis unveraenderter
# Dateien, auch wenn sich die Regelmenge geaendert hat.
rm -rf .pio/check
"$PIO" check --fail-on-defect high --pattern src --pattern include
