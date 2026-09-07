# HTTP-Schnittstelle

Alles, was die Weboberfläche kann, geht über diese Endpunkte — sie ist nur ein Client wie
jeder andere. **Dieses Dokument ist die maßgebliche Beschreibung.** Die OpenAPI-Datei des
Ursprungsprojekts ist entfallen: Sie wurde aus Quelltext-Kommentaren erzeugt, die niemand
gepflegt hat.

Basis: `http://<geräte-ip>/`. Nur HTTP. Antworten sind JSON.

## Ein Antwortformat

Jede Antwort trägt dieselben zwei Felder:

| Feld | Bedeutung |
|---|---|
| `ok` | `true` oder `false` — hat es geklappt? |
| `message` | derselbe Sachverhalt in Worten, deutsch |

Dazu kommen die fachlichen Felder des jeweiligen Endpunkts (`slots`, `rotation`,
`ntp_server` …). Der HTTP-Status sagt dasselbe wie `ok`, mit **einer** Ausnahme: Der
Abschluss eines Updates antwortet auch bei einem abgelehnten Abbild mit 200, die Ablehnung
steht in `ok`.

Bis v0.4.3 gab es drei Formate nebeneinander: `{status, message}` aus der Basis-Firmware,
`{ok, error}` aus diesem Projekt und beim Update einen englischen Satz im Feld `status`.
Die Oberfläche musste je Endpunkt wissen, welches kommt.

**Übergangsweise** schicken die Antworten zusätzlich die alten Felder mit: `status`
(`"ok"`/`"error"`, bei Zwischenständen auch `"rebooting"`, `"cancelling"`, `"gestartet"`,
`"connected"`) und im Fehlerfall `error`. Grund: Zwischen dem Flashen der Firmware und dem
des Dateisystems läuft die alte Oberfläche auf der neuen Firmware. Diese Felder
verschwinden, sobald dieses Fenster Geschichte ist. **Neuer Code liest `ok` und `message`.**

`GET /api/v1/geraet` liefert alles, was die Übersicht über das Gerät zeigt, in einem
leichten Aufruf: `version`, `freeHeap`, `heapFrag`, `uptimeSec`, `verbunden`, `rssi`,
`ssid`, `ip`, `zeitOk`, `zeitStatus`, `zeitzone`, `rotation`, `werte`/`maxWerte`,
`seiten`/`maxSeiten`. Bewusst getrennt von `/api/v1/slots/status`: Der ruft die
Datenquellen ab, dieser nicht.

Seit v0.5.5 kommen Angaben zum **Speicherzustand** dazu: `resetGrund` (trennt einen
Einschaltvorgang von Watchdog oder Unterspannung), `flashEcht`/`flashKonfiguriert` (weichen
sie ab, passt das Flash-Layout nicht zur Hardware), `fsGesamt`/`fsBelegt` und ein Objekt
`dateien` mit `slots`, `slotsSicherung`, `slotsDefekt`, `slotsTemp` und `config`. Eine Zahl
ist die Dateigröße, `null` heißt **gibt es nicht** — „fehlt" und „ist leer" sind zwei
verschiedene Befunde.

`GET /api/v1/dateien` (seit v0.5.6) beantwortet die Frage, was tatsächlich auf dem
Dateisystem liegt: `dateien[]` (Name und Größe je Datei im Wurzelverzeichnis), `ordner[]`
(Name, Anzahl und Summe je Unterordner — `/web` einzeln aufzuführen wäre auf diesem Chip
verschwendeter Speicher), dazu `anzahl`, `summe`, `fsGesamt`, `fsBelegt`, `blockGroesse`
und `ueberhang`. Der `ueberhang` ist `fsBelegt - summe`: Liegt der belegte Platz deutlich
über der Summe aller Dateien, steckt im Flash ein Block, den das Dateisystem nicht mehr
zuordnet. Da jede Datei auf ganze Blöcke aufgerundet wird, ist ein kleiner Überhang normal
— die Bewertung bleibt beim Menschen, das Gerät liefert die Zahlen.

`POST /api/v1/slots/restore` nimmt eine komplette Sicherung entgegen (dasselbe Dokument,
das `GET /api/v1/slots` liefert), prüft sie, übernimmt sie und schreibt **einmal**. Vorher
lief eine Wiederherstellung über bis zu 25 Einzelaufrufe, von denen jeder schrieb — und
zwischen zweien war der Bestand halb entfernt und halb angelegt.

`GET /api/v1/ntp/config` und `POST` darauf tragen neben `ntp_server` eine `zeitzone` als
POSIX-Regel (`CET-1CEST,M3.5.0,M10.5.0/3`). Sie ist freiwillig: Wer das Feld wegläßt,
behält seine Einstellung, eine leere Angabe setzt auf Mitteleuropa zurück. Die Regel wirkt
sofort, ohne Neustart, und der Nachtmodus rechnet damit.

`GET /api/v1/slots/status` liefert neben `slots[]` ein Objekt `geraet` mit dem Zustand des
Geräts: `version`, `freeHeap` (Bytes), `heapFrag` (Prozent), `uptimeSec`, `rssi` (dBm).
`version` ist die verlässliche Auskunft über den Firmware-Stand. Bis v0.4.3 ließ er sich
nur aus der Cache-Kennung der Oberfläche ablesen — die hängt seit v0.5.0 am **Inhalt** des
Dateisystems (`"<kennung>-<dateigröße>"`), damit eine unveränderte Oberfläche nach einem
Firmware-Update im Browser gültig bleibt und eine geänderte, gleich große Datei nicht mehr
als unverändert durchgeht.

`POST /api/v1/ntp/sync` **stößt den Abgleich nur an** und antwortet sofort mit
`{"status": "gestartet"}`; das Ergebnis steht kurz darauf in `GET /api/v1/ntp/status`
(`lastOk`, `lastStatus`, `lastSyncTime`). Bis v0.3.2 wartete der Aufruf bis zu fünf
Sekunden und meldete das Ergebnis selbst — in dieser Zeit stand das Gerät still.

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
| `GET` | `/api/v1/dateien` | Verzeichnis des Dateisystems (Diagnose) |
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
