document.addEventListener("alpine:init", () => {
  Alpine.data("themeSwitcher", themeSwitcher);
  if (typeof otaUploadHandler !== "undefined")
    Alpine.data("otaUploadHandler", otaUploadHandler);
  if (typeof wifiHandler !== "undefined")
    Alpine.data("wifiHandler", wifiHandler);
  if (typeof ntpHandler !== "undefined") Alpine.data("ntpHandler", ntpHandler);
  if (typeof rotationHandler !== "undefined")
    Alpine.data("rotationHandler", rotationHandler);
  if (typeof rebootHandler !== "undefined")
    Alpine.data("rebootHandler", rebootHandler);
  if (typeof anmeldungHandler !== "undefined")
    Alpine.data("anmeldungHandler", anmeldungHandler);
  if (typeof slotsHandler !== "undefined")
    Alpine.data("slotsHandler", slotsHandler);
});
