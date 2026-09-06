<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Herkunft dieser Firmware

Diese Firmware ist ein Fork von
[Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware)
(Stand v1.5.0, GPL-3.0). Daher auch die Lizenz: Sie ist **nicht wählbar**, das Copyleft
der Grundlage gilt weiter. Der Lizenztext liegt in [`../LICENSE`](../LICENSE); neue
Dateien tragen `// SPDX-License-Identifier: GPL-3.0-or-later`.

## Was übernommen wurde

| Bereich | Zustand |
|---|---|
| Gerätezugriff (Display über Arduino_GFX, Hintergrundbeleuchtung, SPI) | übernommen, Konstanten nach `include/hardware/Pins.h` verschoben |
| Webserver-Gerüst (`Webserver`, statische Auslieferung) | übernommen, um ETag- und Cache-Regeln erweitert |
| Rettungsmodus (Startzähler, Notfall-Update) | übernommen, gehärtet (Zähler-Reset, Abbildprüfung, Rückgabewerte) |
| WLAN und Zeitabgleich | übernommen, um AP-Rückweg und echte Erfolgsmeldung erweitert |
| Passwortschutz | übernommen, aber **optional** gemacht — ab Werk ist das Gerät offen |
| Verschlüsselter Kleinspeicher (`SecureStorage`) | unverändert übernommen |

## Was dieses Projekt hinzufügt

Der eigentliche Zweck: Werte per HTTP aus dem Heimnetz holen, umrechnen und als Kacheln
anzeigen (`slots/`, `config_codec.h`, `smalltv_util.h`, `value_extract.h`), die deutsche
Weboberfläche dafür, die gerätelose Testkette und der Mock, der das ganze Gerät ersetzt.

## Was entfernt wurde

Bilder- und GIF-Anzeige, Wetter- und Börsenanzeigen, das RGB-Testbild beim Start sowie
die Entwicklungsumgebung des Ursprungsprojekts (Devcontainer, VS-Code-Einstellungen,
Git-Hooks, Docker-Bauskript). Die OpenAPI-Beschreibung samt Generator ist ebenfalls
entfallen: Sie wurde aus Kommentaren erzeugt, die niemand pflegte.
[`docs/API.md`](../docs/API.md) ist die maßgebliche Beschreibung der Schnittstelle.
