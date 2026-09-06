// SPDX-License-Identifier: GPL-3.0-or-later
//
// Der Kasten "Geraet" auf der UEBERSICHT (index.html), nicht auf der Werte-Seite: Adresse,
// WLAN, Firmware, Laufzeit, Uhrzeit, Speicher, Belegung -- untereinander, nach Themen
// gruppiert. Er zieht alles aus EINEM leichten Aufruf (/api/v1/geraet).
//
// Geprueft wird, was der Kasten aus welchen Daten macht, inklusive der Faelle, in denen
// eine Angabe fehlt: Dann faellt ihre Zeile weg, und eine leere Gruppe verschwindet ganz.
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
  localStorage: { getItem: () => null },
  fetch: async () => ({}),
  apiFetch: async () => ({}),
  document: { addEventListener() {}, getElementById: () => null, hidden: false },
};
sandbox.globalThis = sandbox;
sandbox.window = sandbox;
vm.createContext(sandbox);
vm.runInContext(readFileSync(join(basis, "firmware/data/web/js/utils.js"), "utf8"), sandbox);
vm.runInContext(readFileSync(join(basis, "firmware/data/web/js/geraetHandler.js"), "utf8"), sandbox);

const pruefe = (bedingung, text) => {
  if (!bedingung) {
    console.error("FEHLGESCHLAGEN: " + text);
    process.exit(1);
  }
};

const k = sandbox.geraetHandler();
const gruppe = (titel) => k.gruppen.find((g) => g.titel === titel);
const feld = (titel, name) => {
  const g = gruppe(titel);
  return g ? (g.zeilen.find((z) => z.name === name) || {}).wert : undefined;
};

// --- Ohne Daten bleibt der Kasten leer ---
pruefe(k.gruppen.length === 0, "leerer Zustand ergibt Gruppen: " + JSON.stringify(k.gruppen));

// --- Vollstaendig ---
const heuteIso = new Date().toISOString().slice(0, 10);
k.g = {
  version: "v0.5.4",
  freeHeap: 15432,
  heapFrag: 14,
  uptimeSec: 627,
  rssi: -58,
  ssid: "Heimnetz",
  ip: "192.0.2.10",
  zeitOk: true,
  zeitStatus: `Synchronisiert: ${heuteIso} 14:19:09`,
  zeitzone: "CET-1CEST,M3.5.0,M10.5.0/3",
  rotation: 0,
  werte: 2,
  maxWerte: 12,
  seiten: 2,
  maxSeiten: 4,
};

pruefe(k.gruppen.map((g) => g.titel).join(",") === "Netz,Firmware,Uhrzeit,Anzeige",
       "Gruppen oder ihre Reihenfolge falsch: " + k.gruppen.map((g) => g.titel).join(","));
pruefe(feld("Netz", "Adresse") === "192.0.2.10", "Adresse falsch: " + feld("Netz", "Adresse"));
pruefe(feld("Netz", "WLAN") === "Heimnetz, -58 dBm", "WLAN falsch: " + feld("Netz", "WLAN"));
pruefe(feld("Firmware", "Version") === "v0.5.4", "Version falsch: " + feld("Firmware", "Version"));
pruefe(feld("Firmware", "Läuft seit") === "10 min", "Laufzeit falsch: " + feld("Firmware", "Läuft seit"));
pruefe(feld("Firmware", "Speicher frei") === "15,1 KB (14 % fragmentiert)",
       "Speicher falsch: " + feld("Firmware", "Speicher frei"));
pruefe(feld("Uhrzeit", "Zeitzone") === "MEZ/MESZ", "Zone falsch: " + feld("Uhrzeit", "Zeitzone"));
pruefe(feld("Anzeige", "Werte") === "2 von 12", "Werte falsch: " + feld("Anzeige", "Werte"));
pruefe(feld("Anzeige", "Seiten belegt") === "2 von 4", "Seiten falsch: " + feld("Anzeige", "Seiten belegt"));
pruefe(feld("Anzeige", "Drehung") === "0", "Drehung falsch: " + feld("Anzeige", "Drehung"));

// --- Die Uhrzeit MUSS die des Geraets sein, nicht die des Browsers. Genau daran ist die
//     erste Fassung in der CI gescheitert (dort laeuft UTC: "12:19 PM"). ---
pruefe(feld("Uhrzeit", "Zuletzt gestellt") === "14:19",
       "Uhrzeit von heute falsch: " + feld("Uhrzeit", "Zuletzt gestellt"));
k.g.zeitStatus = "Synchronisiert: 2020-01-02 07:05:00";
pruefe(feld("Uhrzeit", "Zuletzt gestellt") === "2020-01-02 07:05",
       "aeltere Zeit ohne Datum: " + feld("Uhrzeit", "Zuletzt gestellt"));

// --- Fehlgeschlagener Abgleich wird benannt, nicht verschwiegen ---
k.g.zeitOk = false;
pruefe(/fehlgeschlagen/.test(feld("Uhrzeit", "Zuletzt gestellt")),
       "Fehlschlag wird nicht genannt: " + feld("Uhrzeit", "Zuletzt gestellt"));
k.g.zeitStatus = "kein Netzwerk";
pruefe(feld("Uhrzeit", "Zuletzt gestellt") === "fehlgeschlagen",
       "ohne Zeitangabe falsch: " + feld("Uhrzeit", "Zuletzt gestellt"));

// --- Fehlt eine Angabe, faellt genau ihre Zeile weg; eine leere Gruppe verschwindet ---
k.g = { version: "v0.5.4" };
pruefe(k.gruppen.length === 1 && k.gruppen[0].titel === "Firmware",
       "leere Gruppen bleiben stehen: " + JSON.stringify(k.gruppen.map((g) => g.titel)));
pruefe(k.gruppen[0].zeilen.length === 1, "leere Zeilen bleiben stehen");
k.g = { ip: "192.0.2.10", ssid: "" };
pruefe(feld("Netz", "Adresse") === "192.0.2.10" && feld("Netz", "WLAN") === undefined,
       "leere SSID ergibt trotzdem eine Zeile");

// --- Laufzeit in allen Stufen ---
pruefe(k.dauerText(400) === "6 min", "Minuten falsch: " + k.dauerText(400));
pruefe(k.dauerText(11520) === "3 h 12 min", "Stunden falsch: " + k.dauerText(11520));
pruefe(k.dauerText(86400) === "1 Tag 0 h", "Singular falsch: " + k.dauerText(86400));
pruefe(k.dauerText(200000) === "2 Tagen 7 h", "Plural falsch: " + k.dauerText(200000));

// --- Zeitzonen-Kuerzel ---
k.g = { zeitzone: "EST5EDT,M3.2.0,M11.1.0" };
pruefe(k.zonenName() === "EST/EDT", "New York falsch: " + k.zonenName());
k.g = { zeitzone: "UTC0" };
pruefe(k.zonenName() === "UTC", "UTC falsch: " + k.zonenName());
k.g = { zeitzone: "" };
pruefe(k.zonenName() === "", "leere Zone ergibt Text: " + k.zonenName());

console.log("Geraetekasten: 23x OK");
