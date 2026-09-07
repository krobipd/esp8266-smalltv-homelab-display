// SPDX-License-Identifier: GPL-3.0-or-later
// Findet das neueste dist/v*-Verzeichnis -- NUMERISCH, nicht lexikografisch.
//
// Drei Tests hatten dafuer je eine eigene Zeile `…filter(d => d.startsWith("v")).sort().pop()`.
// Das haelt genau so lange, wie alle Zahlen einstellig sind: Lexikografisch steht "v0.10.0"
// VOR "v0.5.7", die Tests waeren also ab v0.10.0 still gegen ein altes Abbild gelaufen und
// haetten es als Erfolg gemeldet. Ein Test, der die falsche Sache prueft, ist schlimmer als
// keiner -- deshalb liegt die Auswahl jetzt EINMAL hier statt dreimal dort.
import { readdirSync } from "node:fs";
import { join } from "node:path";

/// Vergleicht "v0.10.0" und "v0.5.7" wie Versionen, nicht wie Zeichenketten.
const alsZahlen = (name) => name.replace(/^v/, "").split(".").map((t) => parseInt(t, 10) || 0);

export function neuesteFassung(basis) {
  const wurzel = join(basis, "dist");
  const fassungen = readdirSync(wurzel).filter((d) => /^v\d/.test(d));
  if (fassungen.length === 0) {
    throw new Error(`kein dist/v*-Verzeichnis in ${wurzel} — erst tools/paket.sh laufen lassen`);
  }
  fassungen.sort((a, b) => {
    const x = alsZahlen(a);
    const y = alsZahlen(b);
    for (let i = 0; i < Math.max(x.length, y.length); i++) {
      if ((x[i] || 0) !== (y[i] || 0)) return (x[i] || 0) - (y[i] || 0);
    }
    return 0;
  });
  return { name: fassungen[fassungen.length - 1], pfad: join(wurzel, fassungen[fassungen.length - 1]) };
}
