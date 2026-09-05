// SPDX-License-Identifier: GPL-3.0-or-later
// Passwortschutz der Weboberflaeche -- OPTIONAL: Ohne gesetztes Passwort ist das
// Geraet frei bedienbar (Werkszustand). Erst wer eines festlegt, schaltet die
// Anmeldung scharf; ein leeres Passwort speichern hebt den Schutz wieder auf.
// Technisch schickt der Browser das Passwort bei jedem API-Aufruf mit (apiFetch in
// utils.js). Der Speicherschluessel heisst aus historischen Gruenden "Authorization" --
// er ist unsichtbar und alle Handler lesen ihn, also bleibt er.
function anmeldungHandler() {
  const speicherSchluessel = "Authorization";

  return {
    passwort: "",
    neuesPasswort: "",
    meldung: "",
    aenderMeldung: "",
    zeigePasswort: false,
    zeigeNeues: false,
    laeuft: false,
    schutzAktiv: false,
    angemeldet: false,
    geprueft: false,

    async init() {
      // Ist der Schutz ueberhaupt aktiv? Ohne Passwort antwortet das Geraet auch
      // ohne Anmeldung mit 200 -- mit Passwort kommt 401.
      try {
        const res = await fetch("/api/v1/token/check");
        this.schutzAktiv = res.status === 401;
      } catch (e) {
        this.meldung = "Gerät nicht erreichbar.";
        return;
      }
      this.geprueft = true;

      const gespeichert = localStorage.getItem(speicherSchluessel) || "";
      if (!this.schutzAktiv) {
        this.angemeldet = false;
        this.meldung = "";
        return;
      }
      this.passwort = gespeichert;
      this.angemeldet = !!gespeichert;
      this.meldung = gespeichert
        ? "Angemeldet — das Passwort ist in diesem Browser gespeichert."
        : "Nicht angemeldet.";
    },

    async anmelden() {
      const eingabe = this.passwort.trim();

      if (!eingabe) {
        this.meldung = "Bitte ein Passwort eingeben.";
        return;
      }

      this.laeuft = true;
      this.meldung = "Passwort wird geprüft…";

      try {
        // Erst beim Geraet nachfragen, dann speichern -- ein falsches Passwort
        // soll sofort auffallen, nicht erst auf der naechsten Seite.
        const res = await fetch("/api/v1/token/check", {
          method: "GET",
          headers: { Authorization: "Bearer " + eingabe },
        });
        if (!res.ok) {
          this.meldung =
            res.status === 401 ? "Falsches Passwort." : "Prüfung fehlgeschlagen.";
          return;
        }

        localStorage.setItem(speicherSchluessel, eingabe);
        this.angemeldet = true;
        this.meldung = "Angemeldet.";
      } catch (e) {
        this.meldung = "Gerät nicht erreichbar.";
      } finally {
        this.laeuft = false;
      }
    },

    // Passwort setzen (Schutz einschalten oder aendern). Leer = nicht erlaubt --
    // zum Abschalten gibt es den eigenen Knopf, damit niemand den Schutz aus
    // Versehen aufhebt.
    async passwortSetzen() {
      const neu = this.neuesPasswort.trim();

      if (!neu) {
        this.aenderMeldung = "Bitte ein Passwort eingeben.";
        return;
      }
      // Der Browser schickt das Passwort im Anmeldekopf, und dort gibt es nur Latin-1 --
      // gespeichert wird es als UTF-8. Ein Umlaut passt dann nie mehr zusammen: ausgesperrt.
      if (/[^\x20-\x7e]/.test(neu)) {
        this.aenderMeldung =
          "Bitte nur Buchstaben, Ziffern und Zeichen ohne Umlaute — im Anmeldekopf lassen sich Umlaute nicht übertragen.";
        return;
      }

      this.laeuft = true;
      this.aenderMeldung = "Passwort wird gespeichert…";

      try {
        const res = await apiFetch("/api/v1/token/save", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ token: neu }),
        });

        if (!res.ok) {
          let text = "Speichern fehlgeschlagen.";
          try {
            const data = await res.json();
            text = data.message || data.error || text;
          } catch (e) {
            // Antwort ohne JSON: Standardtext behalten
          }
          this.aenderMeldung = text;
          return;
        }

        localStorage.setItem(speicherSchluessel, neu);
        this.passwort = neu;
        this.neuesPasswort = "";
        this.schutzAktiv = true;
        this.angemeldet = true;
        this.aenderMeldung = "Passwort gespeichert — der Schutz ist aktiv.";
        this.meldung = "Angemeldet.";
      } catch (e) {
        this.aenderMeldung = "Gerät nicht erreichbar.";
      } finally {
        this.laeuft = false;
      }
    },

    async schutzAufheben() {
      if (!confirm("Passwortschutz wirklich aufheben? Das Gerät ist dann ohne Anmeldung bedienbar.")) {
        return;
      }
      this.laeuft = true;
      try {
        const res = await apiFetch("/api/v1/token/save", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ token: "" }),
        });
        if (!res.ok) {
          this.aenderMeldung = "Aufheben fehlgeschlagen.";
          return;
        }
        localStorage.removeItem(speicherSchluessel);
        this.passwort = "";
        this.neuesPasswort = "";
        this.schutzAktiv = false;
        this.angemeldet = false;
        this.meldung = "";
        this.aenderMeldung = "Passwortschutz aufgehoben.";
      } catch (e) {
        this.aenderMeldung = "Gerät nicht erreichbar.";
      } finally {
        this.laeuft = false;
      }
    },

    abmelden() {
      localStorage.removeItem(speicherSchluessel);
      this.passwort = "";
      this.neuesPasswort = "";
      this.angemeldet = false;
      this.meldung = "Abgemeldet.";
    },
  };
}
