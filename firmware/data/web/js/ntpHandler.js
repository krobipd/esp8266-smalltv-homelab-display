function ntpHandler() {
  return {
    loading: false,
    lastStatus: "",
    lastSyncTime: 0,
    lastOk: false,
    ntpServer: "",

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

    syncNow() {
      this.loading = true;
      apiFetch("/api/v1/ntp/sync", { method: "POST" })
        .then((r) => r.json())
        .then((data) => {
          this.lastStatus = data.lastStatus || "";
          this.lastSyncTime = data.lastSyncTime || 0;
          this.lastOk = data.status === "ok";
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
        })
        .catch((err) => {
          console.error("failed to fetch ntp config", err);
        });
    },

    saveConfig() {
      const payload = { ntp_server: this.ntpServer };
      apiFetch("/api/v1/ntp/config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      })
        .then((r) => r.json())
        .then((data) => {
          if (data.status === "ok") {
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
