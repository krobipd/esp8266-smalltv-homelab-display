// SPDX-License-Identifier: GPL-3.0-or-later
//
// Der Zustand des Geraets steht seit v0.4.0 im Status (/api/v1/slots/status, Feld
// "geraet") und darunter auf der Werte-Seite -- vorher schrieb ihn ein Heartbeat alle
// zehn Sekunden ins Protokoll und verdraengte dort alles andere (Audit, E11).
// Geprueft wird die Zeile selbst: Einheiten, deutsche Zahlen, Singular/Plural.
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
  fetch: async () => ({}),
  apiFetch: async () => ({}),
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
const zeile = (uptimeSec) => {
  k.geraet = { freeHeap: 18632, heapFrag: 7, uptimeSec, rssi: -61 };
  return k.geraetZeile();
};

// Ohne Daten (Status noch nicht geladen) bleibt die Zeile leer statt "undefined" zu zeigen.
k.geraet = null;
pruefe(k.geraetZeile() === "", "leerer Zustand ergibt keine leere Zeile: " + k.geraetZeile());

const z = zeile(11520);
pruefe(/18,2 KB/.test(z), "Speicher nicht in KB mit Komma: " + z);
pruefe(/Fragmentierung 7 %/.test(z), "Fragmentierung fehlt: " + z);
pruefe(/-61 dBm/.test(z), "Empfangsstaerke fehlt: " + z);
pruefe(/3 h 12 min/.test(z), "Laufzeit falsch: " + z);
pruefe(/6 min/.test(zeile(400)), "kurze Laufzeit falsch: " + zeile(400));
pruefe(/1 Tag 0 h/.test(zeile(86400)), "Singular falsch: " + zeile(86400));
pruefe(/2 Tagen 7 h/.test(zeile(200000)), "Plural falsch: " + zeile(200000));

console.log("Geraetezeile: 8x OK");
