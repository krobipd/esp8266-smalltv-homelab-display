function wifiHandler() {
  return {
    ssid: "",
    password: "",
    networks: [],
    scanning: false,
    statusMsg: "",
    showPassword: false,
    connecting: false,

    async scan() {
      this.scanning = true;
      this.statusMsg = "";
      try {
        const res = await apiFetch("/api/v1/wifi/scan");
        const nets = await res.json();
        // process: sort by rssi desc and enrich display fields
        this.networks = (nets || [])
          .map((n) => {
            const rssi =
              typeof n.rssi === "number" ? n.rssi : parseInt(n.rssi) || 0;
            const bars =
              rssi > -50
                ? "▮▮▮▮"
                : rssi > -60
                  ? "▮▮▮▯"
                  : rssi > -70
                    ? "▮▮▯▯"
                    : "▮▯▯▯";
            // ESP8266-Verschluesselungstypen: 7 = offen (ENC_TYPE_NONE), alles
            // andere ist gesichert. "enc !== 0" war falsch herum gedacht --
            // damit galt ausgerechnet ein offenes Netz als verschluesselt.
            const secured = typeof n.enc === "number" ? n.enc !== 7 : !!n.enc;
            return {
              ssid: n.ssid || "",
              rssi,
              rssiDisplay: rssi + " dBm",
              bars,
              secured,
            };
          })
          .sort((a, b) => b.rssi - a.rssi);
      } catch (e) {
        this.statusMsg = "Suche fehlgeschlagen";
        this.networks = [];
      }
      this.scanning = false;
    },

    selectNetwork(net) {
      this.ssid = net.ssid;
      // Require the user to provide the password explicitly
      this.password = "";
      this.statusMsg = "Zum Verbinden das WLAN-Passwort eingeben";
      // focus password input if secured
      setTimeout(() => {
        const pw = document.getElementById("password");
        if (pw) pw.focus();
      }, 50);
    },

    async quickConnect(net) {
      this.selectNetwork(net);
      if (!net.secured) {
        // try connect without password
        await this.connect();
      } else {
        this.statusMsg = "WLAN-Passwort eingeben und verbinden";
      }
    },

    async connect() {
      this.statusMsg = "Verbinde…";
      this.connecting = true;
      try {
        const res = await apiFetch("/api/v1/wifi/connect", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ ssid: this.ssid, password: this.password }),
        });
        const j = await res.json();
        if (j.status === "connected") {
          this.statusMsg = "Verbunden: " + (j.ip || "");
          this.password = "";
        } else {
          this.statusMsg = "Fehler: " + (j.message || "Verbindung fehlgeschlagen");
        }
      } catch (e) {
        this.statusMsg = "Gerät nicht erreichbar";
      }
      this.connecting = false;
    },

    async forget() {
      this.ssid = "";
      this.password = "";
      this.statusMsg = "Felder geleert";
    },

    async init() {
      try {
        const res = await apiFetch("/api/v1/wifi/status");
        const j = await res.json();
        if (j.connected) {
          this.statusMsg = `Verbunden: ${j.ssid} ${j.ip}`;
          this.ssid = j.ssid || this.ssid;
        }
      } catch (e) {
        // ignore
      }
    },
  };
}
