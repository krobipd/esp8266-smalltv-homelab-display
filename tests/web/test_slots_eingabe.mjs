// SPDX-License-Identifier: GPL-3.0-or-later
//
// Die Werte-Seite prueft Beschriftung, Einheit und Feldname so wie das Geraet: in BYTES
// (UTF-8, "Küche" sind 6) und -- fuer alles, was gezeichnet wird -- nur mit Zeichen,
// die das Display hat: ASCII plus ° ä ö ü Ä Ö Ü ß
// (Audit 05.09.2026, N6). Vorher zaehlte die Seite Zeichen und liess alles durch; das
// Geraet lehnte dann ab oder malte Muell.
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const basis = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const sandbox = {
  console,
  setTimeout,
  setInterval,
  clearInterval,
  TextEncoder,
  localStorage: { getItem: () => null },
  fetch: async () => ({ status: 200, ok: true }),
  apiFetch: async () => ({ status: 200, json: async () => ({}) }),
  document: { addEventListener() {}, getElementById: () => null, hidden: false },
};
sandbox.globalThis = sandbox;
sandbox.window = sandbox;
vm.createContext(sandbox);
vm.runInContext(readFileSync(join(basis, "firmware/data/web/js/utils.js"), "utf8"), sandbox);
vm.runInContext(readFileSync(join(basis, "firmware/data/web/js/slotsHandler.js"), "utf8"), sandbox);

const pruefe = (bedingung, text) => {
  if (!bedingung) {
    console.error("FEHLGESCHLAGEN: " + text);
    process.exit(1);
  }
};

const k = sandbox.slotsHandler();
k.config = { layout: [2, 2, 2, 2], slots: [] };
const entwurf = (aenderung) => {
  k.entwurf = { ...sandbox.leererEntwurf(), url: "http://192.0.2.10/a", label: "A", ...aenderung };
  k.fehler = "";
  return k.entwurfGueltig();
};

pruefe(entwurf({ unit: "°C" }) === true, "Gradzeichen wurde abgelehnt: " + k.fehler);
pruefe(entwurf({ label: "Küche" }) === true, "Umlaut wurde abgelehnt: " + k.fehler);
pruefe(entwurf({ unit: "Ω" }) === false && /Display/.test(k.fehler),
       "Omega ging durch oder Meldung nennt das Display nicht: " + k.fehler);
pruefe(entwurf({ label: "ä".repeat(12) }) === false && /zu lang/.test(k.fehler),
       "12 Umlaute = 24 Byte gingen durch: " + k.fehler);
pruefe(entwurf({ label: "ä".repeat(11) }) === true,
       "11 Umlaute = 22 Byte wurden abgelehnt: " + k.fehler);
pruefe(entwurf({ field: "Ω" }) === true, "Feldname wird nie gezeichnet, Omega abgelehnt: " + k.fehler);
pruefe(entwurf({ field: "we\u0001rt" }) === false, "Steuerzeichen im Feldnamen ging durch");
pruefe(entwurf({ field: "x".repeat(32) }) === false && /zu lang/.test(k.fehler),
       "32 Zeichen Feldname gingen durch: " + k.fehler);

console.log("Eingabepruefung: 8x OK");
