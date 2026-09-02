// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sicherung und Wiederherstellung der Einstellungen.
//
// Anlass: Ein Dateisystem-Update ersetzt die ganze Partition und bringt selbst keine
// Konfiguration mit -- /slots.json ist danach weg. Wer die Oberflaeche aktualisiert,
// verliert also alle eingerichteten Werte. Diese Seite ist die Gegenmassnahme und
// sitzt deshalb bewusst NEBEN dem Update, nicht in einer Ecke der Werte-Seite.
//
// Bewusst ohne neuen Endpunkt gebaut: Sichern ist ein GET auf /api/v1/slots,
// Einspielen sind die vorhandenen POST/DELETE-Routen. Damit ist diese Funktion ein
// reines Oberflaechen-Update -- kein Firmware-Flash noetig, und genau das ist bei
// einem Geraet ohne Bootloader-Rueckfall der Unterschied, auf den es ankommt.

function sicherungHandler() {
  return {
    laeuft: false,
    meldung: "",
    fehler: "",
    fortschritt: "",
    dateiName: "",

    /// Die aktuelle Konfiguration als Datei herunterladen.
    async sichern() {
      this.fehler = "";
      this.meldung = "";
      try {
        const r = await apiFetch("/api/v1/slots");
        if (this.nichtAngemeldet && this.nichtAngemeldet(r)) return;
        if (!r.ok) {
          this.fehler = "Das Gerät hat die Einstellungen nicht herausgegeben.";
          return;
        }
        const daten = await r.json();
        // Zeitstempel im Dateinamen, damit mehrere Sicherungen nebeneinander liegen
        // koennen -- eine einzige "sicherung.json" waere nach dem zweiten Mal weg.
        const t = new Date();
        const zwei = (n) => String(n).padStart(2, "0");
        const name =
          `smalltv-einstellungen-${t.getFullYear()}${zwei(t.getMonth() + 1)}` +
          `${zwei(t.getDate())}-${zwei(t.getHours())}${zwei(t.getMinutes())}.json`;

        const blob = new Blob([JSON.stringify(daten, null, 2)], {
          type: "application/json",
        });
        const url = URL.createObjectURL(blob);
        const a = document.createElement("a");
        a.href = url;
        a.download = name;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);

        const belegt = (daten.slots || []).filter((s) => s.url).length;
        this.meldung = `Gesichert: ${name} (${belegt} eingerichtete Werte).`;
      } catch (e) {
        this.fehler = "Sichern fehlgeschlagen.";
      }
    },

    dateiGewaehlt() {
      this.fehler = "";
      this.meldung = "";
      const f = this.$refs.sicherungDatei;
      this.dateiName = f && f.files.length ? f.files[0].name : "";
    },

    /// Eine Sicherung zurueckspielen.
    ///
    /// Reihenfolge ist nicht beliebig:
    ///   1. Globale Einstellungen ZUERST -- darin steckt das Layout je Seite, und die
    ///      Slot-Pruefung weist einen Rasterplatz ab, den das aktuelle Layout nicht hat.
    ///   2. Dann alle vorhandenen Slots loeschen -- sonst blockiert ein alter Wert den
    ///      Platz ("Rasterplatz ist bereits belegt") und die Sicherung ginge nur halb ein.
    ///   3. Dann die gesicherten Werte anlegen.
    async einspielen() {
      this.fehler = "";
      this.meldung = "";
      const f = this.$refs.sicherungDatei;
      if (!f || !f.files.length) {
        this.fehler = "Bitte zuerst eine Sicherungsdatei auswählen.";
        return;
      }

      let daten;
      try {
        daten = JSON.parse(await f.files[0].text());
      } catch (e) {
        this.fehler = "Die Datei ist keine gültige Sicherung (kein lesbares JSON).";
        return;
      }
      if (!daten || !Array.isArray(daten.slots) || !Array.isArray(daten.layout)) {
        this.fehler =
          "Die Datei sieht nicht wie eine Sicherung dieses Geräts aus " +
          "(es fehlen die Felder „slots\" und „layout\"). Es wurde nichts geändert.";
        return;
      }

      this.laeuft = true;
      try {
        this.fortschritt = "Einstellungen werden übernommen …";
        const rSet = await apiFetch("/api/v1/slots/settings", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(daten),
        });
        const dSet = await rSet.json().catch(() => ({}));
        if (!rSet.ok || dSet.ok === false) {
          this.fehler =
            "Die Einstellungen wurden abgelehnt: " +
            (dSet.error || rSet.status) +
            " — es wurde nichts weiter geändert.";
          return;
        }

        this.fortschritt = "Alte Werte werden entfernt …";
        const rAlt = await apiFetch("/api/v1/slots");
        const dAlt = await rAlt.json();
        for (let i = 0; i < dAlt.slots.length; i++) {
          if (dAlt.slots[i].url) {
            await apiFetch(`/api/v1/slots/${i}`, { method: "DELETE" });
          }
        }

        let angelegt = 0;
        const abgelehnt = [];
        for (let i = 0; i < daten.slots.length; i++) {
          const s = daten.slots[i];
          if (!s.url) continue;
          this.fortschritt = `Wert ${angelegt + 1} wird angelegt …`;
          const r = await apiFetch("/api/v1/slots", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ ...s, index: i }),
          });
          const d = await r.json().catch(() => ({}));
          if (r.ok && d.ok !== false) {
            angelegt++;
          } else {
            // Nicht abbrechen: ein einzelner unbrauchbarer Wert darf nicht die
            // ganze Wiederherstellung verhindern. Aber melden -- still verschlucken
            // waere die schlechtere Haelfte.
            abgelehnt.push(`${s.label || "Wert " + (i + 1)}: ${d.error || r.status}`);
          }
        }

        this.meldung = `${angelegt} Werte wiederhergestellt.`;
        if (abgelehnt.length) {
          this.fehler = "Nicht übernommen — " + abgelehnt.join(" · ");
        }
      } catch (e) {
        this.fehler = "Einspielen fehlgeschlagen.";
      } finally {
        this.laeuft = false;
        this.fortschritt = "";
      }
    },
  };
}
