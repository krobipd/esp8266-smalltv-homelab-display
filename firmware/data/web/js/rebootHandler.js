function rebootHandler() {
  return {
    loading: false,
    message: "",
    async reboot() {
      this.loading = true;
      this.message = "Neustart läuft…";
      try {
        const res = await apiFetch("/api/v1/reboot", { method: "POST" });
        if (res.ok) {
          this.message = "Gerät startet neu…";
        } else {
          this.message = "Neustart fehlgeschlagen";
        }
      } catch (e) {
        this.message = "Fehler: " + e;
      }
      setTimeout(() => {
        this.loading = false;
      }, 5000);
    },
  };
}
