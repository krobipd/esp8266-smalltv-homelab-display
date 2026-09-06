function logsHandler() {
  return {
    loading: false,
    logs: [],
    message: "",

    fetchLogs() {
      this.loading = true;
      this.message = "";
      apiFetch("/api/v1/logs")
        .then((r) => r.json())
        .then((data) => {
          this.logs = data.logs || [];
        })
        .catch((err) => {
          this.message = "Protokoll nicht abrufbar";
          console.error(err);
        })
        .finally(() => {
          this.loading = false;
        });
    },

    downloadLogs() {
      this.loading = true;
      this.message = "";
      apiFetch("/api/v1/logs/download")
        .then((r) => {
          if (!r.ok) throw new Error("Download failed");
          return r.blob();
        })
        .then((blob) => {
          const url = URL.createObjectURL(blob);
          const a = document.createElement("a");
          a.href = url;
          a.download = "logs.log";
          document.body.appendChild(a);
          a.click();
          document.body.removeChild(a);
          URL.revokeObjectURL(url);
          this.message = "Download gestartet";
        })
        .catch((err) => {
          this.message = "Download fehlgeschlagen";
          console.error(err);
        })
        .finally(() => {
          this.loading = false;
        });
    },

    clearLogs() {
      if (!confirm("Protokoll wirklich leeren?")) return;
      this.loading = true;
      this.message = "";
      apiFetch("/api/v1/logs/clear", { method: "POST" })
        .then((r) => r.json().then((data) => ergebnisVon(r, data)))
        .then((data) => {
          if (data.ok) {
            this.logs = [];
            this.message = "Protokoll geleert";
          } else {
            this.message = data.text || "Leeren fehlgeschlagen";
          }
        })
        .catch((err) => {
          this.message = "Leeren fehlgeschlagen";
          console.error(err);
        })
        .finally(() => {
          this.loading = false;
        });
    },

    init() {
      this.fetchLogs();
    },
  };
}
