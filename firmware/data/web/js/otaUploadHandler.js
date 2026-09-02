// Ein Abbild sagt selbst, was es ist -- niemand muss das in einem Auswahlfeld
// nachtragen. Nachgemessen an den ausgelieferten Abbildern (dist/):
//   firmware.bin  beginnt mit 0xE9 (Abbild-Kennung des Chips)
//   littlefs.bin  traegt ab Byte 8 die Zeichen "littlefs"
// Passt keins von beidem, wird NICHT geraten: eine falsch einsortierte Datei
// ueberschreibt den falschen Flash-Bereich, und dieses Geraet hat keinen
// Bootloader-Rueckfall.
const ART_FIRMWARE = "firmware";
const ART_DATEISYSTEM = "fs";

async function abbildArtErkennen(file) {
  const kopfBytes = new Uint8Array(await file.slice(0, 16).arrayBuffer());
  if (kopfBytes.length >= 1 && kopfBytes[0] === 0xe9) return ART_FIRMWARE;

  const kennung = "littlefs";
  let passt = kopfBytes.length >= 16;
  for (let i = 0; passt && i < kennung.length; i++) {
    if (kopfBytes[8 + i] !== kennung.charCodeAt(i)) passt = false;
  }
  return passt ? ART_DATEISYSTEM : null;
}

function artText(art) {
  if (art === ART_FIRMWARE) return "Firmware";
  if (art === ART_DATEISYSTEM) return "Oberfläche (Dateisystem)";
  return "";
}

function otaUploadHandler() {
  return {
    uploading: false,
    uploadMessage: "",
    erkannteArt: null,
    erkanntText: "",
    progress: 0,
    etaText: "",
    xhr: null,

    async uploadFile() {
      const fileInput = this.$refs.fileInput;
      if (!fileInput.files.length) {
        this.uploadMessage = "Bitte eine Datei auswählen";
        return;
      }

      const file = fileInput.files[0];
      if (!file.name.toLowerCase().endsWith(".bin")) {
        this.uploadMessage = "Nur .bin-Dateien sind zulässig";
        return;
      }

      const art = await abbildArtErkennen(file);
      if (art === null) {
        this.uploadMessage =
          "Diese Datei ist weder ein Firmware- noch ein Dateisystem-Abbild. " +
          "Es wird nichts geschrieben — lieber abbrechen als den falschen " +
          "Speicherbereich überschreiben.";
        return;
      }
      this.erkannteArt = art;
      this.erkanntText = artText(art);

      const endpoint =
        art === ART_FIRMWARE ? "/api/v1/ota/fw" : "/api/v1/ota/fs";

      this.uploading = true;
      this.uploadMessage = "";
      this.progress = 0;
      this.etaText = "";

      const formData = new FormData();
      formData.append("file", file);

      this.xhr = new XMLHttpRequest();
      const startTime = Date.now();

      this.xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) {
          const percent = Math.round((e.loaded / e.total) * 100);
          this.progress = percent;
          const elapsed = (Date.now() - startTime) / 1000;
          const rate = e.loaded / Math.max(elapsed, 0.001);
          const remaining = e.total - e.loaded;
          if (rate > 0) {
            const eta = Math.round(remaining / rate);
            this.etaText = "noch ~" + eta + " s";
          } else {
            this.etaText = "";
          }
        } else {
          this.progress = Math.min(99, this.progress + 1);
        }
      };

      this.xhr.onload = async () => {
        this.uploading = false;
        try {
          if (this.xhr.status >= 200 && this.xhr.status < 300) {
            const data = JSON.parse(this.xhr.responseText || "{}");
            this.uploadMessage = data.message || "Hochladen abgeschlossen";
            this.progress = 100;
            this.etaText = "";
          } else {
            this.uploadMessage =
              "Fehler: " + (this.xhr.responseText || this.xhr.status);
          }
        } catch (e) {
          this.uploadMessage = "Hochladen abgeschlossen";
        }
        this.xhr = null;
      };

      this.xhr.onerror = () => {
        this.uploading = false;
        this.uploadMessage = "Hochladen fehlgeschlagen";
        this.xhr = null;
      };

      this.xhr.onabort = () => {
        this.uploading = false;
        this.uploadMessage = "Hochladen abgebrochen";
        this.xhr = null;
      };

      this.xhr.open("POST", endpoint);
      const token = localStorage.getItem("Authorization");
      if (token) {
        this.xhr.setRequestHeader("Authorization", `Bearer ${token}`);
      }
      this.xhr.send(formData);
    },

    /// Beim Auswaehlen der Datei sofort melden, was sie ist. So sieht man VOR dem
    /// Hochladen, welcher Bereich beschrieben wird -- eine Auswahl zum Vertippen
    /// gibt es nicht mehr, eine stille Entscheidung soll es aber auch nicht sein.
    async dateiGewaehlt() {
      this.uploadMessage = "";
      this.erkannteArt = null;
      this.erkanntText = "";
      const fileInput = this.$refs.fileInput;
      if (!fileInput || !fileInput.files.length) return;
      const art = await abbildArtErkennen(fileInput.files[0]);
      this.erkannteArt = art;
      this.erkanntText = artText(art);
      if (art === null) {
        this.uploadMessage =
          "Unbekanntes Abbild — weder Firmware noch Dateisystem.";
      }
    },

    async cancelUpload() {
      if (this.xhr) {
        try {
          this.xhr.abort();
        } catch (e) {
          // ignore
        }
      }

      try {
        await apiFetch("/api/v1/ota/cancel", { method: "POST" });
      } catch (e) {
        // ignore
      }
      this.uploading = false;
      this.progress = 0;
      this.etaText = "";
    },
  };
}
