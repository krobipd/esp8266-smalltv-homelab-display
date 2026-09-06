// SPDX-License-Identifier: GPL-3.0-or-later
// Bedienlogik der Slot-Seite: Uebersicht, Assistent zum Anlegen, Formular zum Aendern.
//
// Der "Testen"-Knopf laesst bewusst das GERAET abrufen (POST /api/v1/slots/test) und
// nicht den Browser: Nur so ist bewiesen, dass das Display die URL erreicht. Ein vom
// Browser ausgefuehrter Test saehe auch dann gruen aus, wenn das Geraet keinen Zugang hat.

// Schriftstufen wie in der Firmware (config_codec.h): rohe GFX-Stufen, keine
// Auswahl aus klein/mittel/gross. Wert, Beschriftung und Einheit sind unabhaengig.
const MIN_TEXT_STUFE = 1;
const MAX_TEXT_STUFE = 10;
const STD_WERT_SIZE = 3;
const STD_LABEL_SIZE = 2;
const STD_UNIT_SIZE = 2;

// Eine Stufe entspricht auf dem Geraet 8 Punkten Hoehe. Die Vorschau ist so breit
// wie das Display (240 Punkte), also wird 1:1 in Pixel gerechnet -- damit zeigt sie
// wirklich, was hinterher zu sehen ist, statt einer freundlichen Schaetzung.
function stufeZuPx(stufe) {
  const n = Number(stufe);
  if (!Number.isFinite(n) || n < MIN_TEXT_STUFE || n > MAX_TEXT_STUFE) return 8;
  return n * 8;
}

function stufeOderStandard(v, standard) {
  if (v === undefined || v === null || v === "") return standard;
  const n = Number(v);
  return Number.isFinite(n) ? n : standard;
}

// Das Geraet speichert Farben als RGB565 (16 bit), der Farbwaehler im Browser
// arbeitet mit Hex. Beide Richtungen werden hier umgerechnet.
function rgb565ZuHex(v) {
  const r = ((v >> 11) & 0x1f) * 255 / 31;
  const g = ((v >> 5) & 0x3f) * 255 / 63;
  const b = (v & 0x1f) * 255 / 31;
  const h = (n) => Math.round(n).toString(16).padStart(2, "0");
  return `#${h(r)}${h(g)}${h(b)}`;
}

