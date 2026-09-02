# SPDX-License-Identifier: GPL-3.0-or-later
"""Entfernt die erzwungene Einbindung der Fliesskomma-Varianten von printf und scanf.

Der ESP8266-Core linkt mit "-u _printf_float -u _scanf_float" und holt damit
nano-vfprintf_float und nano-vfscanf_float ins Abbild -- und ueber deren
Abhaengigkeiten strtod, dtoa und gethex. Zusammen rund 11 KB, unabhaengig davon,
ob irgendwer "%f" benutzt.

Diese Firmware benutzt weder "%f" noch scanf (geprueft; Zahlen werden in
smalltv_util.h ganzzahlig formatiert und geparst). Der Platz wird beim Erstflash
ueber die Werksfirmware gebraucht, deren Update-Bereich knapp ist.

WARNUNG: Wer hier kuenftig "%f" einbaut, bekommt keine Fehlermeldung, sondern
eine falsche Ausgabe. Dann dieses Skript aus der platformio.ini nehmen.
"""
Import("env")

RAUS = ("_printf_float", "_scanf_float")

flags = list(env["LINKFLAGS"])
neu = []
i = 0
entfernt = []
while i < len(flags):
    if flags[i] == "-u" and i + 1 < len(flags) and flags[i + 1] in RAUS:
        entfernt.append(flags[i + 1])
        i += 2
        continue
    # Auch die zusammengeschriebene Form abfangen
    if flags[i].startswith("-u") and flags[i][2:] in RAUS:
        entfernt.append(flags[i][2:])
        i += 1
        continue
    neu.append(flags[i])
    i += 1

env.Replace(LINKFLAGS=neu)
if entfernt:
    print("Fliesskomma-Formatierung abgewaehlt: " + ", ".join(entfernt))
