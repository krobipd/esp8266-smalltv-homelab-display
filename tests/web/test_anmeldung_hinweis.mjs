// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ein 401 heisst "Anmeldung noetig" -- auf JEDER Seite. apiFetch (utils.js) blendet den
// Hinweis im Kopfbereich ein; die Handler muessen nichts davon wissen und bekommen die
// Antwort unveraendert (Audit 05.09.2026, B7: Drehung zeigte still "0", Protokoll
// "0 Eintraege", WLAN "Suche fehlgeschlagen").
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const basis = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const quelle = readFileSync(join(basis, "firmware/data/web/js/utils.js"), "utf8");

const hinweis = { hidden: true };
let status = 200;
const sandbox = {
  console,
  setInterval,
  clearInterval,
  localStorage: { getItem: () => null },
  fetch: async () => ({ status, ok: status === 200 }),
  document: {
    addEventListener() {},
    getElementById: (id) => (id === "anmelde-hinweis" ? hinweis : null),
  },
};
sandbox.globalThis = sandbox;
sandbox.window = sandbox;
vm.createContext(sandbox);
vm.runInContext(quelle, sandbox);

const pruefe = (bedingung, text) => {
  if (!bedingung) {
    console.error("FEHLGESCHLAGEN: " + text);
    process.exit(1);
  }
};

const r1 = await sandbox.apiFetch("/api/v1/slots");
pruefe(r1.status === 200 && hinweis.hidden === true, "Hinweis ohne 401 sichtbar");
status = 401;
const r2 = await sandbox.apiFetch("/api/v1/slots");
pruefe(r2.status === 401, "apiFetch reicht die Antwort nicht unveraendert durch");
pruefe(hinweis.hidden === false, "Hinweis bei 401 nicht eingeblendet");

console.log("Anmeldehinweis: 2x OK");
