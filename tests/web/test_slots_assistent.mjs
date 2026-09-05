// SPDX-License-Identifier: GPL-3.0-or-later
//
// "+ Neuer Wert" darf einen eingerichteten, nur abgeschalteten Wert NICHT als frei
// ansehen -- der Assistent wuerde ihn beim Speichern still ueberschreiben (Audit
// 05.09.2026, N4). Abschalten heisst "behalten, nur nicht anzeigen".
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const basis = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const quelle = readFileSync(join(basis, "firmware/data/web/js/slotsHandler.js"), "utf8");
const sandbox = {
  console,
  setTimeout,
  apiFetch: async () => ({ status: 200, json: async () => ({}) }),
  document: { addEventListener() {}, hidden: false },
  window: {},
};
sandbox.globalThis = sandbox;
vm.createContext(sandbox);
vm.runInContext(quelle, sandbox);

const pruefe = (bedingung, text) => {
  if (!bedingung) {
    console.error("FEHLGESCHLAGEN: " + text);
    process.exit(1);
  }
};

const k = sandbox.slotsHandler();
k.configLaden = async () => {};
k.config = {
  layout: [2, 2, 2, 2],
  slots: [
    { enabled: false, url: "http://192.0.2.10/a", page: 1, pos: 0 }, // abgeschaltet, eingerichtet
    { enabled: false, url: "", page: 1, pos: 0 },                     // wirklich frei
  ],
};
await k.neuerSlot();
pruefe(k.modus === "assistent", "Assistent wurde nicht geoeffnet");
pruefe(k.entwurf.index === 1, `abgeschalteter Wert gilt als frei (index ${k.entwurf.index})`);

// Alles belegt: klare Meldung statt Ueberschreiben.
k.config.slots[1].url = "http://192.0.2.10/b";
k.modus = null;
await k.neuerSlot();
pruefe(k.modus === null && /belegt/.test(k.fehler), "voller Bestand wurde nicht gemeldet");

console.log("Assistent: 2x OK");
