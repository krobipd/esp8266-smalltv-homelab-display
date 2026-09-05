# Bedienung

Alles läuft über die Weboberfläche des Geräts. Sie ist auf Deutsch, ohne Fachjargon, und
braucht kein Konto.

## Erstinbetriebnahme

Das Gerät hängt nach dem Aufspielen **nicht** im Heimnetz — es macht sein eigenes WLAN auf.

1. WLAN-Liste am Handy oder Laptop → **`GeekMagic`** → Passwort **`smalltv123`**
   („kein Internet" ist normal).
2. `http://192.168.4.1` öffnen → Seite **WLAN** → Heimnetz auswählen, Passwort eintragen.
3. Das Gerät verbindet sich und zeigt seine Adresse an. Von da an ist es unter dieser
   Adresse im Heimnetz erreichbar.

Das eigene WLAN geht nur auf, solange kein bekanntes Netz erreichbar ist — es ist der
Rückweg, wenn sich am Heimnetz etwas ändert.

## Einen Wert einrichten

Seite **Werte** → *Neuer Wert*. Der Assistent führt durch fünf Schritte:

**1. Adresse.** Die URL, unter der der Wert steht. Nur `http://`.

```
ioBroker, nackter Wert:  http://<host>:8082/rest-api/v1/state/<objekt-id>/plain
ioBroker, als JSON:      http://<host>:8082/rest-api/v1/state/<objekt-id>
```

Dann **Testen** drücken. Wichtig: Das **Gerät** ruft die Adresse ab, nicht der Browser. Nur
so ist bewiesen, dass das Display die Quelle erreicht — ein Test aus dem Browser sähe auch
dann grün aus, wenn das Gerät keinen Zugang hat. Der Test zeigt die Antwort und, bei JSON,
die gefundenen Felder.

**2. Feld.** Bei `/plain` leer lassen — die ganze Antwort ist der Wert. Bei JSON den
Feldnamen eintragen (bei der ioBroker-rest-api: `val`). Verschachtelte Felder werden nicht
unterstützt.

**3. Beschriftung und Einheit.** Beschriftung darf leer bleiben — ein einzelner,
offensichtlicher Wert braucht keine Überschrift. Die Einheit fasst 15 Zeichen und wird gern
als kleiner Zusatz benutzt (`% (Woche)`).

**4. Darstellung.**

| Einstellung | Wirkung |
|---|---|
| Darstellung | nur Zahl · nur Balken · Balken mit Zahl |
| Balken-Anfang / -Ende | Skala des Balkens |
| Schriftgröße Wert / Beschriftung / Einheit | je 1–10, **voneinander unabhängig** |
| Einheit neben dem Wert | an: `42 %` in einer Zeile · aus: Einheit in eigener Zeile darunter |
| Farbe | Normalfarbe des Werts |

Die Vorschau daneben ist so groß wie das Display und rechnet 1:1 — was dort passt, passt
auch auf dem Gerät.

**5. Schwellen.** Warnung und Alarm, jeweils nach oben und nach unten. Leer heißt: keine
Grenze. Ein Alarm färbt den Wert und **zieht seine Seite nach vorn**, damit man ihn sieht,
ohne auf den Seitenwechsel zu warten.

## Seiten und Wechsel

Seite **Werte** → *Seiten & Wechsel*:

- **Wechselintervall** — wie lange eine Seite steht (0 = kein Wechsel).
- **Aufteilung je Seite** — 1 großer Wert · 2 Werte übereinander · 4 Werte.
- **Höhe der zwei Hälften** (nur bei „2 Werte übereinander"):
  - *automatisch* (Vorgabe) — die Seite teilt sich im Verhältnis der Inhaltshöhen, und die
    hängen nur an den eingestellten **Schriftgrößen**, nicht am gemessenen Wert. Die
    Aufteilung springt also nicht, wenn aus 9 % eine 100 % wird.
  - *fest* — 30/40/50/60/70 %. Keine Hälfte fällt unter 20 %.
- **Warn- und Alarmfarbe** gelten global, nicht je Wert.

Ein Wert liegt auf einer Seite und einem Platz. Wird eine Seite verkleinert (z. B. von 4 auf
2 Werte), lehnt das Gerät die Änderung ab, solange dort noch ein Wert auf einem Platz liegt,
den es danach nicht mehr gäbe — statt ihn stillschweigend zu verschieben.

## Helligkeit und Nacht

Seite **Werte** → *Seiten & Wechsel*:

- **Fester Wert** (0–100 %), oder
- **über einen Datenpunkt geschaltet**: eine URL, die „an" oder „aus" liefert (Zahl, `true`,
  `on`, …), mit eigenem Abrufintervall und je einer Helligkeit für an und aus. Damit dimmt
  das Display z. B. mit dem Licht im Raum.
- **Nachtmodus** — ab Stunde X bis Stunde Y eine eigene Helligkeit; 0 schaltet das Display
  aus.

## Wenn ein Wert veraltet

Bleibt ein Abruf aus, behält das Gerät den letzten guten Wert, markiert ihn aber als
**veraltet** und zeigt sein Alter („vor 5min"). Ein Balken ohne belastbaren Wert bleibt
leer, und statt einer Zahl steht ein Strich — eine fehlgeschlagene Abfrage darf nie wie ein
Messwert aussehen.

Nach mehreren Fehlversuchen fragt das Gerät seltener nach, damit eine tote Quelle nicht die
ganze Schleife bremst.

## Sichern und Wiederherstellen

Seite **Update**: *Sichern* lädt die komplette Einrichtung als Datei herunter,
*Wiederherstellen* spielt sie zurück. Seit Firmware v0.3.0 bleibt die Einrichtung bei einem
Dateisystem-Update von selbst erhalten — die Firmware schreibt sie ins neue Dateisystem
zurück. Läuft noch eine ältere Firmware, **vor dem Dateisystem-Update sichern**. WLAN und
Passwort überleben ohnehin.

## Passwortschutz

Seite **Passwortschutz**. Ab Werk ist die Oberfläche **offen** — im eigenen Netz ist das
eine Entscheidung des Nutzers, kein Versäumnis. Ein Passwort schaltet den Schutz ein, ein
leeres Passwort hebt ihn wieder auf. Vergessen? Der Rettungsmodus der Basis-Firmware setzt
es zurück.

## Weitere Seiten

| Seite | Inhalt |
|---|---|
| **WLAN** | Netz suchen, verbinden, Status |
| **Zeit** | NTP-Server, letzter Abgleich |
| **Drehung** | Bildschirmausrichtung (0–7) |
| **Protokoll** | Meldungen des Geräts, auch als Datei |
| **Update** | Firmware/Oberfläche aufspielen, Sichern/Wiederherstellen, Neustart |
