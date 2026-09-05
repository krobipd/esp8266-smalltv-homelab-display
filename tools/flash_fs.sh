#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Laedt das Dateisystem-Abbild aufs Geraet -- ohne Browser.
#
# Ablauf: Skript starten, DANN das WLAN auf "GeekMagic" wechseln.
# Das Skript wartet, bis das Geraet antwortet, laedt hoch und meldet das Ergebnis.
# Es braucht kein Internet.
#
#   sh tools/flash_fs.sh                      # Standard: dist/<VERSION>/smalltv-littlefs-<VERSION>.bin
#   sh tools/flash_fs.sh <datei> [<adresse>]  # andere Datei / anderes Ziel

set -eu

HIER=$(cd "$(dirname "$0")/.." && pwd)
# Standardabbild folgt firmware/VERSION -- ein fester Pfad flashte irgendwann eine alte Oberflaeche.
VERSION=$(tr -d '[:space:]' < "$HIER/firmware/VERSION")
BIN=${1:-"$HIER/dist/$VERSION/smalltv-littlefs-$VERSION.bin"}
ZIEL=${2:-"192.168.4.1"}
WARTE_MAX=300   # Sekunden, die auf das Geraet gewartet wird
LOG="$HIER/flash_fs.log"

echo "Ziel : http://$ZIEL/legacyupdate"
echo "Datei: $BIN"
echo "Log  : $LOG"
echo

[ -f "$BIN" ] || { echo "FEHLER: Datei nicht gefunden: $BIN"; exit 1; }

{
  echo "=== $(date '+%Y-%m-%d %H:%M:%S') ==="
  echo "Datei: $BIN"
} >> "$LOG"

echo ">> Jetzt das WLAN auf \"GeekMagic\" wechseln (Passwort: smalltv123)."
echo ">> Ich warte bis zu $WARTE_MAX Sekunden darauf, dass das Geraet antwortet."
echo

i=0
while [ "$i" -lt "$WARTE_MAX" ]; do
    if curl -s -m 2 -o /dev/null "http://$ZIEL/legacyupdate"; then
        echo "Geraet antwortet."
        break
    fi
    i=$((i + 2))
    printf "\r  warte ... %ss" "$i"
    sleep 2
done
echo

if [ "$i" -ge "$WARTE_MAX" ]; then
    echo "FEHLER: $ZIEL hat in $WARTE_MAX s nicht geantwortet." | tee -a "$LOG"
    echo
    echo "Pruefen:"
    echo "  1. Steht auf dem Display wirklich $ZIEL?"
    echo "     Zeigt es eine andere Adresse, diese als zweites Argument uebergeben:"
    echo "     sh tools/flash_fs.sh \"$BIN\" <adresse-vom-display>"
    echo "  2. Ist das WLAN \"GeekMagic\" wirklich verbunden?"
    echo "     networksetup -getairportnetwork en0"
    exit 1
fi

echo ">> Lade hoch. NICHT vom Strom trennen."
ANTWORT=$(curl -s --max-time 180 \
    -F "filesystem=@$BIN;filename=$(basename "$BIN")" \
    "http://$ZIEL/legacyupdate" -w '\n[HTTP %{http_code}]') || {
        echo "FEHLER: Upload abgebrochen." | tee -a "$LOG"; exit 1; }

echo "$ANTWORT" | tee -a "$LOG"
echo

case "$ANTWORT" in
    *"[HTTP 200]"*)
        echo "OK -- Upload angenommen. Das Geraet startet jetzt neu (ca. 15 s)."
        echo "Danach: http://$ZIEL aufrufen -> Seite \"WLAN\" -> eigenes Netz eintragen."
        ;;
    *)  echo "Der Server hat nicht mit 200 geantwortet -- Ausgabe oben pruefen."
        exit 1
        ;;
esac
