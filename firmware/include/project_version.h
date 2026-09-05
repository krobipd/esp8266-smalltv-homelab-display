// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Die Version kommt aus firmware/VERSION und wird vom Pre-Build-Skript
// scripts/git_version.py als Makro PROJECT_VER hereingereicht. Bewusst KEIN erzeugter
// Header mehr: Eine waehrend des Builds entstehende Datei in include/ aenderte
// PlatformIOs Projekt-Pruefsumme, worauf der naechste Lauf das Build-Verzeichnis samt
// fertigem Abbild loeschte -- die CI war deshalb seit der Erstveroeffentlichung rot.
#ifndef PROJECT_VER
#error "PROJECT_VER fehlt -- scripts/git_version.py muss als pre-Skript laufen (platformio.ini)"
#endif

static const char PROJECT_VER_STR[] = PROJECT_VER;
