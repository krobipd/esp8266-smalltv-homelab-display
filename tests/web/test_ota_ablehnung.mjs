// SPDX-License-Identifier: GPL-3.0-or-later
//
// Das GERAET muss die Abbild-Art selbst pruefen -- der Weg (/ota/fw oder /ota/fs) kommt
// aus dem Browser, und eine veraltete Seite aus dem Cache zielt auf den falschen
// Flash-Bereich (real passiert am 02.09.2026). Geprueft wird gegen den Mock, der die
// Pruefung 1:1 wie otaHandleWrite() abbildet, und mit den ECHTEN Abbildern aus dist/.
import { readFileSync, readdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const BASIS = "http://127.0.0.1:8099";
const wurzel = join(dirname(fileURLToPath(import.meta.url)), "..", "..");

let fehler = 0;
const pruefe = (bedingung, text) => {
  if (!bedingung) {
    console.error("FEHLGESCHLAGEN: " + text);
    fehler++;
  }
};

const distWurzel = join(wurzel, "dist");
const version = readdirSync(distWurzel).filter((d) => d.startsWith("v")).sort().pop();
const dist = join(distWurzel, version);
const datei = (teil) => {
  const name = readdirSync(dist).find((f) => f.endsWith(".bin") && f.includes(teil));
  // Der Kopf genuegt: die Erkennung sieht ohnehin nur die ersten 16 Bytes.
  return readFileSync(join(dist, name)).subarray(0, 4096);
};

const hochladen = async (pfad, inhalt) => {
  const form = new FormData();
  form.append("file", new Blob([inhalt]), "abbild.bin");
  const r = await fetch(BASIS + pfad, { method: "POST", body: form });
  return r.json();
};

const firmware = datei("firmware");
const dateisystem = datei("littlefs");

// 1) Falsche Sorte auf beiden Wegen -> Ablehnung, und zwar mit klarer Ansage.
{
  const d = await hochladen("/api/v1/ota/fw", dateisystem);
  pruefe(d.status === "Error", `Dateisystem auf Firmware-Weg: status "${d.status}"`);
  pruefe(/Firmware-Abbild/.test(d.message || ""), `Meldung nennt die Sorte nicht: ${d.message}`);
  pruefe(/nichts geschrieben/.test(d.message || ""),
         `Meldung sagt nicht, dass nichts geschrieben wurde: ${d.message}`);
}
{
  const d = await hochladen("/api/v1/ota/fs", firmware);
  pruefe(d.status === "Error", `Firmware auf Dateisystem-Weg: status "${d.status}"`);
  pruefe(/Oberflaeche/.test(d.message || ""), `Meldung nennt die Sorte nicht: ${d.message}`);
}

// 2) Fremde Datei wird nicht geraten.
{
  const d = await hochladen("/api/v1/ota/fw", Buffer.from("PKnicht mal ein Abbild"));
  pruefe(d.status === "Error", `fremde Datei: status "${d.status}"`);
  pruefe(/weder/.test(d.message || ""), `Meldung fuer fremde Datei: ${d.message}`);
}

// 3) Richtige Zuordnung muss weiterhin durchgehen -- eine Pruefung, die alles ablehnt,
//    waere schlimmer als keine.
{
  const d = await hochladen("/api/v1/ota/fw", firmware);
  pruefe(d.status === "Upload successful", `Firmware auf Firmware-Weg: status "${d.status}"`);
  const e = await hochladen("/api/v1/ota/fs", dateisystem);
  pruefe(e.status === "Upload successful", `Dateisystem auf Dateisystem-Weg: status "${e.status}"`);
}

if (fehler > 0) process.exit(1);
console.log(`Abbild-Ablehnung: 4x OK (gegen ${version})`);
