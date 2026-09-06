function ntpHandler() {
  return {
    loading: false,
    lastStatus: "",
    lastSyncTime: 0,
    lastOk: false,
    ntpServer: "",
    zeitzone: "",
    // Die gängigen Regeln als Auswahl; wer etwas anderes braucht, trägt es von Hand ein.
    // Das Format ist die POSIX-TZ-Regel, die die C-Bibliothek des Geräts versteht.
    zeitzonen: [
      { text: "Mitteleuropa (Berlin, Wien, Zürich)", regel: "CET-1CEST,M3.5.0,M10.5.0/3" },
      { text: "Westeuropa (London, Lissabon)", regel: "GMT0BST,M3.5.0/1,M10.5.0" },
      { text: "Osteuropa (Athen, Helsinki)", regel: "EET-2EEST,M3.5.0/3,M10.5.0/4" },
      { text: "UTC (ohne Sommerzeit)", regel: "UTC0" },
      { text: "New York", regel: "EST5EDT,M3.2.0,M11.1.0" },
      { text: "Los Angeles", regel: "PST8PDT,M3.2.0,M11.1.0" },
    ],

    fetchStatus() {
      apiFetch("/api/v1/ntp/status")
        .then((r) => r.json())
        .then((data) => {
          this.lastStatus = data.lastStatus || "";
          this.lastSyncTime = data.lastSyncTime || 0;
          this.lastOk = data.lastOk || false;
        })
        .catch((err) => {
          this.lastStatus = "Status nicht abrufbar";
          console.error(err);
        });
    },

    // Das Gerät stößt den Abgleich nur an und antwortet sofort (es würde sonst bis zu
    // fünf Sekunden stillstehen). Das Ergebnis steht kurz darauf in /ntp/status —
    // deshalb wird zweimal nachgefragt statt aus der Antwort geraten.
    syncNow() {
      this.loading = true;
      this.lastStatus = "Abgleich läuft …";
      apiFetch("/api/v1/ntp/sync", { method: "POST" })
        .then((r) => r.json())
        .then(() => {
          setTimeout(() => this.fetchStatus(), 2000);
          setTimeout(() => this.fetchStatus(), 8000);
        })
        .catch((err) => {
          this.lastStatus = "Synchronisierung fehlgeschlagen";
          console.error(err);
        })
        .finally(() => {
          this.loading = false;
        });
    },

    fetchConfig() {
      apiFetch("/api/v1/ntp/config")
        .then((r) => r.json())
        .then((data) => {
          this.ntpServer = data.ntp_server || "";
          this.zeitzone = data.zeitzone || "";
        })
        .catch((err) => {
          console.error("failed to fetch ntp config", err);
        });
    },

    saveConfig() {
      const payload = { ntp_server: this.ntpServer, zeitzone: this.zeitzone };
      apiFetch("/api/v1/ntp/config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      })
        .then((r) => r.json())
        .then((data) => {
          if (data.ok !== false && data.status !== "error") {
            this.zeitzone = data.zeitzone || this.zeitzone;
            this.lastStatus = "Gespeichert — Synchronisierung angestoßen";
            this.fetchStatus();
          } else {
            this.lastStatus = data.message || "Speichern fehlgeschlagen";
          }
        })
        .catch((err) => {
          this.lastStatus = "Speichern fehlgeschlagen";
          console.error(err);
        });
    },

    humanTime(ts) {
      if (!ts || ts === 0) return "noch nie";
      const d = new Date(ts * 1000);
      return d.toLocaleString();
    },

    init() {
      this.fetchStatus();
      this.fetchConfig();
    },
  };
}
