// SPDX-License-Identifier: GPL-3.0-or-later
//
// EIN Antwortformat (Befund D5, seit v0.5.0): `ok` sagt, ob es geklappt hat, `message`
// sagt es in Worten. Vorher gab es drei Formate nebeneinander, und die Oberflaeche
// pruefte je Handler anders (`data.message || data.error`).
//
// Geprueft wird beides: dass jeder Endpunkt des Mocks -- und damit des Geraets, denn er
// bildet es 1:1 nach -- die Felder mitschickt, und dass ergebnisVon() auch die ALTEN
// Formate noch versteht. Letzteres traegt das Fenster zwischen Firmware- und
// Dateisystem-Update, in dem eine aeltere Oberflaeche auf neuer Firmware laeuft.
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const basis = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const MOCK = process.env.MOCK_URL || "http://127.0.0.1:8099";

const pruefe = (bedingung, text) => {
  if (!bedingung) {
    console.error("FEHLGESCHLAGEN: " + text);
    process.exit(1);
  }
};

const sandbox = {
  console,
  setTimeout,
  setInterval,
  clearInterval,
  TextEncoder,
  localStorage: { getItem: () => null },
  fetch: async () => ({ status: 200, ok: true, json: async () => ({}) }),
  document: { addEventListener() {}, getElementById: () => null, hidden: false },
};
sandbox.globalThis = sandbox;
sandbox.window = sandbox;
vm.createContext(sandbox);
vm.runInContext(readFileSync(join(basis, "firmware/data/web/js/utils.js"), "utf8"), sandbox);
const { ergebnisVon } = sandbox;

// --- 1) ergebnisVon versteht das neue und beide alten Formate ---
const antwortOk = { ok: true, status: 200 };
const antwortFehler = { ok: false, status: 400 };
pruefe(ergebnisVon(antwortOk, { ok: true, message: "Gespeichert" }).ok, "neues Erfolgsformat");
pruefe(ergebnisVon(antwortOk, { ok: true, message: "Gespeichert" }).text === "Gespeichert",
       "Text aus message");
pruefe(!ergebnisVon(antwortFehler, { ok: false, message: "Abgelehnt" }).ok, "neues Fehlerformat");
// Alt 1: die Basis-Firmware
pruefe(ergebnisVon(antwortOk, { status: "ok", message: "Passwort gespeichert" }).ok,
       "altes Basisformat (Erfolg)");
pruefe(!ergebnisVon(antwortFehler, { status: "error", message: "Kaputt" }).ok,
       "altes Basisformat (Fehler)");
pruefe(ergebnisVon(antwortFehler, { status: "error", message: "Kaputt" }).text === "Kaputt",
       "Text aus dem alten Basisformat");
// Alt 2: das Werte-Format
pruefe(!ergebnisVon(antwortFehler, { ok: false, error: "Zu lang" }).ok, "altes Werteformat");
pruefe(ergebnisVon(antwortFehler, { ok: false, error: "Zu lang" }).text === "Zu lang",
       "Text aus error");
// Gar kein Feld: der HTTP-Status entscheidet.
pruefe(ergebnisVon(antwortOk, {}).ok, "leere Antwort auf 200 gilt als Erfolg");
pruefe(!ergebnisVon(antwortFehler, {}).ok, "leere Antwort auf 400 gilt als Fehler");
pruefe(!ergebnisVon(antwortOk, null).ok === false, "null darf nicht werfen");

// --- 2) Jeder schreibende Endpunkt des Mocks liefert die Felder ---
const ruf = async (pfad, koerper, methode) => {
  const antwort = await fetch(MOCK + pfad, {
    method: methode || (koerper ? "POST" : "GET"),
    headers: koerper ? { "Content-Type": "application/json" } : {},
    body: koerper ? JSON.stringify(koerper) : undefined,
  });
  return { antwort, daten: await antwort.json() };
};

const faelle = [
  ["Wert speichern", "/api/v1/slots",
   { index: 0, url: "http://192.0.2.10/a", label: "A", refreshSec: 30, page: 1, pos: 0 }],
  ["Wert abgelehnt", "/api/v1/slots", { index: 0, url: "ftp://x" }],
  ["Einstellungen", "/api/v1/slots/settings", { rotateSec: 12 }],
  ["Zeitserver", "/api/v1/ntp/config", { ntp_server: "pool.ntp.org" }],
  ["Zeitserver leer", "/api/v1/ntp/config", { ntp_server: "" }],
  ["Passwort pruefen", "/api/v1/token/check", null],
  ["Drehung", "/api/v1/display/rotation", { rotation: 3 }],
  ["Protokoll leeren", "/api/v1/logs/clear", {}],
];

for (const [name, pfad, koerper] of faelle) {
  const { antwort, daten } = await ruf(pfad, koerper);
  pruefe(typeof daten.ok === "boolean", `${name}: Feld ok fehlt (${JSON.stringify(daten)})`);
  pruefe(typeof daten.message === "string" && daten.message.length > 0,
         `${name}: Feld message fehlt oder ist leer (${JSON.stringify(daten)})`);
  const e = ergebnisVon(antwort, daten);
  pruefe(e.ok === daten.ok, `${name}: ergebnisVon liest ok falsch`);
  pruefe(e.text === daten.message, `${name}: ergebnisVon liest den Text falsch`);
}

// Löschen zum Schluss, damit der Bestand sauber bleibt.
const { antwort: aDel, daten: dDel } = await ruf("/api/v1/slots/0", null, "DELETE");
pruefe(dDel.ok === true && typeof dDel.message === "string", "Loeschen ohne Ergebnisfelder");
pruefe(ergebnisVon(aDel, dDel).ok, "Loeschen wird als Fehler gelesen");

// --- 3) Gegenprobe gegen die FIRMWARE-Quellen ---
// Der Mock kann hier richtig liegen und das Geraet trotzdem falsch: Genau das war in
// v0.5.1 der Fall -- /ntp/config antwortete ohne ok und message, weil der Handler sein
// Dokument von Hand baute. Deshalb wird hier geprueft, dass KEIN Handler mehr
// doc["status"] setzt, ohne setzeErgebnis() aufzurufen.
const quellen = [
  "firmware/src/web/Api.cpp",
  "firmware/src/slots/SlotApi.cpp",
  "firmware/src/boot/RescueMode.cpp",
];
for (const datei of quellen) {
  const text = readFileSync(join(basis, datei), "utf8");
  // Jede Antwort wird ueber sendeJson/sendeStatus/sendeFehler* verschickt oder von Hand
  // serialisiert. Von Hand gesetzte status-Felder sind nur als ZUSATZ erlaubt -- direkt
  // nach einem setzeErgebnis() in derselben Funktion.
  const zeilen = text.split("\n");
  let letztesErgebnis = -100;
  zeilen.forEach((zeile, i) => {
    if (/setzeErgebnis\(/.test(zeile)) letztesErgebnis = i;
    const m = zeile.match(/^\s*(?:doc|resp|antwort)\["status"\] = /);
    if (m) {
      pruefe(
        i - letztesErgebnis <= 3,
        `${datei}:${i + 1} setzt status ohne setzeErgebnis() davor: ${zeile.trim()}`,
      );
    }
  });
}

console.log(`Antwortformat: ${11 + faelle.length * 4 + 2}x OK (Mock und Firmware-Quellen)`);
