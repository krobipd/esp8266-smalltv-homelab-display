// SPDX-License-Identifier: GPL-3.0-or-later
//
// Die Update-Seite entscheidet selbst, ob eine Datei die Firmware oder das
// Dateisystem ist. Diese Entscheidung schreibt in unterschiedliche Flash-Bereiche
// und ist auf einem Geraet ohne Bootloader-Rueckfall nicht zurueckzunehmen --
// sie wird deshalb gegen die WIRKLICH ausgelieferten Abbilder geprueft, nicht
// gegen nachgebaute Kopfbytes.
import { readFileSync, readdirSync, existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";
import { neuesteFassung } from "./neueste_fassung.mjs";

const basis = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const quelle = readFileSync(join(basis, "firmware/data/web/js/otaUploadHandler.js"), "utf8");

const sandbox = { console, localStorage: { getItem: () => null }, XMLHttpRequest: function () {} };
sandbox.globalThis = sandbox;
vm.createContext(sandbox);
vm.runInContext(quelle, sandbox);

// Minimaler Ersatz fuer das File-Objekt des Browsers: nur slice().arrayBuffer().
const alsDatei = (puffer) => ({
  slice: (von, bis) => ({ arrayBuffer: async () => puffer.subarray(von, bis) }),
});

const pruefe = (bedingung, text) => {
  if (!bedingung) {
    console.error("FEHLGESCHLAGEN: " + text);
    process.exit(1);
  }
};

// Neuestes dist-Verzeichnis nehmen -- der Test soll nicht an einer Version kleben.
const fassung = neuesteFassung(basis);
const dist = fassung.pfad;

const abbilder = readdirSync(dist).filter((f) => f.endsWith(".bin"));
const firmware = abbilder.find((f) => f.includes("firmware"));
const dateisystem = abbilder.find((f) => f.includes("littlefs"));
pruefe(firmware && dateisystem, `beide Abbilder noetig in ${dist}: ${abbilder}`);

let n = 0;

// 1) Das echte Firmware-Abbild
{
  const puffer = readFileSync(join(dist, firmware));
  const art = await sandbox.abbildArtErkennen(alsDatei(puffer));
  pruefe(art === "firmware", `${firmware} wurde als "${art}" erkannt`);
  n++;
}

// 2) Das echte Dateisystem-Abbild
{
  const puffer = readFileSync(join(dist, dateisystem));
  const art = await sandbox.abbildArtErkennen(alsDatei(puffer));
  pruefe(art === "fs", `${dateisystem} wurde als "${art}" erkannt`);
  n++;
}

// 3) Etwas anderes wird NICHT geraten. Lieber abweisen als den falschen
//    Speicherbereich beschreiben.
{
  const muell = Buffer.alloc(64, 0x42);
  const art = await sandbox.abbildArtErkennen(alsDatei(muell));
  pruefe(art === null, `Fremddatei wurde als "${art}" durchgewunken`);
  n++;
}

// 4) Zu kurze Datei ebenfalls nicht.
{
  const kurz = Buffer.from([0x01, 0x02]);
  const art = await sandbox.abbildArtErkennen(alsDatei(kurz));
  pruefe(art === null, `2-Byte-Datei wurde als "${art}" erkannt`);
  n++;
}

console.log(`Update-Erkennung: ${n}x OK (gegen ${fassung.name})`);
