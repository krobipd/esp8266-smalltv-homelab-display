// SPDX-License-Identifier: GPL-3.0-or-later
//
// Der Kasten "Gerät" auf der Übersicht (index.html): Adresse, WLAN, Firmware, Laufzeit,
// Uhrzeit, Speicher und Belegung — untereinander, nach Themen gruppiert. Alles aus EINEM
// Aufruf (/api/v1/geraet); der ist bewusst getrennt vom Werte-Status, der die
// Datenquellen abruft und auf der Übersicht nichts zu suchen hat.
function geraetHandler() {
  return {
    g: null,
    fehler: "",

    async init() {
      await this.laden();
      // Selten genug, dass eine halbe Minute reicht; die Übersicht ist keine Messseite.
      setInterval(() => {
        if (!document.hidden) this.laden();
      }, 30000);
    },

    async laden() {
      try {
        const r = await apiFetch("/api/v1/geraet");
        if (r.status === 401) {
          // Ohne Anmeldung bleibt der Kasten leer; den Hinweis zeigt der Kopfbereich.
          this.g = null;
          return;
        }
        if (!r.ok) {
          this.fehler = "Gerätezustand nicht lesbar";
          return;
        }
        this.g = await r.json();
        this.fehler = "";
      } catch (e) {
        this.fehler = "Gerät nicht erreichbar";
      }
    },

    // Laufzeit in Worten: Minuten, Stunden, Tage — je nachdem, was gerade aussagt.
    dauerText(sekunden) {
      const s = Number(sekunden) || 0;
      if (s < 3600) return Math.floor(s / 60) + " min";
      if (s < 86400) {
        return Math.floor(s / 3600) + " h " + Math.floor((s % 3600) / 60) + " min";
      }
      const tage = Math.floor(s / 86400);
      return tage + (tage === 1 ? " Tag " : " Tagen ") + Math.floor((s % 86400) / 3600) + " h";
    },

    // Das Kürzel der Zeitzone aus der POSIX-Regel: "CET-1CEST,…" → "MEZ/MESZ".
    zonenName() {
      const regel = String((this.g && this.g.zeitzone) || "");
      if (!regel) return "";
      const namen = { CET: "MEZ", CEST: "MESZ", GMT: "GMT", BST: "BST", UTC: "UTC" };
      const teile = regel.match(/^([A-Za-z]+)[^A-Za-z]*([A-Za-z]+)?/);
      if (!teile) return regel;
      const eins = namen[teile[1]] || teile[1];
      const zwei = teile[2] ? namen[teile[2]] || teile[2] : "";
      return zwei && zwei !== eins ? eins + "/" + zwei : eins;
    },

    // Die Uhrzeit steht in der Meldung des Geräts und damit in DESSEN Zeitzone. Aus einem
    // Zeitstempel gerechnet stünde hier die Zeit des Betrachters — mit dem Kürzel des
    // Geräts dahinter, also falsch, sobald beide auseinanderliegen.
    uhrText() {
      const g = this.g;
      if (!g) return "";
      const treffer = String(g.zeitStatus || "").match(/(\d{4}-\d{2}-\d{2}) (\d{2}:\d{2})/);
      if (!treffer) return g.zeitOk === false ? "fehlgeschlagen" : "";
      const heute = new Date().toISOString().slice(0, 10);
      const uhr = treffer[1] === heute ? treffer[2] : `${treffer[1]} ${treffer[2]}`;
      return uhr + (g.zeitOk ? "" : " — fehlgeschlagen");
    },

    // Nach Themen gruppiert, jede Angabe in einer eigenen Zeile. Was das Gerät nicht
    // sagt, bleibt weg — lieber eine Zeile weniger als "unbekannt". Eine Gruppe ohne
    // Zeilen verschwindet mitsamt Überschrift.
    get gruppen() {
      const g = this.g;
      if (!g) return [];
      const kb = (n) => (Number(n) / 1024).toFixed(1).replace(".", ",") + " KB";
      const rohbau = [
        {
          titel: "Netz",
          zeilen: [
            { name: "Adresse", wert: g.ip },
            { name: "WLAN", wert: g.ssid ? g.ssid + (g.rssi ? `, ${g.rssi} dBm` : "") : "" },
          ],
        },
        {
          titel: "Firmware",
          zeilen: [
            { name: "Version", wert: g.version },
            { name: "Läuft seit", wert: g.uptimeSec === undefined ? "" : this.dauerText(g.uptimeSec) },
            {
              name: "Speicher frei",
              wert: g.freeHeap === undefined
                ? ""
                : `${kb(g.freeHeap)} (${Number(g.heapFrag) || 0} % fragmentiert)`,
            },
          ],
        },
        {
          titel: "Uhrzeit",
          zeilen: [
            { name: "Zuletzt gestellt", wert: this.uhrText() },
            { name: "Zeitzone", wert: this.zonenName() },
          ],
        },
        {
          titel: "Anzeige",
          zeilen: [
            { name: "Werte", wert: g.werte === undefined ? "" : `${g.werte} von ${g.maxWerte}` },
            { name: "Seiten belegt", wert: g.seiten === undefined ? "" : `${g.seiten} von ${g.maxSeiten}` },
            { name: "Drehung", wert: g.rotation === undefined ? "" : String(g.rotation) },
          ],
        },
      ];
      return rohbau
        .map((gr) => ({ titel: gr.titel, zeilen: gr.zeilen.filter((z) => z.wert !== "" && z.wert !== undefined) }))
        .filter((gr) => gr.zeilen.length > 0);
    },
  };
}
