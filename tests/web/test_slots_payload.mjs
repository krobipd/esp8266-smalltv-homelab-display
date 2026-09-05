// SPDX-License-Identifier: GPL-3.0-or-later
//
// Prueft, was die Werte-Seite tatsaechlich ans Geraet schickt -- nicht nur, ob die
// Datei syntaktisch heil ist. Anlass: die drei Schriftstufen kommen aus Auswahlfeldern.
// Fehlt eine (alter Slot ohne das Feld, Dialog noch nicht durchlaufen), waere
// Number(undefined) = NaN, im JSON null, und das Geraet lehnte einen Wert ab, an dem
// der Nutzer nie etwas eingestellt hat.
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const basis = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const quelle = readFileSync(join(basis, "firmware/data/web/js/slotsHandler.js"), "utf8");

let gesendet = null;
const sandbox = {
  console,
  setTimeout,
  // Die Seite spricht ueber apiFetch mit dem Geraet -- hier abgefangen statt gesendet.
  apiFetch: async (_pfad, opt) => {
    gesendet = JSON.parse(opt.body);
    return { status: 200, json: async () => ({ ok: true }) };
  },
  document: { addEventListener() {} },
  window: {},
};
sandbox.globalThis = sandbox;
vm.createContext(sandbox);
vm.runInContext(quelle, sandbox);

const komponente = () => {
  const k = sandbox.slotsHandler();
  // Nachladen und Statusabruf gehoeren nicht zu diesem Test.
  k.configLaden = async () => {};
  k.statusLaden = async () => {};
  k.entwurfGueltig = () => true;
  return k;
};

const STUFEN = ["wertSize", "labelSize", "unitSize"];
const pruefe = (bedingung, text) => {
  if (!bedingung) {
    console.error("FEHLGESCHLAGEN: " + text);
    process.exit(1);
  }
};

// 1) Frischer Entwurf: die drei Stufen muessen belegt sein, nicht null/NaN.
{
  const k = komponente();
  k.entwurf = sandbox.leererEntwurf();
  k.entwurf.label = "A";
  k.entwurf.url = "http://192.0.2.10/a";
  await k.speichern();
  for (const f of STUFEN) {
    pruefe(Number.isInteger(gesendet[f]), `frischer Entwurf: ${f} ist ${gesendet[f]}`);
    pruefe(gesendet[f] >= 1 && gesendet[f] <= 10, `frischer Entwurf: ${f} ausserhalb 1..10`);
  }
}

// 2) Alter Slot ohne die neuen Felder: es darf kein null beim Geraet ankommen.
{
  const k = komponente();
  k.entwurf = sandbox.leererEntwurf();
  k.entwurf.label = "B";
  k.entwurf.url = "http://192.0.2.10/b";
  for (const f of STUFEN) delete k.entwurf[f];
  await k.speichern();
  for (const f of STUFEN) {
    pruefe(gesendet[f] !== null && Number.isInteger(gesendet[f]),
           `alter Slot: ${f} ist ${gesendet[f]}`);
  }
}

// 3) Eingestellte Werte kommen unveraendert an -- und unabhaengig voneinander.
{
  const k = komponente();
  k.entwurf = sandbox.leererEntwurf();
  k.entwurf.label = "C";
  k.entwurf.url = "http://192.0.2.10/c";
  k.entwurf.wertSize = 9;
  k.entwurf.labelSize = 6;
  k.entwurf.unitSize = 1;
  await k.speichern();
  pruefe(gesendet.wertSize === 9 && gesendet.labelSize === 6 && gesendet.unitSize === 1,
         "eingestellte Stufen veraendert: " + JSON.stringify(gesendet));
}

// 4) Eine ausdrueckliche 0 bleibt eine 0 -- das Geraet soll sie ablehnen duerfen,
//    statt dass die Seite sie still zum Standard macht.
{
  const k = komponente();
  k.entwurf = sandbox.leererEntwurf();
  k.entwurf.label = "D";
  k.entwurf.url = "http://192.0.2.10/d";
  k.entwurf.wertSize = 0;
  await k.speichern();
  pruefe(gesendet.wertSize === 0, "ausdrueckliche 0 wurde ersetzt: " + gesendet.wertSize);
}

// 5) Die Vorschau muss sich mit den Stufen bewegen -- eine Vorschau, die stehen
//    bleibt, waehrend die Auswahl sich aendert, ist schlechter als gar keine.
{
  const k = komponente();
  k.entwurf = sandbox.leererEntwurf();
  k.entwurf.wertSize = 2;
  const klein = k.vorschauPx("wertSize");
  k.entwurf.wertSize = 8;
  const gross = k.vorschauPx("wertSize");
  pruefe(gross > klein, `Vorschau bewegt sich nicht: ${klein} -> ${gross}`);
  k.entwurf.labelSize = 5;
  pruefe(k.vorschauPx("labelSize") !== k.vorschauPx("unitSize"),
         "Vorschau behandelt Beschriftung und Einheit nicht getrennt");
}

// 6) Der Schalter "Einheit neben dem Wert" geht als echter Wahrheitswert mit -- und
//    zwar in beide Richtungen. Fehlte er, ergaenzte das Geraet die Vorgabe "daneben"
//    und die abgeschaltete Einstellung waere beim naechsten Speichern still weg.
{
  const k = komponente();
  k.entwurf = sandbox.leererEntwurf();
  k.entwurf.label = "A";
  k.entwurf.url = "http://192.0.2.10/a";
  await k.speichern();
  pruefe(gesendet.einheitDaneben === true, `frischer Entwurf: einheitDaneben ist ${gesendet.einheitDaneben}`);

  k.entwurf.einheitDaneben = false;
  await k.speichern();
  pruefe(gesendet.einheitDaneben === false, `abgeschaltet: einheitDaneben ist ${gesendet.einheitDaneben}`);

  // Alter Slot ohne das Feld: das Formular darf daraus kein "aus" machen.
  k.entwurf = sandbox.leererEntwurf();
  delete k.entwurf.einheitDaneben;
  k.entwurf.label = "A";
  k.entwurf.url = "http://192.0.2.10/a";
  await k.speichern();
  pruefe(gesendet.einheitDaneben === true, `Feld fehlt: einheitDaneben ist ${gesendet.einheitDaneben}`);
}

// 7) Ein Abrufintervall 0 bleibt 0 -- das Geraet lehnt es ab; still 30 daraus zu machen
//    waere eine Bevormundung (N10). Nur ein LEERES Feld bekommt die Vorgabe.
{
  const k = komponente();
  k.entwurf = sandbox.leererEntwurf();
  k.entwurf.label = "E";
  k.entwurf.url = "http://192.0.2.10/e";
  k.entwurf.refreshSec = 0;
  await k.speichern();
  pruefe(gesendet.refreshSec === 0, "Intervall 0 wurde ersetzt: " + gesendet.refreshSec);
  k.entwurf.refreshSec = "";
  await k.speichern();
  pruefe(gesendet.refreshSec === 30, "leeres Intervall bekam keine Vorgabe: " + gesendet.refreshSec);
}

console.log("Werte-Seite: 7x OK");
