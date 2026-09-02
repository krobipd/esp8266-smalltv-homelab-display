# Hardware und harte Grenzen

## Das Gerät

**GeekMagic SmallTV-Ultra** — ein fertiges Tischgerät mit ESP8266 (ESP-12F) und einem
240×240-IPS-Display (ST7789, über Hardware-SPI). Stromversorgung per USB-C. Die Firmware
läuft auch auf dem verwandten **HelloCubic Lite** (dieselbe Basis, anderes Gehäuse) —
getestet ist hier nur die SmallTV-Ultra.

| | |
|---|---|
| Chip | ESP8266 (ESP-12F), 80 MHz, **kein** zweiter Kern, **keine** Fließkomma-Einheit |
| Arbeitsspeicher | ~80 KB nutzbar — die Grenze, an der sich alles entscheidet |
| Flash | 4 MB, Aufteilung `eagle.flash.4m2m.ld`: ~1 MB Programm, **2.072.576 Byte Dateisystem** (LittleFS) |
| Display | 240×240, ST7789, Hardware-SPI |
| Netz | WLAN 2,4 GHz, **nur HTTP** |

## Harte Grenzen — nicht verhandelbar

**Kein HTTPS.** BearSSL braucht mehr Speicher, als der Chip hat. Es gibt keinen Trick, der
das ändert. Das Gerät gehört ins eigene Netz; wer es aus dem Internet erreichbar macht,
tut das gegen den ausdrücklichen Rat dieser Dokumentation.

**Kein Bootloader-Rollback.** Anders als beim ESP32 gibt es hier keine zweite Partition, in
die ein Update geschrieben und aus der es zurückgerollt werden könnte. Ein defektes Abbild
startet nicht. Absicherungen dagegen:

- Das Gerät prüft die **Art** eines hochgeladenen Abbilds selbst (erste 16 Bytes: `0xE9` =
  Firmware, `littlefs` ab Byte 8) und lehnt die falsche Sorte ab, **bevor** es schreibt.
- Ist das Dateisystem leer oder unlesbar, schaltet die Firmware von sich aus einen
  Notfall-Upload frei (`/legacyupdate`) — die Oberfläche kann weg sein, der Weg zurück
  bleibt.
- Der **Rettungsmodus** der Basis-Firmware erkennt wiederholte Startabbrüche und bootet in
  einen abgespeckten Zustand mit Upload-Möglichkeit. Er darf nicht gebrochen werden.

**Kein zweiter Kern.** Abruf, Anzeige und Webserver teilen sich eine Schleife. Deshalb ist
jeder Wert-Abruf zeitlich begrenzt, und die Anzeige zeichnet nur, was sich geändert hat.

**Keine Fließkomma-Einheit.** `double` zieht mehrere Kilobyte Umwandlungscode aus der
C-Bibliothek nach sich; deshalb rechnet ArduinoJson hier mit `float`
(`-DARDUINOJSON_USE_DOUBLE=0`), und ein Nachbearbeitungsschritt entfernt die
Fließkomma-Ausgabe aus `printf` (`scripts/strip_float_printf.py`).

## Speicheraufteilung im Flash

```
0x000000  Programm (~1 MB)          <- firmware.bin
0x200000  Dateisystem (2.072.576 B) <- littlefs.bin, enthaelt die Weboberflaeche
0x3FA000  EEPROM / Systembereich    <- WLAN-Zugang und Passwort ueberleben hier
```

Zwei Folgen daraus:

1. Ein **Dateisystem-Update löscht `/slots.json`** und damit alle eingerichteten Werte.
   Deshalb gibt es Sichern/Wiederherstellen auf der Update-Seite — **vorher benutzen.**
2. **WLAN-Zugang und Passwort überleben** jedes Update, weil sie nicht im Dateisystem
   liegen.

## Grenzen der Anzeige

- **4 Seiten × bis zu 4 Werte = 12 Werte.** Das ist eine Anzeige-Grenze, keine technische:
  Die Wartezeit auf einen bestimmten Wert ist Seitenzahl × Wechselintervall.
- Zu breiter oder zu hoher Text wird **an der Kachelkante abgeschnitten**, nie verkleinert.
  Wer eine Schriftstufe zu groß wählt, sieht das sofort und stellt sie kleiner — das Gerät
  rechnet keine Einstellung gegen.
- Das Grundraster der Schrift ist 6×8 Punkte je Stufe; Stufe 10 sind also 60×80 Punkte je
  Zeichen.
