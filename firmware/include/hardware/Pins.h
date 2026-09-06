// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef HARDWARE_PINS_H
#define HARDWARE_PINS_H

// Alles, was an DIESER Platine haengt, an einer Stelle: Anschluesse, Displaymass,
// SPI-Einstellungen, Name und Passwort des Notfall-WLANs. Vorher standen die
// Anschluesse mitten in ConfigManager.h -- einer Klasse, die Einstellungen aus einer
// Datei liest und mit der Verdrahtung nichts zu tun hat.
//
// Hardware-Stand: GeekMagic SmallTV (ESP-12F, ST7789 240x240). Wer die Firmware auf
// eine andere Platine bringt, aendert diese Datei und sonst nichts.
#include <SPI.h>
#include <cstdint>

static constexpr int16_t LCD_W = 240;
static constexpr int16_t LCD_H = 240;
static constexpr int8_t LCD_MOSI_GPIO = 13;
static constexpr int8_t LCD_SCK_GPIO = 14;
static constexpr int8_t LCD_DC_GPIO = 0;
static constexpr int8_t LCD_RST_GPIO = 2;
static constexpr uint8_t LCD_SPI_MODE = SPI_MODE3;
static constexpr uint32_t LCD_SPI_HZ = 40000000;
static constexpr int8_t LCD_BACKLIGHT_GPIO = 5;
// Die Hintergrundbeleuchtung haengt invertiert: 0 ist hell, PWM_MAX ist dunkel.
static constexpr bool LCD_BACKLIGHT_ACTIVE_LOW = true;

// Notfall-WLAN, das das Geraet aufspannt, solange kein bekanntes Netz erreichbar ist.
static constexpr const char AP_SSID[] = "GeekMagic";
// Absichtlich kurz und ohne Sonderzeichen: Dieses Passwort wird genau dann gebraucht,
// wenn das Geraet sein WLAN verloren hat -- also auf einem Handy, im Stehen, unter
// Zeitdruck. Das geerbte "$str0ngPa$$w0rd" der Times-Z-Basis war dort eine Zumutung
// (Nutzer, 02.09.2026). WPA2 verlangt mindestens 8 Zeichen, darunter geht es nicht.
static constexpr const char AP_PASSWORD[] = "smalltv123";

#endif  // HARDWARE_PINS_H
