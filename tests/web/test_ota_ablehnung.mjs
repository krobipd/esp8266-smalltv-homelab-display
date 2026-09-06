// SPDX-License-Identifier: GPL-3.0-or-later
//
// Das GERAET muss die Abbild-Art selbst pruefen -- der Weg (/ota/fw oder /ota/fs) kommt
// aus dem Browser, und eine veraltete Seite aus dem Cache zielt auf den falschen
// Flash-Bereich (real passiert am 02.09.2026). Seit v0.3.2 muss ausserdem die Pruefsumme
// stimmen: Der Updater prueft sonst nur das erste Byte und aktiviert auch ein
// unvollstaendiges Abbild -- ohne Rollback ein Brick (Audit 05.09.2026, A3).
// Geprueft wird gegen den Mock, der beides 1:1 wie die Firmware abbildet, mit den ECHTEN
// Abbildern aus dist/.
import { createHash } from "node:crypto";
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
const md5 = (b) => createHash("md5").update(b).digest("hex");

// pruefsumme: undefined = keine Kopfzeile schicken.
const hochladen = async (pfad, inhalt, pruefsumme) => {
  const form = new FormData();
  form.append("file", new Blob([inhalt]), "abbild.bin");
  const headers = pruefsumme === undefined ? {} : { "X-Abbild-MD5": pruefsumme };
  const r = await fetch(BASIS + pfad, { method: "POST", body: form, headers });
  return r.json();
};

const firmware = datei("firmware");
const dateisystem = datei("littlefs");

// 1) Falsche Sorte auf beiden Wegen -> Ablehnung, und zwar mit klarer Ansage.
{
  const d = await hochladen("/api/v1/ota/fw", dateisystem, md5(dateisystem));
  pruefe(d.status === "error", `Dateisystem auf Firmware-Weg: status "${d.status}"`);
  pruefe(/Firmware-Abbild/.test(d.message || ""), `Meldung nennt die Sorte nicht: ${d.message}`);
  pruefe(/nichts geschrieben/.test(d.message || ""),
         `Meldung sagt nicht, dass nichts geschrieben wurde: ${d.message}`);
}
{
  const d = await hochladen("/api/v1/ota/fs", firmware, md5(firmware));
  pruefe(d.status === "error", `Firmware auf Dateisystem-Weg: status "${d.status}"`);
  pruefe(/Oberflaeche/.test(d.message || ""), `Meldung nennt die Sorte nicht: ${d.message}`);
}

// 2) Fremde Datei wird nicht geraten.
{
  const fremd = Buffer.from("PKnicht mal ein Abbild");
  const d = await hochladen("/api/v1/ota/fw", fremd, md5(fremd));
  pruefe(d.status === "error", `fremde Datei: status "${d.status}"`);
  pruefe(/weder/.test(d.message || ""), `Meldung fuer fremde Datei: ${d.message}`);
}

// 3) Pruefsumme: Firmware ohne Summe wird abgelehnt (alte Update-Seite, nacktes curl),
//    mit falscher Summe ebenfalls -- und zwar so, dass der Grund im Text steht.
{
  const d = await hochladen("/api/v1/ota/fw", firmware);
  pruefe(d.status === "error", `Firmware ohne Pruefsumme: status "${d.status}"`);
  pruefe(/Pruefsumme/.test(d.message || ""), `Meldung nennt die Pruefsumme nicht: ${d.message}`);
  const e = await hochladen("/api/v1/ota/fw", firmware, "0".repeat(32));
  pruefe(e.status === "error", `Firmware mit falscher Pruefsumme: status "${e.status}"`);
  pruefe(/MD5/.test(e.message || ""), `Meldung nennt MD5 nicht: ${e.message}`);
}

// 4) Richtige Zuordnung mit richtiger Summe geht durch -- eine Pruefung, die alles
//    ablehnt, waere schlimmer als keine. Beim Dateisystem ist die Summe freiwillig.
{
  const d = await hochladen("/api/v1/ota/fw", firmware, md5(firmware));
  pruefe(d.status === "ok", `Firmware auf Firmware-Weg: status "${d.status}"`);
  const e = await hochladen("/api/v1/ota/fs", dateisystem, md5(dateisystem));
  pruefe(e.status === "ok", `Dateisystem mit Summe: status "${e.status}"`);
  const f = await hochladen("/api/v1/ota/fs", dateisystem);
  pruefe(f.status === "ok", `Dateisystem ohne Summe: status "${f.status}"`);
}

if (fehler > 0) process.exit(1);
console.log(`Abbild-Ablehnung: 8x OK (gegen ${version})`);
