# HTTP-Schnittstelle

Alles, was die Weboberfläche kann, geht über diese Endpunkte — sie ist nur ein Client wie
jeder andere. Vollständig maschinenlesbar: **`firmware/swagger.yml`** (OpenAPI 3), erzeugt
aus den `@openapi`-Anmerkungen im Quelltext (`firmware/scripts/generate_openapi.py`).

Basis: `http://<geräte-ip>/`. Nur HTTP. Antworten sind JSON.

## Zwei Fehlerformate — und warum

| Herkunft | Format |
|---|---|
| Endpunkte dieses Projekts (`/slots…`) | `{"ok": false, "error": "..."}` plus HTTP-Status |
| Endpunkte der Basis-Firmware (WLAN, NTP, Passwort, OTA, Logs) | `{"status": "error", "message": "..."}` |

Das bleibt so: Die Basis umzubauen würde den Abstand zum Ursprungsprojekt unnötig
vergrößern. **Regel für neuen Code:** eigene Endpunkte nur `{ok, error}`. Wer Antworten der
Basis auswertet, prüft `res.ok` bzw. den HTTP-Status, statt Felder zu raten.

**Abschluss eines Updates** (`POST /api/v1/ota/fw` bzw. `/ota/fs`): `status` ist `ok` oder
`error`, die Einzelheiten stehen in `message`. Der HTTP-Status ist auch bei einem abgelehnten
Abbild 200 — die Ablehnung steht im Feld, nicht im Code.

**Kein CORS.** Die Schnittstelle ist für die eigene Oberfläche auf demselben Gerät gedacht
(same-origin). Freigaben für fremde Seiten gibt es nicht; `OPTIONS` wird wie jeder unbekannte
Pfad mit 404 beantwortet.

## Werte und Seiten

| Methode | Pfad | Zweck |
|---|---|---|
| `GET` | `/api/v1/slots` | komplette Konfiguration: `slots[]`, `layout[]`, `teilung[]`, Farben, Helligkeit, Nachtmodus |
| `POST` | `/api/v1/slots` | **einen** Wert anlegen/ändern (`index` im Rumpf) |
| `DELETE` | `/api/v1/slots/{index}` | einen Wert löschen |
| `POST` | `/api/v1/slots/test` | eine Adresse **vom Gerät** abrufen lassen; liefert Status, Auszug der Antwort und gefundene JSON-Felder |
| `GET` | `/api/v1/slots/status` | Laufzeitzustand je Wert: aktueller Wert, Alter, veraltet, Warn-/Alarmzustand, letzter Fehler |
| `POST` | `/api/v1/slots/settings` | Seiteneinstellungen: Wechselintervall, Layout, Aufteilung, Farben, Helligkeit, Nachtmodus |

Ein Wert (Auszug):

```json
{
  "index": 0,
  "enabled": true,
  "url": "http://192.0.2.10:8082/rest-api/v1/state/beispiel.0.wert/plain",
  "field": "",
  "label": "Auslastung",
  "unit": "%",
  "decimals": 0,
  "refreshSec": 30,
  "page": 1,
  "pos": 0,
  "wertSize": 6, "labelSize": 3, "unitSize": 3,
  "einheitDaneben": true,
  "color": 65535,
  "anzeige": 2, "barMin": 0, "barMax": 100,
  "warnAbove": 80, "alarmAbove": 95, "warnBelow": null, "alarmBelow": null
}
```

`label` (max. 23), `unit` (max. 15) und `field` (max. 31) werden in **Bytes** (UTF-8) begrenzt —
Umlaute und `°` belegen zwei. `label` und `unit` erscheinen auf dem Display und dürfen nur Zeichen
enthalten, die dessen Zeichensatz hat: ASCII sowie `° ä ö ü Ä Ö Ü ß`; alles andere wird mit
`400` abgelehnt. `field` wird nie gezeichnet, dort sind nur Steuerzeichen verboten.

Die Prüfung ist streng und **transaktional**: Wird ein Feld abgelehnt, ändert sich gar
nichts. Ein Wert außerhalb des erlaubten Bereichs wird **abgewiesen**, nicht stillschweigend
zurechtgebogen — sonst bekäme man „gespeichert" für etwas, das man nie eingestellt hat.

## Gerät

| Methode | Pfad | Zweck |
|---|---|---|
| `GET` | `/api/v1/wifi/scan` · `/api/v1/wifi/status` | Netze suchen, Verbindungszustand |
| `POST` | `/api/v1/wifi/connect` | Heimnetz eintragen |
| `GET`/`POST` | `/api/v1/ntp/config` · `/api/v1/ntp/status` · `/api/v1/ntp/sync` | Zeit |
| `GET`/`POST` | `/api/v1/display/rotation` | Bildschirmausrichtung 0–7 |
| `GET` | `/api/v1/logs` · `/api/v1/logs/download` | Protokoll |
| `POST` | `/api/v1/logs/clear` · `/api/v1/reboot` | Protokoll leeren, Neustart |
| `GET`/`POST` | `/api/v1/token/check` · `/api/v1/token/save` | Passwortschutz prüfen/setzen |

## Updates

| Methode | Pfad | Zweck |
|---|---|---|
| `POST` | `/api/v1/ota/fw` | Firmware-Abbild (multipart) |
| `POST` | `/api/v1/ota/fs` | Dateisystem-Abbild (multipart) |
| `GET` | `/api/v1/ota/status` | Fortschritt, Fehler |
| `POST` | `/api/v1/ota/cancel` | laufendes Update abbrechen |

**Das Gerät prüft die Abbild-Art selbst.** Der Weg (`/fw` oder `/fs`) kommt vom Client, der
Inhalt wird am ersten Brocken geprüft: `0xE9` als erstes Byte = Firmware, die Zeichen
`littlefs` ab Byte 8 = Dateisystem. Passt es nicht zusammen, wird abgelehnt, **bevor** ein
Byte in den Flash geht — mit einer Meldung, die sagt, was erkannt wurde und dass nichts
geschrieben wurde. Ein Client, der sich irrt, kann so nicht den falschen Flash-Bereich
überschreiben.

## Passwortschutz

Ist ein Passwort gesetzt, verlangen alle `/api/v1/…`-Endpunkte einen Kopf
`Authorization: Bearer <passwort>`; sonst kommt `401`. Ohne gesetztes Passwort ist alles
frei — die Oberfläche nennt das „Passwortschutz" und spricht bewusst nicht von Tokens.

## Auslieferung der Weboberfläche

Statische Dateien gehen mit `Cache-Control: no-cache` und einer Kennung
(`ETag: "<version>-<dateigröße>"`) raus; `If-None-Match` wird beantwortet und ergibt ein
`304` ohne Inhalt. Das ist kein Detail, sondern Bedingung dafür, dass ein Update überhaupt
ankommt: Vorher galt `max-age=86400` ohne Kennung, und ein Browser mischte nach einem Update
neues HTML mit altem JavaScript.

Ein `304` trägt bewusst **weder `Content-Type` noch `Content-Length`** — beides würde der
Browser in seinen gespeicherten Stand übernehmen und die Datei dort als leer bzw. falsch
typisiert vermerken.
