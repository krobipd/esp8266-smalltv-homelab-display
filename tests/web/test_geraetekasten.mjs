// SPDX-License-Identifier: GPL-3.0-or-later
//
// Der Kasten "Geraet" unter der Uebersicht: Adresse, WLAN, Firmware, Laufzeit, Uhrzeit,
// Speicher und Belegung. Die Zahlen kamen frueher alle zehn Sekunden ins Protokoll und
// verdraengten dort alles andere (Audit, E11); die Adresse stand ueberhaupt nirgends in
// der Oberflaeche (krobi, 06.09.2026: "waere nutzertechnisch auch sinnvoll").
//
// Geprueft wird, was der Kasten aus welchen Daten macht -- inklusive der Faelle, in denen
// eine Quelle fehlt: Dann faellt die Zeile weg, statt "undefined" zu zeigen.
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
  Date,
  Set,
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
const feld = (name) => (k.geraetInfo.find((z) => z.name === name) || {}).wert;

// --- Ohne jede Quelle bleibt der Kasten leer (er wird dann nicht angezeigt) ---
pruefe(k.geraetInfo.length === 0, "leerer Zustand ergibt Zeilen: " + JSON.stringify(k.geraetInfo));

// --- Vollstaendig ---
k.geraet = { version: "v0.5.2", freeHeap: 15432, heapFrag: 14, uptimeSec: 627, rssi: -58 };
k.netz = { connected: true, ssid: "Heimnetz", ip: "192.0.2.10" };
k.zeit = {
  lastOk: true,
  lastStatus: "Synchronisiert: 2026-09-06 14:19:09",
  lastSyncTime: 1788697149,
};
k.zone = "CET-1CEST,M3.5.0,M10.5.0/3";
k.config = {
  layout: [2, 2, 2, 2],
  slots: [
    { url: "http://192.0.2.10/a", enabled: true, page: 1 },
    { url: "http://192.0.2.10/b", enabled: true, page: 3 },
    { url: "", enabled: false, page: 1 },
  ],
};

pruefe(feld("Adresse") === "192.0.2.10", "Adresse falsch: " + feld("Adresse"));
pruefe(feld("WLAN") === "Heimnetz, -58 dBm", "WLAN falsch: " + feld("WLAN"));
pruefe(feld("Firmware") === "v0.5.2", "Firmware falsch: " + feld("Firmware"));
pruefe(feld("Läuft seit") === "10 min", "Laufzeit falsch: " + feld("Läuft seit"));
// Die Uhrzeit MUSS die des Geraets sein, nicht die des Browsers: Sonst stuende hier die
// Zeit des Betrachters mit dem Zonen-Kuerzel des Geraets dahinter. Genau daran ist die
// erste Fassung in der CI gescheitert (dort laeuft UTC -- "12:19 PM").
//
// Der Zeitpunkt wird deshalb hier gesetzt, nicht gerechnet: heute nur die Uhrzeit,
// an einem anderen Tag mit Datum davor.
const heuteIso = new Date().toISOString().slice(0, 10);
k.zeit = { lastOk: true, lastStatus: `Synchronisiert: ${heuteIso} 14:19:09`, lastSyncTime: 1 };
pruefe(feld("Uhr gestellt") === "14:19 (MEZ/MESZ)",
       "Uhrzeit von heute falsch: " + feld("Uhr gestellt"));
k.zeit = { lastOk: true, lastStatus: "Synchronisiert: 2020-01-02 07:05:00", lastSyncTime: 1 };
pruefe(feld("Uhr gestellt") === "2020-01-02 07:05 (MEZ/MESZ)",
       "aeltere Zeit ohne Datum: " + feld("Uhr gestellt"));
pruefe(feld("Speicher frei") === "15,1 KB (14 % fragmentiert)",
       "Speicher falsch: " + feld("Speicher frei"));
pruefe(feld("Werte") === "2 von 12", "Werte falsch: " + feld("Werte"));
pruefe(feld("Seiten belegt") === "2 von 4", "Seiten falsch: " + feld("Seiten belegt"));

// --- Fehlt eine Quelle, faellt genau ihre Zeile weg ---
k.netz = null;
pruefe(feld("Adresse") === undefined && feld("WLAN") === undefined,
       "Netz-Zeilen bleiben trotz fehlender Quelle stehen");
pruefe(feld("Firmware") === "v0.5.2", "andere Zeilen verschwinden mit");
k.netz = { connected: true, ssid: "", ip: "192.0.2.10" };
pruefe(feld("Adresse") === "192.0.2.10" && feld("WLAN") === undefined,
       "leere SSID ergibt trotzdem eine WLAN-Zeile");

// --- Ein fehlgeschlagener Abgleich wird benannt, nicht verschwiegen ---
k.zeit = { lastOk: false, lastStatus: "Synchronisiert: 2026-09-06 14:19:09",
           lastSyncTime: 1788697149 };
pruefe(/fehlgeschlagen/.test(feld("Uhr gestellt")),
       "fehlgeschlagener Abgleich wird nicht genannt: " + feld("Uhr gestellt"));
// Ohne auswertbare Meldung bleibt nur die Aussage, dass es nicht geklappt hat.
k.zeit = { lastOk: false, lastStatus: "kein Netzwerk", lastSyncTime: 1788697149 };
pruefe(feld("Uhr gestellt") === "fehlgeschlagen",
       "ohne Zeitangabe falsch: " + feld("Uhr gestellt"));
k.zeit = { lastOk: true, lastStatus: "Synchronisierung laeuft", lastSyncTime: 1788697149 };
pruefe(feld("Uhr gestellt") === undefined,
       "ohne Zeitangabe und ohne Fehler darf keine Zeile stehen: " + feld("Uhr gestellt"));

// --- Laufzeit in allen drei Stufen ---
pruefe(k.dauerText(400) === "6 min", "Minuten falsch: " + k.dauerText(400));
pruefe(k.dauerText(11520) === "3 h 12 min", "Stunden falsch: " + k.dauerText(11520));
pruefe(k.dauerText(86400) === "1 Tag 0 h", "Singular falsch: " + k.dauerText(86400));
pruefe(k.dauerText(200000) === "2 Tagen 7 h", "Plural falsch: " + k.dauerText(200000));

// --- Zeitzonen-Kuerzel ---
k.zone = "EST5EDT,M3.2.0,M11.1.0";
pruefe(k.zonenName() === "EST/EDT", "New York falsch: " + k.zonenName());
k.zone = "UTC0";
pruefe(k.zonenName() === "UTC", "UTC falsch: " + k.zonenName());
k.zone = "";
pruefe(k.zonenName() === "", "leere Zone ergibt Text: " + k.zonenName());

console.log("Geraetekasten: 21x OK");
