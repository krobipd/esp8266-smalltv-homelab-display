# Bauen, testen, aufspielen

## Voraussetzungen

- **PlatformIO** (`pip install platformio`) — Toolchain und ESP8266-Core holt es selbst
- `c++` (C++17), `node`, `python3` für die gerätelose Testkette
- **kein** Löten, **kein** serieller Anschluss — alles läuft über den Browser

## Bauen

```sh
cd firmware
pio run                 # Firmware  -> .pio/build/esp12e/firmware.bin
pio run -t buildfs      # Dateisystem -> .pio/build/esp12e/littlefs.bin
```

Die Version kommt aus **`firmware/VERSION`** (nicht aus git). `scripts/git_version.py`
erzeugt daraus vor jedem Build `include/project_version.h`. Die Version steckt außerdem in
der Cache-Kennung jeder ausgelieferten Datei — **wer die Oberfläche ändert, zieht die
VERSION mit**, sonst hält ein Browser die alte Fassung für gültig.

## Paket schnüren

```sh
tools/paket.sh          # -> dist/<VERSION>/ mit beiden Abbildern + SHA256SUMS.txt
```

Denselben Schritt macht die CI. Die Testkette prüft die Abbild-Erkennung gegen **genau
diese** Dateien — nicht gegen nachgebaute Kopfbytes.

## Testen (ohne Gerät)

```sh
tools/run_tests.sh
```

Die Kette in dieser Reihenfolge:

| Schritt | Was er prüft |
|---|---|
| **Host-Tests** (`tests/host/*.cpp`) | die gesamte Logik: Konfigurations-Codec, Wert-Extraktion, Schwellen, Zeitarithmetik, Kachelgeometrie, Cache-Kennungen, Abbild-Erkennung |
| **`check_web.sh`** | Syntax jeder JS-Datei und des Mocks — ein falsches Anführungszeichen legt sonst eine ganze Seite lahm, ohne dass ein Build es merkt |
| **`test_slots_payload.mjs`** | was die Werte-Seite wirklich sendet |
| **`test_ota_erkennung.mjs`** | Firmware/Dateisystem-Erkennung gegen die echten Abbilder aus `dist/` |
| **`test_sicherung.mjs`** | Sichern/Wiederherstellen gegen den Mock |
| **`test_cache.mjs`** | Cache-Kopfzeilen und `304`-Antworten |
| **`test_ota_ablehnung.mjs`** | dass das Gerät ein falsch zugeordnetes Abbild ablehnt |

Oberfläche ohne Hardware ansehen und bedienen:

```sh
python3 tools/mock_api.py     # http://127.0.0.1:8099/
```

Der Mock bildet **alle** Endpunkte und Antwortformate der Firmware nach, inklusive der
Ablehnungen. Wer einen Endpunkt in der Firmware ändert, zieht den Mock mit — ein Mock, der
weniger verlangt als das Gerät, verbirgt genau die Fehler, für die er da ist.

## Aufspielen

**Es sind immer zwei Dateien.** `firmware.bin` ist das Programm, `littlefs.bin` die
Oberfläche. Ohne die zweite gibt es keine Weboberfläche.

### Erstinstallation (Werksfirmware)

1. Mit dem WLAN des Geräts verbinden, Werks-Update-Seite öffnen
   (`http://192.168.4.1/update` bzw. `/legacyupdate`).
2. **Firmware** hochladen (`smalltv-firmware-*.bin`) → Neustart.
3. **Dateisystem** hochladen (`smalltv-littlefs-*.bin`) → Neustart.
4. `http://192.168.4.1` → Seite **WLAN** → Heimnetz eintragen.

### Update einer laufenden Installation

Seite **Update** auf dem Gerät. Sie erkennt selbst, welche Sorte Abbild man ausgewählt hat,
und zeigt es an („Erkannt: Firmware" / „Erkannt: Oberfläche (Dateisystem)"). Das Gerät prüft
es unabhängig davon noch einmal und lehnt die falsche Sorte ab, bevor es schreibt.

> ⚠️ **Firmware zuerst, dann Dateisystem.** Seit Firmware v0.3.0 schreibt das Gerät seine
> Einrichtung nach einem Dateisystem-Update selbst ins neue Dateisystem zurück. Läuft noch
> eine ältere Firmware, löscht das Dateisystem-Update `/slots.json` — dann vorher *Sichern*
> (Update-Seite). WLAN-Zugang und Passwort überleben ohnehin (die liegen im EEPROM).

> ⚠️ **Während des Schreibens nicht vom Strom trennen.** Es gibt kein Bootloader-Rollback.

### Wenn die Oberfläche weg ist

Startet das Gerät, findet aber kein brauchbares Dateisystem, schaltet die Firmware
`/legacyupdate` frei — dort lässt sich das Dateisystem erneut hochladen. Bleibt das Gerät
ganz stumm, greift der Rettungsmodus der Basis-Firmware nach mehreren gescheiterten Starts.

## Nach dem Update prüfen

```sh
python3 tools/abnahme.py <geräte-adresse> --messen --zeit --drehung --werte
```

Prüft über die Schnittstelle, was sich ohne Hand am Gerät prüfen lässt: Antwortzeiten
(kein Aussetzer über 500 ms), Zeitabgleich mit falschem und richtigem Server, Drehung hin
und zurück, eingerichtete Werte. `--speichern stand.json` vor einem Dateisystem-Update und
`--vorher stand.json` danach belegen, dass nichts verloren ging. `--wlan` (ab v0.3.0) und
`--ota <firmware.bin>` (ab v0.3.2) sind versionsgesperrt; das Skript verweigert sie sonst.
Bei aktivem Passwortschutz `--passwort` mitgeben.

## Häufige Stolpersteine

| Symptom | Ursache | Lösung |
|---|---|---|
| „Not Enough Space" bei ~79 % | Das Dateisystem-Abbild ging an den Firmware-Weg | Aktuelle Update-Seite benutzen (Browser-Cache leeren) — neuere Firmware lehnt das sofort mit klarer Meldung ab |
| Seite zeigt nur ein Upload-Formular | `littlefs.bin` fehlt | Dateisystem-Abbild aufspielen |
| Oberfläche verhält sich seltsam nach einem Update | Browser hält alte Dateien | Cache leeren oder privates Fenster; ab v0.2.3 verhindert die Firmware das von sich aus |
| `192.168.4.1` nicht erreichbar | Rechner noch im Heimnetz | WLAN wechseln — die Adresse liegt nicht im Heimnetz |
| Kein `GeekMagic`-WLAN sichtbar | Gerät hängt schon im Heimnetz | Adresse vom Display ablesen |
