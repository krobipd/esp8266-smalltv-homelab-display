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

function setHeaderTitle(title) {
  const interval = setInterval(() => {
    const h1 = document.getElementById("header-title");
    if (h1) {
      h1.textContent = title;
      clearInterval(interval);
    }
  }, 20);
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
