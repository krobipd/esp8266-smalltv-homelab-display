// SPDX-License-Identifier: GPL-3.0-or-later
//
// Prueft die Cache-Regeln der Oberflaeche gegen den Mock (der sie 1:1 wie
// Webserver.cpp abbildet).
//
// Anlass 02.09.2026: Jede Datei ging mit "public, max-age=86400" und OHNE Kennung
// raus. Nach dem Dateisystem-Update lieferte Safari neues HTML mit dem alten
// slotsHandler.js aus -- die Schriftgroessen standen fest auf 1, die Vorschau ruehrte
// sich nicht und Gespeichertes kam nie am Geraet an. Kein Test konnte das sehen, weil
// keiner die Kopfzeilen angesehen hat.
const BASIS = "http://127.0.0.1:8099";

let fehler = 0;
const pruefe = (bedingung, text) => {
  if (!bedingung) {
    console.error("FEHLGESCHLAGEN: " + text);
    fehler++;
  }
};

for (const pfad of ["/slots.html", "/js/slotsHandler.js", "/css/style.css", "/"]) {
  const r = await fetch(BASIS + pfad);
  const cache = r.headers.get("cache-control") || "";
  const etag = r.headers.get("etag") || "";
  pruefe(r.status === 200, `${pfad}: Status ${r.status}`);
  // "no-cache" heisst rueckfragen, nicht "nicht speichern". Ein max-age ohne Kennung
  // waere genau der alte Fehler.
  pruefe(cache.includes("no-cache"), `${pfad}: Cache-Control ist "${cache}"`);
  pruefe(/^".+"$/.test(etag), `${pfad}: keine brauchbare Kennung ("${etag}")`);

  // Bekannte Kennung -> 304, die Datei darf NICHT noch einmal kommen.
  const r304 = await fetch(BASIS + pfad, { headers: { "If-None-Match": etag } });
  pruefe(r304.status === 304, `${pfad}: bekannte Kennung ergab ${r304.status} statt 304`);
  // Ein 304 aktualisiert den Speicherstand des Browsers. Traegt es eine Laenge oder
  // einen Inhaltstyp, merkt er sich die Datei als leer bzw. falsch typisiert.
  pruefe(!r304.headers.has("content-length"),
         `${pfad}: 304 traegt Content-Length (${r304.headers.get("content-length")})`);
  pruefe(!r304.headers.has("content-type"),
         `${pfad}: 304 traegt Content-Type (${r304.headers.get("content-type")})`);

  // So schickt ein Browser die Kennung auch: als schwach markiert oder in einer Liste.
  // Ein strikter Vergleich wuerde hier durchfallen und die Datei jedes Mal neu senden.
  const rSchwach = await fetch(BASIS + pfad, { headers: { "If-None-Match": `W/${etag}` } });
  pruefe(rSchwach.status === 304, `${pfad}: schwache Kennung ergab ${rSchwach.status} statt 304`);
  const rListe = await fetch(BASIS + pfad, { headers: { "If-None-Match": `"alt-1", ${etag}` } });
  pruefe(rListe.status === 304, `${pfad}: Kennungsliste ergab ${rListe.status} statt 304`);

  // Fremde Kennung (= Stand nach einem Update) -> die Datei kommt neu.
  const rNeu = await fetch(BASIS + pfad, { headers: { "If-None-Match": '"v0.0.0-1"' } });
  pruefe(rNeu.status === 200, `${pfad}: fremde Kennung ergab ${rNeu.status} statt 200`);
  pruefe((await rNeu.text()).length > 0, `${pfad}: fremde Kennung lieferte leeren Inhalt`);
}

// Die Kennung muss die Dateien unterscheiden -- sonst gilt nach einem Update die
// falsche Datei als bekannt.
const a = (await fetch(BASIS + "/slots.html")).headers.get("etag");
const b = (await fetch(BASIS + "/js/slotsHandler.js")).headers.get("etag");
pruefe(a !== b, "gleiche Kennung fuer verschiedene Dateien");

if (fehler > 0) process.exit(1);
console.log("Cache-Regeln der Oberflaeche: OK");
