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

  return fetch(url, fetchOptions);
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
    });
  }
  if (document.getElementById("footer-placeholder")) {
    includeHTML("footer-placeholder", "./footer.html");
  }
});
