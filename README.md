<div align="center">

# 📺 SmallTV Homelab-Display

**Ein 240×240-Display, das sich seine Werte selbst aus dem Homelab holt · Web-Oberfläche · OTA · kein Löten**

[![Build](https://github.com/krobipd/esp8266-smalltv-homelab-display/actions/workflows/build.yml/badge.svg)](https://github.com/krobipd/esp8266-smalltv-homelab-display/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/krobipd/esp8266-smalltv-homelab-display?sort=semver&label=Release&color=2ea44f)](https://github.com/krobipd/esp8266-smalltv-homelab-display/releases/latest)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](LICENSE)
![Platform](https://img.shields.io/badge/Platform-ESP8266-E7352C?logo=espressif&logoColor=white)
![PlatformIO](https://img.shields.io/badge/PlatformIO-Build-FF7F00?logo=platformio&logoColor=white)
![ioBroker](https://img.shields.io/badge/ioBroker-rest--api-3399CC?logo=iobroker&logoColor=white)
![OTA](https://img.shields.io/badge/Update-OTA%20im%20Browser-555555)

</div>

---

Eigene Firmware für die **GeekMagic SmallTV-Ultra** (ESP8266, 240×240). Das Display **holt
sich seine Werte selbst** per HTTP aus dem Homelab — aus ioBroker, einer eigenen API, einem
beliebigen Endpunkt, der Text oder JSON liefert — und rendert sie lokal. Eingerichtet wird
alles über eine **Web-Oberfläche auf dem Gerät**, komplett auf Deutsch.

Kein Server, der etwas hinschiebt. Kein MQTT, keine Cloud. Das Gerät fragt in seinem Takt,
zeigt an — und wenn die Quelle wegfällt, sagt es das, statt einen alten Wert als frischen
auszugeben.

## ✨ Features

- **Bis zu 12 Werte** auf **4 Seiten**, die im eingestellten Takt wechseln. Je Seite
  1 großer Wert, 2 Werte übereinander oder 4 Werte.
- **Selbst abholen** — jeder Wert hat eigene URL, eigenes Feld und eigenes Abrufintervall.
  Antwortet die Quelle mit nacktem Text, ist die ganze Antwort der Wert; bei JSON wird ein
  Feld herausgezogen.
- **Darstellung je Wert** — nur Zahl, nur Balken oder Balken mit Zahl · drei **unabhängige**
  Schriftstufen (Wert / Beschriftung / Einheit, je 1–10) · Einheit neben dem Wert oder
  darunter · freie Farbe. Beschriftung darf leer bleiben.
- **Warn- und Alarmschwellen** je Wert, oben wie unten. Ein Alarm zieht die betroffene Seite
  nach vorn — man sieht ihn, ohne auf den Seitenwechsel zu warten.
- **Ungleiche Hälften** — bei zwei Werten übereinander teilt sich die Seite automatisch nach
  den eingestellten Schriftgrößen, oder fest nach Prozent.
- **Helligkeit** — fester Wert oder über einen Datenpunkt geschaltet (Licht an → hell), dazu
  ein Nachtmodus mit eigener Stunde und Helligkeit.
- **Veraltet statt gelogen** — bleibt ein Abruf aus, wird der Wert als veraltet markiert und
  sein Alter angezeigt. Ein Balken ohne belastbaren Wert bleibt leer.
- **Update im Browser** — Firmware und Oberfläche per OTA. Die Seite **erkennt selbst**, was
  für ein Abbild hochgeladen wird, und das **Gerät prüft es noch einmal**, bevor ein Byte in
  den Flash geht.
- **Sichern & Wiederherstellen** der ganzen Einrichtung als Datei — ein Dateisystem-Update
  löscht sonst alle Werte.
- **Passwortschutz optional** — ab Werk offen, zuschaltbar, leeres Passwort hebt ihn auf.

## 🔌 Datenquellen

Das Gerät spricht **einfaches HTTP**. Alles, was auf ein GET mit einer Zahl oder mit JSON
antwortet, ist eine Quelle:

- **ioBroker** — über die **rest-api** (Extension von `web.0`):
  `http://<host>:8082/rest-api/v1/state/<objekt-id>/plain` liefert den nackten Wert; ohne
  `/plain` kommt JSON, aus dem das Feld `val` gezogen wird.
- **Eigene Dienste** — jede URL, die Text oder JSON liefert; den Feldnamen trägt man im
  Formular ein.
- **Alles andere**, sofern ein Endpunkt existiert, der einen einzelnen Wert ausgibt.

> **Kein HTTPS.** Auf diesem Chip bleibt für TLS kein Speicher übrig — bewusste, nicht
> verhandelbare Entscheidung. Das Gerät gehört ins eigene Netz, nicht ins Internet.

## 📖 Dokumentation

| Doku | Inhalt |
|---|---|
| **[docs/HARDWARE.md](docs/HARDWARE.md)** | Gerät, Chip, Speicheraufteilung, harte Grenzen |
| **[docs/BUILD.md](docs/BUILD.md)** | Bauen, Testkette, Paket schnüren, OTA-Flash |
| **[docs/USAGE.md](docs/USAGE.md)** | Erstinbetriebnahme, Werte einrichten, Seiten, Schwellen, Sicherung |
| **[docs/API.md](docs/API.md)** | HTTP-/JSON-Schnittstelle (vollständig in `firmware/swagger.yml`) |

## 🚀 Schnellstart

1. **Abbilder holen** — zwei Dateien aus dem
   [aktuellen Release](https://github.com/krobipd/esp8266-smalltv-homelab-display/releases/latest):
   `smalltv-firmware-*.bin` und `smalltv-littlefs-*.bin`.
2. **Aufspielen** — über die Update-Seite der Werksfirmware: erst die Firmware, dann das
   Dateisystem. **Ohne die zweite Datei gibt es keine Oberfläche.** Fallstricke und der
   Rückweg: **[BUILD.md](docs/BUILD.md)**.
3. **Ins WLAN** — das Gerät macht sein eigenes Netz auf (`GeekMagic`, Passwort
   `smalltv123`); dort `http://192.168.4.1` öffnen und das Heimnetz eintragen.
4. **Werte anlegen** — Seite „Werte" → *Neuer Wert*: Adresse eintragen, **testen lassen**
   (das **Gerät** ruft ab, nicht der Browser — nur so ist bewiesen, dass es die Quelle
   erreicht), dann Beschriftung, Darstellung und Schwellen: **[USAGE.md](docs/USAGE.md)**.

## 🗂️ Repo-Struktur

| Pfad | Inhalt |
|---|---|
| `firmware/src`, `firmware/include` | Firmware. Die gesamte Logik liegt in Headern (`config_codec.h`, `smalltv_util.h`, `value_extract.h`, `http_cache.h`, `abbild_art.h`) — deshalb ist sie ohne Gerät prüfbar; die `.cpp`-Schale liest und schreibt nur. |
| `firmware/data/web` | Web-Oberfläche (statisch, Alpine.js, kein Build-Schritt) |
| `tests/host` | Host-Unittests der Logik — laufen ohne Gerät, nur mit `c++` |
| `tests/web` | Verhaltenstests der Oberfläche: Sendepfad, Abbild-Erkennung, Cache-Kopfzeilen, Sichern/Wiederherstellen, Ablehnung falscher Abbilder |
| `tools/mock_api.py` | **Ersetzt das ganze Gerät** — alle Seiten, alle Endpunkte, Antwortformate 1:1. Oberfläche entwickeln ohne Hardware. |
| `tools/run_tests.sh` | die ganze gerätelose Testkette |
| `tools/paket.sh` | schnürt `dist/<version>/` aus den gebauten Abbildern, mit Prüfsummen |

## 🧪 Bauen und testen

```sh
cd firmware && pio run && pio run -t buildfs   # Firmware + Dateisystem
cd .. && tools/paket.sh                        # dist/<version>/ mit Pruefsummen
tools/run_tests.sh                             # ganze geraetelose Testkette
python3 tools/mock_api.py                      # Oberflaeche ohne Geraet: http://127.0.0.1:8099/
```

Der Mock ist bewusst **so streng wie das Gerät** — dieselben Endpunkte, dieselben
Antwortformate, dieselben Ablehnungen. Eine Testumgebung, die weniger verlangt als die
Wirklichkeit, verbirgt genau die Fehler, für die sie da ist.

## 📄 Lizenz und Herkunft

**GPL-3.0-or-later.** Diese Firmware ist ein Fork von
**[Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware)**
(GPL-3) — von dort stammen der Gerätezugriff, das Grundgerüst des Webservers, der
Rettungsmodus und der WLAN-/NTP-Teil. Alles rund um Werte, Seiten, Darstellung, Datenabruf
und die deutschsprachige Oberfläche ist neu. Die Basis wiederum steht auf der Arbeit von
[GeekMagicClock](https://github.com/GeekMagicClock/smalltv-ultra).

Nutzung auf eigenes Risiko, ohne Gewähr. Die vollständige Lizenz steht in [LICENSE](LICENSE);
jede eigene Quelldatei trägt ihren SPDX-Kopf.
