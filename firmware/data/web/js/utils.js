function apiFetch(url, options = {}) {
  const token = localStorage.getItem("Authorization");

  const fetchOptions = { ...options };

  if (!fetchOptions.headers) {
    fetchOptions.headers = {};
  } else {
    fetchOptions.headers = { ...fetchOptions.headers };
  }

  if (token) {
    fetchOptions.headers["Authorization"] = `Bearer ${token}`;
  }

  // Ein 401 heisst "Anmeldung noetig" -- auf JEDER Seite, nicht nur auf der Werte-Seite.
  // Der Hinweis steht im Kopfbereich (header.html); die Handler bekommen die Antwort
  // unveraendert zurueck und muessen nichts davon wissen.
  return fetch(url, fetchOptions).then((antwort) => {
    if (antwort.status === 401) zeigeAnmeldeHinweis();
    return antwort;
  });
}

// Länge in Bytes (UTF-8), so wie das Gerät sie zählt: "Küche" sind 6, nicht 5.
function byteLaenge(s) {
  return new TextEncoder().encode(s == null ? "" : String(s)).length;
}

// Was das Display zeichnen kann: ASCII plus ° ä ö ü Ä Ö Ü ß (Zeichensatz
// Codepage 437, Firmware smalltv_util.h FONT_UMSETZUNG). Gilt für alles, was auf dem
// Display erscheint: Beschriftung und Einheit.
const DISPLAY_ZEICHEN = /^[\x20-\x7e\u00b0\u00e4\u00f6\u00fc\u00c4\u00d6\u00dc\u00df]*$/;
// Steuerzeichen lehnt das Gerät in jedem Text ab, auch im nie gezeichneten Feldnamen.
const OHNE_STEUERZEICHEN = /^[^\x00-\x1f\x7f]*$/;

// Jede Antwort des Geräts nach demselben Muster lesen (D5, seit v0.5.0): `ok` sagt, ob
// es geklappt hat, `message` sagt es in Worten. Ältere Firmware schickt stattdessen
// `status`/`error` — beides wird hier mitgelesen, damit die Seite auch im Fenster
// zwischen Firmware- und Dateisystem-Update funktioniert.
function ergebnisVon(antwort, daten) {
  const d = daten && typeof daten === "object" ? daten : {};
  const ok =
    typeof d.ok === "boolean"
      ? d.ok
      : d.status === "error"
        ? false
        : Boolean(antwort && antwort.ok);
  return { ok, text: d.message || d.error || "" };
}

// Antwort holen und gleich auswerten: liefert { ok, text, daten }.
async function apiErgebnis(pfad, optionen) {
  const antwort = await apiFetch(pfad, optionen);
  let daten = {};
  try {
    daten = await antwort.json();
  } catch (e) {
    daten = {};
  }
  const e = ergebnisVon(antwort, daten);
  return { ok: e.ok, text: e.text, daten, status: antwort.status };
}

// Gemerkt, weil der Kopfbereich nachgeladen wird: Kommt das 401 vor dem Kopf, wird der
// Hinweis eingeblendet, sobald der Kopf da ist.
let anmeldungNoetig = false;
function zeigeAnmeldeHinweis() {
  anmeldungNoetig = true;
  const el = document.getElementById("anmelde-hinweis");
  if (el) el.hidden = false;
}

// Kopf- und Fusszeile werden nachgeladen. Kommt dabei nichts an, fehlt die ganze
// Navigation und die Seite ist eine Sackgasse -- deshalb EIN Versuch mehr, diesmal
// am Zwischenspeicher des Browsers vorbei. Anlass 02.09.2026: Safari lieferte den
// Kopf leer aus, nachdem ein 304 seinen Speicherstand verdorben hatte.
function includeHTML(id, url, callback) {
  const einsetzen = (data) => {
    document.getElementById(id).innerHTML = data;
    if (typeof callback === "function") callback();
  };
  fetch(url)
    .then((response) => response.text())
    .then((data) => {
      if (data && data.trim()) {
        einsetzen(data);
        return;
      }
      fetch(url, { cache: "reload" })
        .then((r) => r.text())
        .then(einsetzen);
    });
}

// Markiert in der gemeinsamen Navigation (header.html) die gerade offene Seite.
function markiereAktiveSeite() {
  const seite = location.pathname.split("/").pop() || "index.html";
  document.querySelectorAll("#seiten-nav a").forEach((a) => {
    if ((a.getAttribute("href") || "").replace("./", "") === seite) {
      const knopf = a.querySelector(".btn");
      if (knopf) knopf.classList.add("act");
    }
  });
}

// Wird erst gerufen, wenn der Kopfbereich eingesetzt ist -- vorher lief hier ein
// Intervall, das alle 20 ms nachsah, ob es das Element schon gibt.
function setHeaderTitle(title) {
  const h1 = document.getElementById("header-title");
  if (h1) h1.textContent = title;
}

document.addEventListener("DOMContentLoaded", () => {
  if (document.getElementById("header-placeholder")) {
    includeHTML("header-placeholder", "./header.html", () => {
      let pageTitle =
        document.title && document.title.trim()
          ? document.title.trim()
          : "Placeholder Title";
      setHeaderTitle(pageTitle);
      markiereAktiveSeite();
      if (anmeldungNoetig) zeigeAnmeldeHinweis();
    });
  }
  if (document.getElementById("footer-placeholder")) {
    includeHTML("footer-placeholder", "./footer.html");
  }
});
