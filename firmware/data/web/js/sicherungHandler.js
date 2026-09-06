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
    ///   1. ZUERST alle vorhandenen Werte loeschen -- das Geraet prueft ein neues Layout
    ///      gegen den Bestand (auch gegen abgeschaltete Werte) und weist es ab, wenn ein
    ///      Wert auf einem Platz liegt, den es nicht mehr gibt. Und ein alter Wert
    ///      blockierte sonst seinen Platz ("Rasterplatz ist bereits belegt").
    ///   2. Dann die globalen Einstellungen -- darin steckt das Layout je Seite, gegen
    ///      das die Slot-Pruefung beim Anlegen den Rasterplatz prueft.
    ///   3. Dann die gesicherten Werte anlegen.
    /// (Bis v0.3.0 kamen die Einstellungen zuerst; eine Sicherung mit engerem Layout als
    /// der Bestand brach damit im ersten Schritt ab -- Audit 05.09.2026, N5.)
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
        // EIN Aufruf für die ganze Sicherung: Das Gerät prüft sie, übernimmt sie und
        // schreibt einmal in den Flash. Vorher liefen bis zu fünfundzwanzig Aufrufe
        // (löschen, Einstellungen, jeden Wert einzeln), von denen jeder schrieb — und
        // zwischen zweien war der Bestand halb entfernt und halb angelegt.
        this.fortschritt = "Sicherung wird übernommen …";
        const r = await apiFetch("/api/v1/slots/restore", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(daten),
        });
        if (r.status === 404) {
          // Ältere Firmware kennt den Weg noch nicht (z. B. im Fenster zwischen
          // Firmware- und Dateisystem-Update). Dann der alte Weg, Wert für Wert.
          await this.einspielenEinzeln(daten);
          return;
        }
        const d = await r.json().catch(() => ({}));
        if (!r.ok || d.ok === false) {
          this.fehler = "Die Sicherung wurde abgelehnt: " + (ergebnisVon(r, d).text || r.status) +
                        " — es wurde nichts geändert.";
          return;
        }
        this.meldung = `${d.werte ?? 0} Werte wiederhergestellt.`;
      } catch (e) {
        this.fehler = "Einspielen fehlgeschlagen.";
      } finally {
        this.laeuft = false;
        this.fortschritt = "";
      }
    },

    // Rückfall für Firmware vor v0.5.0: Wert für Wert, wie bisher.
    async einspielenEinzeln(daten) {
      this.fortschritt = "Alte Werte werden entfernt …";
      const rAlt = await apiFetch("/api/v1/slots");
      if (!rAlt.ok) {
        this.fehler = "Das Gerät hat den Bestand nicht herausgegeben — es wurde nichts geändert.";
        return;
      }
      const dAlt = await rAlt.json();
      for (let i = 0; i < dAlt.slots.length; i++) {
        if (dAlt.slots[i].url) {
          await apiFetch(`/api/v1/slots/${i}`, { method: "DELETE" });
        }
      }

      this.fortschritt = "Einstellungen werden übernommen …";
      const rSet = await apiFetch("/api/v1/slots/settings", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(daten),
      });
      const dSet = await rSet.json().catch(() => ({}));
      if (!rSet.ok || dSet.ok === false) {
        this.fehler =
          "Die Einstellungen wurden abgelehnt: " + (ergebnisVon(rSet, dSet).text || rSet.status) +
          " — die alten Werte sind bereits entfernt, die gesicherten wurden nicht angelegt.";
        return;
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
          abgelehnt.push(`${s.label || "Wert " + (i + 1)}: ${ergebnisVon(r, d).text || r.status}`);
        }
      }

      this.meldung = `${angelegt} Werte wiederhergestellt.`;
      if (abgelehnt.length) {
        this.fehler = "Nicht übernommen — " + abgelehnt.join(" · ");
      }
    },
  };
}
