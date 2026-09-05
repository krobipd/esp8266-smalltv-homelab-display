// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sichern und Wiederherstellen gegen den laufenden Mock -- also gegen dieselben
// Endpunkte, die auch das Geraet anbietet. Ein reiner Syntax- oder Einheitentest
// wuerde hier nichts sagen: Der Wert dieser Funktion liegt genau darin, ob die
// REIHENFOLGE stimmt (Einstellungen mit Layout zuerst, dann loeschen, dann anlegen).
// Faellt eine davon um, geht die Wiederherstellung still nur halb durch -- und das
// merkt man sonst erst, wenn die Werte schon weg sind.
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const basis = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const MOCK = process.env.MOCK_URL || "http://127.0.0.1:8099";

const pruefe = (bedingung, text) => {
  if (!bedingung) {
    console.error("FEHLGESCHLAGEN: " + text);
    process.exit(1);
  }
};

// Der Browser-Download wird abgefangen: statt einer Datei landet der Inhalt hier.
let gesichert = null;
const sandbox = {
  console,
  URL: { createObjectURL: () => "blob:test", revokeObjectURL: () => {} },
  Blob: function (teile) { gesichert = JSON.parse(teile.join("")); },
  document: {
    createElement: () => ({ click() {}, set href(_v) {}, set download(_v) {} }),
    body: { appendChild() {}, removeChild() {} },
  },
  Date,
  String,
  JSON,
  Array,
  apiFetch: (pfad, opt) => fetch(MOCK + pfad, opt),
};
sandbox.globalThis = sandbox;
vm.createContext(sandbox);
vm.runInContext(readFileSync(join(basis, "firmware/data/web/js/sicherungHandler.js"), "utf8"), sandbox);

const h = sandbox.sicherungHandler();
h.$refs = {};

// Ausgangszustand herstellen: Layout "1 grosser Wert" auf Seite 1 + ein Wert mit
// auffaelligen Schriftstufen, damit eine halbe Wiederherstellung sichtbar wird.
await fetch(MOCK + "/api/v1/slots/settings", {
  method: "POST", headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ rotateSec: 25, layout: [0, 2, 2, 2] }),
});
const original = {
  index: 0, enabled: true,
  url: "http://192.0.2.10:8082/rest-api/v1/state/x/plain",
  field: "", label: "Testwert", unit: "%", decimals: 0, refreshSec: 45,
  page: 1, pos: 0, wertSize: 9, labelSize: 4, unitSize: 3,
  color: 59230, anzeige: 2, barMin: 0, barMax: 100,
  warnAbove: 80, alarmAbove: null, warnBelow: null, alarmBelow: null,
};
const rVor = await fetch(MOCK + "/api/v1/slots", {
  method: "POST", headers: { "Content-Type": "application/json" },
  body: JSON.stringify(original),
});
pruefe((await rVor.json()).ok, "Ausgangs-Slot liess sich nicht anlegen");

// 1) Sichern
await h.sichern();
pruefe(gesichert !== null, "Sichern hat keine Datei erzeugt: " + h.fehler);
pruefe(gesichert.slots[0].label === "Testwert", "Sicherung enthaelt den Wert nicht");
pruefe(gesichert.rotateSec === 25, "Sicherung enthaelt die globalen Einstellungen nicht");
pruefe(h.meldung.includes("1 eingerichtete"), "Meldung zaehlt falsch: " + h.meldung);

// 2) Alles kaputt machen -- anderer Wert, anderes Layout, andere Einstellungen.
await fetch(MOCK + "/api/v1/slots/0", { method: "DELETE" });
await fetch(MOCK + "/api/v1/slots/settings", {
  method: "POST", headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ rotateSec: 7, layout: [2, 2, 2, 2] }),
});
await fetch(MOCK + "/api/v1/slots", {
  method: "POST", headers: { "Content-Type": "application/json" },
  // Platz 3 gibt es im Layout der Sicherung (ein grosser Wert) nicht: Die Firmware prueft
  // ein neues Layout gegen den Bestand -- werden die Einstellungen VOR dem Loeschen
  // geschickt, bricht die Wiederherstellung hier ab (Audit 05.09.2026, N5).
  body: JSON.stringify({ ...original, label: "Fremdwert", pos: 3, wertSize: 1 }),
});

// 3) Sicherung einspielen
const inhalt = JSON.stringify(gesichert);
h.$refs.sicherungDatei = { files: [{ name: "s.json", text: async () => inhalt }] };
h.dateiName = "s.json";
await h.einspielen();
pruefe(!h.fehler, "Einspielen meldete einen Fehler: " + h.fehler);

// 4) Ergebnis pruefen -- alles muss wieder da sein, der Fremdwert weg.
const nach = await (await fetch(MOCK + "/api/v1/slots")).json();
pruefe(nach.rotateSec === 25, `rotateSec nicht wiederhergestellt: ${nach.rotateSec}`);
pruefe(nach.layout[0] === 0, `Layout nicht wiederhergestellt: ${nach.layout}`);
const s = nach.slots[0];
pruefe(s.label === "Testwert", `Wert nicht wiederhergestellt: ${s.label}`);
pruefe(s.wertSize === 9 && s.labelSize === 4 && s.unitSize === 3,
       `Schriftstufen verloren: ${s.wertSize}/${s.labelSize}/${s.unitSize}`);
pruefe(s.refreshSec === 45 && s.warnAbove === 80, "Intervall oder Schwelle verloren");
pruefe(nach.slots.filter((x) => x.url).length === 1, "Es blieb ein Fremdwert stehen");

// 5) Eine Datei, die keine Sicherung ist, darf NICHTS aendern.
h.$refs.sicherungDatei = { files: [{ name: "x.json", text: async () => '{"foo":1}' }] };
h.dateiName = "x.json";
h.fehler = "";
await h.einspielen();
pruefe(h.fehler.includes("Sicherung"), "Fremddatei wurde nicht abgewiesen: " + h.fehler);
const unveraendert = await (await fetch(MOCK + "/api/v1/slots")).json();
pruefe(unveraendert.slots[0].label === "Testwert", "Fremddatei hat den Bestand verändert");

console.log("Sicherung: 5x OK");
