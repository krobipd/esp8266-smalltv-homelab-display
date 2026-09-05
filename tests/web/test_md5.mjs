// SPDX-License-Identifier: GPL-3.0-or-later
//
// Die Update-Seite rechnet die MD5-Pruefsumme des Abbilds selbst (md5.js) und schickt sie
// dem Geraet; der Updater verwirft ein Abbild, dessen Summe nicht stimmt, bevor es aktiv
// wird (Audit 05.09.2026, A3). WebCrypto steht auf http:// nicht zur Verfuegung, deshalb
// eine eigene Fassung -- hier gegen Node und gegen die echten Abbilder aus dist/ geprueft.
import { createHash } from "node:crypto";
import { readFileSync, readdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const basis = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const quelle = readFileSync(join(basis, "firmware/data/web/js/md5.js"), "utf8");
const sandbox = { console };
sandbox.globalThis = sandbox;
vm.createContext(sandbox);
vm.runInContext(quelle, sandbox);

const pruefe = (bedingung, text) => {
  if (!bedingung) {
    console.error("FEHLGESCHLAGEN: " + text);
    process.exit(1);
  }
};
const md5 = (bytes) => sandbox.md5Bytes(new Uint8Array(bytes));

// Testvektoren aus RFC 1321.
pruefe(md5(Buffer.from("")) === "d41d8cd98f00b204e9800998ecf8427e", "leer");
pruefe(md5(Buffer.from("abc")) === "900150983cd24fb0d6963f7d28e17f72", "abc");
pruefe(md5(Buffer.from("12345678901234567890123456789012345678901234567890123456789012345678901234567890"))
       === "57edf4a22be3c955ac49da2e2107b67a", "80 Ziffern");
// Blockgrenzen -- dort sitzen Auffuell-Fehler.
for (const n of [55, 56, 63, 64, 65, 119, 120, 1000]) {
  const b = Buffer.alloc(n, 0x61);
  pruefe(md5(b) === createHash("md5").update(b).digest("hex"), `${n} Byte`);
}
// Die echten Abbilder der neuesten Version.
const distWurzel = join(basis, "dist");
const version = readdirSync(distWurzel).filter((d) => d.startsWith("v")).sort().pop();
const dist = join(distWurzel, version);
for (const f of readdirSync(dist).filter((f) => f.endsWith(".bin"))) {
  const b = readFileSync(join(dist, f));
  pruefe(md5(b) === createHash("md5").update(b).digest("hex"), f);
}
console.log(`MD5: Testvektoren, Blockgrenzen und Abbilder ${version} OK`);