function hexZuRgb565(hex) {
  const m = /^#?([0-9a-f]{6})$/i.exec(hex || "");
  if (!m) return 0xffff;
  const n = parseInt(m[1], 16);
  const r = (n >> 16) & 0xff;
  const g = (n >> 8) & 0xff;
  const b = n & 0xff;
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

function leererEntwurf() {
  return {
    index: null,
    enabled: true,
    url: "",
    field: "",
    label: "",
    unit: "",
    decimals: 0,
    refreshSec: 30,
    page: 1,
    pos: 0,
    wertSize: STD_WERT_SIZE,
    labelSize: STD_LABEL_SIZE,
    unitSize: STD_UNIT_SIZE,
    // Vorgabe: neben dem Wert. Als eigene Zeile rutscht die Einheit bei einem Balken
    // unter diesen -- weit weg von der Zahl, zu der sie gehoert.
    einheitDaneben: true,
    colorHex: "#e2e8f0",
    anzeige: 0,
    barMin: 0,
    barMax: 100,
    warnAbove: "",
    alarmAbove: "",
    warnBelow: "",
    alarmBelow: "",
  };
}

function slotsHandler() {
  return {
    config: null,
    status: [],
    geraet: null,
    laden: true,
    fehler: "",
    meldung: "",

    // Assistent bzw. Formular
    modus: null, // null | "assistent" | "aendern"
    schritt: 1,
    entwurf: leererEntwurf(),
    testLaeuft: false,
    testErgebnis: null, // {ok, httpStatus, preview, fields}

    async init() {
      await this.configLaden();
      await this.statusLaden();
      this.laden = false;
      // Werte regelmaessig nachziehen, solange kein Formular offen ist. Nicht im
      // Hintergrund-Tab (unnoetige Dauerlast auf dem Geraet) und nie ueberholend
      // (bei einem langsamen Abruf wuerden sich Anfragen sonst stapeln).
      setInterval(() => {
        if (!this.modus && !document.hidden) this.statusLaden();
      }, 5000);
    },

    // Ein 401 ist kein Serverfehler, sondern eine fehlende Anmeldung -- und muss auch
    // so benannt werden. Sonst sucht man den Fehler bei der Konfiguration.
    nichtAngemeldet(r) {
      if (r.status === 401) {
        this.fehler = "Anmeldung nötig — bitte zuerst unter „Passwortschutz“ anmelden.";
        return true;
      }
      return false;
    },

    async configLaden() {
      try {
        const r = await apiFetch("/api/v1/slots");
        if (this.nichtAngemeldet(r)) return;
        if (!r.ok) {
          this.fehler = "Konfiguration konnte nicht gelesen werden.";
          return;
        }
        const d = await r.json();
        if (!d || !Array.isArray(d.slots)) {
          this.fehler = "Unerwartete Antwort beim Lesen der Konfiguration.";
          return;
        }
        this.config = d;
        // Startproblem des Geraets (z. B. unlesbare Konfigurationsdatei) sichtbar
        // machen -- sonst wirkt eine leere Konfiguration wie gewollt.
        if (d.warnung) {
          this.fehler = d.warnung;
        }
      } catch (e) {
        this.fehler = "Konfiguration nicht erreichbar";
      }
    },

    _statusLaeuft: false,
    // Zustand des Geräts in einer Zeile. Die Zahlen kamen früher alle zehn Sekunden ins
    // Protokoll und verdrängten dort alles andere; hier stehen sie, wo man sie sucht.
    geraetZeile() {
      const g = this.geraet;
      if (!g) return "";
      const kb = (n) => (Number(n) / 1024).toFixed(1).replace(".", ",") + " KB";
      const s = Number(g.uptimeSec) || 0;
      const dauer =
        s < 3600
          ? Math.floor(s / 60) + " min"
          : s < 86400
            ? Math.floor(s / 3600) + " h " + Math.floor((s % 3600) / 60) + " min"
            : Math.floor(s / 86400) +
              (Math.floor(s / 86400) === 1 ? " Tag " : " Tagen ") +
              Math.floor((s % 86400) / 3600) + " h";
      return (
        "Speicher frei " + kb(g.freeHeap) +
        " · Fragmentierung " + (Number(g.heapFrag) || 0) + " %" +
        " · läuft seit " + dauer +
        " · WLAN " + (Number(g.rssi) || 0) + " dBm"
      );
    },

    async statusLaden() {
      if (this._statusLaeuft) return;
      this._statusLaeuft = true;
      try {
        const r = await apiFetch("/api/v1/slots/status");
        if (this.nichtAngemeldet(r)) return;
        if (!r.ok) return;
        const d = await r.json();
        this.status = d.slots || [];
        this.geraet = d.geraet || null;
      } catch (e) {
        this.fehler = "Status nicht erreichbar";
      } finally {
        this._statusLaeuft = false;
      }
    },

    // ---------- Darstellung ----------
    belegteSlots() {
      if (!this.config) return [];
      // Auch abgeschaltete Werte auflisten -- sonst sehen sie aus wie geloescht und
      // niemand findet den Weg zurueck.
      return this.config.slots
        .map((s, i) => ({ ...s, index: i }))
        .filter((s) => s.url);
    },

    statusVon(index) {
      return this.status.find((s) => s.index === index) || null;
    },

    kachelKlasse(index) {
      if (this.config && this.config.slots[index] && !this.config.slots[index].enabled) {
        return "card stale";
      }
      const st = this.statusVon(index);
      if (!st || !st.enabled) return "card";
      // Nur "veraltet" graut aus, NICHT ein einzelner Fehlversuch: Die Firmware behält
      // den letzten guten Wert und zeigt ihn weiter an. Würde die Oberfläche hier schon
      // ausgrauen, widersprächen sich Display und Weboberfläche sichtbar.
      if (st.stale) return "card stale";
      if (st.state === "alarm") return "card alarm";
      if (st.state === "warn") return "card warn";
      return "card";
    },

    anzeigeWert(index) {
      const st = this.statusVon(index);
      if (!st || !st.enabled) return "—";
      // Ein vorhandener Wert wird auch bei einem Fehlversuch weiter gezeigt --
      // wie alt er ist, sagt die Alterszeile darunter.
      if (st.value === undefined || st.value === null) return "—";
      return st.value;
    },

    // Alter des zuletzt gelungenen Abrufs, menschenlesbar.
    alterText(index) {
      const st = this.statusVon(index);
      if (!st || !st.enabled || st.ageSec === undefined) return "";
      const a = Number(st.ageSec);
      if (a < 60) return `vor ${a} s`;
      if (a < 3600) return `vor ${Math.floor(a / 60)} min`;
      return `vor ${Math.floor(a / 3600)} h`;
    },

    // ---------- Seiteneinstellungen ----------
    zeigtEinstellungen: false,
    eRotate: 10,
    eLayout: [2, 2, 2, 2],
    eTeilung: [0, 0, 0, 0],
    eWarnHex: "#fd9a00",
    eAlarmHex: "#ff0000",
    eHell: 100,
    eHellModus: 0,
    eHellUrl: "",
    eHellField: "",
    eHellSec: 30,
    eHellAn: 100,
    eHellAus: 15,
    eNachtAn: false,
    eNachtVon: 22,
    eNachtBis: 7,
    eNachtHell: 15,

    async einstellungenOeffnen() {
      if (!this.config) {
        await this.configLaden();
        if (!this.config) return;
      }
      this.eRotate = this.config.rotateSec;
      this.eLayout = [...this.config.layout];
      // Aeltere Geraete liefern das Feld nicht -- dann gilt "automatisch" (0).
      this.eTeilung = (this.config.teilung ?? []).map((t) => Number(t) || 0);
      while (this.eTeilung.length < this.eLayout.length) this.eTeilung.push(0);
      this.eWarnHex = rgb565ZuHex(this.config.colorWarn);
      this.eAlarmHex = rgb565ZuHex(this.config.colorAlarm);
      this.eHell = this.config.helligkeit ?? 100;
      this.eHellModus = this.config.hellModus ?? 0;
      this.eHellUrl = this.config.hellUrl ?? "";
      this.eHellField = this.config.hellField ?? "";
      this.eHellSec = this.config.hellSec ?? 30;
      this.eHellAn = this.config.hellAn ?? 100;
      this.eHellAus = this.config.hellAus ?? 15;
      this.eNachtAn = this.config.nachtAn ?? false;
      this.eNachtVon = this.config.nachtVon ?? 22;
      this.eNachtBis = this.config.nachtBis ?? 7;
      this.eNachtHell = this.config.nachtHelligkeit ?? 15;
      this.fehler = "";
      this.zeigtEinstellungen = true;
    },

    plaetzeProLayout(l) {
      return Number(l) === 0 ? 1 : Number(l) === 1 ? 2 : 4;
    },

    /// Wie viele Plätze die aktuelle Einstellung insgesamt bietet.
    plaetzeGesamt() {
      return this.eLayout.reduce((n, l) => n + this.plaetzeProLayout(l), 0);
    },

    /// Seiten, auf denen mindestens ein Wert liegt -- nur die werden angezeigt.
    genutzteSeiten() {
      if (!this.config) return 0;
      const seiten = new Set();
      this.config.slots.forEach((s) => {
        if (s.enabled && s.url) seiten.add(s.page);
      });
      return seiten.size;
    },

    /// Wartezeit, bis ein bestimmter Wert wieder an der Reihe ist.
    umlaufSek() {
      const n = this.genutzteSeiten();
      if (n <= 1 || Number(this.eRotate) === 0) return 0;
      return n * Number(this.eRotate);
    },

    async einstellungenSpeichern() {
      this.fehler = "";
      const r = Number(this.eRotate);
      if (r !== 0 && (r < 3 || r > 3600)) {
        this.fehler = "Wechselintervall: 0 zum Abschalten, sonst 3 bis 3600 Sekunden.";
        return;
      }
      if (Number(this.eHellModus) === 1) {
        if (!this.eHellUrl.trim().startsWith("http://")) {
          this.fehler = "Der Schalter braucht eine Adresse, die mit http:// beginnt.";
          return;
        }
        const hs = Number(this.eHellSec);
        if (hs < 5 || hs > 3600) {
          this.fehler = "Schalter-Intervall: 5 bis 3600 Sekunden.";
          return;
        }
      }
      try {
        const antwort = await apiFetch("/api/v1/slots/settings", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            rotateSec: r,
            layout: this.eLayout.map(Number),
            teilung: this.eTeilung.map(Number),
            colorWarn: hexZuRgb565(this.eWarnHex),
            colorAlarm: hexZuRgb565(this.eAlarmHex),
            helligkeit: Number(this.eHell),
            hellModus: Number(this.eHellModus),
            hellUrl: this.eHellUrl.trim(),
            hellField: this.eHellField.trim(),
            hellSec: Number(this.eHellSec),
            hellAn: Number(this.eHellAn),
            hellAus: Number(this.eHellAus),
            nachtAn: !!this.eNachtAn,
            nachtVon: Number(this.eNachtVon),
            nachtBis: Number(this.eNachtBis),
            nachtHelligkeit: Number(this.eNachtHell),
          }),
        });
        if (this.nichtAngemeldet(antwort)) return;
        const d = await antwort.json();
        if (!d.ok) {
          this.fehler = d.error || "Speichern abgelehnt";
          return;
        }
        this.meldung = "Einstellungen gespeichert.";
        setTimeout(() => (this.meldung = ""), 2500);
        this.zeigtEinstellungen = false;
        await this.configLaden();
      } catch (e) {
        this.fehler = "Das Gerät hat das Speichern nicht bestätigt.";
      }
    },

    // ---------- Assistent ----------
    // Ohne geladene Konfiguration (z. B. nicht angemeldet beim Seitenaufruf) darf der
    // Knopf nicht still abstürzen: einmal nachladen, sonst bleibt die Meldung
    // aus configLaden() ("Nicht angemeldet ...") sichtbar stehen.
    async neuerSlot() {
      if (!this.config) {
        await this.configLaden();
        if (!this.config) return;
      }
      // Frei ist nur ein Platz ohne Adresse. Ein abgeschalteter Wert ist eingerichtet und
      // bleibt es -- ihn als frei zu nehmen hiesse, ihn beim Speichern still zu ueberschreiben.
      const frei = this.config.slots.findIndex((s) => !s.url);
      if (frei < 0) {
        this.fehler = "Alle Slots sind belegt — zuerst einen löschen.";
        return;
      }
      this.entwurf = leererEntwurf();
      this.entwurf.index = frei;
      this.entwurf.pos = this.ersterFreierPlatz(1);
      this.testErgebnis = null;
      this.schritt = 1;
      this.modus = "assistent";
    },

    slotAendern(index) {
      const s = this.config.slots[index];
      this.entwurf = {
        index,
        enabled: s.enabled,
        url: s.url,
        field: s.field,
        label: s.label,
        unit: s.unit,
        decimals: s.decimals,
        refreshSec: s.refreshSec,
        page: s.page,
        pos: s.pos,
        wertSize: s.wertSize ?? STD_WERT_SIZE,
        labelSize: s.labelSize ?? STD_LABEL_SIZE,
        unitSize: s.unitSize ?? STD_UNIT_SIZE,
        einheitDaneben: s.einheitDaneben !== false,
        colorHex: rgb565ZuHex(s.color),
        anzeige: s.anzeige ?? 0,
        barMin: s.barMin ?? 0,
        barMax: s.barMax ?? 100,
        warnAbove: s.warnAbove ?? "",
        alarmAbove: s.alarmAbove ?? "",
        warnBelow: s.warnBelow ?? "",
        alarmBelow: s.alarmBelow ?? "",
      };
      this.testErgebnis = null;
      this.modus = "aendern";
    },

    abbrechen() {
      this.modus = null;
      this.testErgebnis = null;
      this.fehler = "";
    },

    maxPos(seite) {
      // Aus plaetzeProLayout abgeleitet statt als zweite Layout-Tabelle -- zwei
      // Tabellen koennten bei einem neuen Layout auseinanderlaufen.
      const l = this.config ? this.config.layout[seite - 1] : 2;
      return this.plaetzeProLayout(l) - 1;
    },

    ersterFreierPlatz(seite) {
      // JEDER eingerichtete Slot belegt seinen Platz, auch ein abgeschalteter --
      // dieselbe Regel wie in der Firmware. Vorher schlug der Assistent Plaetze
      // abgeschalteter Slots als frei vor, und der Nutzer erfuhr erst beim
      // Speichern in Schritt 5 von der Kollision.
      const belegt = this.config.slots
        .filter((s, i) => s.url && s.page === seite && i !== this.entwurf.index)
        .map((s) => s.pos);
      for (let p = 0; p <= this.maxPos(seite); p++) {
        if (!belegt.includes(p)) return p;
      }
      return 0;
    },

    async testen() {
      this.fehler = "";
      this.testErgebnis = null;
      if (!this.entwurf.url.startsWith("http://")) {
        this.fehler = "Die URL muss mit http:// beginnen (verschlüsselte Verbindungen kann das Gerät nicht).";
        return;
      }
      this.testLaeuft = true;
      try {
        const r = await apiFetch("/api/v1/slots/test", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ url: this.entwurf.url }),
        });
        if (this.nichtAngemeldet(r)) return;
        this.testErgebnis = await r.json();
        // Antwort ohne JSON-Struktur: Feldauswahl entfaellt, die Antwort selbst ist der Wert
        if (this.testErgebnis.ok && (this.testErgebnis.fields || []).length === 0) {
          this.entwurf.field = "";
        }
      } catch (e) {
        this.testErgebnis = { ok: false, error: "Das Gerät konnte die Adresse nicht abrufen." };
      } finally {
        this.testLaeuft = false;
      }
    },

    feldWaehlen(f) {
      this.entwurf.field = f;
    },

    // Wert, wie er nach dem Test aussehen wuerde -- fuer die Vorschau
    vorschauWert() {
      const t = this.testErgebnis;
      if (!t || !t.ok || !t.preview) return "1234";
      let roh = t.preview.trim();
      if (this.entwurf.field) {
        try {
          const d = JSON.parse(t.preview);
          if (d && typeof d === "object" && this.entwurf.field in d) {
            roh = String(d[this.entwurf.field]);
          }
        } catch (e) {
          /* Vorschau bleibt beim Rohtext */
        }
      }
      const z = parseFloat(roh);
      if (!isNaN(z) && String(z) === roh.trim()) {
        return z.toFixed(this.entwurf.decimals);
      }
      return roh.slice(0, 12);
    },

    // Anteil fuer die Balkenvorschau -- entspricht barFraction() der Firmware.
    // Streng wie parseNumber() der Firmware: "12abc" ist KEINE 12. parseFloat()
    // wuerde den Zahlen-Praefix nehmen und die Vorschau zeigte einen Balken,
    // den das Geraet nie zeichnen wuerde.
    vorschauAnteil() {
      const text = String(this.vorschauWert()).trim();
      if (!/^[+-]?(\d+(\.\d*)?|\.\d+)$/.test(text)) return 0;
      const z = parseFloat(text);
      const min = Number(this.entwurf.barMin);
      const max = Number(this.entwurf.barMax);
      if (!(max > min)) return 0;
      if (z <= min) return 0;
      if (z >= max) return 1;
      return (z - min) / (max - min);
    },

    zeigtBalken() {
      return Number(this.entwurf.anzeige) !== 0;
    },

    zeigtZahl() {
      return Number(this.entwurf.anzeige) !== 1;
    },

    vorschauPx(feld) {
      return stufeZuPx(this.entwurf[feld]);
    },

    // Die Einheit steht nur dann neben dem Wert, wenn es beides ueberhaupt gibt --
    // ohne Zahl (reiner Balken) oder ohne Einheit ist nichts danebenzustellen.
    // Dieselbe Bedingung wie in SlotDisplay.cpp; laufen die beiden auseinander,
    // zeigt die Vorschau etwas anderes als das Display.
    einheitNebenWert() {
      return (
        this.entwurf.einheitDaneben !== false &&
        this.zeigtZahl() &&
        String(this.entwurf.unit || "").trim() !== ""
      );
    },

    weiter() {
      if (this.schritt === 1 && !(this.testErgebnis && this.testErgebnis.ok)) {
        this.fehler = "Bitte zuerst die Adresse testen — sonst weiß niemand, ob das Gerät sie erreicht.";
        return;
      }
      if (this.schritt === 4 && this.zeigtBalken() &&
          !(Number(this.entwurf.barMax) > Number(this.entwurf.barMin))) {
        this.fehler = "Das Balken-Ende muss größer als der Anfang sein.";
        return;
      }
      this.fehler = "";
      this.schritt = Math.min(5, this.schritt + 1);
    },

    zurueck() {
      this.fehler = "";
      this.schritt = Math.max(1, this.schritt - 1);
    },

    zahlOderNull(v) {
      if (v === "" || v === null || v === undefined) return null;
      const n = parseFloat(v);
      return isNaN(n) ? null : n;
    },

    // Dieselben Regeln wie im Assistenten. Vorher prüfte nur der Assistent, sodass beim
    // Ändern eines Slots erst die rohe Meldung des Geräts zurückkam.
    entwurfGueltig() {
      const e = this.entwurf;
      if (!String(e.url || "").startsWith("http://")) {
        this.fehler = "Die URL muss mit http:// beginnen (verschlüsselte Verbindungen kann das Gerät nicht).";
        return false;
      }
      if (byteLaenge(e.url) > 127) {
        this.fehler = "Die URL ist zu lang (höchstens 127 Zeichen).";
        return false;
      }
      // Das Gerät zählt Bytes (UTF-8): Umlaute und ° belegen zwei. Und es zeichnet nur,
      // was sein Zeichensatz hat -- alles andere lehnt es beim Speichern ab. Die Meldung
      // soll deshalb schon hier stehen, nicht erst als rohe Antwort des Geräts.
      // Keine Pflicht-Beschriftung: Ein einzelner, offensichtlicher Wert braucht
      // keine Überschrift. Zu lang darf sie weiterhin nicht sein.
      const label = String(e.label || "");
      const unit = String(e.unit || "");
      const field = String(e.field || "");
      const fremd = " enthält ein Zeichen, das das Display nicht kennt. Möglich: Buchstaben, Ziffern, Satzzeichen, Umlaute, ß und °.";
      if (byteLaenge(label) > 23) {
        this.fehler = "Die Beschriftung ist zu lang (höchstens 23 Zeichen, Umlaute und ° zählen doppelt).";
        return false;
      }
      if (!DISPLAY_ZEICHEN.test(label)) {
        this.fehler = "Die Beschriftung" + fremd;
        return false;
      }
      if (byteLaenge(unit) > 15) {
        this.fehler = "Die Einheit ist zu lang (höchstens 15 Zeichen, Umlaute und ° zählen doppelt).";
        return false;
      }
      if (!DISPLAY_ZEICHEN.test(unit)) {
        this.fehler = "Die Einheit" + fremd;
        return false;
      }
      // Der Feldname wird nie gezeichnet: nur die Länge und keine Steuerzeichen.
      if (byteLaenge(field) > 31) {
        this.fehler = "Der Feldname ist zu lang (höchstens 31 Zeichen).";
        return false;
      }
      if (!OHNE_STEUERZEICHEN.test(field)) {
        this.fehler = "Der Feldname enthält ein Steuerzeichen.";
        return false;
      }
      if (Number(e.anzeige) !== 0 && !(Number(e.barMax) > Number(e.barMin))) {
        this.fehler = "Das Balken-Ende muss größer als der Anfang sein.";
        return false;
      }
      if (Number(e.pos) > this.maxPos(Number(e.page))) {
        this.fehler = "Diesen Rasterplatz gibt es im Layout der gewählten Seite nicht.";
        return false;
      }
      return true;
    },

    async speichern() {
      this.fehler = "";
      if (!this.entwurfGueltig()) return;
      const e = this.entwurf;
      const nutzlast = {
        index: e.index,
        // Frueher fest auf true: Ein Wert liess sich nur loeschen, nie voruebergehend
        // abschalten -- und jedes Aendern hat ihn wieder eingeschaltet.
        enabled: e.enabled !== false,
        url: e.url.trim(),
        field: (e.field || "").trim(),
        label: e.label.trim(),
        unit: (e.unit || "").trim(),
        decimals: Number(e.decimals) || 0,
        // Leer = nicht angefasst (alter Slot): Vorgabe. Eine eingetragene 0 bleibt 0 und
        // wird vom Geraet abgelehnt -- nicht still durch 30 ersetzt.
        refreshSec:
          e.refreshSec === "" || e.refreshSec === null || e.refreshSec === undefined
            ? 30
            : Number(e.refreshSec),
        page: Number(e.page) || 1,
        pos: Number(e.pos) || 0,
        // Eine eingetragene 0 bleibt eine 0 -- sie ist ungueltig und soll vom Geraet
        // als Fehler zurueckkommen, statt still zum Standard zu werden. Abgefangen wird
        // nur der Fall "gar kein Wert" (alter Slot ohne diese Felder, unverbundenes
        // Auswahlfeld): Number(undefined) waere NaN, im JSON null, und das Geraet
        // meldete einen Fehler fuer etwas, das der Nutzer nie angefasst hat.
        wertSize: stufeOderStandard(e.wertSize, STD_WERT_SIZE),
        labelSize: stufeOderStandard(e.labelSize, STD_LABEL_SIZE),
        unitSize: stufeOderStandard(e.unitSize, STD_UNIT_SIZE),
        einheitDaneben: e.einheitDaneben !== false,
        color: hexZuRgb565(e.colorHex),
        anzeige: Number(e.anzeige) || 0,
        barMin: Number(e.barMin) || 0,
        // Kein "|| 100": eine eingetragene 0 ist ein gültiger Wert und würde sonst
        // stillschweigend zu 100 -- der Balken zeigte dann etwas anderes als eingestellt.
        barMax: Number.isFinite(Number(e.barMax)) ? Number(e.barMax) : 100,
        warnAbove: this.zahlOderNull(e.warnAbove),
        alarmAbove: this.zahlOderNull(e.alarmAbove),
        warnBelow: this.zahlOderNull(e.warnBelow),
        alarmBelow: this.zahlOderNull(e.alarmBelow),
      };
      try {
        const r = await apiFetch("/api/v1/slots", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(nutzlast),
        });
        if (this.nichtAngemeldet(r)) return;
        const d = await r.json();
        if (!d.ok) {
          this.fehler = d.error || "Speichern abgelehnt";
          return;
        }
        this.meldung = "Gespeichert.";
        setTimeout(() => (this.meldung = ""), 2500);
        this.modus = null;
        await this.configLaden();
        await this.statusLaden();
      } catch (err) {
        this.fehler = "Das Gerät hat das Speichern nicht bestätigt.";
      }
    },

    /// Wert an- oder abschalten, ohne ihn zu verlieren.
    async umschalten(index) {
      const s = this.config.slots[index];
      this.slotAendern(index);
      this.entwurf.enabled = !s.enabled;
      await this.speichern();
    },

    async loeschen(index) {
      if (!confirm("Diesen Slot wirklich löschen?")) return;
      try {
        const r = await apiFetch(`/api/v1/slots/${index}`, { method: "DELETE" });
        if (this.nichtAngemeldet(r)) return;
        const d = await r.json();
        if (!d.ok) {
          this.fehler = d.error || "Löschen abgelehnt";
          return;
        }
        this.modus = null;
        await this.configLaden();
        await this.statusLaden();
      } catch (e) {
        this.fehler = "Das Gerät hat das Löschen nicht bestätigt.";
      }
    },
  };
}
