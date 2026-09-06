// SPDX-License-Identifier: GPL-3.0-or-later
//
// Laedt JEDE Seite so, wie der Browser es taete: alle in ihr eingebundenen Skripte in
// EINEN Kontext, dann wird geprueft, ob jede x-data-Komponente wirklich existiert und
// sich ohne Ausnahme aufbauen laesst (Audit 05.09.2026, N23).
//
// Anlass: Ein Syntaxfehler in einer Datei blieb fuenf Tage unbemerkt, weil ihn niemand
// ausfuehrte -- die Seite war tot. Die reine Syntaxpruefung (check_web.sh) haette auch
// eine Seite durchgewunken, die ein Skript gar nicht einbindet oder eine Komponente
// nennt, die es nicht gibt.
import { readFileSync, existsSync, readdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const basis = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const web = join(basis, "firmware/data/web");

const pruefe = (bedingung, text) => {
  if (!bedingung) {
    console.error("FEHLGESCHLAGEN: " + text);
    process.exit(1);
  }
};

// Ein Browser-Ersatz, der so viel kann wie noetig und nichts vortaeuscht, was das
// Geraet nicht koennte.
function bauKontext() {
  const element = () => ({
    addEventListener() {},
    removeEventListener() {},
    setAttribute() {},
    getAttribute: () => null,
    querySelector: () => null,
    querySelectorAll: () => [],
    appendChild() {},
    remove() {},
    focus() {},
    classList: { add() {}, remove() {}, contains: () => false },
    style: {},
    dataset: {},
    hidden: false,
    textContent: "",
    innerHTML: "",
    value: "",
    files: [],
  });
  const sandbox = {
    console: { log() {}, warn() {}, error() {} },
    setTimeout: () => 0,
    clearTimeout() {},
    setInterval: () => 0,
    clearInterval() {},
    TextEncoder,
    URL,
    Blob: class {},
    FormData: class {
      append() {}
    },
    XMLHttpRequest: class {
      open() {}
      send() {}
      setRequestHeader() {}
      addEventListener() {}
      get upload() {
        return { addEventListener() {} };
      }
    },
    localStorage: { getItem: () => null, setItem() {}, removeItem() {} },
    fetch: async () => ({ ok: true, status: 200, json: async () => ({}), text: async () => "" }),
    location: { pathname: "/slots.html", href: "", reload() {} },
    navigator: { userAgent: "test" },
    matchMedia: () => ({ matches: false, addEventListener() {}, removeEventListener() {}, addListener() {} }),
    requestAnimationFrame: (f) => { f(0); return 0; },
    getComputedStyle: () => ({ getPropertyValue: () => "" }),
    document: {
      addEventListener() {},
      removeEventListener() {},
      createElement: element,
      getElementById: () => null,
      querySelector: () => null,
      querySelectorAll: () => [],
      documentElement: element(),
      body: element(),
      hidden: false,
      title: "Test",
    },
    Alpine: { data() {}, store: {} },
  };
  sandbox.globalThis = sandbox;
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  return sandbox;
}

const alle = readdirSync(web).filter((d) => d.endsWith(".html"));
// header.html und footer.html sind Fragmente: Sie binden keine Skripte ein, sondern
// werden von den Vollseiten nachgeladen (includeHTML) und laufen in deren Kontext.
// Ihre Komponenten werden deshalb bei jeder Seite mitgeprueft, die sie nachlaedt.
const istFragment = (d) => !/<!doctype/i.test(readFileSync(join(web, d), "utf8"));
const fragmente = alle.filter(istFragment);
const seiten = alle.filter((d) => !istFragment(d));
pruefe(seiten.length >= 8, "zu wenige Seiten gefunden: " + seiten.length);
pruefe(fragmente.length >= 1, "keine Fragmente gefunden");

let komponenten = 0;
for (const datei of seiten) {
  let html = readFileSync(join(web, datei), "utf8");
  const sandbox = bauKontext();

  // Was die Seite nachlaedt, gehoert zu ihr.
  for (const f of fragmente) {
    if (html.includes(f.replace(".html", "-placeholder"))) {
      html += readFileSync(join(web, f), "utf8");
    }
  }

  // Alle eingebundenen Skripte -- in der Reihenfolge der Seite.
  const quellen = [...html.matchAll(/<script[^>]*src="([^"]+)"/g)].map((m) => m[1]);
  for (const src of quellen) {
    const pfad = join(web, src.replace(/^\.\//, ""));
    pruefe(existsSync(pfad), `${datei} bindet ${src} ein, aber die Datei fehlt`);
    if (src.includes("alpinejs")) continue; // die Bibliothek selbst braucht einen echten Browser
    try {
      vm.runInContext(readFileSync(pfad, "utf8"), sandbox, { filename: pfad });
    } catch (e) {
      pruefe(false, `${datei}: ${src} laesst sich nicht laden — ${e.message}`);
    }
  }

  // Jede x-data-Komponente muss es geben und sich aufbauen lassen.
  for (const treffer of html.matchAll(/x-data="([A-Za-z_$][\w$]*)\(\)"/g)) {
    const name = treffer[1];
    komponenten++;
    pruefe(
      typeof sandbox[name] === "function",
      `${datei} nutzt x-data="${name}()", aber kein geladenes Skript definiert ${name}`,
    );
    let k;
    try {
      k = sandbox[name]();
    } catch (e) {
      pruefe(false, `${datei}: ${name}() wirft beim Aufbau — ${e.message}`);
    }
    pruefe(k && typeof k === "object", `${datei}: ${name}() liefert kein Objekt`);
    if (typeof k.init === "function") {
      try {
        k.init();
      } catch (e) {
        pruefe(false, `${datei}: ${name}().init() wirft — ${e.message}`);
      }
    }
  }
}

console.log(`Seiten: ${seiten.length} geladen, ${komponenten} Komponenten OK`);
